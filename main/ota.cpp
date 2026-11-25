#include "ota.h"

#include "matter_setup.h"
#include "purifier.h"
#include "remote_log.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <nvs.h>
#include <sdkconfig.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

static const char *const TAG = "ota";

namespace {

constexpr const char *FACTORY_PARTITION = CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL;
constexpr const char *NVS_NAMESPACE = "levoit";
constexpr const char *FACTORY_SALT = "ota-salt";
constexpr const char *FACTORY_HASH = "ota-hash";
constexpr const char *OVERRIDE_SALT = "ota_salt";
constexpr const char *OVERRIDE_HASH = "ota_hash";
constexpr size_t SALT_LEN = 16;
constexpr size_t HASH_LEN = 32;
constexpr size_t MIN_TOKEN_LEN = 12;
constexpr size_t MAX_TOKEN_LEN = 64;
constexpr size_t MIN_IMAGE_SIZE = 64 * 1024;
constexpr int MAX_CONSECUTIVE_TIMEOUTS = 15;
constexpr int64_t RECEIVE_DEADLINE_US = 10LL * 60 * 1000 * 1000;
constexpr uint32_t HTTPD_STACK = 6144;
constexpr uint32_t HEALTH_DEADLINE_MS = 120000;
constexpr uint32_t HEALTH_POLL_MS = 1000;

httpd_handle_t server = nullptr;
std::atomic<bool> pending_verify{false};

struct Credentials {
  bool present = false;
  uint8_t salt[SALT_LEN] = {};
  uint8_t hash[HASH_LEN] = {};
};

Credentials credentials;

bool read_credentials(nvs_handle_t handle, const char *salt_key,
                      const char *hash_key, Credentials &out) {
  size_t salt_len = SALT_LEN;
  size_t hash_len = HASH_LEN;
  if (nvs_get_blob(handle, salt_key, out.salt, &salt_len) != ESP_OK ||
      nvs_get_blob(handle, hash_key, out.hash, &hash_len) != ESP_OK ||
      salt_len != SALT_LEN || hash_len != HASH_LEN)
    return false;
  out.present = true;
  return true;
}

void load_credentials() {
  Credentials loaded;
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
    bool found = read_credentials(handle, OVERRIDE_SALT, OVERRIDE_HASH, loaded);
    nvs_close(handle);
    if (found) {
      credentials = loaded;
      ESP_LOGI(TAG, "using a changed update token");
      return;
    }
  }
  if (nvs_open_from_partition(FACTORY_PARTITION, NVS_NAMESPACE, NVS_READONLY,
                              &handle) == ESP_OK) {
    read_credentials(handle, FACTORY_SALT, FACTORY_HASH, loaded);
    nvs_close(handle);
  }
  credentials = loaded;
}

bool hash_token(const uint8_t *salt, const char *token, size_t token_len,
                uint8_t *out) {
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool ok = mbedtls_sha256_starts(&context, 0) == 0 &&
            mbedtls_sha256_update(&context, salt, SALT_LEN) == 0 &&
            mbedtls_sha256_update(&context,
                                  reinterpret_cast<const uint8_t *>(token),
                                  token_len) == 0 &&
            mbedtls_sha256_finish(&context, out) == 0;
  mbedtls_sha256_free(&context);
  return ok;
}

bool authorized(httpd_req_t *request) {
  char header[160];
  if (!credentials.present ||
      httpd_req_get_hdr_value_str(request, "Authorization", header,
                                  sizeof(header)) != ESP_OK)
    return false;
  static const char PREFIX[] = "Bearer ";
  if (strncmp(header, PREFIX, sizeof(PREFIX) - 1) != 0)
    return false;
  const char *token = header + sizeof(PREFIX) - 1;
  uint8_t hash[HASH_LEN];
  if (!hash_token(credentials.salt, token, strlen(token), hash))
    return false;
  uint8_t difference = 0;
  for (size_t i = 0; i < HASH_LEN; i++)
    difference |= static_cast<uint8_t>(hash[i] ^ credentials.hash[i]);
  return difference == 0;
}

esp_err_t fail(httpd_req_t *request, httpd_err_code_t code, const char *message) {
  ESP_LOGE(TAG, "upload rejected: %s", message);
  httpd_resp_send_err(request, code, message);
  return ESP_FAIL;
}

#if CONFIG_LEVOIT_HTTP_OTA
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
#endif

