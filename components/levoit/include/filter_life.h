#pragma once

#include "levoit_core.h"

#include <cstdint>

namespace levoit {

class FilterLife {
public:
  explicit FilterLife(const ModelSpec &model)
      : model_(model), months_(model.filter_months) {}

  void load(uint32_t used_cadr, uint32_t runtime_minutes);
  void tick_minute(const PurifierState &state, uint8_t last_level);
  void reset();

  uint8_t percent() const;
  uint32_t used_cadr() const { return this->used_cadr_; }
  uint32_t runtime_minutes() const { return this->runtime_minutes_; }

private:
  uint32_t cadr_per_hour_(const PurifierState &state, uint8_t last_level) const;

  const ModelSpec &model_;
  uint8_t months_;
  uint32_t used_cadr_{0};
  uint32_t remainder_{0};
  uint32_t runtime_minutes_{0};
};

} // namespace levoit
