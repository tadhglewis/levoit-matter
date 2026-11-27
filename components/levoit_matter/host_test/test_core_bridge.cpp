#include "core_bridge.h"
#include "levoit_frame.h"
#include "purifier_map.h"
#include "test_models.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace levoit_matter;
namespace core = levoit_core;

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

using Bytes = std::vector<uint8_t>;

struct FakePlatform : core::LevoitPlatform {
  std::vector<Bytes> tx;
  uint32_t now{0};
  std::map<std::string, uint32_t> prefs;

  void write(const uint8_t *data, size_t len) override { tx.emplace_back(data, data + len); }
  uint32_t millis() override { return now; }
  void set_timeout(const char *, uint32_t, std::function<void()>) override {}
  void cancel_timeout(const char *) override {}
  bool load_u32(const char *key, uint32_t &out) override {
    auto it = prefs.find(key);
    if (it == prefs.end())
      return false;
    out = it->second;
    return true;
  }
  void save_u32(const char *key, uint32_t value) override { prefs[key] = value; }
  bool has_clock() override { return false; }
  bool local_time(uint32_t &) override { return false; }
  core::NetworkState network_state() override { return core::NetworkState::CONNECTED; }
};

struct FakeHost : core::LevoitHost {
  int fan_reports{0};
  bool button_held{false};
  float filter_left{-1};

  void switch_changed(core::SwitchType, bool) override {}
  void number_changed(core::NumberType, float) override {}
  void select_changed(core::SelectType, uint32_t) override {}
  void text_changed(core::TextSensorType, const std::string &) override {}
  void sensor_changed(core::SensorType type, float value) override {
    if (type == core::SensorType::FILTER_LIFE_LEFT)
      filter_left = value;
  }
  void binary_changed(core::BinarySensorType type, bool state) override {
    if (type == core::BinarySensorType::POWER_BUTTON_HELD)
      button_held = state;
  }
  void fan_changed(int, int, int) override { fan_reports++; }
};

struct Rig {
  FakePlatform platform;
  FakeHost host;
  core::LevoitDevice device{&host, &platform};

  Rig() {
    device.set_device_model("CORE300S");
    device.note_number_state(core::NumberType::FILTER_LIFETIME_MONTHS, 6);
    device.begin();
    platform.tx.clear();
  }

  void feed(const Bytes &frame) {
    platform.now += 100;
    device.receive(frame.data(), frame.size(), platform.now);
  }
};

static Bytes hex_bytes(const std::string &text) {
  Bytes out;
  std::istringstream in(text);
  std::string token;
  while (in >> token)
    out.push_back(static_cast<uint8_t>(std::strtoul(token.c_str(), nullptr, 16)));
  return out;
}

static MatterView view_after(Rig &rig, PurifierState &state, uint8_t &last_level, const char *frame) {
  rig.feed(hex_bytes(frame));
  CHECK(state_from_core(CORE_300S, rig.device.state(), state, state));
  if (state.level != 0 && (state.mode == Mode::MANUAL || state.running_level_valid))
    last_level = state.level;
  return make_view(CORE_300S, state, last_level, 80);
}

static void test_capture() {
  Rig rig;
  PurifierState state;
  uint8_t last_level = 0;

  MatterView speed3 = view_after(rig, state, last_level,
      "A5 22 D8 17 00 63 01 32 40 00 00 00 01 01 00 03 00 64 01 03 00 01 05 00 00 00 00 00 00");
  CHECK(speed3.fan_mode == FanModeView::HIGH && speed3.percent_setting == 100 && speed3.speed_current == 3);
  CHECK(speed3.display && !speed3.child_lock && !speed3.sleep);
  CHECK(speed3.air_quality == 1 && speed3.pm25_valid && speed3.pm25 == 5);
  CHECK(state.mcu_major == 1 && state.mcu_minor == 0 && state.mcu_patch == 0);

  MatterView sleep = view_after(rig, state, last_level,
      "A5 22 DC 17 00 61 01 32 40 00 00 00 01 01 01 03 00 64 01 00 00 01 05 00 00 00 00 00 00");
  CHECK(sleep.sleep && sleep.fan_mode == FanModeView::HIGH && sleep.percent_setting == 33);
  CHECK(!sleep.display && state.level == 3 && state.running_level == 0);

  MatterView automatic = view_after(rig, state, last_level,
      "A5 22 E0 17 00 5C 01 32 40 00 00 00 01 01 02 03 00 64 01 00 00 01 05 00 00 00 00 00 00");
  CHECK(automatic.fan_mode == FanModeView::AUTO && automatic.current_valid && automatic.speed_current == 1);
  CHECK(state.level == 3 && last_level == 3);

  MatterView display_off = view_after(rig, state, last_level,
      "A5 22 E4 17 00 C4 01 32 40 00 00 00 01 01 00 01 00 00 00 01 00 01 01 00 00 00 00 00 00");
  CHECK(!display_off.display && display_off.speed_current == 1 && display_off.pm25 == 1);

  MatterView locked = view_after(rig, state, last_level,
      "A5 22 E8 17 00 5A 01 32 40 00 00 00 01 01 00 01 00 64 01 01 00 01 01 00 01 00 00 00 00");
  CHECK(locked.child_lock && locked.display);

  MatterView off = view_after(rig, state, last_level,
      "A5 22 D4 17 00 D6 01 32 40 00 00 00 01 00 00 01 00 00 01 FF 00 01 01 00 00 00 00 00 00");
  CHECK(off.fan_mode == FanModeView::OFF && !off.current_valid && !off.display);

  Rig cold;
  PurifierState boot;
  uint8_t boot_last = 0;
  view_after(cold, boot, boot_last,
      "A5 22 E0 17 00 5C 01 32 40 00 00 00 01 01 02 03 00 64 01 00 00 01 05 00 00 00 00 00 00");
  CHECK(boot.mode == Mode::AUTO && boot_last == 3);
}

