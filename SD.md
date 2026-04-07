# SD Card File Storage — Implementation Plan

## Hardware

The Core2 SD slot shares SPI3_HOST (VSPI) with the display:

| Signal | GPIO |
|--------|------|
| MOSI   | 23   |
| MISO   | 38   |
| CLK    | 18   |
| SD CS  | 4    |
| LCD CS | 5    |

`lgfx_config.h` already has `bus_shared = true`, which makes LovyanGFX acquire/release
the SPI bus per-transaction rather than holding it indefinitely.  The ESP-IDF SPI driver
handles arbitration between devices on the same host via `spi_device_acquire_bus`.

---

## 1. CMakeLists.txt

Add `fatfs` and `sdmmc` to the component REQUIRES:

```cmake
REQUIRES
    driver
    esp_timer
    esp_hw_support
    esp_driver_i2c
    bt
    LovyanGFX
    fatfs        # ← new
    sdmmc        # ← new
```

---

## 2. `main/sd_storage.h`

```cpp
#pragma once
#include <list>
#include <string>

// Mount the SD card over SPI (SPI3_HOST, CS=GPIO4).
// Call once after display init.  Returns true on success.
bool sd_init();

// Save document lines to /sd/<filename>.txt.
// Returns true on success; sets err_msg on failure.
bool sd_save(const char* filename,
             const std::list<std::string>& document,
             std::string& err_msg);

// Load /sd/<filename>.txt into document (replaces existing content).
// Returns true on success; sets err_msg on failure.
bool sd_load(const char* filename,
             std::list<std::string>& document,
             std::string& err_msg);
```

---

## 3. `main/sd_storage.cpp`

### Mount

```cpp
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"

static sdmmc_card_t* s_card = nullptr;
static bool          s_mounted = false;

bool sd_init() {
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;  // shared with display

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI3_HOST;
    slot.gpio_cs = GPIO_NUM_4;

    esp_err_t err = esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &cfg, &s_card);
    s_mounted = (err == ESP_OK);
    return s_mounted;
}
```

**Note**: `esp_vfs_fat_sdspi_mount` registers the SPI device on SPI3_HOST.
Because LovyanGFX already initialised SPI3_HOST, the SPI bus must be
initialised before calling `sd_init()`.  Call `sd_init()` after `display.init()`
in `app_main`.

### Save

```cpp
bool sd_save(const char* filename,
             const std::list<std::string>& document,
             std::string& err_msg) {
    if (!s_mounted) { err_msg = "no sd"; return false; }

    char path[64];
    snprintf(path, sizeof(path), "/sd/%.50s.txt", filename);

    FILE* f = fopen(path, "w");
    if (!f) { err_msg = "open failed"; return false; }

    for (const auto& line : document) {
        fputs(line.c_str(), f);
        fputc('\n', f);
    }
    fclose(f);
    return true;
}
```

### Load

```cpp
bool sd_load(const char* filename,
             std::list<std::string>& document,
             std::string& err_msg) {
    if (!s_mounted) { err_msg = "no sd"; return false; }

    char path[64];
    snprintf(path, sizeof(path), "/sd/%.50s.txt", filename);

    FILE* f = fopen(path, "r");
    if (!f) { err_msg = "not found"; return false; }

    document.clear();
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        std::string line(buf);
        // Strip trailing newline
        if (!line.empty() && line.back() == '\n') line.pop_back();
        document.push_back(std::move(line));
    }
    fclose(f);

    if (document.empty()) document.push_back("");
    return true;
}
```

---

## 4. Editor integration (`editor.cpp`)

Add `filename_` member to `Editor` (in `editor.h`):

```cpp
std::string filename_ = "untitled";
```

Update `DispatchCommand` / the `:w` and `:e` handlers:

```cpp
// :w  — save (uses stored filename_)
// :w filename  — save as; updates filename_
// :e filename  — load; updates filename_

if (command.rfind(":w", 0) == 0) {
    auto arg = command.substr(2);
    // trim leading space
    while (!arg.empty() && arg[0] == ' ') arg.erase(0, 1);
    if (!arg.empty()) filename_ = arg;

    std::string err;
    if (sd_save(filename_.c_str(), document_, err)) {
        output_->CommandLine("saved: " + filename_);
    } else {
        output_->CommandLine("error: " + err);
    }
}

if (command.rfind(":e ", 0) == 0) {
    auto arg = command.substr(3);
    std::string err;
    if (sd_load(arg.c_str(), document_, err)) {
        filename_ = arg;
        row_ = document_.begin();
        current_line_ = *row_;
        ncolumn_ = 0;
        output_->Emit(current_line_, ncolumn_, mode_);
    } else {
        output_->CommandLine("error: " + err);
    }
}
```

Include `sd_storage.h` at the top of `editor.cpp`.

---

## 5. `main.cpp` changes

Call `sd_init()` after `display.init()`:

```cpp
// 2. Display
display.init();
display.setRotation(1);

// 2b. SD card (SPI3_HOST shared with display; must init after display)
if (!sd_init()) {
    ESP_LOGW(TAG, "SD card not found or mount failed");
}
```

---

## 6. SPI bus sharing notes

- LovyanGFX's `bus_shared = true` uses `spi_device_acquire_bus` per drawing
  transaction.  `esp_vfs_fat_sdspi_mount` registers the SD as a second SPI
  device on the same host — the driver serialises access automatically.
- The only risk is if a long display `startWrite()`/`endWrite()` block overlaps
  with a file operation.  Currently all display writes are short (one Emit call
  ≈ one `startWrite`/`endWrite` pair), so this is safe.
- If you later add background tasks that write to the display, protect them with
  a FreeRTOS mutex shared with the SD access paths.

---

## 7. File naming

- Files are stored as `/sd/<name>.txt`; the `.txt` extension is appended
  automatically.
- Max filename length: 50 chars (enforced by `snprintf` format string).
- FAT32 constraint: 8.3 names unless LFN is enabled in sdkconfig
  (`CONFIG_FATFS_LFN_HEAP=y` or `CONFIG_FATFS_LFN_STACK=y`).
  Enable LFN to allow longer names.

---

## 8. Implementation order

1. Add `fatfs` + `sdmmc` to CMakeLists
2. Write `sd_storage.h` / `sd_storage.cpp`
3. Call `sd_init()` in `main.cpp`
4. Add `filename_` to `editor.h`
5. Wire `:w` / `:e` in `editor.cpp`
6. Test: `:w test` → power off → `:e test`
