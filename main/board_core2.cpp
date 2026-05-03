#include "board.h"
#include "axp192.h"

void  board_init()                   { axp192_init(nullptr); }
void  board_set_backlight(uint8_t b) { axp192_set_lcd_backlight(b); }
bool  board_is_charging()            { return axp192_is_charging(); }
float board_read_voltage_mv()        { return axp192_read_voltage_mv(); }
void  board_set_bat_max_mv(float v)  { axp192_set_bat_max_mv(v); }
int   board_get_battery_pct()        { return axp192_get_battery_pct(); }
void  board_shutdown()               { axp192_shutdown(); }
