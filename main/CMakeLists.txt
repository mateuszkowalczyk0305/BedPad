idf_component_register(SRCS "app_main.c" "button_input.c" "joystick.c"
                            "input_math.c" "ble_hid.c" "input_controller.c"
                       PRIV_REQUIRES esp_driver_gpio esp_adc esp_hid bt nvs_flash log freertos
                       INCLUDE_DIRS "")

# ESP-IDF 6.1 defaults to C23; use C17 for this component and editor compatibility.
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu17)
