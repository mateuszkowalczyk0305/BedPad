#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "hal/adc_types.h"
#include <stdbool.h>

#define BEDPAD_DEVICE_NAME "MKBedPad"
#define BUTTON_LEFT_PIN GPIO_NUM_25
#define BUTTON_RIGHT_PIN GPIO_NUM_26
#define BUTTON_MIDDLE_PIN GPIO_NUM_27
#define JOYSTICK_SWITCH_PIN GPIO_NUM_32
#define JOYSTICK_X_CHANNEL ADC_CHANNEL_7 /* GPIO35, ADC1; logical cursor X */
#define JOYSTICK_Y_CHANNEL ADC_CHANNEL_6 /* GPIO34, ADC1; logical cursor Y */

#define BUTTON_DEBOUNCE_MS 30
#define BUTTON_SAMPLE_MS 10
#define INPUT_PERIOD_MS 20
#define INPUT_QUEUE_LENGTH 32
#define JOYSTICK_AVERAGE_SAMPLES 8
#define JOYSTICK_CALIBRATION_SAMPLES 64
#define JOYSTICK_DEAD_ZONE 220
#define JOYSTICK_MAX_STEP 15 /* Relative pixels per input period, 1..127. */
#define JOYSTICK_INVERT_X true
#define JOYSTICK_INVERT_Y false
#define KEYBOARD_HOLD_MS 50

_Static_assert(JOYSTICK_MAX_STEP > 0 && JOYSTICK_MAX_STEP <= 127,
               "Invalid mouse speed");
_Static_assert(JOYSTICK_DEAD_ZONE > 0 && JOYSTICK_DEAD_ZONE < 1024,
               "Invalid dead zone");
_Static_assert(JOYSTICK_AVERAGE_SAMPLES > 0, "Averaging requires samples");
_Static_assert(JOYSTICK_CALIBRATION_SAMPLES > 0,
               "Calibration requires samples");
_Static_assert(pdMS_TO_TICKS(INPUT_PERIOD_MS) > 0,
               "Input period below one tick");
_Static_assert(pdMS_TO_TICKS(BUTTON_SAMPLE_MS) > 0,
               "Debounce sampling below one tick");
