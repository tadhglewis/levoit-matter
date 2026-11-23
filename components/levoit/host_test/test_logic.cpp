#include "filter_life.h"
#include "levoit_core.h"
#include "levoit_frame.h"
#include "purifier_map.h"

#include <cstdio>
#include <cstring>

using namespace levoit;

static int failures = 0;
static int checks = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    checks++;                                                                  \
    if (!(condition)) {                                                        \
      failures++;                                                              \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);               \
    }                                                                          \
  } while (0)

static const uint8_t SAMPLE_STATUS[] = {
    0xA5, 0x22, 0x1D, 0x16, 0x00, 0xE4, 0x01, 0x30, 0x40, 0x00,
    0x07, 0x00, 0x02, 0x01, 0x00, 0x01, 0x64, 0x01, 0x00, 0x00,
    0x01, 0x03, 0x00, 0x00, 0x00, 0x3B, 0x01, 0x00};

static PurifierState state_of(bool power, Mode mode, uint8_t level) {
  PurifierState state;
  state.power = power;
  state.mode = mode;
  state.level = level;
  state.pm25_valid = true;
  return state;
}

static void test_frame() {
  CHECK(frame_checksum(SAMPLE_STATUS, sizeof(SAMPLE_STATUS)) == 0xE4);

  FrameParser parser;
  Frame frame;
  const uint8_t noise[] = {0x00, 0xA5, 0xFF, 0x13};
  parser.feed(noise, sizeof(noise));
  parser.feed(SAMPLE_STATUS, 10);
  CHECK(!parser.next(frame));
  parser.feed(SAMPLE_STATUS + 10, sizeof(SAMPLE_STATUS) - 10);
  CHECK(parser.next(frame));
  CHECK(frame.type == TYPE_SEND);
  CHECK(frame.counter == 0x1D);
  CHECK(frame.command == 0x013040);
  CHECK(frame.payload_len == 18);
  CHECK(!parser.next(frame));

  uint8_t corrupt[sizeof(SAMPLE_STATUS)];
  memcpy(corrupt, SAMPLE_STATUS, sizeof(corrupt));
  corrupt[12] ^= 0x01;
  parser.feed(corrupt, sizeof(corrupt));
  parser.feed(SAMPLE_STATUS, sizeof(SAMPLE_STATUS));
  CHECK(parser.next(frame));
  CHECK(frame.payload[2] == 0x02);
  CHECK(!parser.next(frame));

  uint8_t out[MAX_FRAME_LEN];
  Command power = command_power(true);
  size_t len = encode_frame(TYPE_SEND, 0x10, power.command, 0x00, power.payload,
                            power.payload_len, out, sizeof(out));
  CHECK(len == 11);
  const uint8_t expected[] = {0xA5, 0x22, 0x10, 0x05, 0x00, 0x00,
                              0x01, 0x00, 0xA0, 0x00, 0x01};
  CHECK(memcmp(out, expected, 5) == 0);
  CHECK(memcmp(out + 6, expected + 6, 5) == 0);
  CHECK(out[5] == frame_checksum(out, len));
  parser.feed(out, len);
  CHECK(parser.next(frame));
  CHECK(frame.command == 0x0100A0 && frame.payload_len == 1);

  len = encode_ack(0x11, 0x013040, false, out, sizeof(out));
  CHECK(len == 10 && out[1] == 0x12 && out[2] == 0x11 && out[3] == 0x04);
  CHECK(out[4] == 0xFF && out[6] == 0x01 && out[9] == 0x00);
  CHECK(out[5] == frame_checksum(out, len));
  const uint8_t stock_ack_200s[] = {0xA5, 0x52, 0x00, 0x04, 0x00,
                                    0x62, 0x01, 0x60, 0x40, 0x01};
  len = encode_ack(0x00, 0x016040, true, out, sizeof(out));
  CHECK(len == 10 && memcmp(out, stock_ack_200s, 10) == 0);
  const uint8_t stock_ack_600s[] = {0xA5, 0x52, 0x02, 0x04, 0x00,
                                    0x7F, 0x01, 0x40, 0x41, 0x01};
  len = encode_ack(0x02, 0x014041, true, out, sizeof(out));
  CHECK(len == 10 && memcmp(out, stock_ack_600s, 10) == 0);

  FrameParser stalled;
  const uint8_t partial[] = {0xA5, 0x12, 0x20, 0x20};
  stalled.feed(partial, sizeof(partial));
  stalled.feed(SAMPLE_STATUS, sizeof(SAMPLE_STATUS));
  CHECK(!stalled.next(frame));
  stalled.skip_byte();
  CHECK(stalled.next(frame) && frame.command == 0x013040);

  CHECK(next_counter(0x10) == 0x11);
  CHECK(next_counter(0xFF) == 0x10);
}

