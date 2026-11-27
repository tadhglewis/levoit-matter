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

// The MCU's raw fan mode (fan_modes.h) mapped to the three Matter shows:
// Manual, Sleep, Auto. Turbo runs flat out, so it reads as Manual at full
// speed; Pet is adaptive, so it reads as Auto. An unknown mode keeps the last.
Mode mode_from_raw(int raw, Mode previous) {
  switch (raw) {
  case 0: return Mode::MANUAL;
  case 1: return Mode::SLEEP;
  case 2: return Mode::AUTO;
  case 4: return Mode::MANUAL;  // Turbo (EverestAir)
  case 5: return Mode::AUTO;    // Pet (Vital)
  default: return previous;
  }
}

} // namespace

bool state_from_core(const ModelSpec &model, const core::LevoitState &state,
                     const PurifierState &previous, PurifierState &out) {
  const core::FanState &fan = state.fan;
  if (fan.power == -1)
    return false;

  out = previous;
  out.power = fan.power == 1;
  out.mode = mode_from_raw(fan.mode, previous.mode);

  uint8_t speed = speed_in_range(model, fan.speed);
  out.running_level_valid = fan.selected_speed != -1;
  out.level = out.running_level_valid ? speed_in_range(model, fan.selected_speed) : speed;
  out.running_level = out.power ? speed : 0;

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

void send_to_core(core::LevoitDevice &device, const Command &command) {
  bool on = command.value != 0;
  switch (command.kind) {
  case CommandKind::POWER:
    device.on_fan_command(on ? 1 : 0, -1, -1);
    break;
  case CommandKind::LEVEL:
    device.on_fan_command(-1, command.value, -1);
    break;
  case CommandKind::MODE:
    device.on_fan_command(-1, -1, command.value);
    break;
  case CommandKind::DISPLAY:
    device.on_switch_command(core::SwitchType::DISPLAY, on);
    break;
  case CommandKind::CHILD_LOCK:
    device.on_switch_command(core::SwitchType::CHILD_LOCK, on);
    break;
  case CommandKind::FAN:
    device.on_fan_command(command.power, command.speed, command.mode);
    break;
  }
}

} // namespace levoit_matter
