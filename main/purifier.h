#pragma once

#include "purifier_map.h"

#include <esp_err.h>

#include <cstdint>

namespace levoit_matter {

enum class RequestKind : uint8_t {
  FAN_OFF,
  FAN_AUTO,
  FAN_LEVEL,
  FAN_RESUME,
  POWER,
  SLEEP,
  DISPLAY,
  CHILD_LOCK,
  FILTER_RESET,
  FILTER_SET,
  WIFI_LED,
  IDENTIFY,
};

struct Request {
  RequestKind kind;
  uint8_t value;
};

struct PurifierCallbacks {
  bool (*publish)(const MatterView &view);
  void (*factory_reset)();
};

const ModelSpec &purifier_model();
void purifier_load();
uint8_t purifier_filter_percent();
esp_err_t purifier_start(const PurifierCallbacks &callbacks);
bool purifier_post(const Request &request);
bool purifier_ready();
bool purifier_link_alive();

} // namespace levoit_matter
