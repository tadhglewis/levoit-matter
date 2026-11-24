#include "remote_log.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

constexpr size_t CAPACITY = 6144;
constexpr size_t LINE_BUFFER = 192;

char ring[CAPACITY];
size_t head = 0;
bool wrapped = false;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t previous = nullptr;

void append(const char *text, size_t len) {
  portENTER_CRITICAL(&lock);
  for (size_t i = 0; i < len; i++) {
    ring[head] = text[i];
    head = (head + 1) % CAPACITY;
    if (head == 0)
      wrapped = true;
  }
  portEXIT_CRITICAL(&lock);
}

int capture(const char *format, va_list args) {
  char line[LINE_BUFFER];
  va_list copy;
  va_copy(copy, args);
  int len = vsnprintf(line, sizeof(line), format, copy);
  va_end(copy);
  if (len > 0)
    append(line, static_cast<size_t>(len) < sizeof(line) ? static_cast<size_t>(len)
                                                         : sizeof(line) - 1);
  return previous != nullptr ? previous(format, args) : 0;
}

} // namespace

void remote_log_install() { previous = esp_log_set_vprintf(capture); }

size_t remote_log_capacity() { return CAPACITY; }

size_t remote_log_copy(char *out, size_t max) {
  portENTER_CRITICAL(&lock);
  size_t start = wrapped ? head : 0;
  size_t available = wrapped ? CAPACITY : head;
  size_t count = available < max ? available : max;
  size_t skip = available - count;
  for (size_t i = 0; i < count; i++)
    out[i] = ring[(start + skip + i) % CAPACITY];
  portEXIT_CRITICAL(&lock);
  return count;
}
