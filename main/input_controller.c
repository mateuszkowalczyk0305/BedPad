#include "input_controller.h"
#include "ble_hid.h"
#include "board_config.h"
#include "button_input.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid_reports.h"
#include "joystick.h"

static QueueHandle_t events;

typedef struct {
  uint32_t session;
  bool active;
  uint8_t buttons;
  uint8_t sent_buttons;
  bool mouse_dirty;
  bool filter_old_events;
  bool key_held;
  unsigned shortcuts;
  TickType_t key_pressed_at;
  TickType_t ready_at;
  TickType_t last_error_at;
} controller_t;

static void report_error(controller_t *c, const char *operation,
                         esp_err_t err) {
  TickType_t now = xTaskGetTickCount();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE &&
      (c->last_error_at == 0 ||
       now - c->last_error_at >= pdMS_TO_TICKS(1000))) {
    ESP_LOGW("inputs", "%s failed: %s; state will be retried", operation,
             esp_err_to_name(err));
    c->last_error_at = now;
  }
}

static void update_connection(controller_t *c) {
  ble_hid_status_t status = ble_hid_status();
  if (!status.ready || status.session != c->session) {
    c->active = false;
    c->key_held = false;
    c->shortcuts = 0;
    c->mouse_dirty = true;
    c->session = status.session;
  }
  if (!status.ready || c->active)
    return;
  /* Clear stale modifiers before accepting input on a new/resumed link. */
  esp_err_t err = ble_hid_send_keyboard(c->session, 0, 0);
  if (err != ESP_OK) {
    report_error(c, "Initial keyboard release", err);
    return;
  }
  c->active = true;
  c->ready_at = xTaskGetTickCount();
  c->filter_old_events = true;
  c->mouse_dirty = true;
  ESP_LOGI("inputs", "HID ready: mouse + keyboard");
}

static void send_mouse(controller_t *c, int8_t dx, int8_t dy) {
  if (!c->active ||
      (!dx && !dy && !c->mouse_dirty && c->buttons == c->sent_buttons))
    return;
  esp_err_t err = ble_hid_send_mouse(c->session, c->buttons, dx, dy);
  if (err == ESP_OK) {
    c->sent_buttons = c->buttons;
    c->mouse_dirty = false;
  } else {
    c->mouse_dirty = true;
    report_error(c, "Mouse report", err);
  }
}

static void handle_button(controller_t *c, const button_event_t *event) {
  c->buttons = event->mouse_buttons;
  /* Offline input updates the physical state, but must not replay clicks or
   * shortcuts after pairing/reconnection. Signed subtraction handles wrap.
   */
  if (!c->active ||
      (c->filter_old_events && (int32_t)(event->timestamp - c->ready_at) < 0))
    return;
  if (event->button != BUTTON_JOYSTICK) {
    /* Preserve each debounced edge, including clicks queued between frames. */
    send_mouse(c, 0, 0);
  } else if (event->pressed && c->active && ble_hid_status().ready) {
    if (c->shortcuts < INPUT_QUEUE_LENGTH) {
      ++c->shortcuts;
    } else {
      ESP_LOGW("inputs", "Shortcut queue full");
    }
  }
}

static void update_keyboard(controller_t *c) {
  if (!c->active)
    return;
  TickType_t now = xTaskGetTickCount();
  if (c->key_held) {
    if (now - c->key_pressed_at < pdMS_TO_TICKS(KEYBOARD_HOLD_MS))
      return;
    esp_err_t err = ble_hid_send_keyboard(c->session, 0, 0);
    if (err == ESP_OK)
      c->key_held = false;
    else
      report_error(c, "Keyboard release", err);
    /* Leave a frame between release and the next shortcut. */
    return;
  }
  if (c->shortcuts == 0)
    return;
  esp_err_t err = ble_hid_send_keyboard(
      c->session, HID_MOD_LEFT_GUI | HID_MOD_LEFT_CTRL, HID_KEY_O);
  if (err == ESP_OK) {
    c->key_held = true;
    c->key_pressed_at = now;
    --c->shortcuts;
    ESP_LOGI("inputs", "Shortcut: Win+Ctrl+O");
  } else
    report_error(c, "Keyboard shortcut", err);
}

static void run(void *arg) {
  (void)arg;
  controller_t controller = {.mouse_dirty = true};
  TickType_t last_frame = xTaskGetTickCount();
  for (;;) {
    update_connection(&controller);
    button_event_t event;
    /* Bound a frame's work so input traffic cannot starve key releases. */
    for (unsigned i = 0;
         i < INPUT_QUEUE_LENGTH && xQueueReceive(events, &event, 0) == pdTRUE;
         ++i) {
      handle_button(&controller, &event);
    }
    if (uxQueueMessagesWaiting(events) == 0)
      controller.filter_old_events = false;
    int8_t dx = 0, dy = 0;
    esp_err_t err = joystick_read_movement(&dx, &dy);
    if (err != ESP_OK)
      report_error(&controller, "ADC read", err);
    update_keyboard(&controller);
    send_mouse(&controller, dx, dy);
    /* Drop missed frames after a slow BLE call instead of burst-polling ADC. */
    TickType_t now = xTaskGetTickCount();
    if (now - last_frame >= pdMS_TO_TICKS(INPUT_PERIOD_MS))
      last_frame = now;
    xTaskDelayUntil(&last_frame, pdMS_TO_TICKS(INPUT_PERIOD_MS));
  }
}

esp_err_t input_controller_start(void) {
  events = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(button_event_t));
  if (!events)
    return ESP_ERR_NO_MEM;
  esp_err_t err = button_input_start(events);
  if (err != ESP_OK)
    return err;
  /* 4 KiB includes ADC and the synchronous official HID transport calls. */
  if (xTaskCreate(run, "inputs", 4096, NULL, 5, NULL) != pdPASS)
    return ESP_ERR_NO_MEM;
  return ESP_OK;
}
