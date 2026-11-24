#pragma once

#include <cstddef>
#include <cstdint>

namespace levoit {

enum class Mode : uint8_t { MANUAL = 0, SLEEP = 1, AUTO = 2 };
enum class WifiLed : uint8_t { OFF, SOLID, BLINK };

constexpr int8_t NO_FIELD = -1;

struct StatusLayout {
  uint8_t min_payload_len;
  int8_t level;
  int8_t display;
  int8_t display_alt;
  int8_t air_quality;
  int8_t pm25;
  int8_t child_lock;
  int8_t auto_profile;
  int8_t error;
  int8_t running_level;
};

struct ModelSpec {
  const char *name;
  uint8_t level_max;
  uint16_t cadr_m3h;
  uint8_t filter_months;
  bool has_auto;
  uint32_t status_command;
  uint32_t status_command_alt;
  uint32_t status_request;
  bool status_ack_alt;
  StatusLayout layout;
  uint32_t status_command_v2;
  StatusLayout layout_v2;
  uint32_t status_request_v2;
};

extern const ModelSpec CORE_200S;
extern const ModelSpec CORE_300S;
extern const ModelSpec CORE_400S;
extern const ModelSpec CORE_600S;

inline bool has_air_quality(const ModelSpec &model) {
  return model.layout.pm25 != NO_FIELD;
}

struct PurifierState {
  bool power{false};
  Mode mode{Mode::MANUAL};
  uint8_t level{0};
  bool running_level_valid{false};
  uint8_t running_level{0};
  bool display_on{true};
  bool child_lock{false};
  uint8_t air_quality{0};
  uint16_t pm25{0};
  bool pm25_valid{false};
  uint8_t auto_profile{0};
  uint8_t error{0};
  uint8_t mcu_major{0};
  uint8_t mcu_minor{0};
  uint8_t mcu_patch{0};
};

struct Command {
  uint32_t command{0};
  uint8_t payload[6]{};
  uint8_t payload_len{0};
};

constexpr uint32_t COMMAND_FILTER_RESET = 0x01E4A5;
constexpr uint32_t COMMAND_BUTTON_HOLD = 0x0102D1;

bool is_status_command(const ModelSpec &model, uint32_t command);
bool decode_status(const ModelSpec &model, const uint8_t *payload, size_t len,
                   PurifierState &state);
bool decode_status(const ModelSpec &model, uint32_t command,
                   const uint8_t *payload, size_t len, PurifierState &state);

Command command_status_request(const ModelSpec &model, bool v2 = false);
uint32_t status_command_for_reply(const ModelSpec &model, uint32_t request);
Command command_power(bool on);
Command command_level(uint8_t level);
Command command_mode(Mode mode);
Command command_display(bool on);
Command command_child_lock(bool locked);
Command command_filter_led(bool on);
Command command_wifi_led(WifiLed state);

} // namespace levoit
