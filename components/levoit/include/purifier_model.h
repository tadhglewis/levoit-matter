#pragma once

#include <cstdint>

namespace levoit_matter {

enum class Mode : uint8_t { MANUAL = 0, SLEEP = 1, AUTO = 2 };
enum class WifiLed : uint8_t { OFF, SOLID, BLINK };

struct ModelSpec {
  const char *name;
  const char *core_name;
  uint8_t level_max;
  uint16_t cadr_m3h;
  uint8_t filter_months;
  bool has_auto;
  bool has_air_quality;
  bool poll_status;  // MCU pushes slowly; ask for status until one arrives
};

extern const ModelSpec CORE_200S;
extern const ModelSpec CORE_300S;
extern const ModelSpec CORE_400S;
extern const ModelSpec CORE_600S;
extern const ModelSpec SPROUT;

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

enum class CommandKind : uint8_t { POWER, LEVEL, MODE, DISPLAY, CHILD_LOCK, FILTER_LED };

struct Command {
  CommandKind kind{CommandKind::POWER};
  uint8_t value{0};
};

inline Command command_power(bool on) { return {CommandKind::POWER, on}; }
inline Command command_level(uint8_t level) { return {CommandKind::LEVEL, level}; }
inline Command command_mode(Mode mode) { return {CommandKind::MODE, static_cast<uint8_t>(mode)}; }
inline Command command_display(bool on) { return {CommandKind::DISPLAY, on}; }
inline Command command_child_lock(bool locked) { return {CommandKind::CHILD_LOCK, locked}; }
inline Command command_filter_led(bool on) { return {CommandKind::FILTER_LED, on}; }

} // namespace levoit_matter
