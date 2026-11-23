#include "filter_life.h"

namespace levoit {

void FilterLife::load(uint32_t used_cadr, uint32_t runtime_minutes) {
  this->used_cadr_ = used_cadr;
  this->runtime_minutes_ = runtime_minutes;
  this->remainder_ = 0;
}

void FilterLife::reset() {
  this->used_cadr_ = 0;
  this->remainder_ = 0;
  this->runtime_minutes_ = 0;
}

uint32_t FilterLife::cadr_per_hour_(const PurifierState &state,
                                    uint8_t last_level) const {
  if (!state.power)
    return 0;
  if (state.mode == Mode::SLEEP)
    return static_cast<uint32_t>(this->model_.cadr_m3h) / this->model_.level_max * 63 / 100;
  uint8_t level = state.level != 0 ? state.level : last_level != 0 ? last_level : 1;
  if (level > this->model_.level_max)
    level = this->model_.level_max;
  return static_cast<uint32_t>(this->model_.cadr_m3h) * level / this->model_.level_max;
}

void FilterLife::tick_minute(const PurifierState &state, uint8_t last_level) {
  if (!state.power)
    return;
  this->runtime_minutes_++;
  this->remainder_ += this->cadr_per_hour_(state, last_level);
  this->used_cadr_ += this->remainder_ / 60;
  this->remainder_ %= 60;
}

uint8_t FilterLife::percent() const {
  uint32_t capacity = static_cast<uint32_t>(this->model_.cadr_m3h) * 24 * 30 * this->months_;
  if (capacity == 0)
    return 100;
  if (this->used_cadr_ >= capacity)
    return 0;
  return static_cast<uint8_t>(100 - static_cast<uint64_t>(this->used_cadr_) * 100 / capacity);
}

} // namespace levoit
