#pragma once

#include <stdint.h>

// AXS15231B QSPI display driver — ESP-IDF native, no Arduino.
// Ported from rsvpnano (ionutdecebal/rsvpnano), Apache-2.0.
//
// Hardware wiring (Waveshare ESP32-S3-Touch-LCD-3.49):
//   CS=9, CLK=10, D0=11, D1=12, D2=13, D3=14, RST=21
//   SPI3_HOST, mode 3, 40 MHz QSPI

void axs15231b_init(void);

// Push RGB565 pixels to the panel. data must be contiguous (width*height pixels)
// in big-endian RGB565 byte order (as expected by the panel over SPI).
void axs15231b_push_colors(uint16_t x, uint16_t y,
                           uint16_t width, uint16_t height,
                           const uint16_t *data);
