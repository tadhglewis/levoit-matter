#include "matter_setup.h"

#include "commissionable_data.h"
#include "device_info.h"
#include "ota.h"
#include "purifier.h"

#include <esp_log.h>
#include <esp_matter.h>
#include <sdkconfig.h>

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/air-quality-server/AirQualityCluster.h>
#include <app/clusters/fan-control-server/FanControlCluster.h>
#include <app/clusters/fan-control-server/fan-control-delegate.h>
#include <app/clusters/resource-monitoring-server/ResourceMonitoringCluster.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <clusters/resource_monitor/integration.h>
#include <data_model_provider/esp_matter_data_model_provider.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

#include <atomic>
#include <cstdio>

namespace chip::app::Clusters::FanControl {
FanControlCluster *FindClusterOnEndpoint(EndpointId endpoint_id);
void SetDefaultDelegate(EndpointId endpoint_id, Delegate *delegate);
} // namespace chip::app::Clusters::FanControl

static const char *const TAG = "matter";

using namespace chip::app::Clusters;
namespace DeviceEventType = chip::DeviceLayer::DeviceEventType;
using chip::Protocols::InteractionModel::Status;
using levoit::MatterView;
using levoit::Request;
using levoit::RequestKind;

namespace {

uint16_t purifier_endpoint = 0;
uint16_t air_quality_endpoint = 0;
uint16_t sleep_endpoint = 0;
uint16_t display_endpoint = 0;
uint16_t child_lock_endpoint = 0;

bool pushing = false;
std::atomic<bool> network_ready{false};

void post(RequestKind kind, uint8_t value = 0) {
  if (!levoit::purifier_post({kind, value}))
    ESP_LOGW(TAG, "request queue full");
}

void set_wifi_led(levoit::WifiLed state) {
  post(RequestKind::WIFI_LED, static_cast<uint8_t>(state));
}

class FanDelegate final : public FanControl::Delegate {
public:
  FanDelegate() : FanControl::Delegate(chip::kInvalidEndpointId) {}

  Status HandleStep(FanControl::StepDirectionEnum, bool, bool) override {
    return Status::UnsupportedCommand;
  }

