#pragma once

#include "levoit_core.h"

#include <cstdint>

namespace levoit {

enum class FanModeView : uint8_t { OFF = 0, LOW = 1, MEDIUM = 2, HIGH = 3, AUTO = 5 };

struct MatterView {
  FanModeView fan_mode{FanModeView::OFF};
  uint8_t percent_setting{0};
  bool current_valid{false};
  uint8_t percent_current{0};
  uint8_t speed_current{0};
  bool pm25_valid{false};
  uint16_t pm25{0};
  uint8_t air_quality{0};
  uint8_t filter_percent{100};
  uint8_t change_indication{0};
  bool sleep{false};
  bool display{true};
  bool child_lock{false};

  bool operator==(const MatterView &other) const;
  bool operator!=(const MatterView &other) const { return !(*this == other); }
};

uint8_t level_to_percent(uint8_t level, uint8_t level_max);
uint8_t percent_to_level(uint8_t percent, uint8_t level_max);
uint8_t air_quality_from_level(uint8_t level);
uint8_t change_indication_from_percent(uint8_t percent);

MatterView make_view(const ModelSpec &model, const PurifierState &state,
                     uint8_t last_level, uint8_t filter_percent);

constexpr uint8_t FIELD_POWER = 1 << 0;
constexpr uint8_t FIELD_MODE = 1 << 1;
constexpr uint8_t FIELD_LEVEL = 1 << 2;
constexpr uint8_t FIELD_DISPLAY = 1 << 3;
constexpr uint8_t FIELD_CHILD_LOCK = 1 << 4;

struct Plan {
  Command commands[3];
  uint8_t count{0};
  PurifierState expected;
  uint8_t expected_fields{0};

  void add(const Command &command) { this->commands[this->count++] = command; }
};

Plan plan_fan_off(const PurifierState &state);
Plan plan_power_on(const PurifierState &state);
Plan plan_fan_auto(const ModelSpec &model, const PurifierState &state);
Plan plan_fan_level(const ModelSpec &model, const PurifierState &state,
                    uint8_t level);
Plan plan_sleep(const PurifierState &state, bool on);
Plan plan_display(const PurifierState &state, bool on);
Plan plan_child_lock(const PurifierState &state, bool locked);

class Holdoff {
public:
  static constexpr uint32_t WINDOW_MS = 2000;

  void expect(const Plan &plan, uint32_t now_ms);
  void apply(PurifierState &state, uint32_t now_ms);
  bool pending(uint8_t field) const { return (this->fields_ & field) != 0; }

private:
  PurifierState expected_;
  uint8_t fields_{0};
  uint32_t started_ms_{0};
};

class HoldReset {
public:
  static constexpr uint8_t STARTED = 0x02;
  static constexpr uint8_t RELEASED = 0x03;
  static constexpr uint32_t HOLD_MS = 11000;

  void feed(uint8_t event, uint32_t now_ms);
  bool holding() const { return this->holding_; }
  bool due(uint32_t now_ms);

private:
  bool holding_{false};
  uint32_t started_ms_{0};
};

} // namespace levoit
