#include "levoit_link.h"

#include "levoit_frame.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cinttypes>

static const char *const TAG = "link";

namespace levoit {

namespace {

constexpr uart_port_t UART_PORT = UART_NUM_2;
constexpr int BAUD_RATE = 115200;
constexpr size_t QUEUE_SIZE = 16;
constexpr uint32_t ACK_TIMEOUT_MS = 300;
constexpr uint32_t MIN_GAP_MS = 60;
constexpr uint8_t MAX_ATTEMPTS = 3;
constexpr uint32_t ALIVE_WINDOW_MS = 30000;
constexpr uint32_t STATUS_QUERY_MS = 5000;
constexpr uint32_t PARTIAL_FRAME_MS = 50;
constexpr uint32_t TASK_STACK = 6144;

const ModelSpec *model = nullptr;
LinkHandlers handlers{};
FrameParser parser;
uint8_t counter = 0x10;

Command queue[QUEUE_SIZE];
size_t queue_head = 0;
size_t queue_count = 0;

bool awaiting_ack = false;
uint8_t attempts = 0;
uint32_t last_tx_ms = 0;
std::atomic<uint32_t> last_frame_ms{0};
std::atomic<bool> frame_seen{false};
uint32_t last_status_ms = 0;
uint32_t last_query_ms = 0;
uint32_t last_bytes_ms = 0;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void write_frame(uint8_t type, uint32_t command, uint8_t trailer,
                 const uint8_t *payload, size_t payload_len) {
  uint8_t frame[MAX_FRAME_LEN];
  size_t len = encode_frame(type, counter, command, trailer, payload,
                            payload_len, frame, sizeof(frame));
  if (len == 0)
    return;
  int written = uart_write_bytes(UART_PORT, frame, len);
  if (written != static_cast<int>(len))
    ESP_LOGE(TAG, "uart write failed: %d of %u", written,
             static_cast<unsigned>(len));
  counter = next_counter(counter);
}

void transmit_head() {
  const Command &command = queue[queue_head];
  write_frame(TYPE_SEND, command.command, 0x00, command.payload,
              command.payload_len);
  awaiting_ack = true;
  attempts++;
  last_tx_ms = now_ms();
}

void pop_head() {
  queue_head = (queue_head + 1) % QUEUE_SIZE;
  queue_count--;
  awaiting_ack = false;
  attempts = 0;
}

void write_ack(const Frame &received) {
  bool alternate =
      model->status_ack_alt && is_status_command(*model, received.command);
  uint8_t frame[HEADER_LEN];
  size_t len = encode_ack(alternate ? received.counter : counter,
                          received.command, alternate, frame, sizeof(frame));
  if (len == 0)
    return;
  uart_write_bytes(UART_PORT, frame, len);
  if (!alternate)
    counter = next_counter(counter);
}

void deliver_status(const Frame &frame, uint32_t now) {
  PurifierState state;
  if (!decode_status(*model, frame.payload, frame.payload_len, state)) {
    ESP_LOGW(TAG, "status frame rejected, %u bytes",
             static_cast<unsigned>(frame.payload_len));
    return;
  }
  if (last_status_ms == 0)
    ESP_LOG_BUFFER_HEX(TAG, frame.payload, frame.payload_len);
  last_status_ms = now == 0 ? 1 : now;
  handlers.on_status(state, now);
}

void handle_frame(const Frame &frame) {
  uint32_t now = now_ms();
  last_frame_ms.store(now);
  frame_seen.store(true);

  if (frame.type == TYPE_ACK) {
    if (awaiting_ack && frame.command == queue[queue_head].command)
      pop_head();
    if (model->status_request != 0 && frame.command == model->status_request &&
        frame.payload_len >= model->layout.min_payload_len)
      deliver_status(frame, now);
    return;
  }
  if (frame.type != TYPE_SEND) {
    ESP_LOGW(TAG, "unexpected frame type 0x%02X command %06" PRIX32, frame.type,
             frame.command);
    return;
  }

  write_ack(frame);

  if (is_status_command(*model, frame.command)) {
    deliver_status(frame, now);
    return;
  }
  if (frame.command == COMMAND_FILTER_RESET) {
    handlers.on_filter_reset(now);
    return;
  }
  ESP_LOGI(TAG, "unhandled command %06" PRIX32 ", %u bytes", frame.command,
           static_cast<unsigned>(frame.payload_len));
}

void drive_transmit() {
  uint32_t now = now_ms();
  if (awaiting_ack) {
    if (now - last_tx_ms < ACK_TIMEOUT_MS)
      return;
    if (attempts >= MAX_ATTEMPTS) {
      ESP_LOGW(TAG, "no ack for command %06" PRIX32, queue[queue_head].command);
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
    uint32_t now = now_ms();
    Frame frame;
    if (n > 0) {
      last_bytes_ms = now;
      parser.feed(chunk, static_cast<size_t>(n));
      while (parser.next(frame))
        handle_frame(frame);
    } else if (parser.buffered() > 0 && now - last_bytes_ms > PARTIAL_FRAME_MS) {
      parser.skip_byte();
      while (parser.next(frame))
        handle_frame(frame);
    }
    if (model->status_request != 0 && queue_count == 0 &&
        now - last_status_ms > STATUS_QUERY_MS &&
        now - last_query_ms > STATUS_QUERY_MS) {
      last_query_ms = now;
      link_send(command_status_request(*model));
    }
    handlers.on_poll(now);
    drive_transmit();
  }
}

} // namespace

esp_err_t link_start(const ModelSpec &spec, int tx_pin, int rx_pin,
                     const LinkHandlers &link_handlers) {
  model = &spec;
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
    err = uart_set_pin(UART_PORT, tx_pin, rx_pin, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    uart_driver_delete(UART_PORT);
    return err;
  }
  if (xTaskCreate(link_task, "levoit_link", TASK_STACK, nullptr, 5, nullptr) !=
      pdPASS) {
    uart_driver_delete(UART_PORT);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool link_send(const Command &command) {
  if (queue_count == QUEUE_SIZE) {
    ESP_LOGW(TAG, "command queue full");
    return false;
  }
  queue[(queue_head + queue_count) % QUEUE_SIZE] = command;
  queue_count++;
  return true;
}

bool link_alive() {
  return frame_seen.load() && now_ms() - last_frame_ms.load() < ALIVE_WINDOW_MS;
}

} // namespace levoit
