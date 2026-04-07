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
