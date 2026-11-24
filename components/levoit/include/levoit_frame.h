#pragma once

#include <cstddef>
#include <cstdint>

namespace levoit {

constexpr uint8_t FRAME_START = 0xA5;
constexpr uint8_t TYPE_SEND = 0x22;
constexpr uint8_t TYPE_ACK = 0x12;
constexpr uint8_t TYPE_ACK_ALT = 0x52;
constexpr size_t HEADER_LEN = 10;
constexpr size_t MAX_PAYLOAD = 64;
constexpr size_t MAX_FRAME_LEN = HEADER_LEN + MAX_PAYLOAD;

struct Frame {
  uint8_t type{0};
  uint8_t counter{0};
  uint32_t command{0};
  uint8_t trailer{0};
  uint8_t payload[MAX_PAYLOAD]{};
  size_t payload_len{0};
  uint8_t raw[MAX_FRAME_LEN]{};
  size_t raw_len{0};
};

uint8_t frame_checksum(const uint8_t *data, size_t len);

size_t encode_frame(uint8_t type, uint8_t counter, uint32_t command,
                    uint8_t trailer, const uint8_t *payload, size_t payload_len,
                    uint8_t *out, size_t out_size);

size_t encode_ack(uint8_t counter, uint32_t command, bool alternate,
                  uint8_t *out, size_t out_size);

uint8_t next_counter(uint8_t counter);

class FrameParser {
public:
  void feed(const uint8_t *data, size_t len);
  bool next(Frame &frame);
  void skip_byte();
  size_t buffered() const { return this->have_; }

private:
  static constexpr size_t BUFFER_SIZE = 512;
  uint8_t buffer_[BUFFER_SIZE]{};
  size_t have_{0};
};

} // namespace levoit
