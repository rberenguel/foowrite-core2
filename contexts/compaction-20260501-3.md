# Session Compaction Summary

## User Intent
- Fix build errors in the LovyanGFX QSPI panel path (Panel_AXS15231B / lgfx_config_waveshare349)
- Debug blank display after successful build and flash
- Result: display still not working; approach remains unresolved

## Contextual Work Summary

### Build Fixes Applied
- `lgfx_config_waveshare349.h`: `Panel_AXS15231B` → `lgfx::Panel_AXS15231B` (namespace); `pin_d2`/`pin_d3` → `pin_io0..3` (Bus_SPI QSPI field names)
- `Panel_AXS15231B.cpp`: added `<lgfx/v1/Bus.hpp>` to resolve `IBus` incomplete type error in `setRotation`
- `board_waveshare349.cpp`: `i2c_master_bus_create` → `i2c_new_master_bus` (correct ESP-IDF v5 API)
- `CMakeLists.txt`: removed `LGFX_USE_QSPI` from `add_compile_definitions` — LovyanGFX's `esp32/common.hpp` defines it unconditionally, causing redefinition warning

### Init Sequence Fix (did not solve blank screen)
- Removed spurious second `0x11` (SLPOUT), `0x2C` RAMWR priming, and `0x22` ALL_PIXEL_OFF from `s_init_cmds[]` in `Panel_AXS15231B.cpp`
- `0x22` was the obvious bug (actively blanks all pixels after DISPON) but removing it still produced no output

### Diagnostics Added
- `board_waveshare349.cpp`: I2C probe loop (0x20–0x27) to find actual TCA9554 address before connecting — two `i2c_master_transmit` failures observed at 0x20 in logs
- `main.cpp`: `board_set_backlight(200)` added immediately after `board_init()` so splash is lit before BLE connects (user reverted this along with other main.cpp changes)

### Reference Implementation Found
- `rsvpnano` (ionutdecebal/rsvpnano) confirmed working on same hardware
- Uses raw ESP-IDF `spi_master.h`, NOT LovyanGFX
- Init is 5 commands only: `0x11`, `0x36`(0x00), `0x3A`(0x55), `0x11` again, `0x29` — no vendor unlock/gamma sequence at all
- SPI configured with `command_bits=8`, `address_bits=24`, mode 3, `SPI_DEVICE_HALFDUPLEX`, `SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR`
- Pixel push uses `SPI_TRANS_MODE_QIO` with cmd=0x32, addr=0x002C00

### State After Session
- User reverted `main.cpp` and `CMakeLists.txt` to Core2-only state (axp192 calls, no board abstraction)
- Build/flash errors are fixed but LovyanGFX Panel_SH8601Z subclass approach produces no pixels
- Conclusion aligns with kimi session: LovyanGFX QSPI path is opaque and unreliable for this panel

## Files Touched

### Modified (some reverted by user)
- **main/lgfx_config_waveshare349.h**: namespace + QSPI pin name fixes (kept)
- **main/Panel_AXS15231B.cpp**: Bus.hpp include, init sequence trimmed (kept)
- **main/board_waveshare349.cpp**: i2c API fix, I2C probe scan added (kept)
- **main/CMakeLists.txt**: removed LGFX_USE_QSPI define (user reverted to no-board-select form)
- **main/main.cpp**: backlight turn-on added (user reverted to axp192 version)

## Next Steps (per kimi compaction-20260501-2.md)
1. Use `espressif/esp_lcd_axs15231b` official driver via `idf_component.yml`
2. Render into LovyanGFX sprite (software only), push via `esp_lcd_panel_draw_bitmap`
3. Do not touch TCA9554 (factory state appears sufficient)
4. Keep "PWR" button (actual BOOT/GPIO0) accessible for recovery
