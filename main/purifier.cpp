#include "purifier.h"

#include "core_bridge.h"
#include "filter_life.h"
#include "levoit_link.h"

#include <esp_log.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <nvs.h>

#include <atomic>
#include <cstring>
#include <functional>

static const char *const TAG = "purifier";

namespace levoit_matter {

namespace {

namespace core = levoit_core;

constexpr int TX_PIN = CONFIG_LEVOIT_UART_TX_GPIO;
constexpr int RX_PIN = CONFIG_LEVOIT_UART_RX_GPIO;
constexpr const char *NVS_NAMESPACE = "levoit";
constexpr const char *NVS_USED_CADR = "used_cadr";
constexpr const char *NVS_RUNTIME = "runtime_min";
constexpr uint32_t REPUBLISH_WINDOW_MS = 3000;
constexpr uint32_t STATUS_QUERY_MS = 5000;
constexpr uint32_t FILTER_LED_RESEND_MS = 1500;
constexpr size_t MAX_TIMEOUTS = 8;

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

Holdoff holdoff;
HoldReset hold_reset;

PurifierState raw_state;
bool have_state = false;
bool status_pending = false;
uint8_t last_level = 0;
MatterView last_view;
bool have_view = false;

uint32_t republish_started_ms = 0;
bool republish_active = false;
uint32_t last_status_ms = 0;
uint32_t last_query_ms = 0;
bool filter_led_on = false;
bool filter_led_sent = false;
uint32_t filter_led_resend_ms = 0;

WifiLed wifi_led = WifiLed::OFF;
WifiLed wifi_led_shown = WifiLed::OFF;
bool identifying = false;
bool started = false;

std::atomic<bool> ready{false};
std::atomic<uint8_t> filter_percent{100};

const char *nvs_key(const char *core_key) {
  if (strcmp(core_key, core::LevoitDevice::PREF_USED_CADR) == 0)
    return NVS_USED_CADR;
  if (strcmp(core_key, core::LevoitDevice::PREF_TOTAL_RUNTIME) == 0)
    return NVS_RUNTIME;
  return nullptr;
}

struct Timeout {
  const char *name{nullptr};
  uint32_t due_ms{0};
  std::function<void()> fn;
};

class Platform : public core::LevoitPlatform {
public:
  void write(const uint8_t *data, size_t len) override { link_write(data, len); }
  uint32_t millis() override { return link_now_ms(); }

  void set_timeout(const char *name, uint32_t ms, std::function<void()> fn) override {
    Timeout *slot = find_(name);
    if (slot == nullptr)
      slot = find_(nullptr);
    if (slot == nullptr) {
      ESP_LOGW(TAG, "no room for timeout %s", name);
      return;
    }
    slot->name = name;
    slot->due_ms = link_now_ms() + ms;
    slot->fn = std::move(fn);
  }

  void cancel_timeout(const char *name) override {
    Timeout *slot = find_(name);
    if (slot != nullptr) {
      slot->name = nullptr;
      slot->fn = nullptr;
    }
  }

  void run_due(uint32_t now_ms) {
    for (Timeout &slot : timeouts_) {
      if (slot.name == nullptr || static_cast<int32_t>(now_ms - slot.due_ms) < 0)
        continue;
      std::function<void()> fn = std::move(slot.fn);
      slot.name = nullptr;
      slot.fn = nullptr;
      if (fn)
        fn();
    }
  }

