#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t joystick_init(void);
esp_err_t joystick_read_movement(int8_t *dx, int8_t *dy);
