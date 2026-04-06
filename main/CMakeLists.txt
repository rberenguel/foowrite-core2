idf_component_register(
    SRCS
        "main.cpp"
        "axp192.cpp"
        "ble_hid_host.cpp"
        "splash.cpp"
        "keymap.cpp"
        "output.cpp"
        "editor.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_hw_support
        esp_driver_i2c
        bt
        LovyanGFX
)
