#include "button_input.h"
#include "board_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"

typedef struct {
  gpio_num_t pin;
  const char *name;
  int raw;
  int stable;
  TickType_t changed_at;
} button_t;

static button_t buttons[BUTTON_COUNT] = {
    [BUTTON_LEFT] = {.pin = BUTTON_LEFT_PIN, .name = "LEFT"},
    [BUTTON_RIGHT] = {.pin = BUTTON_RIGHT_PIN, .name = "RIGHT"},
    [BUTTON_MIDDLE] = {.pin = BUTTON_MIDDLE_PIN, .name = "MIDDLE"},
    [BUTTON_JOYSTICK] = {.pin = JOYSTICK_SWITCH_PIN, .name = "JOYSTICK_SW"},
};
static TaskHandle_t task;
static QueueHandle_t event_queue;

static uint8_t mouse_mask(void) {
  return (!buttons[BUTTON_LEFT].stable ? 1 : 0) |
         (!buttons[BUTTON_RIGHT].stable ? 2 : 0) |
         (!buttons[BUTTON_MIDDLE].stable ? 4 : 0);
}

static void emit_event(button_id_t id) {
  button_event_t event = {
      .button = id,
      .pressed = buttons[id].stable == 0,
      .mouse_buttons = mouse_mask(),
      .timestamp = xTaskGetTickCount(),
  };
  /* Backpressure preserves press/release order; this runs in a task, not ISR.
   */
  xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void button_isr(void *arg) {
  (void)arg;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(task, &woken);
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

static void monitor(void *arg) {
  (void)arg;
  /* Synchronize held mouse buttons, without synthesizing a shortcut at boot. */
  emit_event(BUTTON_LEFT);
  bool pending = false;
  for (;;) {
    ulTaskNotifyTake(pdTRUE,
                     pending ? pdMS_TO_TICKS(BUTTON_SAMPLE_MS) : portMAX_DELAY);
    TickType_t now = xTaskGetTickCount();
    pending = false;
    for (button_id_t id = BUTTON_LEFT; id < BUTTON_COUNT; ++id) {
      button_t *b = &buttons[id];
      int level = gpio_get_level(b->pin);
      if (level != b->raw) {
        b->raw = level;
        b->changed_at = now;
      }
      if (b->raw == b->stable) {
        continue;
      }
      if ((TickType_t)(now - b->changed_at) <
          pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
        pending = true;
        continue;
      }
      b->stable = b->raw;
      ESP_LOGI("buttons", "%s: %s", b->name,
               b->stable ? "RELEASED" : "PRESSED");
      emit_event(id);
    }
  }
}

esp_err_t button_input_start(QueueHandle_t queue) {
  if (!queue || task) {
    return ESP_ERR_INVALID_STATE;
  }
  const gpio_config_t config = {
      .pin_bit_mask = (1ULL << BUTTON_LEFT_PIN) | (1ULL << BUTTON_RIGHT_PIN) |
                      (1ULL << BUTTON_MIDDLE_PIN) |
                      (1ULL << JOYSTICK_SWITCH_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&config), "buttons", "GPIO setup failed");
  for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
    buttons[i].raw = buttons[i].stable = gpio_get_level(buttons[i].pin);
  }
  ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), "buttons",
                      "ISR service failed");
  event_queue = queue;
  if (xTaskCreate(monitor, "buttons", 3072, NULL, 6, &task) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(buttons[i].pin, button_isr, NULL),
                        "buttons", "ISR setup failed");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(buttons[i].pin, GPIO_INTR_ANYEDGE),
                        "buttons", "IRQ setup failed");
  }
  xTaskNotifyGive(task);
  return ESP_OK;
}
