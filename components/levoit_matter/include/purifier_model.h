#pragma once

#include <cstdint>

namespace levoit_matter {

enum class Mode : uint8_t { MANUAL = 0, SLEEP = 1, AUTO = 2 };
enum class WifiLed : uint8_t { OFF, SOLID, BLINK };

// What the Matter mapping needs about the model, all filled at startup from the
// core's own accessors (purifier.cpp).
struct ModelSpec {
  const char *name;
  uint8_t level_max;
  bool has_auto;
  bool has_air_quality;
};

struct PurifierState {
  bool power{false};
  Mode mode{Mode::MANUAL};
  uint8_t level{0};
  bool running_level_valid{false};
  uint8_t running_level{0};
  bool display_on{true};
  bool child_lock{false};
  uint8_t air_quality{0};
  uint16_t pm25{0};
  bool pm25_valid{false};
  uint8_t mcu_major{0};
  uint8_t mcu_minor{0};
  uint8_t mcu_patch{0};
};

enum class CommandKind : uint8_t { POWER, LEVEL, MODE, DISPLAY, CHILD_LOCK, FAN };

struct Command {
  CommandKind kind{CommandKind::POWER};
  uint8_t value{0};
  // For CommandKind::FAN: power/speed/mode in one call, -1 to leave a field.
  // The core orders them per model (EverestAir sets the mode before the speed).
  int8_t power{-1};
  int8_t speed{-1};
  int8_t mode{-1};
};

inline Command command_power(bool on) { return {CommandKind::POWER, on}; }
inline Command command_level(uint8_t level) { return {CommandKind::LEVEL, level}; }
inline Command command_mode(Mode mode) { return {CommandKind::MODE, static_cast<uint8_t>(mode)}; }
inline Command command_display(bool on) { return {CommandKind::DISPLAY, on}; }
inline Command command_child_lock(bool locked) { return {CommandKind::CHILD_LOCK, locked}; }
inline Command command_fan(int power, int speed, int mode) {
  Command c;
  c.kind = CommandKind::FAN;
  c.power = static_cast<int8_t>(power);
  c.speed = static_cast<int8_t>(speed);
  c.mode = static_cast<int8_t>(mode);
  return c;
}

} // namespace levoit_matter
