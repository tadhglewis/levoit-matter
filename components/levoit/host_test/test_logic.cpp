#include "levoit_frame.h"
#include "purifier_map.h"

#include <cstdio>
#include <cstring>

using namespace levoit_matter;

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

  FrameParser stalled;
  const uint8_t partial[] = {0xA5, 0x12, 0x20, 0x20};
  stalled.feed(partial, sizeof(partial));
  stalled.feed(SAMPLE_STATUS, sizeof(SAMPLE_STATUS));
  CHECK(!stalled.next(frame));
  stalled.skip_byte();
  CHECK(stalled.next(frame) && frame.command == 0x013040);
}

static void test_mapping() {
  CHECK(level_to_percent(1, 3) == 33 && level_to_percent(2, 3) == 66 && level_to_percent(3, 3) == 100);
  CHECK(level_to_percent(3, 4) == 75);
  CHECK(percent_to_level(0, 3) == 0);
  CHECK(percent_to_level(1, 3) == 1 && percent_to_level(33, 3) == 1);
  CHECK(percent_to_level(34, 3) == 2 && percent_to_level(44, 3) == 2 && percent_to_level(66, 3) == 2);
  CHECK(percent_to_level(67, 3) == 3 && percent_to_level(100, 3) == 3);
  for (uint8_t level = 1; level <= 3; level++)
    CHECK(percent_to_level(level_to_percent(level, 3), 3) == level);
  CHECK(air_quality_from_level(1) == 1 && air_quality_from_level(2) == 1);
  CHECK(air_quality_from_level(3) == 3 && air_quality_from_level(4) == 4);
  CHECK(air_quality_from_level(0) == 0 && air_quality_from_level(9) == 0);
  CHECK(change_indication_from_percent(100) == 0);
  CHECK(change_indication_from_percent(14) == 1);
  CHECK(change_indication_from_percent(4) == 2);
}

static void test_view() {
  MatterView off = make_view(CORE_300S, state_of(false, Mode::MANUAL, 2), 2, 80);
  CHECK(off.fan_mode == FanModeView::OFF && !off.sleep && !off.current_valid);

  MatterView manual = make_view(CORE_300S, state_of(true, Mode::MANUAL, 2), 2, 80);
  CHECK(manual.fan_mode == FanModeView::HIGH);
  CHECK(manual.percent_setting == 66);
  CHECK(manual.current_valid && manual.percent_current == 66 && manual.speed_current == 2);

  MatterView high = make_view(CORE_300S, state_of(true, Mode::MANUAL, 3), 3, 80);
  CHECK(high.fan_mode == FanModeView::HIGH && high.percent_setting == 100);

  MatterView unreported = make_view(CORE_300S, state_of(true, Mode::MANUAL, 0), 3, 80);
  CHECK(unreported.speed_current == 3);

  MatterView sleep = make_view(CORE_300S, state_of(true, Mode::SLEEP, 3), 3, 80);
  CHECK(sleep.sleep && sleep.fan_mode == FanModeView::HIGH && sleep.percent_setting == 33);

  MatterView automatic = make_view(CORE_300S, state_of(true, Mode::AUTO, 0), 3, 80);
  CHECK(automatic.fan_mode == FanModeView::AUTO && !automatic.current_valid);
  MatterView automatic_level = make_view(CORE_300S, state_of(true, Mode::AUTO, 2), 3, 80);
  CHECK(automatic_level.current_valid && automatic_level.speed_current == 2);

  PurifierState sensor_error = state_of(true, Mode::MANUAL, 1);
  sensor_error.air_quality = 3;
  MatterView errored = make_view(CORE_300S, sensor_error, 1, 3);
  CHECK(errored.pm25_valid && errored.air_quality == 3);
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
      CHECK(plan.commands[i].kind != CommandKind::POWER);
  }

  CHECK(plan_display(manual2, false).count == 1);
  CHECK(plan_child_lock(manual2, true).count == 1);

  Plan power_off = plan_fan_off(manual2);
  CHECK(power_off.count == 1 && power_off.commands[0].kind == CommandKind::POWER);
  CHECK(power_off.commands[0].value == 0);

  CHECK(plan_fan_level(CORE_300S, manual2, 2).count == 0);
  Plan change = plan_fan_level(CORE_300S, manual2, 3);
  CHECK(change.count == 2);
  CHECK(change.commands[0].kind == CommandKind::LEVEL && change.commands[0].value == 3);
  CHECK(change.commands[1].kind == CommandKind::MODE && change.commands[1].value == 0);
  CHECK(change.expected_fields == (FIELD_LEVEL | FIELD_MODE));

  Plan from_off = plan_fan_level(CORE_300S, off, 1);
  CHECK(from_off.count == 3 && from_off.commands[0].value == 1);
  CHECK(plan_fan_level(CORE_300S, automatic, 9).commands[0].value == 3);

  CHECK(plan_fan_auto(CORE_300S, automatic).count == 0);
  Plan to_auto = plan_fan_auto(CORE_300S, manual2);
  CHECK(to_auto.count == 1 && to_auto.commands[0].value == 2);
  CHECK(plan_fan_auto(CORE_300S, off).count == 2);
  Plan power_on = plan_power_on(off);
  CHECK(power_on.count == 1 && power_on.commands[0].kind == CommandKind::POWER && power_on.commands[0].value == 1);
  CHECK(power_on.expected.power && plan_power_on(manual2).count == 0);

  CHECK(plan_sleep(sleeping, true).count == 0);
  Plan to_sleep = plan_sleep(manual2, true);
  CHECK(to_sleep.count == 1 && to_sleep.commands[0].value == 1);
  CHECK(plan_sleep(off, true).count == 2);
  Plan wake = plan_sleep(sleeping, false);
  CHECK(wake.count == 1 && wake.commands[0].kind == CommandKind::MODE && wake.commands[0].value == 0);
  CHECK(plan_display(sleeping, true).count == 0);
  CHECK(plan_display(off, true).count == 0);
  CHECK(plan_display(manual2, false).count == 1);
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

static void test_hold_reset() {
  HoldReset hold;
  CHECK(!hold.due(100000));
  hold.feed(HoldReset::STARTED, 1000);
  CHECK(hold.holding() && !hold.due(11999));
  hold.feed(HoldReset::RELEASED, 1700);
  CHECK(!hold.holding() && !hold.due(20000));
  hold.feed(HoldReset::STARTED, 30000);
  CHECK(!hold.due(40999) && hold.due(41000));
  CHECK(!hold.holding() && !hold.due(42000));
  hold.feed(0x09, 40000);
  CHECK(!hold.holding());
  hold.feed(HoldReset::STARTED, 50005);
  CHECK(!hold.due(50000));
  CHECK(!hold.due(61004) && hold.due(61005));
}


int main() {
  test_frame();
  test_mapping();
  test_view();
  test_plans();
  test_holdoff();
  test_hold_reset();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
