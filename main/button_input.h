#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  BUTTON_LEFT,
  BUTTON_RIGHT,
  BUTTON_MIDDLE,
  BUTTON_JOYSTICK,
  BUTTON_COUNT
} button_id_t;

typedef struct {
  button_id_t button;
  bool pressed;
  uint8_t mouse_buttons;
  TickType_t timestamp;
} button_event_t;

/* The queue must hold button_event_t. Called once during startup. */
esp_err_t button_input_start(QueueHandle_t queue);
