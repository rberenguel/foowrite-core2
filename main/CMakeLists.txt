idf_component_register(
    SRCS
        "main.cpp"
        "axp192.cpp"
        "ble_hid_host.cpp"
        "keymap.cpp"
        "output.cpp"
        "editor.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_driver_i2c
        bt
        LovyanGFX
)
