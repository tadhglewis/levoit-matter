#pragma once

#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace levoit_matter {

struct LinkHandlers {
  void (*on_bytes)(const uint8_t *data, size_t len, uint32_t now_ms);
  void (*on_poll)(uint32_t now_ms);
};

esp_err_t link_start(int tx_pin, int rx_pin, const LinkHandlers &handlers);
bool link_write(const uint8_t *frame, size_t len);
bool link_alive();
uint32_t link_now_ms();

} // namespace levoit_matter
