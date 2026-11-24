#include "device_info.h"

#include "purifier.h"

#include <platform/DeviceInstanceInfoProvider.h>

#include <cstdio>
#include <cstring>

namespace {

using chip::DeviceLayer::DeviceInstanceInfoProvider;

constexpr const char *VENDOR_NAME = "levoit-matter";

CHIP_ERROR copy(const char *text, char *buf, size_t size) {
  if (strlen(text) + 1 > size)
    return CHIP_ERROR_BUFFER_TOO_SMALL;
  strcpy(buf, text);
  return CHIP_NO_ERROR;
}

class NamedDeviceInfo final : public DeviceInstanceInfoProvider {
public:
  void wrap(DeviceInstanceInfoProvider *inner) { this->inner_ = inner; }

  CHIP_ERROR GetVendorName(char *buf, size_t size) override {
    return copy(VENDOR_NAME, buf, size);
  }
  CHIP_ERROR GetProductName(char *buf, size_t size) override {
    char name[32];
    snprintf(name, sizeof(name), "Levoit %s", levoit::purifier_model().name);
    return copy(name, buf, size);
  }
  CHIP_ERROR GetVendorId(uint16_t &id) override { return this->inner_->GetVendorId(id); }
  CHIP_ERROR GetProductId(uint16_t &id) override { return this->inner_->GetProductId(id); }
  CHIP_ERROR GetPartNumber(char *buf, size_t size) override {
    return this->inner_->GetPartNumber(buf, size);
  }
  CHIP_ERROR GetProductURL(char *buf, size_t size) override {
    return this->inner_->GetProductURL(buf, size);
  }
  CHIP_ERROR GetProductLabel(char *buf, size_t size) override {
    return this->inner_->GetProductLabel(buf, size);
  }
  CHIP_ERROR GetSerialNumber(char *buf, size_t size) override {
    return this->inner_->GetSerialNumber(buf, size);
  }
  CHIP_ERROR GetManufacturingDate(uint16_t &year, uint8_t &month, uint8_t &day) override {
    return this->inner_->GetManufacturingDate(year, month, day);
  }
  CHIP_ERROR GetManufacturingDateSuffix(chip::MutableCharSpan &suffix) override {
    return this->inner_->GetManufacturingDateSuffix(suffix);
  }
  CHIP_ERROR GetHardwareVersion(uint16_t &version) override {
    return this->inner_->GetHardwareVersion(version);
  }
  CHIP_ERROR GetHardwareVersionString(char *buf, size_t size) override {
    return this->inner_->GetHardwareVersionString(buf, size);
  }
  CHIP_ERROR GetRotatingDeviceIdUniqueId(chip::MutableByteSpan &id) override {
    return this->inner_->GetRotatingDeviceIdUniqueId(id);
  }
  CHIP_ERROR GetProductFinish(chip::app::Clusters::BasicInformation::ProductFinishEnum *finish) override {
    return this->inner_->GetProductFinish(finish);
  }
  CHIP_ERROR GetProductPrimaryColor(chip::app::Clusters::BasicInformation::ColorEnum *color) override {
    return this->inner_->GetProductPrimaryColor(color);
  }
  CHIP_ERROR GetJointFabricMode(uint8_t &mode) override {
    return this->inner_->GetJointFabricMode(mode);
  }
  CHIP_ERROR GetLocalConfigDisabled(bool &disabled) override {
    return this->inner_->GetLocalConfigDisabled(disabled);
  }
  CHIP_ERROR SetLocalConfigDisabled(bool disabled) override {
    return this->inner_->SetLocalConfigDisabled(disabled);
  }
  DeviceInfoCapabilityMinimas GetSupportedCapabilityMinimaValues() override {
    return this->inner_->GetSupportedCapabilityMinimaValues();
  }

private:
  DeviceInstanceInfoProvider *inner_ = nullptr;
};

NamedDeviceInfo named;

} // namespace

void device_info_install() {
  DeviceInstanceInfoProvider *current = chip::DeviceLayer::GetDeviceInstanceInfoProvider();
  if (current == nullptr || current == &named)
    return;
  named.wrap(current);
  chip::DeviceLayer::SetDeviceInstanceInfoProvider(&named);
}
