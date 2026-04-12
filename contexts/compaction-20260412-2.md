# Session Compaction Summary

## User Intent
- Fix battery charging indicator (green when charging) — had been broken for 3 prior attempts
- Improve battery percentage accuracy using coulomb counter instead of voltage-only
- Investigate and reduce battery drain (1-2h runtime, suspiciously low)

## Contextual Work Summary

### Charging Detection Fix
- Previous attempts used wrong registers (reg `0x01` bit 6, then reg `0x00` bit 5)
- Fixed by checking M5Core2 source (not M5StickC): correct register is `0x00` bit 2 (`0x04`)
- `axp192_is_charging()` now matches M5Core2's own `isCharging()` implementation

### Battery Percentage — Coulomb Counter
- Enabled coulomb counter at init (reg `0xB8 = 0x80`)
- Added `axp_read32()` helper for 32-bit register reads
- `axp192_get_battery_pct()` anchors to a voltage-based estimate at first call, then tracks delta via coulomb counter (charge reg `0xB0`, discharge reg `0xB4`)
- Added `axp192_read_voltage_mv()` and `axp192_set_bat_max_mv()` to public API
- Max voltage ever seen (= 100% reference) persisted to SD via `sd_storage`

### Battery Calibration Persistence
- Added `sd_load_bat_max_mv()` / `sd_save_bat_max_mv()` to `sd_storage.cpp/h`
- File stored as `/sd/.bat_max_mv` (hidden, not shown in `:e` listing)
- `main.cpp` loads max after `sd_init()`, passes to axp192 via `axp192_set_bat_max_mv()`
- Max updated only when not charging (charger inflates voltage)
- To reset calibration: delete `/sd/.bat_max_mv`, charge fully while off, boot on battery

### Power Management
- CPU reduced from 160MHz to 80MHz (sufficient for BLE HID + text rendering)
- `FREERTOS_HZ` reduced from 1000 to 100 (1ms tick was preventing light sleep)
- `CONFIG_PM_ENABLE=y` + `CONFIG_BT_NIMBLE_SLEEP_ENABLE=y` added to `sdkconfig.defaults`
- `esp_pm_configure()` called at startup with `light_sleep_enable=true`
- CPU now sleeps during the 50ms `xQueueReceive` idle blocks (~99% of runtime)

### Unknown Percentage Display
- `bat_pct = -1` no longer triggers red colour (threshold check now guards `bat_pct >= 0`)
- `?%` shown when no reading available (plugged in before any battery-only boot)
- Both `output.cpp` and `splash.cpp` updated consistently

## Files Touched

### Core Logic
- **main/axp192.cpp**: Charging fix (reg 0x00 bit 2); coulomb counter enable + read; `axp192_read_voltage_mv()`; `axp192_set_bat_max_mv()`; `axp192_get_battery_pct()` with coulomb delta; all SD/file I/O removed
- **main/axp192.h**: New declarations: `axp192_read_voltage_mv`, `axp192_set_bat_max_mv`, updated `axp192_get_battery_pct` doc
- **main/main.cpp**: `esp_pm_configure()` at startup; battery calibration block after `sd_init()`; debug logging for cal block; `esp_pm.h` include
- **main/sd_storage.cpp**: `sd_load_bat_max_mv()` / `sd_save_bat_max_mv()` added
- **main/sd_storage.h**: Declarations for the two new battery cal functions

### Display
- **main/output.cpp**: `bat_pct >= 0` guard on red colour threshold; `?%` format for unknown
- **main/splash.cpp**: Same colour guard fix; `?%` format for unknown

### Config
- **sdkconfig.defaults**: CPU 80MHz, `PM_ENABLE`, `BT_NIMBLE_SLEEP_ENABLE`, `FREERTOS_HZ=100`

## Outstanding Notes
- Calibration requires one full boot on battery after a full charge to establish max_mv
- `charging=1` always reported when USB is connected (correct; cannot update max while plugged)
- `sdkconfig` must be deleted and rebuilt to pick up new `sdkconfig.defaults` changes
