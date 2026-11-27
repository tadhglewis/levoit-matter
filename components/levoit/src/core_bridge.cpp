#include "core_bridge.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace levoit_matter {

namespace {

namespace core = levoit_core;

uint8_t speed_in_range(const ModelSpec &model, int speed) {
  return speed >= 1 && speed <= model.level_max ? static_cast<uint8_t>(speed) : 0;
}

} // namespace

const char *core_model_name(const ModelSpec &model) {
  if (&model == &CORE_200S)
    return "CORE200S";
  if (&model == &CORE_400S)
    return "CORE400S";
  if (&model == &CORE_600S)
    return "CORE600S";
  return "CORE300S";
}

bool state_from_core(const ModelSpec &model, const core::LevoitState &state,
                     const PurifierState &previous, PurifierState &out) {
  const core::FanState &fan = state.fan;
  if (fan.power == -1)
    return false;

  out = previous;
  out.power = fan.power == 1;
  if (fan.mode >= 0 && fan.mode <= static_cast<int>(Mode::AUTO))
    out.mode = static_cast<Mode>(fan.mode);

  bool reports_running = &model == &CORE_300S;
  uint8_t speed = speed_in_range(model, fan.speed);
  out.running_level_valid = reports_running;
  if (out.power && out.mode != Mode::MANUAL && reports_running) {
    out.running_level = speed;
  } else {
    out.level = speed;
    out.running_level = out.power ? speed : 0;
  }

  bool on = false;
  if (state.get_switch(core::SwitchType::DISPLAY, on))
    out.display_on = on;
  if (state.get_switch(core::SwitchType::CHILD_LOCK, on))
    out.child_lock = on;

  float value = 0;
  if (state.get_sensor(core::SensorType::AQI, value))
    out.air_quality = static_cast<uint8_t>(value);
  if (state.get_sensor(core::SensorType::PM25, value)) {
    out.pm25_valid = true;
    out.pm25 = static_cast<uint16_t>(value);
  }

  uint32_t select = 0;
  if (state.get_select(core::SelectType::AUTO_MODE, select))
    out.auto_profile = static_cast<uint8_t>(select);

  std::string version;
  unsigned major = 0, minor = 0, patch = 0;
  if (state.get_text(core::TextSensorType::MCU_VERSION, version) &&
      std::sscanf(version.c_str(), "%u.%u.%u", &major, &minor, &patch) == 3) {
    out.mcu_major = static_cast<uint8_t>(major);
    out.mcu_minor = static_cast<uint8_t>(minor);
    out.mcu_patch = static_cast<uint8_t>(patch);
  }
  return true;
}

uint8_t filter_percent_from_core(float life_left) {
  if (!(life_left > 0))
    return 0;
  if (life_left >= 100)
    return 100;
  return static_cast<uint8_t>(std::ceil(life_left - 0.001f));
}

bool send_to_core(core::LevoitDevice &device, const Command &command) {
  const uint8_t *p = command.payload;
  switch (command.command) {
  case 0x0100A0:
    device.on_fan_command(p[0] != 0 ? 1 : 0, -1, -1);
    return true;
  case 0x0160A2:
    device.on_fan_command(-1, p[2], -1);
    return true;
  case 0x01E0A5:
    device.on_fan_command(-1, -1, p[0]);
    return true;
  case 0x0105A1:
    device.on_switch_command(core::SwitchType::DISPLAY, p[0] != 0);
    return true;
  case 0x0100D1:
    device.on_switch_command(core::SwitchType::CHILD_LOCK, p[0] != 0);
    return true;
  case 0x01E2A5:
    device.sendCommand(p[0] != 0 ? core::CommandType::setFilterLedOn
                                 : core::CommandType::setFilterLedOff);
    return true;
  default:
    return false;
  }
}

} // namespace levoit_matter
