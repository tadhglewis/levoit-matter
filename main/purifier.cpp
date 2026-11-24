#include "purifier.h"

#include "filter_life.h"
#include "levoit_link.h"

#include <esp_log.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <nvs.h>

#include <atomic>

static const char *const TAG = "purifier";

namespace levoit {

namespace {

constexpr int TX_PIN = CONFIG_LEVOIT_UART_TX_GPIO;
constexpr int RX_PIN = CONFIG_LEVOIT_UART_RX_GPIO;
constexpr const char *NVS_NAMESPACE = "levoit";
constexpr const char *NVS_USED_CADR = "used_cadr";
constexpr const char *NVS_RUNTIME = "runtime_min";
constexpr uint32_t SAVE_EVERY_MINUTES = 30;
constexpr uint32_t REPUBLISH_WINDOW_MS = 3000;
constexpr uint32_t DISPLAY_COMMAND_WINDOW_MS = 3000;

#if CONFIG_LEVOIT_MODEL_CORE200S
const ModelSpec &model = CORE_200S;
#elif CONFIG_LEVOIT_MODEL_CORE400S
const ModelSpec &model = CORE_400S;
#elif CONFIG_LEVOIT_MODEL_CORE600S
const ModelSpec &model = CORE_600S;
#else
const ModelSpec &model = CORE_300S;
#endif
PurifierCallbacks callbacks{};
QueueHandle_t requests = nullptr;

FilterLife filter(model);
Holdoff holdoff;
ResetGesture reset_gesture;

PurifierState raw_state;
bool have_state = false;
uint8_t last_level = 0;
MatterView last_view;
bool have_view = false;

uint32_t republish_started_ms = 0;
bool republish_active = false;
uint32_t display_command_ms = 0;
bool display_commanded = false;
uint32_t last_minute_ms = 0;
uint32_t saved_runtime = 0;
bool filter_led_on = false;

WifiLed wifi_led = WifiLed::OFF;
bool wifi_led_sent = false;
uint32_t wifi_led_since_ms = 0;

std::atomic<bool> ready{false};
std::atomic<uint8_t> filter_percent{100};

void save_filter() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return;
  esp_err_t err = nvs_set_u32(handle, NVS_USED_CADR, filter.used_cadr());
  if (err == ESP_OK)
    err = nvs_set_u32(handle, NVS_RUNTIME, filter.runtime_minutes());
  if (err == ESP_OK)
    err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "filter save failed: %s", esp_err_to_name(err));
    return;
  }
  saved_runtime = filter.runtime_minutes();
}

void publish(uint32_t now_ms, bool force) {
  if (!have_state)
    return;
  PurifierState effective = raw_state;
  holdoff.apply(effective, now_ms);
  MatterView view = make_view(model, effective, last_level, filter.percent());
  if (!force && have_view && view == last_view && !republish_active)
    return;
  last_view = view;
  have_view = callbacks.publish(view);

  bool led_wanted = view.change_indication == 2;
  if (led_wanted != filter_led_on) {
    filter_led_on = led_wanted;
    link_send(command_filter_led(led_wanted));
  }
}

void apply_plan(const Plan &plan, uint32_t now_ms) {
  for (uint8_t i = 0; i < plan.count; i++)
    link_send(plan.commands[i]);
  holdoff.expect(plan, now_ms);
  republish_started_ms = now_ms;
  republish_active = true;
}

void set_wifi_led(WifiLed state, uint32_t now_ms) {
  if (wifi_led == state && wifi_led_sent)
    return;
  wifi_led = state;
  wifi_led_sent = true;
  wifi_led_since_ms = now_ms;
  link_send(command_wifi_led(state));
}

