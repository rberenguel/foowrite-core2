#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Unified board API — power management, backlight, battery.
// Implemented by board_core2.cpp or board_waveshare349.cpp.

void  board_init(void);

// brightness 0-255; 0 = off
void  board_set_backlight(uint8_t brightness);

bool  board_is_charging(void);

// Raw battery voltage in millivolts
float board_read_voltage_mv(void);

// Caller sets the calibration ceiling (loaded from SD)
void  board_set_bat_max_mv(float max_mv);

// 0-100, or -1 if unknown
int   board_get_battery_pct(void);

void  board_shutdown(void);

#ifdef __cplusplus
}
#endif
