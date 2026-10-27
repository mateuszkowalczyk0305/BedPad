#include "joystick.h"
#include "board_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input_math.h"

static adc_oneshot_unit_handle_t adc;
static int center_x, center_y;
static int filtered_x, filtered_y;

static esp_err_t read_axes(int *x, int *y) {
  int sum_x = 0, sum_y = 0;
  for (unsigned i = 0; i < JOYSTICK_AVERAGE_SAMPLES; ++i) {
    int raw_x, raw_y;
    esp_err_t err = adc_oneshot_read(adc, JOYSTICK_X_CHANNEL, &raw_x);
    if (err != ESP_OK) {
      return err;
    }
    err = adc_oneshot_read(adc, JOYSTICK_Y_CHANNEL, &raw_y);
    if (err != ESP_OK) {
      return err;
    }
    sum_x += raw_x;
    sum_y += raw_y;
  }
  *x = sum_x / JOYSTICK_AVERAGE_SAMPLES;
  *y = sum_y / JOYSTICK_AVERAGE_SAMPLES;
  return ESP_OK;
}

esp_err_t joystick_init(void) {
  const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
  ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &adc), "joystick",
                      "ADC init failed");
  const adc_oneshot_chan_cfg_t channel = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  ESP_RETURN_ON_ERROR(
      adc_oneshot_config_channel(adc, JOYSTICK_X_CHANNEL, &channel), "joystick",
      "X init failed");
  ESP_RETURN_ON_ERROR(
      adc_oneshot_config_channel(adc, JOYSTICK_Y_CHANNEL, &channel), "joystick",
      "Y init failed");
  ESP_LOGI("joystick", "Calibrating center: leave joystick released");
  int sum_x = 0, sum_y = 0;
  int min_x = 4095, min_y = 4095, max_x = 0, max_y = 0;
  for (unsigned i = 0; i < JOYSTICK_CALIBRATION_SAMPLES; ++i) {
    int x, y;
    ESP_RETURN_ON_ERROR(read_axes(&x, &y), "joystick",
                        "Calibration read failed");
    sum_x += x;
    sum_y += y;
    if (x < min_x)
      min_x = x;
    if (y < min_y)
      min_y = y;
    if (x > max_x)
      max_x = x;
    if (y > max_y)
      max_y = y;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  center_x = sum_x / JOYSTICK_CALIBRATION_SAMPLES;
  center_y = sum_y / JOYSTICK_CALIBRATION_SAMPLES;
  /* Reject a moved or disconnected stick instead of producing cursor drift. */
  if (center_x < 700 || center_x > 3400 || center_y < 700 || center_y > 3400 ||
      max_x - min_x > JOYSTICK_DEAD_ZONE ||
      max_y - min_y > JOYSTICK_DEAD_ZONE) {
    ESP_LOGE("joystick",
             "Unstable/invalid center (%d,%d); release stick and reset",
             center_x, center_y);
    return ESP_ERR_INVALID_STATE;
  }
  filtered_x = center_x;
  filtered_y = center_y;
  ESP_LOGI("joystick", "Center X=%d Y=%d; dead zone=%d, speed=%d", center_x,
           center_y, JOYSTICK_DEAD_ZONE, JOYSTICK_MAX_STEP);
  return ESP_OK;
}

esp_err_t joystick_read_movement(int8_t *dx, int8_t *dy) {
  int x, y;
  esp_err_t err = read_axes(&x, &y);
  if (err != ESP_OK) {
    *dx = *dy = 0;
    return err;
  }
  /* A short moving average plus a first-order filter reduces ADC jitter. */
  filtered_x += (x - filtered_x) / 4;
  filtered_y += (y - filtered_y) / 4;
  *dx = input_axis_delta(filtered_x, center_x, JOYSTICK_DEAD_ZONE,
                         JOYSTICK_MAX_STEP, JOYSTICK_INVERT_X);
  *dy = input_axis_delta(filtered_y, center_y, JOYSTICK_DEAD_ZONE,
                         JOYSTICK_MAX_STEP, JOYSTICK_INVERT_Y);
  return ESP_OK;
}
