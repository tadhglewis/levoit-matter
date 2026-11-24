#include "purifier_map.h"

namespace levoit {

bool MatterView::operator==(const MatterView &other) const {
  return this->fan_mode == other.fan_mode &&
         this->percent_setting == other.percent_setting &&
         this->current_valid == other.current_valid &&
         this->percent_current == other.percent_current &&
         this->speed_current == other.speed_current &&
         this->pm25_valid == other.pm25_valid && this->pm25 == other.pm25 &&
         this->air_quality == other.air_quality &&
         this->filter_percent == other.filter_percent &&
         this->change_indication == other.change_indication &&
         this->sleep == other.sleep && this->display == other.display &&
         this->child_lock == other.child_lock;
}

uint8_t level_to_percent(uint8_t level, uint8_t level_max) {
  if (level == 0 || level_max == 0)
    return 0;
  if (level > level_max)
    level = level_max;
  return static_cast<uint8_t>(level * 100 / level_max);
}

uint8_t percent_to_level(uint8_t percent, uint8_t level_max) {
  if (percent == 0 || level_max == 0)
    return 0;
  if (percent > 100)
    percent = 100;
  uint8_t level = static_cast<uint8_t>((percent * level_max + 99) / 100);
  return level < 1 ? 1 : level > level_max ? level_max : level;
}

uint8_t air_quality_from_level(uint8_t level) {
  return level >= 1 && level <= 4 ? level : 0;
}

uint8_t change_indication_from_percent(uint8_t percent) {
  if (percent < 5)
    return 2;
  if (percent < 15)
    return 1;
  return 0;
}

MatterView make_view(const ModelSpec &model, const PurifierState &state,
                     uint8_t last_level, uint8_t filter_percent) {
  MatterView view;
  uint8_t known_level = state.level != 0 ? state.level : last_level;

  if (!state.power) {
    view.fan_mode = FanModeView::OFF;
  } else if (state.mode == Mode::AUTO) {
    view.fan_mode = FanModeView::AUTO;
    uint8_t running = state.running_level_valid
                          ? (state.running_level != 0 ? state.running_level : 1)
                          : state.level;
    if (running != 0) {
      view.current_valid = true;
      view.speed_current = running;
      view.percent_current = level_to_percent(running, model.level_max);
    }
  } else {
    uint8_t level = state.mode == Mode::SLEEP ? 1 : known_level != 0 ? known_level : 1;
    view.percent_setting = level_to_percent(level, model.level_max);
    view.fan_mode = level <= 1                 ? FanModeView::LOW
                    : level >= model.level_max ? FanModeView::HIGH
                                               : FanModeView::MEDIUM;
    view.current_valid = true;
    view.speed_current = level;
    view.percent_current = view.percent_setting;
  }

  view.pm25_valid = state.pm25_valid && state.pm25 <= 1000;
  view.pm25 = state.pm25;
  view.air_quality = air_quality_from_level(state.air_quality);
  view.filter_percent = filter_percent;
  view.change_indication = change_indication_from_percent(filter_percent);
  view.sleep = state.power && state.mode == Mode::SLEEP;
  view.display = state.display_on && state.power && state.mode != Mode::SLEEP;
  view.child_lock = state.child_lock;
  return view;
}

Plan plan_fan_off(const PurifierState &state) {
  Plan plan;
  plan.expected = state;
  if (!state.power)
    return plan;
  plan.add(command_power(false));
  plan.expected.power = false;
  plan.expected_fields = FIELD_POWER;
  return plan;
}

Plan plan_power_on(const PurifierState &state) {
  Plan plan;
  plan.expected = state;
  if (state.power)
    return plan;
  plan.add(command_power(true));
  plan.expected.power = true;
  plan.expected_fields = FIELD_POWER;
  return plan;
}

Plan plan_fan_auto(const ModelSpec &model, const PurifierState &state) {
  Plan plan;
  plan.expected = state;
  if (!model.has_auto)
    return plan;
  if (!state.power) {
    plan.add(command_power(true));
    plan.expected.power = true;
    plan.expected_fields |= FIELD_POWER;
  }
  if (!state.power || state.mode != Mode::AUTO) {
    plan.add(command_mode(Mode::AUTO));
    plan.expected.mode = Mode::AUTO;
    plan.expected_fields |= FIELD_MODE;
  }
  return plan;
}

Plan plan_fan_level(const ModelSpec &model, const PurifierState &state,
                    uint8_t level) {
  Plan plan;
  plan.expected = state;
  if (level < 1)
    level = 1;
  if (level > model.level_max)
    level = model.level_max;
  if (!state.power) {
    plan.add(command_power(true));
    plan.expected.power = true;
    plan.expected_fields |= FIELD_POWER;
  }
  if (!state.power || state.mode != Mode::MANUAL || state.level != level) {
    plan.add(command_level(level));
    plan.add(command_mode(Mode::MANUAL));
    plan.expected.level = level;
    plan.expected.mode = Mode::MANUAL;
    plan.expected_fields |= FIELD_LEVEL | FIELD_MODE;
  }
  return plan;
}

Plan plan_sleep(const PurifierState &state, bool on) {
  Plan plan;
  plan.expected = state;
  if (on) {
    if (!state.power) {
      plan.add(command_power(true));
      plan.expected.power = true;
      plan.expected_fields |= FIELD_POWER;
    }
    if (!state.power || state.mode != Mode::SLEEP) {
      plan.add(command_mode(Mode::SLEEP));
      plan.expected.mode = Mode::SLEEP;
      plan.expected_fields |= FIELD_MODE;
    }
    return plan;
  }
  if (state.power && state.mode == Mode::SLEEP) {
    plan.add(command_level(1));
    plan.add(command_mode(Mode::MANUAL));
    plan.expected.level = 1;
    plan.expected.mode = Mode::MANUAL;
    plan.expected_fields = FIELD_LEVEL | FIELD_MODE;
  }
  return plan;
}

Plan plan_display(const PurifierState &state, bool on) {
  Plan plan;
  plan.expected = state;
  if (state.display_on == on)
    return plan;
  plan.add(command_display(on));
  plan.expected.display_on = on;
  plan.expected_fields = FIELD_DISPLAY;
  return plan;
}

Plan plan_child_lock(const PurifierState &state, bool locked) {
  Plan plan;
  plan.expected = state;
  if (state.child_lock == locked)
    return plan;
  plan.add(command_child_lock(locked));
  plan.expected.child_lock = locked;
  plan.expected_fields = FIELD_CHILD_LOCK;
  return plan;
}

void Holdoff::expect(const Plan &plan, uint32_t now_ms) {
  if (plan.expected_fields == 0)
    return;
  if (plan.expected_fields & FIELD_POWER)
    this->expected_.power = plan.expected.power;
  if (plan.expected_fields & FIELD_MODE)
    this->expected_.mode = plan.expected.mode;
  if (plan.expected_fields & FIELD_LEVEL)
    this->expected_.level = plan.expected.level;
  if (plan.expected_fields & FIELD_DISPLAY)
    this->expected_.display_on = plan.expected.display_on;
  if (plan.expected_fields & FIELD_CHILD_LOCK)
    this->expected_.child_lock = plan.expected.child_lock;
  this->fields_ |= plan.expected_fields;
  this->started_ms_ = now_ms;
}

void Holdoff::apply(PurifierState &state, uint32_t now_ms) {
  if (this->fields_ == 0)
    return;
  if (now_ms - this->started_ms_ > WINDOW_MS) {
    this->fields_ = 0;
    return;
  }
  if (this->fields_ & FIELD_POWER) {
    if (state.power == this->expected_.power)
      this->fields_ &= ~FIELD_POWER;
    else
      state.power = this->expected_.power;
  }
  if (this->fields_ & FIELD_MODE) {
    if (state.mode == this->expected_.mode)
      this->fields_ &= ~FIELD_MODE;
    else
      state.mode = this->expected_.mode;
  }
  if (this->fields_ & FIELD_LEVEL) {
    if (state.level == this->expected_.level)
      this->fields_ &= ~FIELD_LEVEL;
    else
      state.level = this->expected_.level;
  }
  if (this->fields_ & FIELD_DISPLAY) {
    if (state.display_on == this->expected_.display_on)
      this->fields_ &= ~FIELD_DISPLAY;
    else
      state.display_on = this->expected_.display_on;
  }
  if (this->fields_ & FIELD_CHILD_LOCK) {
    if (state.child_lock == this->expected_.child_lock)
      this->fields_ &= ~FIELD_CHILD_LOCK;
    else
      state.child_lock = this->expected_.child_lock;
  }
}

void HoldReset::feed(uint8_t event, uint32_t now_ms) {
  if (event == STARTED) {
    this->holding_ = true;
    this->started_ms_ = now_ms;
  } else if (event == RELEASED) {
    this->holding_ = false;
  }
}

bool HoldReset::due(uint32_t now_ms) {
  if (!this->holding_ || now_ms - this->started_ms_ < HOLD_MS)
    return false;
  this->holding_ = false;
  return true;
}

} // namespace levoit
