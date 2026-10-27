#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

#define DEBOUNCE_MS 30
#define SAMPLE_MS 10

static const char *TAG = "buttons";
static TaskHandle_t button_task;

typedef struct {
  gpio_num_t pin;
  const char *name;
  int raw;
  int stable;
  TickType_t changed_at;
} button_t;

static button_t buttons[] = {
    {.pin = GPIO_NUM_27, .name = "MIDDLE"},
    {.pin = GPIO_NUM_26, .name = "RIGHT"},
    {.pin = GPIO_NUM_25, .name = "LEFT"},
    {.pin = GPIO_NUM_32, .name = "JOYSTICK_SW"},
};

/* ESP32 ADC1: GPIO34 = channel 6, GPIO35 = channel 7. */
static void joystick_monitor(void *arg) {
  (void)arg;
  adc_oneshot_unit_handle_t adc;
  const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc));
  const adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_12,
                                          .bitwidth = ADC_BITWIDTH_12};
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_6, &channel));
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_7, &channel));
  ESP_LOGI("joystick", "ADC ready: X=GPIO34, Y=GPIO35; raw range 0..4095");
  for (;;) {
    int sum_x = 0, sum_y = 0;
    for (int i = 0; i < 16; ++i) {
      int x, y;
      ESP_ERROR_CHECK(adc_oneshot_read(adc, ADC_CHANNEL_6, &x));
      ESP_ERROR_CHECK(adc_oneshot_read(adc, ADC_CHANNEL_7, &y));
      sum_x += x;
      sum_y += y;
    }
    ESP_LOGI("joystick", "X=%4d Y=%4d", sum_x / 16, sum_y / 16);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

/* ISR only wakes the task: no logging or blocking operations here. */
static void button_isr(void *arg) {
  (void)arg;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(button_task, &woken);
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

static void button_monitor(void *arg) {
  (void)arg;
  bool pending = false;
  const TickType_t debounce = pdMS_TO_TICKS(DEBOUNCE_MS);
  for (;;) {
    /* Sleep until an IRQ; sample only while a change is settling. */
    ulTaskNotifyTake(pdTRUE,
                     pending ? pdMS_TO_TICKS(SAMPLE_MS) : portMAX_DELAY);
    TickType_t now = xTaskGetTickCount();
    pending = false;
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
      button_t *b = &buttons[i];
      int level = gpio_get_level(b->pin);
      if (level != b->raw) {
        b->raw = level;
        b->changed_at = now;
      }
      if (b->raw != b->stable) {
        if ((TickType_t)(now - b->changed_at) >= debounce) {
          b->stable = b->raw;
          ESP_LOGI(TAG, "%s GPIO%d: %s", b->name, (int)b->pin,
                   b->stable == 0 ? "PRESSED" : "RELEASED");
        } else {
          pending = true;
        }
      }
    }
  }
}

void app_main(void) {
  const gpio_config_t config = {
      .pin_bit_mask = (1ULL << GPIO_NUM_27) | (1ULL << GPIO_NUM_26) |
                      (1ULL << GPIO_NUM_25) | (1ULL << GPIO_NUM_32),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&config));
  for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    buttons[i].raw = buttons[i].stable = gpio_get_level(buttons[i].pin);
    ESP_LOGI(TAG, "%s GPIO%d initial: %s", buttons[i].name, (int)buttons[i].pin,
             buttons[i].stable ? "RELEASED" : "PRESSED");
  }
  ESP_ERROR_CHECK(gpio_install_isr_service(0));
  BaseType_t result =
      xTaskCreate(button_monitor, "buttons", 3072, NULL, 10, &button_task);
  ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
  for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    ESP_ERROR_CHECK(gpio_isr_handler_add(buttons[i].pin, button_isr, NULL));
    ESP_ERROR_CHECK(gpio_set_intr_type(buttons[i].pin, GPIO_INTR_ANYEDGE));
  }
  /* Also catch a level change during initialization. */
  xTaskNotifyGive(button_task);
  result = xTaskCreate(joystick_monitor, "joystick", 3072, NULL, 5, NULL);
  ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_LOGI(
      TAG,
      "IRQ test ready. Connect buttons between GPIO and GND; debounce %d ms.",
      DEBOUNCE_MS);
}
