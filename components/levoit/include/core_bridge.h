#pragma once

#include "levoit_shared_core.h"
#include "purifier_model.h"

namespace levoit_matter {

bool state_from_core(const ModelSpec &model, const levoit_core::LevoitState &core,
                     const PurifierState &previous, PurifierState &out);

uint8_t filter_percent_from_core(float life_left);

void send_to_core(levoit_core::LevoitDevice &device, const Command &command);

} // namespace levoit_matter
