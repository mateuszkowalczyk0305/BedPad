#include "ble_hid.h"
#include "board_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "input_controller.h"
#include "joystick.h"
#include "nvs_flash.h"

void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW("app",
             "Reinitializing NVS; previous Bluetooth bonds will be lost");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_ERROR_CHECK(joystick_init());
  ESP_ERROR_CHECK(ble_hid_init());
  ESP_ERROR_CHECK(input_controller_start());
  ESP_LOGI("app", "%s started. Pair in Windows Bluetooth settings.",
           BEDPAD_DEVICE_NAME);
}