static void test_commands() {
  const struct {
    Command command;
    const char *bytes;
  } cases[] = {
      {command_power(true), "01 00 A0 00 01"},
      {command_power(false), "01 00 A0 00 00"},
      {command_level(2), "01 60 A2 00 01 01 02"},
      {command_mode(Mode::MANUAL), "01 E0 A5 00 00"},
      {command_mode(Mode::SLEEP), "01 E0 A5 00 01"},
      {command_mode(Mode::AUTO), "01 E0 A5 00 02"},
      {command_display(true), "01 05 A1 00 64"},
      {command_display(false), "01 05 A1 00 00"},
      {command_child_lock(true), "01 00 D1 00 01"},
      {command_filter_led(true), "01 E2 A5 00 01"},
      {command_filter_led(false), "01 E2 A5 00 00"},
  };
  for (const auto &c : cases) {
    Rig rig;
    send_to_core(rig.device, c.command);
    CHECK(rig.platform.tx.size() == 1);
    if (rig.platform.tx.size() != 1)
      continue;
    const Bytes &sent = rig.platform.tx[0];
    CHECK(sent[0] == 0xA5 && sent[1] == TYPE_SEND && sent[5] == frame_checksum(sent.data(), sent.size()));
    CHECK(Bytes(sent.begin() + 6, sent.end()) == hex_bytes(c.bytes));
  }
}

static void test_mode_mapping() {
  core::LevoitState st;
  PurifierState out;
  PurifierState prev;
  prev.mode = Mode::AUTO;
  st.set_fan(1, 3, 4);  // Turbo -> Manual, distinct from the AUTO it replaces
  CHECK(state_from_core(CORE_300S, st, prev, out) && out.mode == Mode::MANUAL);
  prev.mode = Mode::MANUAL;
  st.set_fan(1, 1, 5);  // Pet -> Auto
  CHECK(state_from_core(CORE_300S, st, prev, out) && out.mode == Mode::AUTO);
  prev.mode = Mode::SLEEP;
  st.set_fan(1, 1, 3);  // Humidity, unused by any purifier -> keep previous
  CHECK(state_from_core(CORE_300S, st, prev, out) && out.mode == Mode::SLEEP);
}

static void test_state_needs_fan_report() {
  Rig rig;
  PurifierState out;
  CHECK(!state_from_core(CORE_300S, rig.device.state(), PurifierState{}, out));
}

static void test_button_hold() {
  Rig rig;
  Bytes frame(11, 0);
  frame[0] = 0xA5;
  frame[1] = 0x22;
  frame[2] = 0x30;
  frame[3] = 5;
  frame[6] = 0x01;
  frame[7] = 0x02;
  frame[8] = 0xD1;
  frame[10] = 0x02;
  frame[5] = frame_checksum(frame.data(), frame.size());
  rig.feed(frame);
  CHECK(rig.host.button_held);
  frame[2] = 0x31;
  frame[10] = 0x03;
  frame[5] = frame_checksum(frame.data(), frame.size());
  rig.feed(frame);
  CHECK(!rig.host.button_held);
}

static void test_filter_percent() {
  // The core owns the math now (set_filter_percent); check our rounding of it.
  for (uint8_t percent = 0; percent <= 100; percent += 5) {
    Rig rig;
    rig.device.set_filter_percent(percent);
    CHECK(filter_percent_from_core(rig.device.calculate_filter_life_left_percent()) == percent);
  }
}

int main() {
  test_capture();
  test_commands();
  test_state_needs_fan_report();
  test_mode_mapping();
  test_button_hold();
  test_filter_percent();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
