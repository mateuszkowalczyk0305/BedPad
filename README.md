# MKBedPad

ESP32-WROOM-32 joystick controller using ESP-IDF 6.1, FreeRTOS and the official
Espressif `esp_hid` component. It advertises as **MKBedPad**: one BLE device,
one HID service, mouse and keyboard application collections with separate
report IDs. Windows pairs with it once for both functions.

## Wiring

| Signal | ESP32 connection | Function |
| --- | --- | --- |
| Joystick VCC | 3V3 | Power at 3.3 V, even if module says +5V |
| Joystick GND | GND | Common ground |
| Joystick VRx | GPIO34 / ADC1 channel 6 | Cursor X |
| Joystick VRy | GPIO35 / ADC1 channel 7 | Cursor Y |
| Joystick SW | GPIO32 | Win + Ctrl + O on each press |
| Left button | GPIO25 | Left mouse button |
| Right button | GPIO26 | Right mouse button |
| Middle button | GPIO27 | Middle mouse button |

All switches connect GPIO to GND when pressed and use internal pull-ups.
Do not connect 5 V analog signals to the ESP32. Multiple mouse buttons can be
held simultaneously, including while moving the stick (dragging).
MPU6050 and LM2596 are planned hardware; they are not used by this firmware.

## Build, flash and monitor

Use the installed **ESP-IDF PowerShell** terminal in this project folder:

```powershell
idf.py build
idf.py -p COM3 flash monitor
```

Replace COM3 with your actual port. Exit the monitor with `Ctrl+]`. Serial
output uses 115200 baud. Leave the joystick centered during reset/startup:
64 averaged samples calibrate the center in about 0.64 seconds. If calibration
reports an unstable or invalid center, release the stick and reset again.
If flashing stalls on connecting, hold BOOT until writing starts.

VS Code: select the installed setup with
`ESP-IDF: Select Current ESP-IDF Version`, choose COM with
`ESP-IDF: Select Port to Use`, then Build, Flash (UART) and Monitor.

### Configuration

`sdkconfig.defaults` enables Bluetooth, the BLE-only controller, Bluedroid,
BLE 4.2 support, GATT server and the large single-app partition. Classic BT
and GATT client are disabled. Target is `esp32`. No manual menuconfig changes
or external components are needed for a fresh configuration.

Existing `sdkconfig` values override defaults. This workspace has already been
regenerated for BLE; the previous peripheral configuration is saved in
`build/sdkconfig.before-ble`. In another existing checkout, use
`idf.py menuconfig` to set the following, or back up/remove its generated
`sdkconfig` and run `idf.py reconfigure` to load defaults:

