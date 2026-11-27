#include "matter_setup.h"
#include "ota.h"
#include "purifier.h"
#include "remote_log.h"

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <cstring>
#include <map>
#include <mbedtls/sha256.h>
#include <string>

static const char *const TAG = "app";

static unsigned stack_free(const char *name) {
  TaskHandle_t task = xTaskGetHandle(name);
  return task == nullptr ? 0 : static_cast<unsigned>(uxTaskGetStackHighWaterMark(task));
}

static void log_resources() {
  ESP_LOGI(TAG,
           "heap free=%u min=%u largest=%u stack link=%u chip=%u httpd=%u main=%u",
           static_cast<unsigned>(esp_get_free_heap_size()),
           static_cast<unsigned>(esp_get_minimum_free_heap_size()),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
           stack_free("levoit_link"), stack_free("CHIP"), stack_free("httpd"),
           static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

static void report_traces(const esp_partition_t *other) {
  std::map<std::string, int> namespaces;
  nvs_iterator_t it = nullptr;
  for (esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY, &it);
       err == ESP_OK; err = nvs_entry_next(&it)) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    namespaces[info.namespace_name]++;
  }
  nvs_release_iterator(it);
  for (const auto &entry : namespaces)
    ESP_LOGI(TAG, "trace: nvs namespace %s, %d keys", entry.first.c_str(), entry.second);

  static uint8_t chunk[1024];
  const esp_partition_t *fctry =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fctry");
  if (fctry != nullptr) {
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    for (uint32_t at = 0; at < fctry->size; at += sizeof(chunk)) {
      esp_partition_read(fctry, at, chunk, sizeof(chunk));
      mbedtls_sha256_update(&sha, chunk, sizeof(chunk));
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    char hex[65];
    for (int i = 0; i < 32; i++)
      snprintf(hex + i * 2, 3, "%02x", digest[i]);
    ESP_LOGI(TAG, "trace: fctry sha256 %s", hex);
  }

  if (other != nullptr) {
    uint32_t written = 0;
    for (uint32_t at = 0; at < other->size; at += sizeof(chunk)) {
      esp_partition_read(other, at, chunk, sizeof(chunk));
      for (uint8_t byte : chunk)
        written += byte != 0xFF;
    }
    ESP_LOGI(TAG, "trace: %s has %u bytes not erased", other->label, static_cast<unsigned>(written));
  }
}

static void erase_other_firmware(void *) {
  while (ota_image_unconfirmed() || !matter_network_ready())
    vTaskDelay(pdMS_TO_TICKS(5000));
  vTaskDelay(pdMS_TO_TICKS(30000));
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
  esp_app_desc_t mine, theirs;
  if (other != nullptr && esp_ota_get_partition_description(running, &mine) == ESP_OK &&
      esp_ota_get_partition_description(other, &theirs) == ESP_OK &&
      strncmp(mine.project_name, theirs.project_name, sizeof(mine.project_name)) != 0) {
    ESP_LOGW(TAG, "erasing %s from %s, heap free=%u min=%u", theirs.project_name, other->label,
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(esp_get_minimum_free_heap_size()));
    constexpr uint32_t STEP = 64 * 1024;
    for (uint32_t at = 0; at < other->size; at += STEP) {
      esp_partition_erase_range(other, at, STEP < other->size - at ? STEP : other->size - at);
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "erased, heap free=%u min=%u", static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(esp_get_minimum_free_heap_size()));
  }
  report_traces(other);
  vTaskDelete(nullptr);
}

extern "C" void app_main() {
  remote_log_install();
  esp_log_level_set("esp_matter_attribute", ESP_LOG_WARN);
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  nvs_handle_t esphome;
  if (nvs_open("esphome", NVS_READONLY, &esphome) == ESP_OK) {
    nvs_close(esphome);
    ESP_LOGW(TAG, "ESPHome settings found, erasing all settings");
    ESP_ERROR_CHECK(nvs_flash_deinit());
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  levoit_matter::purifier_load();
  ESP_LOGI(TAG, "%s, filter %u%%", levoit_matter::purifier_model().name,
           levoit_matter::purifier_filter_percent());

  ESP_ERROR_CHECK(matter_setup_start());
  ESP_ERROR_CHECK(levoit_matter::purifier_start({matter_publish, matter_factory_reset}));
  ota_health_start();
  xTaskCreate(erase_other_firmware, "erase_other", 6144, nullptr, 1, nullptr);

  log_resources();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(60000));
    log_resources();
  }
}
