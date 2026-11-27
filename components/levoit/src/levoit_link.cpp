#include "levoit_link.h"

#include "levoit_frame.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include <atomic>
#include <cinttypes>
#include <cstring>

static const char *const TAG = "link";

namespace levoit_matter {

namespace {

constexpr uart_port_t UART_PORT = UART_NUM_2;
constexpr int BAUD_RATE = 115200;
constexpr size_t QUEUE_SIZE = 16;
constexpr uint32_t ACK_TIMEOUT_MS = 300;
constexpr uint32_t MIN_GAP_MS = 60;
constexpr uint8_t MAX_ATTEMPTS = 3;
constexpr uint32_t ALIVE_WINDOW_MS = 30000;
constexpr uint32_t PARTIAL_FRAME_MS = 50;
constexpr uint32_t TASK_STACK = 8192;

struct Pending {
  uint8_t bytes[MAX_FRAME_LEN];
  size_t len;
};

LinkHandlers handlers{};
FrameParser parser;

Pending queue[QUEUE_SIZE];
size_t queue_head = 0;
size_t queue_count = 0;

bool awaiting_ack = false;
uint8_t attempts = 0;
uint32_t last_tx_ms = 0;
std::atomic<uint32_t> last_frame_ms{0};
std::atomic<bool> frame_seen{false};
uint32_t last_bytes_ms = 0;

uint32_t command_of(const uint8_t *frame) {
  return (static_cast<uint32_t>(frame[6]) << 16) |
         (static_cast<uint32_t>(frame[7]) << 8) | frame[8];
}

void log_frame(const char *direction, const uint8_t *data, size_t len) {
#if CONFIG_LEVOIT_LOG_FRAMES
  static const char DIGITS[] = "0123456789ABCDEF";
  char text[MAX_FRAME_LEN * 3 + 1];
  size_t at = 0;
  for (size_t i = 0; i < len && i < MAX_FRAME_LEN; i++) {
    text[at++] = DIGITS[data[i] >> 4];
    text[at++] = DIGITS[data[i] & 0x0F];
    text[at++] = ' ';
  }
  text[at == 0 ? 0 : at - 1] = '\0';
  ESP_LOGI(TAG, "%s %s", direction, text);
#else
  (void)direction;
  (void)data;
  (void)len;
#endif
}

void write_raw(const uint8_t *frame, size_t len) {
  log_frame("tx", frame, len);
  int written = uart_write_bytes(UART_PORT, frame, len);
  if (written != static_cast<int>(len))
    ESP_LOGE(TAG, "uart write failed: %d of %u", written, static_cast<unsigned>(len));
}

void transmit_head() {
  const Pending &head = queue[queue_head];
  write_raw(head.bytes, head.len);
  awaiting_ack = true;
  attempts++;
  last_tx_ms = link_now_ms();
}

void pop_head() {
  queue_head = (queue_head + 1) % QUEUE_SIZE;
  queue_count--;
  awaiting_ack = false;
  attempts = 0;
}

void note_frame(const Frame &frame) {
  last_frame_ms.store(link_now_ms());
  frame_seen.store(true);
  log_frame("rx", frame.raw, frame.raw_len);
  if (frame.type == TYPE_ACK && awaiting_ack &&
      frame.command == command_of(queue[queue_head].bytes))
    pop_head();
}

void drive_transmit() {
  uint32_t now = link_now_ms();
  if (awaiting_ack) {
    if (now - last_tx_ms < ACK_TIMEOUT_MS)
      return;
    if (attempts >= MAX_ATTEMPTS) {
      ESP_LOGW(TAG, "no ack for command %06" PRIX32, command_of(queue[queue_head].bytes));
      pop_head();
    } else {
      transmit_head();
    }
    return;
  }
  if (queue_count > 0 && now - last_tx_ms >= MIN_GAP_MS)
    transmit_head();
}

void link_task(void *) {
  uint8_t chunk[128];
  for (;;) {
    int n = uart_read_bytes(UART_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(20));
    uint32_t now = link_now_ms();
    Frame frame;
    if (n > 0) {
      last_bytes_ms = now;
      parser.feed(chunk, static_cast<size_t>(n));
      while (parser.next(frame))
        note_frame(frame);
      handlers.on_bytes(chunk, static_cast<size_t>(n), now);
    } else if (parser.buffered() > 0 && now - last_bytes_ms > PARTIAL_FRAME_MS) {
      parser.skip_byte();
      while (parser.next(frame))
        note_frame(frame);
    }
    handlers.on_poll(link_now_ms());
    drive_transmit();
  }
}

} // namespace

uint32_t link_now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

esp_err_t link_start(int tx_pin, int rx_pin, const LinkHandlers &link_handlers) {
  handlers = link_handlers;

  uart_config_t config = {};
  config.baud_rate = BAUD_RATE;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_driver_install(UART_PORT, 1024, 512, 0, nullptr, 0);
  if (err != ESP_OK)
    return err;
  err = uart_param_config(UART_PORT, &config);
  if (err == ESP_OK)
    err = uart_set_pin(UART_PORT, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    uart_driver_delete(UART_PORT);
    return err;
  }
  if (xTaskCreate(link_task, "levoit_link", TASK_STACK, nullptr, 5, nullptr) != pdPASS) {
    uart_driver_delete(UART_PORT);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool link_write(const uint8_t *frame, size_t len) {
  if (len < HEADER_LEN || len > MAX_FRAME_LEN)
    return false;
  if (frame[1] != TYPE_SEND) {
    write_raw(frame, len);
    return true;
  }
  if (queue_count == QUEUE_SIZE) {
    ESP_LOGW(TAG, "command queue full");
    return false;
  }
  Pending &slot = queue[(queue_head + queue_count) % QUEUE_SIZE];
  memcpy(slot.bytes, frame, len);
  slot.len = len;
  queue_count++;
  return true;
}

bool link_alive() {
  return frame_seen.load() && link_now_ms() - last_frame_ms.load() < ALIVE_WINDOW_MS;
}

} // namespace levoit_matter
