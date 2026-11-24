#include "levoit_core.h"

#include <cstring>

namespace levoit {

constexpr uint8_t DISPLAY_FULL = 0x64;

const ModelSpec CORE_200S = {
    "Core 200S", 3,    167, 6, false, 0x016040, 0,
    0x016140,    true, {12, 5, 6, 7, NO_FIELD, NO_FIELD, 10, NO_FIELD, NO_FIELD, NO_FIELD},
    0,           {},
};

const ModelSpec CORE_300S = {
    "Core 300S", 3,     214, 6, true, 0x013040, 0x01B040,
    0x013140,    false, {18, 5, 7, NO_FIELD, 10, 11, 13, 14, 17, 8},
    0x013240,    {19, 5, 8, NO_FIELD, 11, 12, 14, 15, 18, 9},
};

const ModelSpec CORE_400S = {
    "Core 400S", 4,     442, 6, true, 0x013040, 0x01B040,
    0x013140,    false, {18, 5, 7, NO_FIELD, 10, 11, 13, 14, 17, NO_FIELD},
    0,           {},
};

const ModelSpec CORE_600S = {
    "Core 600S", 4,    641, 6, true, 0x014041, 0,
    0x014141,    true, {22, 5, 8, NO_FIELD, 11, 12, 14, 15, NO_FIELD, NO_FIELD},
    0,           {},
};

namespace {

uint8_t read_u8(const uint8_t *payload, int8_t offset) {
  return offset == NO_FIELD ? 0 : payload[offset];
}

Command make_command(uint32_t command, const uint8_t *payload, uint8_t len) {
  Command out;
  out.command = command;
  out.payload_len = len;
  if (len > 0)
    memcpy(out.payload, payload, len);
  return out;
}

} // namespace

bool is_status_command(const ModelSpec &model, uint32_t command) {
  return command == model.status_command ||
         (model.status_command_alt != 0 && command == model.status_command_alt) ||
         (model.status_command_v2 != 0 && command == model.status_command_v2);
}

namespace {

bool decode_with(const ModelSpec &model, const StatusLayout &layout,
                 const uint8_t *payload, size_t len, PurifierState &state);

} // namespace

bool decode_status(const ModelSpec &model, const uint8_t *payload, size_t len,
                   PurifierState &state) {
  return decode_with(model, model.layout, payload, len, state);
}

bool decode_status(const ModelSpec &model, uint32_t command,
                   const uint8_t *payload, size_t len, PurifierState &state) {
  bool v2 = model.status_command_v2 != 0 && command == model.status_command_v2;
  return decode_with(model, v2 ? model.layout_v2 : model.layout, payload, len, state);
}

namespace {

bool decode_with(const ModelSpec &model, const StatusLayout &layout,
                 const uint8_t *payload, size_t len, PurifierState &state) {
  if (len < layout.min_payload_len || len < 5)
    return false;
  if (payload[4] > static_cast<uint8_t>(Mode::AUTO))
    return false;

  state.mcu_patch = payload[0];
  state.mcu_minor = payload[1];
  state.mcu_major = payload[2];
  state.power = payload[3] != 0;
  state.mode = static_cast<Mode>(payload[4]);

  uint8_t level = read_u8(payload, layout.level);
  state.level = (level >= 1 && level <= model.level_max) ? level : 0;
  state.running_level_valid = layout.running_level != NO_FIELD;
  uint8_t running = read_u8(payload, layout.running_level);
  state.running_level = (running >= 1 && running <= model.level_max) ? running : 0;
  if (layout.display == NO_FIELD)
    state.display_on = true;
  else if (layout.display_alt == NO_FIELD)
    state.display_on = payload[layout.display] != 0;
  else
    state.display_on = payload[layout.display] == DISPLAY_FULL ||
                       payload[layout.display_alt] == DISPLAY_FULL;
  state.air_quality = read_u8(payload, layout.air_quality);
  state.pm25_valid = layout.pm25 != NO_FIELD;
  state.pm25 = state.pm25_valid
                   ? static_cast<uint16_t>(payload[layout.pm25] |
                                           (payload[layout.pm25 + 1] << 8))
                   : 0;
  state.child_lock = read_u8(payload, layout.child_lock) != 0;
  state.auto_profile = read_u8(payload, layout.auto_profile);
  state.error = read_u8(payload, layout.error);
  return true;
}

} // namespace

Command command_status_request(const ModelSpec &model) {
  return make_command(model.status_request, nullptr, 0);
}

Command command_power(bool on) {
  const uint8_t payload[] = {static_cast<uint8_t>(on ? 0x01 : 0x00)};
  return make_command(0x0100A0, payload, sizeof(payload));
}

Command command_level(uint8_t level) {
  const uint8_t payload[] = {0x01, 0x01, level};
  return make_command(0x0160A2, payload, sizeof(payload));
}

Command command_mode(Mode mode) {
  const uint8_t payload[] = {static_cast<uint8_t>(mode)};
  return make_command(0x01E0A5, payload, sizeof(payload));
}

Command command_display(bool on) {
  const uint8_t payload[] = {static_cast<uint8_t>(on ? DISPLAY_FULL : 0x00)};
  return make_command(0x0105A1, payload, sizeof(payload));
}

Command command_child_lock(bool locked) {
  const uint8_t payload[] = {static_cast<uint8_t>(locked ? 0x01 : 0x00)};
  return make_command(0x0100D1, payload, sizeof(payload));
}

Command command_filter_led(bool on) {
  const uint8_t payload[] = {static_cast<uint8_t>(on ? 0x01 : 0x00)};
  return make_command(0x01E2A5, payload, sizeof(payload));
}

Command command_wifi_led(WifiLed state) {
  static const uint8_t SOLID[] = {0x01, 0x7D, 0x00, 0x7D, 0x00, 0x00};
  static const uint8_t OFF[] = {0x00, 0xF4, 0x01, 0xF4, 0x01, 0x00};
  static const uint8_t BLINK[] = {0x02, 0xF4, 0x01, 0xF4, 0x01, 0x00};
  const uint8_t *payload =
      state == WifiLed::SOLID ? SOLID : state == WifiLed::BLINK ? BLINK : OFF;
  return make_command(0x0129A1, payload, 6);
}

} // namespace levoit
