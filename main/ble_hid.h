#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool ready;
  uint32_t session;
} ble_hid_status_t;

esp_err_t ble_hid_init(void);
ble_hid_status_t ble_hid_status(void);
/* All report calls belong to the input-controller task; callbacks never send.
 */
esp_err_t ble_hid_send_mouse(uint32_t session, uint8_t buttons, int8_t dx,
                             int8_t dy);
esp_err_t ble_hid_send_keyboard(uint32_t session, uint8_t modifiers,
                                uint8_t key);