```text
CONFIG_BT_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=n
CONFIG_BT_BLE_ENABLED=y
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y
CONFIG_BT_GATTS_ENABLE=y
CONFIG_BT_GATTC_ENABLE=n
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

The application uses GNU C17. IntelliSense uses C17, the Xtensa compiler and
`build/compile_commands.json`, avoiding mismatched C23 `stdbool.h`
interpretation (`true`/`false` shown as undefined). After building, run
**C/C++: Reset IntelliSense Database** or **Developer: Reload Window** if the
editor still shows diagnostics for the removed `hello_world_main.c`.

## Pair with Windows 10/11

1. Flash and leave the stick centered while the device boots.
2. Wait for `Advertising as MKBedPad` in the monitor.
3. In Windows Settings, open **Devices / Bluetooth & devices**.
4. Choose **Add Bluetooth or other device / Add device**, then **Bluetooth**.
5. Select **MKBedPad**. It uses encrypted Just Works bonding without a PIN.
6. Wait for both input notifications and `HID ready: mouse + keyboard`.
7. Test movement, simultaneous buttons and dragging. Click the stick once:
   Win+Ctrl+O toggles the Windows on-screen keyboard. Holding SW does not repeat.

Advertising restarts after disconnection. Windows can reconnect to the saved
bond. Reports wait for encryption and both HID input subscriptions. Offline
shortcut presses are discarded. On reconnect/resume, a zero keyboard report
clears old modifiers, and physical mouse-button state is synchronized. Failed
key releases are retried. At rest in the dead zone, unchanged reports are suppressed.

To pair again, remove MKBedPad in Windows Bluetooth settings, reset the ESP32
and add it again. Do this after changing HID descriptors. If bonding still
fails, run `idf.py -p COM3 erase-flash`, then flash and remove/add MKBedPad in
Windows. Erasing flash deletes bonds and all other NVS data.

## Joystick tuning

Edit `main/board_config.h` and rebuild:

| Constant | Default | Meaning |
| --- | --- | --- |
| `JOYSTICK_DEAD_ZONE` | 220 | ADC counts around calibrated center |
| `JOYSTICK_MAX_STEP` | 15 | Maximum relative movement per frame, 1..127 |
| `JOYSTICK_INVERT_X` / `JOYSTICK_INVERT_Y` | false | Reverse an axis |
| `JOYSTICK_AVERAGE_SAMPLES` | 8 | Samples per read, followed by a 1/4 low-pass filter |
| `JOYSTICK_CALIBRATION_SAMPLES` | 64 | Startup calibration samples |
| `INPUT_PERIOD_MS` | 20 | Joystick/report processing interval |
| `BUTTON_DEBOUNCE_MS` | 30 | Stable level required for an edge |
| `KEYBOARD_HOLD_MS` | 50 | Shortcut press duration before release |

Scaling accounts for unequal travel around the calibrated center. Raw ADC
values are not voltage measurements; ADC nonlinearity and saturation near
3.3 V are expected. Reverse an axis with the corresponding invert constant.
Keep task intervals at least one FreeRTOS tick (10 ms by default).

## Source structure

```text
main/
  app_main.c             NVS and application startup
  board_config.h         Pins and tuning constants
  button_input.c/.h      GPIO IRQs, per-button debounce, ordered event queue
  joystick.c/.h          ADC, calibration, averaging and filtering
  input_math.c/.h        Axis scaling, dead zone, inversion and clamping
  hid_reports.h          Combined mouse/keyboard HID descriptor
  ble_hid.c/.h           BLE/GAP, bonding, subscriptions and HID transport
  input_controller.c/.h  Physical input to mouse/keyboard reports
  CMakeLists.txt
sdkconfig.defaults
tests/
  host_tests.c           Axis boundary tests and official HID parser checks
  host_stubs/            PC logging shims for the IDF parser
  run_host_tests.ps1
```

ISRs only send task notifications. The button task sleeps until an edge and
samples while debouncing. A queue preserves press/release order; one controller
task sends all BLE reports. BLE callbacks protect connection state with short
critical sections and never hold locks during transport calls. Joystick polling
is limited to the configured 50 Hz input rate.

BLE setup/descriptor derive from the installed official
`$IDF_PATH/examples/bluetooth/esp_hid_device` (CC0-1.0 / Unlicense). No external
repository is included. CCC discovery follows the Report Reference / CCC
layout in ESP-IDF 6.1 `components/esp_hid/src/ble_hidd.c`; recheck the adapter
when upgrading IDF. Windows uses report protocol; boot-protocol input is paused.

## Verification

Build with `idf.py build`. Portable host checks require a native GCC compiler,
not the Xtensa compiler:

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_host_tests.ps1 -IdfPath $env:IDF_PATH -Compiler gcc
```

Tests cover joystick boundaries, monotonic movement, dead zone, inversion and
clamping. They also parse the actual descriptor with the installed Espressif
HID parser to verify IDs, usages and report lengths. Host tests do not exercise
Bluetooth radio or Windows drivers. Pairing, reconnecting, simultaneous buttons,
dragging and the shortcut require hardware verification after flashing this version.
