#include "ota.h"

#include "matter_setup.h"
#include "purifier.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>

static const char *const TAG = "ota";

namespace {

constexpr const char *TOKEN = CONFIG_LEVOIT_OTA_TOKEN;
constexpr size_t MIN_IMAGE_SIZE = 64 * 1024;
constexpr int MAX_CONSECUTIVE_TIMEOUTS = 15;
constexpr int64_t RECEIVE_DEADLINE_US = 10LL * 60 * 1000 * 1000;
constexpr uint32_t HTTPD_STACK = 6144;
constexpr uint32_t HEALTH_DEADLINE_MS = 120000;
constexpr uint32_t HEALTH_POLL_MS = 1000;

httpd_handle_t server = nullptr;
std::atomic<bool> pending_verify{false};

bool constant_time_equal(const char *a, const char *b) {
  size_t length_a = strlen(a);
  size_t length_b = strlen(b);
  uint8_t difference = static_cast<uint8_t>(length_a ^ length_b);
  for (size_t i = 0; i < length_a && i < length_b; i++)
    difference |= static_cast<uint8_t>(a[i] ^ b[i]);
  return difference == 0;
}

bool authorized(httpd_req_t *request) {
  char header[160];
  if (httpd_req_get_hdr_value_str(request, "Authorization", header,
                                  sizeof(header)) != ESP_OK)
    return false;
  static const char PREFIX[] = "Bearer ";
  if (strncmp(header, PREFIX, sizeof(PREFIX) - 1) != 0)
    return false;
  return constant_time_equal(header + sizeof(PREFIX) - 1, TOKEN);
}

esp_err_t fail(httpd_req_t *request, httpd_err_code_t code, const char *message) {
  ESP_LOGE(TAG, "upload rejected: %s", message);
  httpd_resp_send_err(request, code, message);
  return ESP_FAIL;
}

esp_err_t handle_upload(httpd_req_t *request) {
  if (!authorized(request))
    return fail(request, HTTPD_401_UNAUTHORIZED, "bad token");
  if (pending_verify.load())
    return fail(request, HTTPD_400_BAD_REQUEST, "running image not confirmed yet");

  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (slot == nullptr)
    return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "no update slot");
  if (request->content_len < MIN_IMAGE_SIZE)
    return fail(request, HTTPD_400_BAD_REQUEST, "image missing or too small");
  if (request->content_len > slot->size)
    return fail(request, HTTPD_400_BAD_REQUEST, "image larger than the slot");

  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(slot, request->content_len, &handle) != ESP_OK) {
    if (handle != 0)
      esp_ota_abort(handle);
    return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
  }

  static char buffer[1024];
  size_t remaining = request->content_len;
  int timeouts = 0;
  int64_t deadline = esp_timer_get_time() + RECEIVE_DEADLINE_US;
  while (remaining > 0) {
    if (esp_timer_get_time() > deadline) {
      esp_ota_abort(handle);
      return fail(request, HTTPD_408_REQ_TIMEOUT, "deadline exceeded");
    }
    int received = httpd_req_recv(request, buffer, sizeof(buffer));
    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      if (++timeouts > MAX_CONSECUTIVE_TIMEOUTS) {
        esp_ota_abort(handle);
        return fail(request, HTTPD_408_REQ_TIMEOUT, "client stalled");
      }
      continue;
    }
    timeouts = 0;
    if (received <= 0) {
      esp_ota_abort(handle);
      return fail(request, HTTPD_400_BAD_REQUEST, "receive failed");
    }
    if (esp_ota_write(handle, buffer, static_cast<size_t>(received)) != ESP_OK) {
      esp_ota_abort(handle);
      return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "flash write failed");
    }
    remaining -= static_cast<size_t>(received);
  }

  if (esp_ota_end(handle) != ESP_OK)
    return fail(request, HTTPD_400_BAD_REQUEST, "image validation failed");
  if (esp_ota_set_boot_partition(slot) != ESP_OK)
    return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot slot failed");

  ESP_LOGI(TAG, "image written to %s, rebooting", slot->label);
  httpd_resp_sendstr(request, "ok, rebooting\n");
  vTaskDelay(pdMS_TO_TICKS(800));
  esp_restart();
  return ESP_OK;
}

void health_task(void *) {
  uint32_t waited = 0;
  while (waited < HEALTH_DEADLINE_MS) {
    if (matter_network_ready() && ota_server_running() &&
        levoit::purifier_link_alive()) {
      esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
      ESP_LOGI(TAG, "new image confirmed: %s", esp_err_to_name(err));
      if (err == ESP_OK)
        pending_verify.store(false);
      vTaskDelete(nullptr);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(HEALTH_POLL_MS));
    waited += HEALTH_POLL_MS;
  }
  ESP_LOGE(TAG, "new image unhealthy after %u s, rolling back",
           static_cast<unsigned>(HEALTH_DEADLINE_MS / 1000));
  esp_ota_mark_app_invalid_rollback_and_reboot();
  esp_restart();
}

} // namespace

void ota_server_start() {
  if (server != nullptr)
    return;
  if (TOKEN[0] == '\0') {
    ESP_LOGW(TAG, "LEVOIT_OTA_TOKEN is empty; upload endpoint disabled");
    return;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = HTTPD_STACK;
  config.max_open_sockets = 2;
  config.lru_purge_enable = true;
  if (httpd_start(&server, &config) != ESP_OK) {
    server = nullptr;
    ESP_LOGE(TAG, "http server failed to start");
    return;
  }
  httpd_uri_t upload = {};
  upload.uri = "/ota";
  upload.method = HTTP_POST;
  upload.handler = handle_upload;
  if (httpd_register_uri_handler(server, &upload) != ESP_OK) {
    httpd_stop(server);
    server = nullptr;
    ESP_LOGE(TAG, "failed to register /ota");
  }
}

bool ota_server_running() { return server != nullptr; }

void ota_health_start() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running == nullptr ||
      esp_ota_get_state_partition(running, &state) != ESP_OK ||
      state != ESP_OTA_IMG_PENDING_VERIFY)
    return;
  pending_verify.store(true);
  if (xTaskCreate(health_task, "ota_health", 3072, nullptr, 3, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "health task failed to start");
    pending_verify.store(false);
  }
}
