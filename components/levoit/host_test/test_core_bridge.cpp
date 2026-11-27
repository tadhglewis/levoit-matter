#include "core_bridge.h"
#include "filter_life.h"
#include "levoit_core.h"
#include "levoit_frame.h"
#include "purifier_map.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
    device.note_number_state(core::NumberType::FILTER_LIFETIME_MONTHS, CORE_300S.filter_months);
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

static std::vector<Bytes> mcu_frames(const char *path) {
  std::vector<Bytes> frames;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    size_t at = line.find("MCU->ESP");
    if (at != std::string::npos)
      frames.push_back(hex_bytes(line.substr(at + 8)));
  }
  return frames;
}

static bool own_decode(const Bytes &raw, PurifierState &state) {
  FrameParser parser;
  Frame frame;
  parser.feed(raw.data(), raw.size());
  if (!parser.next(frame))
    return false;
  uint32_t layout = frame.type == TYPE_ACK
                        ? status_command_for_reply(CORE_300S, frame.command)
                        : frame.command;
  if (layout == 0 || !is_status_command(CORE_300S, layout))
    return false;
  return decode_status(CORE_300S, layout, frame.payload, frame.payload_len, state);
}

static void test_capture_parity(const char *path) {
  std::vector<Bytes> frames = mcu_frames(path);
  CHECK(frames.size() > 40);

  Rig rig;
  PurifierState own;
  PurifierState bridged;
  bool have_bridged = false;
  uint8_t own_last = 0;
  uint8_t bridged_last = 0;
  int compared = 0;

  for (const Bytes &frame : frames) {
    int before = rig.host.fan_reports;
    rig.feed(frame);
    if (rig.host.fan_reports != before)
      have_bridged = state_from_core(CORE_300S, rig.device.state(), bridged, bridged);

    PurifierState decoded;
    if (!own_decode(frame, decoded))
      continue;
    own = decoded;
    if (own.level != 0 && (own.mode == Mode::MANUAL || own.running_level_valid))
      own_last = own.level;
    CHECK(have_bridged);
    if (!have_bridged)
      continue;
    if (bridged.level != 0 && (bridged.mode == Mode::MANUAL || bridged.running_level_valid))
      bridged_last = bridged.level;

    MatterView mine = make_view(CORE_300S, own, own_last, 80);
    MatterView theirs = make_view(CORE_300S, bridged, bridged_last, 80);
    compared++;
    CHECK(mine == theirs);
    CHECK(own.power == bridged.power);
    CHECK(own.mode == bridged.mode);
    CHECK(own.child_lock == bridged.child_lock);
    CHECK(own.air_quality == bridged.air_quality);
    CHECK(own.pm25 == bridged.pm25);
    if (mine != theirs)
      printf("  view differs at frame counter %02X\n", frame[2]);
  }
  CHECK(compared > 30);
  printf("capture parity: %d status frames compared\n", compared);
}

static void test_commands_match_bytes() {
  const Command commands[] = {
      command_power(true),        command_power(false),
      command_level(1),           command_level(2),
      command_level(3),           command_mode(Mode::MANUAL),
      command_mode(Mode::SLEEP),  command_mode(Mode::AUTO),
      command_display(true),      command_display(false),
      command_child_lock(true),   command_child_lock(false),
      command_filter_led(true),   command_filter_led(false),
  };
  for (const Command &command : commands) {
    Rig rig;
    CHECK(send_to_core(rig.device, command));
    CHECK(rig.platform.tx.size() == 1);
    if (rig.platform.tx.size() != 1)
      continue;
    const Bytes &sent = rig.platform.tx[0];
    uint8_t expected[MAX_FRAME_LEN];
    size_t len = encode_frame(TYPE_SEND, sent[2], command.command, 0x00,
                              command.payload, command.payload_len, expected,
                              sizeof(expected));
    CHECK(sent.size() == len);
    CHECK(len > 0 && memcmp(sent.data(), expected, len) == 0);
  }
  Rig rig;
  CHECK(!send_to_core(rig.device, command_wifi_led(WifiLed::SOLID)));
}

static void test_state_needs_fan_report() {
  Rig rig;
  PurifierState out;
  CHECK(!state_from_core(CORE_300S, rig.device.state(), PurifierState{}, out));
}

static void test_auto_keeps_manual_pick() {
  Rig rig;
  PurifierState state;
  rig.feed(hex_bytes("A5 22 D8 17 00 63 01 32 40 00 00 00 01 01 00 03 00 64 01 03 00 01 05 00 00 00 00 00 00"));
  CHECK(state_from_core(CORE_300S, rig.device.state(), state, state));
  CHECK(state.level == 3);
  rig.feed(hex_bytes("A5 22 E0 17 00 5C 01 32 40 00 00 00 01 01 02 03 00 64 01 00 00 01 05 00 00 00 00 00 00"));
  CHECK(state_from_core(CORE_300S, rig.device.state(), state, state));
  CHECK(state.mode == Mode::AUTO);
  CHECK(state.level == 3);
  CHECK(state.running_level_valid && state.running_level == 0);

  Rig cold;
  PurifierState boot;
  cold.feed(hex_bytes("A5 22 E0 17 00 5C 01 32 40 00 00 00 01 01 02 03 00 64 01 00 00 01 05 00 00 00 00 00 00"));
  CHECK(state_from_core(CORE_300S, cold.device.state(), boot, boot));
  CHECK(boot.level == 0);
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

static void test_filter_percent_matches() {
  for (uint8_t percent = 0; percent <= 100; percent++) {
    FilterLife own(CORE_300S);
    own.set_percent(percent);
    Rig rig;
    rig.device.set_used_cadr(own.used_cadr());
    rig.device.publish_filter_stats_now();
    CHECK(filter_percent_from_core(rig.host.filter_left) == own.percent());
  }
}

int main(int argc, char **argv) {
  const char *capture = argc > 1 ? argv[1]
                                 : "../../../third_party/levoit/devices/levoit-core300s/uart/status_32_40.txt";
  test_capture_parity(capture);
  test_commands_match_bytes();
  test_state_needs_fan_report();
  test_auto_keeps_manual_pick();
  test_button_hold();
  test_filter_percent_matches();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
