# Board selection: set FOOWRITE_BOARD=waveshare349 for the Waveshare build.
# Default (no flag) → Core2.
if(DEFINED FOOWRITE_BOARD AND FOOWRITE_BOARD STREQUAL "waveshare349")
    set(BOARD_SRCS
        "board_waveshare349.cpp"
        "axs15231b.cpp"
        "display_context_waveshare349.cpp"
    )
    set(BOARD_DEFINES FOOWRITE_BOARD_WAVESHARE349)
else()
    set(BOARD_SRCS
        "board_core2.cpp"
        "axp192.cpp"
        "display_context_core2.cpp"
    )
    set(BOARD_DEFINES)
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "ble_hid_host.cpp"
        "splash.cpp"
        "keymap.cpp"
        "output.cpp"
        "editor.cpp"
        "sd_storage.cpp"
        ${BOARD_SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_adc
        esp_timer
        esp_hw_support
        esp_driver_i2c
        bt
        LovyanGFX
        fatfs
        sdmmc
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${BOARD_DEFINES})
