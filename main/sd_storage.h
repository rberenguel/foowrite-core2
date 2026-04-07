#pragma once
#include <list>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Runtime configuration loaded from /sd/config.txt
// ---------------------------------------------------------------------------

struct FooConfig {
    bool qwerty     = false; // false = Colemak (default)
    int  brightness = 50;    // 1-100; 0 is treated as 1 to keep backlight on
    int  rotation   = 1;     // 1 = normal, -1 = flipped / upside-down
};

// Parse /sd/config.txt (key: value  # comment format).
// Returns struct with defaults if the file is absent or SD is not mounted.
FooConfig sd_load_config();

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

// Returns filenames (without .txt extension) for all non-hidden .txt files
// in /sd/.  Returns empty vector if SD is not mounted or directory is empty.
std::vector<std::string> sd_list();
