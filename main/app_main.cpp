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

void erase_other_firmware(void *) {
  while (ota_image_unconfirmed())
    vTaskDelay(pdMS_TO_TICKS(5000));
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
  esp_app_desc_t mine, theirs;
  if (other != nullptr && esp_ota_get_partition_description(running, &mine) == ESP_OK &&
      esp_ota_get_partition_description(other, &theirs) == ESP_OK &&
      strncmp(mine.project_name, theirs.project_name, sizeof(mine.project_name)) != 0) {
    ESP_LOGW(TAG, "erasing %s from %s", theirs.project_name, other->label);
    esp_partition_erase_range(other, 0, other->size);
  }
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

  levoit::purifier_load();
  ESP_LOGI(TAG, "%s, filter %u%%", levoit::purifier_model().name,
           levoit::purifier_filter_percent());

  ESP_ERROR_CHECK(matter_setup_start());
  ESP_ERROR_CHECK(levoit::purifier_start({matter_publish, matter_factory_reset}));
  ota_health_start();
  xTaskCreate(erase_other_firmware, "erase_other", 3072, nullptr, 1, nullptr);

  log_resources();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(60000));
    log_resources();
  }
}
