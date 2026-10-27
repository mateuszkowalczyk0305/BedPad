/* BLE setup adapted from the installed ESP-IDF esp_hid_device example.
 * The official esp_hid component owns the HID GATT database and transport.
 */
#include "ble_hid.h"
#include "board_config.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hid_reports.h"
#include <string.h>

static const char *TAG = "ble_hid";
static esp_hidd_dev_t *device;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
  bool connected, encrypted, mouse_notify, keyboard_notify;
  bool suspended, boot_mode;
  bool services_ready, adv_ready, advertising;
  uint32_t session;
  esp_bd_addr_t peer;
} state;
static esp_gatt_if_t hid_interface = ESP_GATT_IF_NONE;
static uint16_t mouse_ccc, keyboard_ccc;

static esp_ble_adv_params_t advertising_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_hid_raw_report_map_t report_maps[] = {
    {.data = bedpad_report_map, .len = sizeof(bedpad_report_map)},
};
static const esp_hid_device_config_t device_config = {
    .vendor_id = 0x16c0,
    .product_id = 0x05df,
    .version = 0x0100,
    .device_name = BEDPAD_DEVICE_NAME,
    .manufacturer_name = "MKBedPad",
    .serial_number = "MKBedPad-01",
    .report_maps = report_maps,
    .report_maps_len = 1,
};

static void log_result(const char *operation, esp_err_t err) {
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s: %s", operation, esp_err_to_name(err));
  }
}

ble_hid_status_t ble_hid_status(void) {
  portENTER_CRITICAL(&state_lock);
  ble_hid_status_t status = {
      .ready = state.connected && state.encrypted && state.mouse_notify &&
               state.keyboard_notify && !state.suspended && !state.boot_mode,
      .session = state.session,
  };
  portEXIT_CRITICAL(&state_lock);
  return status;
}

static void start_advertising(void) {
  portENTER_CRITICAL(&state_lock);
  bool start = state.services_ready && state.adv_ready && !state.connected &&
               !state.advertising;
  if (start)
    state.advertising = true;
  portEXIT_CRITICAL(&state_lock);
  if (!start)
    return;
  esp_err_t err = esp_ble_gap_start_advertising(&advertising_params);
  if (err != ESP_OK) {
    portENTER_CRITICAL(&state_lock);
    state.advertising = false;
    portEXIT_CRITICAL(&state_lock);
    log_result("Start advertising", err);
  }
}

static void gap_callback(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param) {
  switch (event) {
  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    if (param->adv_data_cmpl.status != ESP_BT_STATUS_SUCCESS) {
      ESP_LOGE(TAG, "Advertising data failed: %d", param->adv_data_cmpl.status);
      break;
    }
    portENTER_CRITICAL(&state_lock);
    state.adv_ready = true;
    portEXIT_CRITICAL(&state_lock);
    start_advertising();
    break;
  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      ESP_LOGI(TAG, "Advertising as %s", BEDPAD_DEVICE_NAME);
    } else {
      portENTER_CRITICAL(&state_lock);
      state.advertising = false;
      portEXIT_CRITICAL(&state_lock);
      ESP_LOGE(TAG, "Advertising start failed: %d",
               param->adv_start_cmpl.status);
    }
    break;
  case ESP_GAP_BLE_SEC_REQ_EVT:
    log_result(
        "Security response",
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true));
    break;
  case ESP_GAP_BLE_AUTH_CMPL_EVT: {
    bool success = param->ble_security.auth_cmpl.success;
    portENTER_CRITICAL(&state_lock);
    if (state.connected &&
        memcmp(state.peer, param->ble_security.auth_cmpl.bd_addr,
               sizeof(state.peer)) == 0) {
      state.encrypted = success;
    }
    portEXIT_CRITICAL(&state_lock);
    if (success)
      ESP_LOGI(TAG, "Encrypted/bonded connection established");
    else
      ESP_LOGE(TAG,
               "Pairing failed: 0x%x; remove device in Windows and pair again",
               param->ble_security.auth_cmpl.fail_reason);
    break;
  }
  default:
    break;
  }
}

