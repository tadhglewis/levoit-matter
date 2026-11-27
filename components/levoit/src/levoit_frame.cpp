#include "levoit_frame.h"

#include <cstring>

namespace levoit_matter {

uint8_t frame_checksum(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    if (i != 5)
      sum += data[i];
  }
  return static_cast<uint8_t>(0xFF - (sum & 0xFF));
}

size_t encode_frame(uint8_t type, uint8_t counter, uint32_t command,
                    uint8_t trailer, const uint8_t *payload, size_t payload_len,
                    uint8_t *out, size_t out_size) {
  size_t frame_len = HEADER_LEN + payload_len;
  if (payload_len > MAX_PAYLOAD || out_size < frame_len)
    return 0;
  out[0] = FRAME_START;
  out[1] = type;
  out[2] = counter;
  out[3] = static_cast<uint8_t>(4 + payload_len);
  out[4] = 0x00;
  out[5] = 0x00;
  out[6] = static_cast<uint8_t>(command >> 16);
  out[7] = static_cast<uint8_t>(command >> 8);
  out[8] = static_cast<uint8_t>(command);
  out[9] = trailer;
  if (payload_len > 0)
    memcpy(out + HEADER_LEN, payload, payload_len);
  out[5] = frame_checksum(out, frame_len);
  return frame_len;
}

size_t encode_ack(uint8_t counter, uint32_t command, bool alternate,
                  uint8_t *out, size_t out_size) {
  size_t len = encode_frame(alternate ? TYPE_ACK_ALT : TYPE_ACK, counter, command,
                            alternate ? 0x01 : 0x00, nullptr, 0, out, out_size);
  if (len == 0)
    return 0;
  if (!alternate)
    out[4] = 0xFF;
  out[5] = frame_checksum(out, len);
  return len;
}

uint8_t next_counter(uint8_t counter) {
  return counter == 0xFF ? 0x10 : static_cast<uint8_t>(counter + 1);
}

void FrameParser::feed(const uint8_t *data, size_t len) {
  if (len > BUFFER_SIZE) {
    data += len - BUFFER_SIZE;
    len = BUFFER_SIZE;
  }
  if (this->have_ + len > BUFFER_SIZE)
    this->have_ = 0;
  memcpy(this->buffer_ + this->have_, data, len);
  this->have_ += len;
}

void FrameParser::skip_byte() {
  if (this->have_ == 0)
    return;
  memmove(this->buffer_, this->buffer_ + 1, this->have_ - 1);
  this->have_--;
}

bool FrameParser::next(Frame &frame) {
  size_t pos = 0;
  bool found = false;
  while (this->have_ - pos >= 6) {
    const uint8_t *p = this->buffer_ + pos;
    if (p[0] != FRAME_START) {
      pos++;
      continue;
    }
    size_t frame_len = 6u + p[3];
    if (frame_len < HEADER_LEN || frame_len > MAX_FRAME_LEN) {
      pos++;
      continue;
    }
    if (this->have_ - pos < frame_len)
      break;
    if (frame_checksum(p, frame_len) != p[5]) {
      pos++;
      continue;
    }
    frame.type = p[1];
    frame.counter = p[2];
    frame.command = (static_cast<uint32_t>(p[6]) << 16) |
                    (static_cast<uint32_t>(p[7]) << 8) | p[8];
    frame.trailer = p[9];
    frame.payload_len = frame_len - HEADER_LEN;
    memcpy(frame.payload, p + HEADER_LEN, frame.payload_len);
    memcpy(frame.raw, p, frame_len);
    frame.raw_len = frame_len;
    pos += frame_len;
    found = true;
    break;
  }
  if (pos > 0) {
    memmove(this->buffer_, this->buffer_ + pos, this->have_ - pos);
    this->have_ -= pos;
  }
  return found;
}

} // namespace levoit_matter
