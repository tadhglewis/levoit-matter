#include "device_info.h"

#include "purifier.h"

#include <esp_matter_providers.h>
#include <platform/ConfigurationManager.h>
#include <platform/ESP32/ConfigurationManagerImpl.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.ipp>

#include <cstdio>
#include <cstring>

namespace {

using chip::DeviceLayer::ConfigurationManagerImpl;
using chip::DeviceLayer::Internal::ESP32Config;
using chip::DeviceLayer::Internal::GenericDeviceInstanceInfoProvider;

constexpr const char *VENDOR_NAME = "tadhglewis.dev";
constexpr const char *HARDWARE_VERSION = "Stock ESP32";
constexpr const char *PRODUCT_URL = "https://github.com/tadhglewis/levoit-matter";

bool local_config_disabled = false;
void (*local_config_handler)(bool) = nullptr;

CHIP_ERROR copy(const char *text, char *buf, size_t size) {
  if (strlen(text) + 1 > size)
    return CHIP_ERROR_BUFFER_TOO_SMALL;
  strcpy(buf, text);
  return CHIP_NO_ERROR;
}

class NamedDeviceInfo final : public GenericDeviceInstanceInfoProvider<ESP32Config> {
public:
  NamedDeviceInfo()
      : GenericDeviceInstanceInfoProvider<ESP32Config>(
            ConfigurationManagerImpl::GetDefaultInstance()) {}

  CHIP_ERROR GetVendorName(char *buf, size_t size) override {
    return copy(VENDOR_NAME, buf, size);
  }
  CHIP_ERROR GetHardwareVersionString(char *buf, size_t size) override {
    return copy(HARDWARE_VERSION, buf, size);
  }
  CHIP_ERROR GetProductURL(char *buf, size_t size) override {
    return copy(PRODUCT_URL, buf, size);
  }
  CHIP_ERROR GetLocalConfigDisabled(bool &disabled) override {
    disabled = local_config_disabled;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetLocalConfigDisabled(bool disabled) override {
    if (disabled != local_config_disabled && local_config_handler != nullptr)
      local_config_handler(disabled);
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetProductName(char *buf, size_t size) override {
    char name[32];
    snprintf(name, sizeof(name), "Levoit %s", levoit_matter::purifier_model().name);
    return copy(name, buf, size);
  }
};

} // namespace

void device_info_install() {
  static NamedDeviceInfo named;
  esp_matter::set_custom_device_instance_info_provider(&named);
}

const char *device_info_product_url() { return PRODUCT_URL; }

void device_info_on_local_config_write(void (*handler)(bool disabled)) {
  local_config_handler = handler;
}

bool device_info_set_local_config_disabled(bool disabled) {
  if (local_config_disabled == disabled)
    return false;
  local_config_disabled = disabled;
  return true;
}