static void hid_callback(void *arg, esp_event_base_t base, int32_t id,
                         void *data) {
  (void)arg;
  (void)base;
  esp_hidd_event_data_t *event = data;
  switch (id) {
  case ESP_HIDD_START_EVENT:
    portENTER_CRITICAL(&state_lock);
    state.services_ready = true;
    portEXIT_CRITICAL(&state_lock);
    start_advertising();
    break;
  case ESP_HIDD_CONNECT_EVENT:
    ESP_LOGI(TAG,
             "Connected; waiting for encryption and both HID subscriptions");
    break;
  case ESP_HIDD_DISCONNECT_EVENT:
    ESP_LOGI(TAG, "Disconnected, reason=0x%x", event->disconnect.reason);
    start_advertising();
    break;
  case ESP_HIDD_CONTROL_EVENT:
    portENTER_CRITICAL(&state_lock);
    state.suspended = event->control.control == 0;
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGI(TAG, "Host %s", event->control.control ? "resumed" : "suspended");
    break;
  case ESP_HIDD_PROTOCOL_MODE_EVENT:
    portENTER_CRITICAL(&state_lock);
    state.boot_mode = event->protocol_mode.protocol_mode == 0;
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGI(TAG, "Protocol: %s",
             event->protocol_mode.protocol_mode ? "report"
                                                : "boot (input paused)");
    break;
  case ESP_HIDD_OUTPUT_EVENT:
    ESP_LOGD(TAG, "Keyboard LED/output report, length=%u",
             event->output.length);
    break;
  default:
    break;
  }
}

static void discover_report_cccs(esp_ble_gatts_cb_param_t *param) {
  /* ESP-IDF 6.1 ble_hidd.c places each input Report Reference immediately
   * after its CCC descriptor. Discover handles rather than hardcode offsets.
   * Boot reports have no Report Reference and are deliberately excluded.
   */
  for (unsigned i = 1; i < param->add_attr_tab.num_handle; ++i) {
    uint16_t length = 0;
    const uint8_t *value = NULL;
    if (esp_ble_gatts_get_attr_value(param->add_attr_tab.handles[i], &length,
                                     &value) != ESP_OK ||
        length != 2 || value[1] != ESP_HID_REPORT_TYPE_INPUT)
      continue;
    if (value[0] == HID_MOUSE_REPORT_ID)
      mouse_ccc = param->add_attr_tab.handles[i - 1];
    if (value[0] == HID_KEYBOARD_REPORT_ID)
      keyboard_ccc = param->add_attr_tab.handles[i - 1];
  }
  if (!mouse_ccc || !keyboard_ccc) {
    ESP_LOGE(
        TAG,
        "HID input CCC discovery failed; check installed ESP-IDF HID layout");
  }
}

static void gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t interface,
                           esp_ble_gatts_cb_param_t *param) {
  if (event == ESP_GATTS_REG_EVT && param->reg.status == ESP_GATT_OK &&
      param->reg.app_id == ESP_GATT_UUID_HID_SVC)
    hid_interface = interface;

  if (interface == hid_interface) {
    if (event == ESP_GATTS_CONNECT_EVT) {
      portENTER_CRITICAL(&state_lock);
      state.connected = true;
      state.encrypted = state.mouse_notify = state.keyboard_notify = false;
      state.suspended = state.boot_mode = state.advertising = false;
      ++state.session;
      memcpy(state.peer, param->connect.remote_bda, sizeof(state.peer));
      portEXIT_CRITICAL(&state_lock);
    } else if (event == ESP_GATTS_DISCONNECT_EVT) {
      portENTER_CRITICAL(&state_lock);
      state.connected = state.encrypted = state.mouse_notify =
          state.keyboard_notify = false;
      ++state.session;
      portEXIT_CRITICAL(&state_lock);
    }
  }
  /* Always forward events: the official component owns GATT responses. */
  esp_hidd_gatts_event_handler(event, interface, param);
  if (interface != hid_interface)
    return;
  if (event == ESP_GATTS_CREAT_ATTR_TAB_EVT &&
      param->add_attr_tab.status == ESP_GATT_OK) {
    discover_report_cccs(param);
  } else if (event == ESP_GATTS_WRITE_EVT && !param->write.is_prep &&
             param->write.len == 2) {
    bool enabled = (param->write.value[0] & 1) != 0;
    portENTER_CRITICAL(&state_lock);
    if (param->write.handle == mouse_ccc)
      state.mouse_notify = enabled;
    if (param->write.handle == keyboard_ccc)
      state.keyboard_notify = enabled;
    portEXIT_CRITICAL(&state_lock);
    if (param->write.handle == mouse_ccc ||
        param->write.handle == keyboard_ccc) {
      ESP_LOGI(TAG, "%s notifications %s",
               param->write.handle == mouse_ccc ? "Mouse" : "Keyboard",
               enabled ? "enabled" : "disabled");
    }
  }
}