  bool load_u32(const char *key, uint32_t &out) override {
    const char *mapped = nvs_key(key);
    nvs_handle_t handle;
    if (mapped == nullptr || nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
      return false;
    esp_err_t err = nvs_get_u32(handle, mapped, &out);
    nvs_close(handle);
    return err == ESP_OK;
  }

  void save_u32(const char *key, uint32_t value) override {
    const char *mapped = nvs_key(key);
    nvs_handle_t handle;
    if (mapped == nullptr || nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
      return;
    uint32_t stored = 0;
    esp_err_t err = ESP_OK;
    if (nvs_get_u32(handle, mapped, &stored) != ESP_OK || stored != value) {
      err = nvs_set_u32(handle, mapped, value);
      if (err == ESP_OK)
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK)
      ESP_LOGW(TAG, "save %s failed: %s", mapped, esp_err_to_name(err));
  }

  bool has_clock() override { return false; }
  bool local_time(uint32_t &) override { return false; }

  core::NetworkState network_state() override {
    switch (wifi_led_shown) {
    case WifiLed::SOLID:
      return core::NetworkState::CONNECTED;
    case WifiLed::BLINK:
      return core::NetworkState::CONNECTING;
    default:
      return core::NetworkState::DISABLED;
    }
  }

private:
  Timeout *find_(const char *name) {
    for (Timeout &slot : timeouts_) {
      if (name == nullptr ? slot.name == nullptr
                          : slot.name != nullptr && strcmp(slot.name, name) == 0)
        return &slot;
    }
    return nullptr;
  }

  Timeout timeouts_[MAX_TIMEOUTS];
};

class Host : public core::LevoitHost {
public:
  void switch_changed(core::SwitchType, bool) override {}
  void number_changed(core::NumberType, float) override {}
  void select_changed(core::SelectType, uint32_t) override {}
  void text_changed(core::TextSensorType, const std::string &) override {}

  void sensor_changed(core::SensorType type, float value) override {
    if (type == core::SensorType::FILTER_LIFE_LEFT) {
      filter_percent.store(filter_percent_from_core(value));
      filter_changed_ = true;
    }
  }

  void binary_changed(core::BinarySensorType type, bool state) override {
    if (type != core::BinarySensorType::POWER_BUTTON_HELD)
      return;
    ESP_LOGI(TAG, "button hold %s", state ? "started" : "released");
    hold_reset.feed(state ? HoldReset::STARTED : HoldReset::RELEASED, link_now_ms());
    hold_changed_ = true;
  }

  void fan_changed(int, int, int) override { status_pending = true; }

  bool take_filter_changed() {
    bool changed = filter_changed_;
    filter_changed_ = false;
    return changed;
  }

  bool take_hold_changed() {
    bool changed = hold_changed_;
    hold_changed_ = false;
    return changed;
  }

private:
  bool filter_changed_{false};
  bool hold_changed_{false};
};

Platform platform;
Host host;
core::LevoitDevice device(&host, &platform);

uint32_t filter_capacity() {
  return static_cast<uint32_t>(model.cadr_m3h) * 24 * 30 * model.filter_months;
}

void publish(uint32_t now_ms, bool force) {
  if (!have_state)
    return;
  PurifierState effective = raw_state;
  holdoff.apply(effective, now_ms);
  MatterView view = make_view(model, effective, last_level, filter_percent.load());
  if (!force && have_view && view == last_view && !republish_active)
    return;
  last_view = view;
  have_view = callbacks.publish(view);

  bool led_wanted = view.change_indication == 2;
  bool resend = filter_led_resend_ms != 0 &&
                static_cast<int32_t>(now_ms - filter_led_resend_ms) >= 0;
  if (led_wanted != filter_led_on || !filter_led_sent || resend) {
    filter_led_on = led_wanted;
    filter_led_sent = true;
    filter_led_resend_ms = 0;
    send_to_core(device, command_filter_led(led_wanted));
  }
}

void apply_plan(const Plan &plan, uint32_t now_ms) {
  if (plan.count == 0) {
    publish(now_ms, true);
    return;
  }
  for (uint8_t i = 0; i < plan.count; i++)
    send_to_core(device, plan.commands[i]);
  holdoff.expect(plan, now_ms);
  republish_started_ms = now_ms;
  republish_active = true;
}

void show_wifi_led(uint32_t now_ms) {
  bool attention = identifying || hold_reset.holding();
  WifiLed shown = !attention                    ? wifi_led
                  : wifi_led == WifiLed::BLINK ? WifiLed::SOLID
                                               : WifiLed::BLINK;
  if (shown == wifi_led_shown)
    return;
  wifi_led_shown = shown;
  if (shown == WifiLed::SOLID)
    filter_led_resend_ms = now_ms + FILTER_LED_RESEND_MS;
}

void set_filter_percent(uint8_t percent, uint32_t now_ms) {
  if (percent > 100)
    percent = 100;
  uint64_t used = (static_cast<uint64_t>(filter_capacity()) * (100 - percent) + 99) / 100;
  device.set_used_cadr(static_cast<uint32_t>(used));
  device.publish_filter_stats_now();
  publish(now_ms, true);
}

void handle_request(const Request &request, uint32_t now_ms) {
  if (request.kind == RequestKind::WIFI_LED) {
    wifi_led = static_cast<WifiLed>(request.value);
    show_wifi_led(now_ms);
    return;
  }
  if (request.kind == RequestKind::IDENTIFY) {
    identifying = request.value != 0;
    show_wifi_led(now_ms);
    return;
  }
  if (request.kind == RequestKind::FILTER_SET) {
    set_filter_percent(request.value, now_ms);
    ESP_LOGI(TAG, "filter life set to %u%%", filter_percent.load());
    return;
  }
  if (request.kind == RequestKind::FILTER_RESET) {
    device.set_used_cadr(0);
    device.set_total_runtime(0);
    device.publish_filter_stats_now();
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
  case RequestKind::POWER:
    apply_plan(request.value != 0 ? plan_power_on(current) : plan_fan_off(current), now_ms);
    break;
  case RequestKind::FAN_RESUME: {
    bool manual_on = current.power && current.mode == Mode::MANUAL;
    uint8_t level = manual_on ? current.level : last_level;
    apply_plan(plan_fan_level(model, current, level != 0 ? level : 1), now_ms);
    break;
  }
  case RequestKind::SLEEP:
    apply_plan(plan_sleep(current, request.value != 0), now_ms);
    break;
  case RequestKind::DISPLAY:
    apply_plan(plan_display(current, request.value != 0), now_ms);
    break;
  case RequestKind::CHILD_LOCK:
    apply_plan(plan_child_lock(current, request.value != 0), now_ms);
    break;
  default:
    break;
  }
}

void take_status(uint32_t now_ms) {
  PurifierState next;
  if (!state_from_core(model, device.state(), raw_state, next))
    return;
  raw_state = next;
  have_state = true;
  last_status_ms = now_ms == 0 ? 1 : now_ms;
  if (raw_state.level != 0 &&
      (raw_state.mode == Mode::MANUAL || raw_state.running_level_valid))
    last_level = raw_state.level;
  if (!ready.exchange(true))
    ESP_LOGI(TAG, "MCU %u.%u.%u", raw_state.mcu_major, raw_state.mcu_minor,
             raw_state.mcu_patch);
  publish(now_ms, false);
}

void on_bytes(const uint8_t *data, size_t len, uint32_t now_ms) {
  device.receive(data, len, now_ms);
  if (status_pending) {
    status_pending = false;
    take_status(now_ms);
  }
  if (host.take_hold_changed())
    show_wifi_led(now_ms);
  if (host.take_filter_changed())
    publish(now_ms, false);
}

void on_poll(uint32_t now_ms) {
  if (!started) {
    started = true;
    device.begin();
  }
  platform.run_due(now_ms);

  Request request;
  while (xQueueReceive(requests, &request, 0) == pdTRUE)
    handle_request(request, now_ms);

  device.tick(now_ms);
  if (host.take_filter_changed())
    publish(now_ms, false);

  if (&model == &CORE_300S && now_ms - last_status_ms > STATUS_QUERY_MS &&
      now_ms - last_query_ms > STATUS_QUERY_MS) {
    last_query_ms = now_ms;
    device.sendCommand(core::CommandType::requestCore300sStatus);
  }

  if (filter_led_resend_ms != 0 && static_cast<int32_t>(now_ms - filter_led_resend_ms) >= 0)
    publish(now_ms, true);

  if (hold_reset.due(now_ms)) {
    ESP_LOGW(TAG, "button held, resetting to pairing mode");
    callbacks.factory_reset();
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
  FilterLife filter(model);
  filter.load(used_cadr, runtime);
  filter_percent.store(filter.percent());
}

uint8_t purifier_filter_percent() { return filter_percent.load(); }

esp_err_t purifier_start(const PurifierCallbacks &purifier_callbacks) {
  callbacks = purifier_callbacks;
  if (requests == nullptr)
    requests = xQueueCreate(16, sizeof(Request));
  if (requests == nullptr)
    return ESP_ERR_NO_MEM;
  esp_log_level_set("levoit", ESP_LOG_WARN);
  esp_log_level_set("levoit.decoder", ESP_LOG_WARN);
  esp_log_level_set("levoit.core", ESP_LOG_WARN);
  esp_log_level_set("levoit.core_cmd", ESP_LOG_WARN);
  device.set_device_model(core_model_name(model));
  device.note_number_state(core::NumberType::FILTER_LIFETIME_MONTHS, model.filter_months);
  LinkHandlers handlers = {on_bytes, on_poll};
  return link_start(TX_PIN, RX_PIN, handlers);
}

bool purifier_post(const Request &request) {
  if (requests == nullptr)
    return false;
  return xQueueSend(requests, &request, 0) == pdTRUE;
}

bool purifier_ready() { return ready.load(); }

bool purifier_link_alive() { return link_alive(); }

} // namespace levoit_matter