static void test_decode() {
  PurifierState state;
  CHECK(is_status_command(CORE_300S, 0x013040));
  CHECK(is_status_command(CORE_300S, 0x01B040));
  CHECK(!is_status_command(CORE_300S, 0x016040));
  CHECK(decode_status(CORE_300S, SAMPLE_STATUS + HEADER_LEN, 18, state));
  CHECK(state.power && state.mode == Mode::MANUAL && state.level == 1);
  CHECK(state.display_on && !state.child_lock);
  CHECK(state.air_quality == 1 && state.pm25 == 3 && state.pm25_valid);
  CHECK(state.mcu_major == 2 && state.mcu_minor == 0 && state.mcu_patch == 7);
  CHECK(state.error == 0);
  CHECK(!decode_status(CORE_300S, SAMPLE_STATUS + HEADER_LEN, 17, state));

  uint8_t payload[18];
  memcpy(payload, SAMPLE_STATUS + HEADER_LEN, 18);
  payload[5] = 0xFF;
  CHECK(decode_status(CORE_300S, payload, 18, state) && state.level == 0);
  payload[4] = 0x07;
  CHECK(!decode_status(CORE_300S, payload, 18, state));
}

static bool decode_capture(const ModelSpec &model, const uint8_t *raw,
                           size_t len, uint8_t type, PurifierState &state) {
  FrameParser parser;
  Frame frame;
  parser.feed(raw, len);
  if (!parser.next(frame) || frame.type != type)
    return false;
  if (type == TYPE_SEND && !is_status_command(model, frame.command))
    return false;
  if (type == TYPE_ACK && frame.command != model.status_request)
    return false;
  return decode_status(model, frame.payload, frame.payload_len, state);
}