esp_err_t ble_hid_init(void) {
  ESP_RETURN_ON_ERROR(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT),
                      TAG, "Release Classic BT failed");
  esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_bt_controller_init(&config), TAG,
                      "Controller init failed");
  ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_BLE), TAG,
                      "Controller enable failed");
  ESP_RETURN_ON_ERROR(esp_bluedroid_init(), TAG, "Bluedroid init failed");
  ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "Bluedroid enable failed");
  ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_callback), TAG,
                      "GAP callback failed");
  ESP_RETURN_ON_ERROR(esp_ble_gatts_register_callback(gatts_callback), TAG,
                      "GATT callback failed");

  /* No display/keypad: Secure Connections bonding with Just Works pairing. */
  esp_ble_auth_req_t auth = ESP_LE_AUTH_REQ_SC_BOND;
  esp_ble_io_cap_t io = ESP_IO_CAP_NONE;
  uint8_t key_size = 16;
  uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,
                                                     &auth, sizeof(auth)),
                      TAG, "Auth mode failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, sizeof(io)),
      TAG, "IO mode failed");
  ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(
                          ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size)),
                      TAG, "Key size failed");
  ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,
                                                     &keys, sizeof(keys)),
                      TAG, "Init keys failed");
  ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,
                                                     &keys, sizeof(keys)),
                      TAG, "Response keys failed");
  ESP_RETURN_ON_ERROR(esp_ble_gap_set_device_name(BEDPAD_DEVICE_NAME), TAG,
                      "Device name failed");
  ESP_RETURN_ON_ERROR(esp_hidd_dev_init(&device_config, ESP_HID_TRANSPORT_BLE,
                                        hid_callback, &device),
                      TAG, "HID init failed");

  /* Bluedroid expects 128-bit UUIDs here and compresses the Bluetooth base
   * UUID to the 16-bit HID service UUID (0x1812) in advertising data.
   */
  static uint8_t service_uuid[] = {
      0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
      0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
  };
  esp_ble_adv_data_t adv = {
      .include_name = true,
      .appearance = ESP_HID_APPEARANCE_GENERIC,
      .service_uuid_len = sizeof(service_uuid),
      .p_service_uuid = service_uuid,
      .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
  };
  return esp_ble_gap_config_adv_data(&adv);
}

static esp_err_t send_report(uint32_t session, unsigned id, uint8_t *report,
                             size_t size) {
  ble_hid_status_t status = ble_hid_status();
  if (!status.ready || status.session != session ||
      !esp_hidd_dev_connected(device)) {
    return ESP_ERR_INVALID_STATE;
  }
  return esp_hidd_dev_input_set(device, 0, id, report, size);
}

esp_err_t ble_hid_send_mouse(uint32_t session, uint8_t buttons, int8_t dx,
                             int8_t dy) {
  uint8_t report[HID_MOUSE_REPORT_SIZE] = {buttons & 0x07, (uint8_t)dx,
                                           (uint8_t)dy, 0};
  return send_report(session, HID_MOUSE_REPORT_ID, report, sizeof(report));
}

esp_err_t ble_hid_send_keyboard(uint32_t session, uint8_t modifiers,
                                uint8_t key) {
  uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {modifiers, 0, key, 0, 0, 0, 0, 0};
  return send_report(session, HID_KEYBOARD_REPORT_ID, report, sizeof(report));
}
