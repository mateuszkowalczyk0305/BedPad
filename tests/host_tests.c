#include "esp_hid_common.h"
#include "hid_reports.h"
#include "input_math.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void test_axes(void) {
  const int centers[] = {1100, 2048, 3000};
  for (unsigned i = 0; i < sizeof(centers) / sizeof(centers[0]); ++i) {
    int center = centers[i];
    int previous = -128;
    for (int raw = 0; raw <= 4095; ++raw) {
      int value = input_axis_delta(raw, center, 220, 15, false);
      assert(value >= -15 && value <= 15);
      assert(value >= previous);
      assert(input_axis_delta(raw, center, 220, 15, true) == -value);
      if (abs(raw - center) <= 220)
        assert(value == 0);
      else
        assert(raw > center ? value > 0 : value < 0);
      previous = value;
    }
    assert(input_axis_delta(0, center, 220, 15, false) == -15);
    assert(input_axis_delta(4095, center, 220, 15, false) == 15);
  }
  assert(input_axis_delta(4095, 2048, 220, 500, false) == 127);
  assert(input_axis_delta(0, 2048, 220, 500, false) == -127);
  assert(input_axis_delta(3000, 2048, 220, 0, false) == 0);
  assert(input_axis_delta(4095, 4090, 220, 15, false) == 0);
}

static void test_descriptor(void) {
  /* Exercise the actual installed Espressif parser, not a second parser. */
  esp_hid_report_map_t *map =
      esp_hid_parse_report_map(bedpad_report_map, sizeof(bedpad_report_map));
  assert(map);
  unsigned mouse = 0, keyboard = 0, leds = 0;
  for (unsigned i = 0; i < map->reports_len; ++i) {
    const esp_hid_report_item_t *r = &map->reports[i];
    if (r->protocol_mode != ESP_HID_PROTOCOL_MODE_REPORT)
      continue;
    if (r->report_id == HID_MOUSE_REPORT_ID &&
        r->report_type == ESP_HID_REPORT_TYPE_INPUT) {
      assert(r->usage == ESP_HID_USAGE_MOUSE);
      assert(r->value_len == HID_MOUSE_REPORT_SIZE);
      ++mouse;
    } else if (r->report_id == HID_KEYBOARD_REPORT_ID &&
               r->report_type == ESP_HID_REPORT_TYPE_INPUT) {
      assert(r->usage == ESP_HID_USAGE_KEYBOARD);
      assert(r->value_len == HID_KEYBOARD_REPORT_SIZE);
      ++keyboard;
    } else if (r->report_id == HID_KEYBOARD_REPORT_ID &&
               r->report_type == ESP_HID_REPORT_TYPE_OUTPUT) {
      assert(r->value_len == 1);
      ++leds;
    } else
      assert(!"Unexpected HID report");
  }
  assert(mouse == 1 && keyboard == 1 && leds == 1);
  esp_hid_free_report_map(map);
}

int main(void) {
  test_axes();
  test_descriptor();
  puts("PASS: joystick boundaries, dead zone, inversion, clamping; official "
       "HID descriptor parser");
  return 0;
}