static void test_models() {
  PurifierState state;

  const uint8_t status_200s[] = {0xA5, 0x22, 0x00, 0x10, 0x00, 0x11, 0x01, 0x60,
                                 0x40, 0x00, 0x0B, 0x00, 0x02, 0x01, 0x00, 0x02,
                                 0x64, 0x01, 0x00, 0x00, 0x01, 0x00};
  CHECK(decode_capture(CORE_200S, status_200s, sizeof(status_200s), TYPE_SEND, state));
  CHECK(state.power && state.mode == Mode::MANUAL && state.level == 2);
  CHECK(state.display_on && state.child_lock && !state.pm25_valid);
  CHECK(state.mcu_major == 2 && state.mcu_minor == 0 && state.mcu_patch == 11);

  const uint8_t display_off_200s[] = {0xA5, 0x22, 0x00, 0x10, 0x00, 0x75, 0x01, 0x60,
                                      0x40, 0x00, 0x0B, 0x00, 0x02, 0x01, 0x00, 0x03,
                                      0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
  CHECK(decode_capture(CORE_200S, display_off_200s, sizeof(display_off_200s), TYPE_SEND, state));
  CHECK(state.level == 3 && !state.display_on && state.child_lock);

  uint8_t transient_200s[12];
  memcpy(transient_200s, display_off_200s + HEADER_LEN, 12);
  transient_200s[6] = 0x01;
  CHECK(decode_status(CORE_200S, transient_200s, 12, state) && !state.display_on);
  transient_200s[7] = 0x64;
  CHECK(decode_status(CORE_200S, transient_200s, 12, state) && state.display_on);

  const uint8_t reply_200s[] = {0xA5, 0x12, 0x04, 0x10, 0x00, 0x21, 0x01, 0x61,
                                0x40, 0x00, 0x0B, 0x00, 0x02, 0x00, 0x00, 0x00,
                                0x64, 0x00, 0x00, 0x00, 0x00, 0x00};
  CHECK(command_status_request(CORE_200S).command == 0x016140);
  CHECK(decode_capture(CORE_200S, reply_200s, sizeof(reply_200s), TYPE_ACK, state));
  CHECK(!state.power && state.level == 0 && state.display_on && !state.child_lock);

  CHECK(!has_air_quality(CORE_200S) && !CORE_200S.has_auto);
  CHECK(has_air_quality(CORE_300S) && has_air_quality(CORE_400S) && has_air_quality(CORE_600S));
  CHECK(plan_fan_auto(CORE_200S, state_of(true, Mode::MANUAL, 2)).count == 0);
  CHECK(!make_view(CORE_200S, state, 2, 80).pm25_valid);

  const uint8_t status_600s[] = {0xA5, 0x22, 0x02, 0x1C, 0x00, 0x9A, 0x01, 0x40, 0x41,
                                 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0xFF, 0x01, 0x00,
                                 0x00, 0x01, 0x00, 0x02, 0xB5, 0x00, 0x00, 0x02, 0x2E,
                                 0x10, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01};
  CHECK(decode_capture(CORE_600S, status_600s, sizeof(status_600s), TYPE_SEND, state));
  CHECK(!state.power && state.level == 0 && !state.display_on && !state.child_lock);
  CHECK(state.air_quality == 2 && state.pm25 == 181 && state.pm25_valid);
  CHECK(state.auto_profile == 2 && state.mcu_major == 2 && state.mcu_patch == 1);
  CHECK(!decode_status(CORE_600S, status_600s + HEADER_LEN, 21, state));
  CHECK(!is_status_command(CORE_600S, 0x013040));

  uint8_t payload_400s[18];
  memcpy(payload_400s, SAMPLE_STATUS + HEADER_LEN, 18);
  payload_400s[5] = 0x04;
  payload_400s[6] = 0x00;
  payload_400s[7] = 0x64;
  CHECK(decode_status(CORE_400S, payload_400s, 18, state));
  CHECK(state.level == 4 && state.display_on);
  CHECK(decode_status(CORE_300S, payload_400s, 18, state));
  CHECK(state.level == 0 && !state.display_on);

  MatterView level3 = make_view(CORE_400S, state_of(true, Mode::MANUAL, 3), 3, 80);
  CHECK(level3.fan_mode == FanModeView::MEDIUM && level3.percent_setting == 75);
  CHECK(level3.speed_current == 3);
  MatterView level4 = make_view(CORE_400S, state_of(true, Mode::MANUAL, 4), 4, 80);
  CHECK(level4.fan_mode == FanModeView::HIGH && level4.percent_setting == 100);
  CHECK(percent_to_level(75, 4) == 3 && percent_to_level(76, 4) == 4);
  CHECK(plan_fan_level(CORE_400S, state_of(true, Mode::MANUAL, 1), 4).commands[0].payload[2] == 4);

  FilterLife filter(CORE_600S);
  PurifierState level2 = state_of(true, Mode::MANUAL, 2);
  for (int i = 0; i < 60; i++)
    filter.tick_minute(level2, 2);
  CHECK(filter.used_cadr() == 320);
}

static void test_commands() {
  Command level = command_level(2);
  CHECK(level.command == 0x0160A2 && level.payload_len == 3);
  CHECK(level.payload[0] == 0x01 && level.payload[1] == 0x01 && level.payload[2] == 0x02);
  CHECK(command_mode(Mode::SLEEP).payload[0] == 0x01);
  CHECK(command_mode(Mode::AUTO).command == 0x01E0A5);
  CHECK(command_display(true).payload[0] == 0x64);
  CHECK(command_display(false).payload[0] == 0x00);
  CHECK(command_child_lock(true).command == 0x0100D1);
  Command led = command_wifi_led(WifiLed::BLINK);
  CHECK(led.command == 0x0129A1 && led.payload_len == 6 && led.payload[0] == 0x02);
  CHECK(command_wifi_led(WifiLed::SOLID).payload[1] == 0x7D);
}

static void test_mapping() {
  CHECK(level_to_percent(1, 3) == 33 && level_to_percent(2, 3) == 66 && level_to_percent(3, 3) == 100);
  CHECK(level_to_percent(3, 4) == 75);
  CHECK(percent_to_level(0, 3) == 0);
  CHECK(percent_to_level(1, 3) == 1 && percent_to_level(33, 3) == 1);
  CHECK(percent_to_level(34, 3) == 2 && percent_to_level(66, 3) == 2);
  CHECK(percent_to_level(67, 3) == 3 && percent_to_level(100, 3) == 3);
  for (uint8_t level = 1; level <= 3; level++)
    CHECK(percent_to_level(level_to_percent(level, 3), 3) == level);
  CHECK(air_quality_from_level(1) == 1 && air_quality_from_level(4) == 4);
  CHECK(air_quality_from_level(0) == 0 && air_quality_from_level(9) == 0);
  CHECK(change_indication_from_percent(100) == 0);
  CHECK(change_indication_from_percent(14) == 1);
  CHECK(change_indication_from_percent(4) == 2);
}

static void test_view() {
  MatterView off = make_view(CORE_300S, state_of(false, Mode::MANUAL, 2), 2, 80);
  CHECK(off.fan_mode == FanModeView::OFF && !off.sleep && !off.current_valid);

  MatterView manual = make_view(CORE_300S, state_of(true, Mode::MANUAL, 2), 2, 80);
  CHECK(manual.fan_mode == FanModeView::MEDIUM);
  CHECK(manual.percent_setting == 66);
  CHECK(manual.current_valid && manual.percent_current == 66 && manual.speed_current == 2);

  MatterView high = make_view(CORE_300S, state_of(true, Mode::MANUAL, 3), 3, 80);
  CHECK(high.fan_mode == FanModeView::HIGH && high.percent_setting == 100);

  MatterView unreported = make_view(CORE_300S, state_of(true, Mode::MANUAL, 0), 3, 80);
  CHECK(unreported.speed_current == 3);

  MatterView sleep = make_view(CORE_300S, state_of(true, Mode::SLEEP, 3), 3, 80);
  CHECK(sleep.sleep && sleep.fan_mode == FanModeView::LOW && sleep.percent_setting == 33);

  MatterView automatic = make_view(CORE_300S, state_of(true, Mode::AUTO, 0), 3, 80);
  CHECK(automatic.fan_mode == FanModeView::AUTO && !automatic.current_valid);
  MatterView automatic_level = make_view(CORE_300S, state_of(true, Mode::AUTO, 2), 3, 80);
  CHECK(automatic_level.current_valid && automatic_level.speed_current == 2);

  PurifierState sensor_error = state_of(true, Mode::MANUAL, 1);
  sensor_error.error = 1;
  sensor_error.air_quality = 2;
  MatterView errored = make_view(CORE_300S, sensor_error, 1, 3);
  CHECK(errored.pm25_valid && errored.air_quality == 2);
  CHECK(errored.change_indication == 2);
}

static void test_plans() {
  PurifierState off = state_of(false, Mode::SLEEP, 1);
  PurifierState manual2 = state_of(true, Mode::MANUAL, 2);
  PurifierState sleeping = state_of(true, Mode::SLEEP, 1);
  PurifierState automatic = state_of(true, Mode::AUTO, 0);

  CHECK(plan_sleep(off, false).count == 0);
  CHECK(plan_fan_off(off).count == 0);
  PurifierState off_lit_locked = off;
  off_lit_locked.display_on = true;
  off_lit_locked.child_lock = true;
  const Plan all_off[] = {plan_sleep(off_lit_locked, false),
                          plan_display(off_lit_locked, false),
                          plan_child_lock(off_lit_locked, false),
                          plan_fan_off(off_lit_locked)};
  for (const Plan &plan : all_off) {
    CHECK(plan.count <= 1);
    for (uint8_t i = 0; i < plan.count; i++)
      CHECK(plan.commands[i].command != 0x0100A0);
  }
  CHECK(command_status_request(CORE_300S).command == 0x013140);
  CHECK(command_status_request(CORE_300S).payload_len == 0);

  CHECK(plan_display(manual2, false).count == 1);
  CHECK(plan_child_lock(manual2, true).count == 1);

  Plan power_off = plan_fan_off(manual2);
  CHECK(power_off.count == 1 && power_off.commands[0].command == 0x0100A0);
  CHECK(power_off.commands[0].payload[0] == 0x00);

  CHECK(plan_fan_level(CORE_300S, manual2, 2).count == 0);
  Plan change = plan_fan_level(CORE_300S, manual2, 3);
  CHECK(change.count == 2);
  CHECK(change.commands[0].command == 0x0160A2 && change.commands[0].payload[2] == 3);
  CHECK(change.commands[1].command == 0x01E0A5 && change.commands[1].payload[0] == 0);
  CHECK(change.expected_fields == (FIELD_LEVEL | FIELD_MODE));

  Plan from_off = plan_fan_level(CORE_300S, off, 1);
  CHECK(from_off.count == 3 && from_off.commands[0].payload[0] == 0x01);
  CHECK(plan_fan_level(CORE_300S, automatic, 9).commands[0].payload[2] == 3);

  CHECK(plan_fan_auto(CORE_300S, automatic).count == 0);
  Plan to_auto = plan_fan_auto(CORE_300S, manual2);
  CHECK(to_auto.count == 1 && to_auto.commands[0].payload[0] == 0x02);
  CHECK(plan_fan_auto(CORE_300S, off).count == 2);

  CHECK(plan_sleep(sleeping, true).count == 0);
  Plan to_sleep = plan_sleep(manual2, true);
  CHECK(to_sleep.count == 1 && to_sleep.commands[0].payload[0] == 0x01);
  CHECK(plan_sleep(off, true).count == 2);
  Plan wake = plan_sleep(sleeping, false);
  CHECK(wake.count == 2 && wake.commands[0].payload[2] == 1);
  CHECK(plan_sleep(manual2, false).count == 0);
}

static void test_holdoff() {
  PurifierState manual2 = state_of(true, Mode::MANUAL, 2);
  Plan change = plan_fan_level(CORE_300S, manual2, 3);
  Holdoff holdoff;
  holdoff.expect(change, 1000);

  PurifierState stale = manual2;
  holdoff.apply(stale, 1500);
  CHECK(stale.level == 3 && holdoff.pending(FIELD_LEVEL));

  PurifierState fresh = state_of(true, Mode::MANUAL, 3);
  holdoff.apply(fresh, 1800);
  CHECK(fresh.level == 3 && !holdoff.pending(FIELD_LEVEL));

  holdoff.expect(change, 5000);
  PurifierState refused = manual2;
  holdoff.apply(refused, 5000 + Holdoff::WINDOW_MS + 1);
  CHECK(refused.level == 2 && !holdoff.pending(FIELD_LEVEL));

  Holdoff idle;
  PurifierState untouched = manual2;
  idle.apply(untouched, 10);
  CHECK(untouched.level == 2);
}

static void test_reset_gesture() {
  ResetGesture gesture;
  bool display = true;
  CHECK(!gesture.feed(display, false, 0));
  bool fired = false;
  for (int i = 0; i < 6; i++) {
    display = !display;
    fired = gesture.feed(display, false, 500 + i * 500);
    CHECK(fired == (i == 5));
  }

  ResetGesture slow;
  slow.feed(true, false, 0);
  display = true;
  fired = false;
  for (int i = 0; i < 6; i++) {
    display = !display;
    fired |= slow.feed(display, false, 1000 + i * 4000);
  }
  CHECK(!fired);

  ResetGesture commanded;
  commanded.feed(true, false, 0);
  display = true;
  fired = false;
  for (int i = 0; i < 12; i++) {
    display = !display;
    fired |= commanded.feed(display, true, 100 + i * 100);
  }
  CHECK(!fired);

  ResetGesture repeated;
  repeated.feed(true, false, 0);
  fired = false;
  for (int i = 0; i < 20; i++)
    fired |= repeated.feed(true, false, i * 100);
  CHECK(!fired);
}

static void test_filter() {
  FilterLife filter(CORE_300S);
  CHECK(filter.percent() == 100);

  PurifierState off = state_of(false, Mode::MANUAL, 3);
  filter.tick_minute(off, 3);
  CHECK(filter.runtime_minutes() == 0 && filter.used_cadr() == 0);

  PurifierState high = state_of(true, Mode::MANUAL, 3);
  for (int i = 0; i < 60; i++)
    filter.tick_minute(high, 3);
  CHECK(filter.runtime_minutes() == 60 && filter.used_cadr() == 214);

  FilterLife low(CORE_300S);
  PurifierState level1 = state_of(true, Mode::MANUAL, 1);
  for (int i = 0; i < 60; i++)
    low.tick_minute(level1, 1);
  CHECK(low.used_cadr() == 71);

  FilterLife sleep(CORE_300S);
  PurifierState sleeping = state_of(true, Mode::SLEEP, 0);
  for (int i = 0; i < 60; i++)
    sleep.tick_minute(sleeping, 3);
  CHECK(sleep.used_cadr() == 44);

  FilterLife automatic(CORE_300S);
  PurifierState auto_unreported = state_of(true, Mode::AUTO, 0);
  for (int i = 0; i < 60; i++)
    automatic.tick_minute(auto_unreported, 2);
  CHECK(automatic.used_cadr() == 142);

  FilterLife worn(CORE_300S);
  uint32_t capacity = 214u * 24 * 30 * 6;
  worn.load(capacity / 2, 1000);
  CHECK(worn.percent() == 50);
  worn.load(capacity, 1000);
  CHECK(worn.percent() == 0);
  worn.reset();
  CHECK(worn.percent() == 100 && worn.runtime_minutes() == 0);
}

int main() {
  test_frame();
  test_decode();
  test_models();
  test_commands();
  test_mapping();
  test_view();
  test_plans();
  test_holdoff();
  test_reset_gesture();
  test_filter();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
