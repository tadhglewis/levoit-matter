#pragma once

#include "levoit_core.h"
#include "levoit_shared_core.h"

namespace levoit_matter {

const char *core_model_name(const ModelSpec &model);

bool state_from_core(const ModelSpec &model, const levoit_core::LevoitState &core,
                     const PurifierState &previous, PurifierState &out);

uint8_t filter_percent_from_core(float life_left);

bool send_to_core(levoit_core::LevoitDevice &device, const Command &command);

} // namespace levoit_matter