  void OnFanDriveStateChanged(const FanControl::FanDriveState &state) override {
    if (pushing || !levoit::purifier_ready())
      return;
    uint8_t level_max = levoit::purifier_model().level_max;
    switch (state.mode) {
    case FanControl::FanModeEnum::kOff:
      post(RequestKind::FAN_OFF);
      return;
    case FanControl::FanModeEnum::kAuto:
    case FanControl::FanModeEnum::kSmart:
      post(RequestKind::FAN_AUTO);
      return;
    default:
      break;
    }
    uint8_t level = 0;
    if (!state.speedSetting.IsNull() && state.speedSetting.Value() >= 1 &&
        state.speedSetting.Value() <= level_max) {
      level = state.speedSetting.Value();
    } else if (!state.percentSetting.IsNull() && state.percentSetting.Value() > 0) {
      level = levoit::percent_to_level(state.percentSetting.Value(), level_max);
    } else if (state.mode == FanControl::FanModeEnum::kLow) {
      level = 1;
    } else if (state.mode == FanControl::FanModeEnum::kMedium) {
      level = static_cast<uint8_t>((level_max + 1) / 2);
    } else {
      level = level_max;
    }
    post(RequestKind::FAN_LEVEL, level);
  }
};

class FilterDelegate final : public ResourceMonitoring::Delegate {
public:
  CHIP_ERROR Init() override { return CHIP_NO_ERROR; }
  Status PreResetCondition() override {
    post(RequestKind::FILTER_RESET);
    return Status::Success;
  }
};

FanDelegate fan_delegate;
FilterDelegate filter_delegate;

void attach_delegates() {
  FanControl::SetDefaultDelegate(purifier_endpoint, &fan_delegate);
  CHIP_ERROR err = ResourceMonitoring::SetDefaultDelegate(
      purifier_endpoint, HepaFilterMonitoring::Id, &filter_delegate);
  if (err != CHIP_NO_ERROR)
    ESP_LOGW(TAG, "filter delegate not attached: %" CHIP_ERROR_FORMAT, err.Format());
}

void report_switch(uint16_t endpoint, bool on) {
  if (endpoint == 0)
    return;
  esp_matter_attr_val_t value = esp_matter_bool(on);
  esp_matter::attribute::report(endpoint, OnOff::Id, OnOff::Attributes::OnOff::Id,
                                &value);
}

void apply_view(const MatterView &view) {
  pushing = true;

  if (auto *fan = FanControl::FindClusterOnEndpoint(purifier_endpoint)) {
    switch (view.fan_mode) {
    case levoit::FanModeView::OFF:
      fan->SetFanMode(FanControl::FanModeEnum::kOff);
      break;
    case levoit::FanModeView::AUTO:
      fan->SetFanMode(FanControl::FanModeEnum::kAuto);
      break;
    default:
      fan->SetPercentSetting(
          chip::app::DataModel::MakeNullable<chip::Percent>(view.percent_setting));
      fan->SetSpeedSetting(
          chip::app::DataModel::MakeNullable<uint8_t>(view.speed_current));
      break;
    }
    if (view.current_valid) {
      fan->SetPercentCurrent(view.percent_current);
      fan->SetSpeedCurrent(view.speed_current);
    }
  }

  if (auto *filter = ResourceMonitoring::GetClusterInstance(
          purifier_endpoint, HepaFilterMonitoring::Id)) {
    filter->UpdateCondition(view.filter_percent);
    filter->UpdateChangeIndication(
        static_cast<ResourceMonitoring::ChangeIndicationEnum>(view.change_indication));
  }

  if (air_quality_endpoint != 0) {
    auto *air_quality =
        esp_matter::data_model::provider::get_instance().registry().Get(
            {air_quality_endpoint, AirQuality::Id});
    if (air_quality != nullptr)
      static_cast<AirQualityCluster *>(air_quality)
          ->SetAirQuality(static_cast<AirQuality::AirQualityEnum>(view.air_quality));

    esp_matter_attr_val_t pm25 = esp_matter_nullable_float(
        view.pm25_valid ? nullable<float>(static_cast<float>(view.pm25))
                        : nullable<float>());
    esp_matter::attribute::report(
        air_quality_endpoint, Pm25ConcentrationMeasurement::Id,
        Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, &pm25);
  }

  report_switch(sleep_endpoint, view.sleep);
  report_switch(display_endpoint, view.display);
  report_switch(child_lock_endpoint, view.child_lock);

  pushing = false;
}

void apply_view_work(intptr_t context) {
  auto *view = reinterpret_cast<MatterView *>(context);
  apply_view(*view);
  delete view;
}

esp_err_t attribute_callback(esp_matter::attribute::callback_type_t type,
                             uint16_t endpoint_id, uint32_t cluster_id,
                             uint32_t attribute_id, esp_matter_attr_val_t *value,
                             void *) {
  if (type != esp_matter::attribute::PRE_UPDATE || pushing ||
      cluster_id != OnOff::Id || attribute_id != OnOff::Attributes::OnOff::Id ||
      value == nullptr)
    return ESP_OK;
  if (!levoit::purifier_ready())
    return ESP_OK;
  uint8_t on = value->val.b ? 1 : 0;
  if (endpoint_id == 0)
    return ESP_OK;
  if (endpoint_id == sleep_endpoint)
    post(RequestKind::SLEEP, on);
  else if (endpoint_id == display_endpoint)
    post(RequestKind::DISPLAY, on);
  else if (endpoint_id == child_lock_endpoint)
    post(RequestKind::CHILD_LOCK, on);
  return ESP_OK;
}

void reset_everything() {
  ota_token_restore_default();
  esp_matter::factory_reset();
}

bool has_fabric() {
  return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

void event_callback(const ChipDeviceEvent *event, intptr_t) {
  switch (event->Type) {
  case DeviceEventType::kServerReady:
    attach_delegates();
    break;
  case DeviceEventType::kInterfaceIpAddressChanged: {
    auto change = event->InterfaceIpAddressChanged.Type;
    if (change == chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Assigned ||
        change == chip::DeviceLayer::InterfaceIpChangeType::kIpV6_Assigned) {
      network_ready.store(true);
      if (has_fabric()) {
        set_wifi_led(levoit::WifiLed::SOLID);
        ota_server_start();
      }
    } else if (change == chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Lost) {
      network_ready.store(false);
      set_wifi_led(levoit::WifiLed::BLINK);
    }
    break;
  }
  case DeviceEventType::kCommissioningComplete:
    ESP_LOGI(TAG, "commissioning complete");
    set_wifi_led(levoit::WifiLed::SOLID);
    ota_server_start();
    break;
  case DeviceEventType::kFabricRemoved:
    if (!has_fabric()) {
      ESP_LOGW(TAG, "last fabric removed, resetting to pairing mode");
      post(RequestKind::CHILD_LOCK, 0);
      CHIP_ERROR err = chip::DeviceLayer::SystemLayer().StartTimer(
          chip::System::Clock::Milliseconds32(1500),
          [](chip::System::Layer *, void *) { reset_everything(); },
          nullptr);
      if (err != CHIP_NO_ERROR)
        reset_everything();
    }
    break;
  default:
    break;
  }
}

bool create_purifier_endpoint(esp_matter::node_t *node) {
  namespace cluster = esp_matter::cluster;
  const levoit::ModelSpec &model = levoit::purifier_model();

  esp_matter::endpoint::air_purifier::config_t config;
  config.fan_control.fan_mode_sequence = static_cast<uint8_t>(
      model.has_auto ? FanControl::FanModeSequenceEnum::kOffLowMedHighAuto
                     : FanControl::FanModeSequenceEnum::kOffLowMedHigh);
  config.fan_control.delegate = &fan_delegate;
  esp_matter::endpoint_t *endpoint = esp_matter::endpoint::air_purifier::create(
      node, &config, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
  if (endpoint == nullptr)
    return false;
  purifier_endpoint = esp_matter::endpoint::get_id(endpoint);

  esp_matter::cluster_t *fan = cluster::get(endpoint, FanControl::Id);
  if (fan == nullptr)
    return false;
  cluster::fan_control::feature::multi_speed::config_t multi_speed;
  multi_speed.speed_max = model.level_max;
  if (cluster::fan_control::feature::multi_speed::add(fan, &multi_speed) != ESP_OK)
    return false;
  if (model.has_auto && cluster::fan_control::feature::fan_auto::add(fan) != ESP_OK)
    return false;

  uint8_t filter_percent = levoit::purifier_filter_percent();
  cluster::hepa_filter_monitoring::config_t filter_config;
  filter_config.delegate = &filter_delegate;
  esp_matter::cluster_t *filter = cluster::hepa_filter_monitoring::create(
      endpoint, &filter_config, esp_matter::CLUSTER_FLAG_SERVER);
  if (filter == nullptr)
    return false;
  if (esp_matter::attribute::create(
          filter, HepaFilterMonitoring::Attributes::DegradationDirection::Id,
          esp_matter::ATTRIBUTE_FLAG_NONE,
          esp_matter_enum8(static_cast<uint8_t>(
              ResourceMonitoring::DegradationDirectionEnum::kDown))) == nullptr)
    return false;
  cluster::hepa_filter_monitoring::feature::condition::config_t condition;
  condition.condition = filter_percent;
  condition.degradation_direction =
      static_cast<uint8_t>(ResourceMonitoring::DegradationDirectionEnum::kDown);
  if (cluster::hepa_filter_monitoring::feature::condition::add(filter, &condition) != ESP_OK)
    return false;
  if (cluster::hepa_filter_monitoring::feature::warning::add(filter) != ESP_OK)
    return false;
  return cluster::hepa_filter_monitoring::command::create_reset_condition(filter) != nullptr;
}

bool create_air_quality_endpoint(esp_matter::node_t *node) {
  namespace cluster = esp_matter::cluster;

  esp_matter::endpoint::air_quality_sensor::config_t config;
  esp_matter::endpoint_t *endpoint =
      esp_matter::endpoint::air_quality_sensor::create(
          node, &config, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
  if (endpoint == nullptr)
    return false;
  air_quality_endpoint = esp_matter::endpoint::get_id(endpoint);

  esp_matter::cluster_t *air_quality = cluster::get(endpoint, AirQuality::Id);
  if (air_quality == nullptr ||
      cluster::air_quality::feature::fair::add(air_quality) != ESP_OK ||
      cluster::air_quality::feature::moderate::add(air_quality) != ESP_OK)
    return false;

  cluster::pm25_concentration_measurement::config_t pm25;
  pm25.measurement_medium = 0;
  pm25.feature_flags =
      cluster::concentration_measurement::feature::numeric_measurement::get_id();
  pm25.features.numeric_measurement.min_measured_value = nullable<float>(0.0f);
  pm25.features.numeric_measurement.max_measured_value = nullable<float>(1000.0f);
  pm25.features.numeric_measurement.measurement_unit = 4;
  return cluster::pm25_concentration_measurement::create(
             endpoint, &pm25, esp_matter::CLUSTER_FLAG_SERVER) != nullptr;
}

uint16_t create_switch_endpoint(esp_matter::node_t *node) {
  esp_matter::endpoint::on_off_plug_in_unit::config_t config;
  esp_matter::endpoint_t *endpoint =
      esp_matter::endpoint::on_off_plug_in_unit::create(
          node, &config, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
  return endpoint == nullptr ? 0 : esp_matter::endpoint::get_id(endpoint);
}

void print_onboarding_codes(bool factory) {
  if (factory) {
    printf("[pairing] setup code is on the label made by tools/factory.sh\n");
    fflush(stdout);
    return;
  }
  static char buffer[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
  static char url[256];
  chip::RendezvousInformationFlags flags(chip::RendezvousInformationFlag::kBLE);

  chip::MutableCharSpan qr(buffer);
  if (GetQRCode(qr, flags) == CHIP_NO_ERROR) {
    printf("[pairing] QR code: %.*s\n", static_cast<int>(qr.size()), qr.data());
    if (GetQRCodeUrl(url, sizeof(url), qr) == CHIP_NO_ERROR)
      printf("[pairing] QR image: %s\n", url);
  }
  chip::MutableCharSpan manual(buffer);
  if (GetManualPairingCode(manual, flags) == CHIP_NO_ERROR)
    printf("[pairing] manual code: %.*s\n", static_cast<int>(manual.size()),
           manual.data());
  fflush(stdout);
}

} // namespace

esp_err_t matter_setup_start() {
  bool factory_setup_code = commissionable_data_install();
  device_info_install();

  esp_matter::node::config_t node_config;
  esp_matter::node_t *node =
      esp_matter::node::create(&node_config, attribute_callback, nullptr);
  if (node == nullptr)
    return ESP_FAIL;

  if (!create_purifier_endpoint(node))
    return ESP_FAIL;
  if (levoit::has_air_quality(levoit::purifier_model()) &&
      !create_air_quality_endpoint(node))
    return ESP_FAIL;
  sleep_endpoint = create_switch_endpoint(node);
  if (sleep_endpoint == 0)
    return ESP_FAIL;
#if CONFIG_LEVOIT_DISPLAY_SWITCH
  display_endpoint = create_switch_endpoint(node);
  if (display_endpoint == 0)
    return ESP_FAIL;
#endif
#if CONFIG_LEVOIT_CHILD_LOCK_SWITCH
  child_lock_endpoint = create_switch_endpoint(node);
  if (child_lock_endpoint == 0)
    return ESP_FAIL;
#endif
  ESP_LOGI(TAG, "endpoints: purifier=%u air_quality=%u sleep=%u display=%u child_lock=%u",
           purifier_endpoint, air_quality_endpoint, sleep_endpoint,
           display_endpoint, child_lock_endpoint);

  esp_err_t err = esp_matter::start(event_callback);
  if (err != ESP_OK)
    return err;

  CHIP_ERROR scheduled = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
    attach_delegates();
    if (!has_fabric())
      set_wifi_led(levoit::WifiLed::BLINK);
    uint8_t direction = 0xFF;
    if (auto *filter = ResourceMonitoring::GetClusterInstance(
            purifier_endpoint, HepaFilterMonitoring::Id)) {
      direction = static_cast<uint8_t>(filter->GetDegradationDirection());
      uint8_t percent = levoit::purifier_filter_percent();
      filter->UpdateCondition(percent);
      filter->UpdateChangeIndication(
          static_cast<ResourceMonitoring::ChangeIndicationEnum>(
              levoit::change_indication_from_percent(percent)));
    }
    ESP_LOGI(TAG, "filter degradation direction=%u (1 is Down), fabrics=%u",
             direction, chip::Server::GetInstance().GetFabricTable().FabricCount());
  });
  if (scheduled != CHIP_NO_ERROR)
    ESP_LOGW(TAG, "post-start work not scheduled: %" CHIP_ERROR_FORMAT, scheduled.Format());

  print_onboarding_codes(factory_setup_code);
  return ESP_OK;
}

bool matter_publish(const MatterView &view) {
  auto *copy = new MatterView(view);
  CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(
      apply_view_work, reinterpret_cast<intptr_t>(copy));
  if (err != CHIP_NO_ERROR) {
    delete copy;
    ESP_LOGW(TAG, "publish not scheduled: %" CHIP_ERROR_FORMAT, err.Format());
    return false;
  }
  return true;
}

void matter_factory_reset() {
  CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t) { reset_everything(); });
  if (err != CHIP_NO_ERROR)
    ESP_LOGE(TAG, "factory reset not scheduled: %" CHIP_ERROR_FORMAT, err.Format());
}

bool matter_network_ready() { return network_ready.load(); }
