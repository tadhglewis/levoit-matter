#pragma once

#include "levoit_core.h"

#include <esp_err.h>

#include <cstdint>

namespace levoit {

struct LinkHandlers {
  void (*on_status)(const PurifierState &state, uint32_t now_ms);
  void (*on_filter_reset)(uint32_t now_ms);
  void (*on_poll)(uint32_t now_ms);
};

esp_err_t link_start(const ModelSpec &model, int tx_pin, int rx_pin,
                     const LinkHandlers &handlers);
bool link_send(const Command &command);
bool link_alive();

} // namespace levoit