void handle_request(const Request &request, uint32_t now_ms) {
  if (request.kind == RequestKind::WIFI_LED) {
    set_wifi_led(static_cast<WifiLed>(request.value), now_ms);
    return;
  }
  if (request.kind == RequestKind::FILTER_RESET) {
    filter.reset();
    filter_percent.store(filter.percent());
    save_filter();
    publish(now_ms, true);
    return;
  }
  if (!have_state)
    return;

  PurifierState current = raw_state;
  holdoff.apply(current, now_ms);

  switch (request.kind) {
  case RequestKind::FAN_OFF:
    apply_plan(plan_fan_off(current), now_ms);
    break;
  case RequestKind::FAN_AUTO:
    apply_plan(plan_fan_auto(model, current), now_ms);
    break;
  case RequestKind::FAN_LEVEL:
    apply_plan(plan_fan_level(model, current, request.value), now_ms);
    break;
  case RequestKind::FAN_RESUME: {
    bool manual_on = current.power && current.mode == Mode::MANUAL;
    uint8_t level = manual_on ? (request.value != 0 ? model.level_max : current.level)
                              : last_level;
    apply_plan(plan_fan_level(model, current, level != 0 ? level : 1), now_ms);
    break;
  }
  case RequestKind::SLEEP:
    apply_plan(plan_sleep(current, request.value != 0), now_ms);
    break;
  case RequestKind::DISPLAY: {
    Plan plan = plan_display(current, request.value != 0);
    if (plan.count > 0) {
      display_command_ms = now_ms;
      display_commanded = true;
    }
    apply_plan(plan, now_ms);
    break;
  }
  case RequestKind::CHILD_LOCK:
    apply_plan(plan_child_lock(current, request.value != 0), now_ms);
    break;
  default:
    break;
  }
}

void on_status(const PurifierState &state, uint32_t now_ms) {
  bool was_on = have_state && raw_state.power;
  raw_state = state;
  have_state = true;
  if (state.level != 0)
    last_level = state.level;
  if (was_on && !state.power)
    save_filter();

  bool commanded = display_commanded &&
                   now_ms - display_command_ms < DISPLAY_COMMAND_WINDOW_MS;
  if (reset_gesture.feed(state.display_on, commanded, now_ms)) {
    ESP_LOGW(TAG, "reset gesture detected");
    callbacks.factory_reset();
  }

  if (!ready.exchange(true)) {
    ESP_LOGI(TAG, "MCU %u.%u.%u", state.mcu_major, state.mcu_minor,
             state.mcu_patch);
    if (wifi_led_sent)
      link_send(command_wifi_led(wifi_led));
  }
  publish(now_ms, false);
}

void on_filter_reset(uint32_t now_ms) {
  ESP_LOGI(TAG, "filter reset from the panel");
  filter.reset();
  filter_percent.store(filter.percent());
  save_filter();
  publish(now_ms, true);
}

void on_poll(uint32_t now_ms) {
  Request request;
  while (xQueueReceive(requests, &request, 0) == pdTRUE)
    handle_request(request, now_ms);

  if (now_ms - last_minute_ms >= 60000) {
    last_minute_ms = now_ms;
    if (have_state) {
      filter.tick_minute(raw_state, last_level);
      filter_percent.store(filter.percent());
      if (filter.runtime_minutes() - saved_runtime >= SAVE_EVERY_MINUTES)
        save_filter();
      publish(now_ms, false);
    }
  }

  if (republish_active && now_ms - republish_started_ms > REPUBLISH_WINDOW_MS) {
    republish_active = false;
    publish(now_ms, true);
  }
}

} // namespace

const ModelSpec &purifier_model() { return model; }

void purifier_load() {
  if (requests == nullptr)
    requests = xQueueCreate(16, sizeof(Request));
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return;
  uint32_t used_cadr = 0;
  uint32_t runtime = 0;
  nvs_get_u32(handle, NVS_USED_CADR, &used_cadr);
  nvs_get_u32(handle, NVS_RUNTIME, &runtime);
  nvs_close(handle);
  filter.load(used_cadr, runtime);
  saved_runtime = runtime;
  filter_percent.store(filter.percent());
}

uint8_t purifier_filter_percent() { return filter_percent.load(); }

esp_err_t purifier_start(const PurifierCallbacks &purifier_callbacks) {
  callbacks = purifier_callbacks;
  if (requests == nullptr)
    requests = xQueueCreate(16, sizeof(Request));
  if (requests == nullptr)
    return ESP_ERR_NO_MEM;
  LinkHandlers handlers = {on_status, on_filter_reset, on_poll};
  return link_start(model, TX_PIN, RX_PIN, handlers);
}

bool purifier_post(const Request &request) {
  if (requests == nullptr)
    return false;
  return xQueueSend(requests, &request, 0) == pdTRUE;
}

bool purifier_ready() { return ready.load(); }

bool purifier_link_alive() { return link_alive(); }

} // namespace levoit