esp_err_t handle_set_token(httpd_req_t *request) {
  if (!authorized(request))
    return fail(request, HTTPD_401_UNAUTHORIZED, "bad token");
  if (request->content_len < MIN_TOKEN_LEN || request->content_len > MAX_TOKEN_LEN)
    return fail(request, HTTPD_400_BAD_REQUEST, "new token must be 12 to 64 characters");

  char token[MAX_TOKEN_LEN];
  size_t have = 0;
  while (have < request->content_len) {
    int received = httpd_req_recv(request, token + have, request->content_len - have);
    if (received <= 0)
      return fail(request, HTTPD_400_BAD_REQUEST, "receive failed");
    have += static_cast<size_t>(received);
  }
  for (size_t i = 0; i < have; i++)
    if (token[i] <= 0x20 || token[i] >= 0x7F)
      return fail(request, HTTPD_400_BAD_REQUEST, "new token has a space or non-printable character");

  Credentials changed;
  esp_fill_random(changed.salt, SALT_LEN);
  if (!hash_token(changed.salt, token, have, changed.hash))
    return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "hash failed");
  changed.present = true;

  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "storage unavailable");
  esp_err_t err = nvs_set_blob(handle, OVERRIDE_SALT, changed.salt, SALT_LEN);
  if (err == ESP_OK)
    err = nvs_set_blob(handle, OVERRIDE_HASH, changed.hash, HASH_LEN);
  if (err == ESP_OK)
    err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK)
    return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "storage write failed");

  credentials = changed;
  ESP_LOGI(TAG, "update token changed");
  httpd_resp_sendstr(request, "ok, token changed\n");
  return ESP_OK;
}

esp_err_t handle_filter(httpd_req_t *request) {
  if (!authorized(request))
    return fail(request, HTTPD_401_UNAUTHORIZED, "bad token");
  char body[4] = {};
  if (request->content_len < 1 || request->content_len > 3 ||
      httpd_req_recv(request, body, request->content_len) !=
          static_cast<int>(request->content_len))
    return fail(request, HTTPD_400_BAD_REQUEST, "send a percentage, 0 to 100");
  int percent = 0;
  for (size_t i = 0; i < request->content_len; i++) {
    if (body[i] < '0' || body[i] > '9')
      return fail(request, HTTPD_400_BAD_REQUEST, "send a percentage, 0 to 100");
    percent = percent * 10 + (body[i] - '0');
  }
  if (percent > 100)
    return fail(request, HTTPD_400_BAD_REQUEST, "send a percentage, 0 to 100");
  if (!levoit::purifier_post({levoit::RequestKind::FILTER_SET, static_cast<uint8_t>(percent)}))
    return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "busy, try again");
  httpd_resp_sendstr(request, "ok, filter life set\n");
  return ESP_OK;
}

esp_err_t handle_log(httpd_req_t *request) {
  if (!authorized(request))
    return fail(request, HTTPD_401_UNAUTHORIZED, "bad token");
  char *copy = static_cast<char *>(malloc(remote_log_capacity()));
  if (copy == nullptr)
    return fail(request, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
  size_t len = remote_log_copy(copy, remote_log_capacity());
  httpd_resp_set_type(request, "text/plain");
  esp_err_t err = httpd_resp_send(request, copy, static_cast<ssize_t>(len));
  free(copy);
  return err;
}

void health_task(void *) {
  uint32_t waited = 0;
  while (waited < HEALTH_DEADLINE_MS) {
    if (matter_network_ready() && (ota_server_running() || !credentials.present) &&
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
  load_credentials();
  if (!credentials.present) {
    ESP_LOGW(TAG, "no update token in the factory data; the log and filter endpoints are off");
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
  httpd_uri_t set_token = {};
  set_token.uri = "/ota/token";
  set_token.method = HTTP_POST;
  set_token.handler = handle_set_token;
  httpd_uri_t log = {};
  log.uri = "/log";
  log.method = HTTP_GET;
  log.handler = handle_log;
  httpd_uri_t filter = {};
  filter.uri = "/filter";
  filter.method = HTTP_POST;
  filter.handler = handle_filter;
#if CONFIG_LEVOIT_HTTP_OTA
  httpd_uri_t upload = {};
  upload.uri = "/ota";
  upload.method = HTTP_POST;
  upload.handler = handle_upload;
  if (httpd_register_uri_handler(server, &upload) != ESP_OK) {
    httpd_stop(server);
    server = nullptr;
    ESP_LOGE(TAG, "failed to register the update endpoint");
    return;
  }
#endif
  if (httpd_register_uri_handler(server, &set_token) != ESP_OK ||
      httpd_register_uri_handler(server, &log) != ESP_OK ||
      httpd_register_uri_handler(server, &filter) != ESP_OK) {
    httpd_stop(server);
    server = nullptr;
    ESP_LOGE(TAG, "failed to register the endpoints");
  }
}

void ota_token_restore_default() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return;
  nvs_erase_key(handle, OVERRIDE_SALT);
  nvs_erase_key(handle, OVERRIDE_HASH);
  nvs_commit(handle);
  nvs_close(handle);
}

bool ota_server_running() { return server != nullptr; }

bool ota_image_unconfirmed() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  return running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

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
