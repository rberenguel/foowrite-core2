#pragma once

// LovyanGFX configuration for M5Stack Core2
// ILI9342C, 320x240, SPI (VSPI: MOSI=23, MISO=38, CLK=18, CS=5, DC=15)
// Backlight driven by AXP192 DCDC3 — no GPIO backlight pin.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    // Note: must NOT name these _panel/_bus — those names are taken by base class members.
    lgfx::Panel_ILI9342 _panel_instance;
    lgfx::Bus_SPI        _bus_instance;

public:
    LGFX() {
        // SPI bus (VSPI = SPI3_HOST on ESP32)
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host    = SPI3_HOST;   // VSPI
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 18;
            cfg.pin_mosi    = 23;
            cfg.pin_miso    = 38;
            cfg.pin_dc      = 15;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // Panel
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs          = 5;
            cfg.pin_rst         = -1;   // Reset handled by AXP192 GPIO4
            cfg.pin_busy        = -1;
            cfg.panel_width     = 320;
            cfg.panel_height    = 240;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 3;    // Core2 physical orientation
            cfg.readable        = true;
            cfg.invert          = true; // ILI9342C on Core2 requires inversion
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = true; // SD card shares SPI bus later
            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
    }
};
