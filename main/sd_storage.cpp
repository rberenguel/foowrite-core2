#include "sd_storage.h"

#include <stdio.h>
#include <string.h>

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

bool sd_save(const char* filename,
             const std::list<std::string>& document,
             std::string& err_msg) {
    if (!s_mounted) { err_msg = "no sd"; return false; }

    char path[128];
    snprintf(path, sizeof(path), "/sd/%.100s.txt", filename);

    FILE* f = fopen(path, "w");
    if (!f) { err_msg = "open failed"; return false; }

    for (const auto& line : document) {
        fputs(line.c_str(), f);
        fputc('\n', f);
    }
    fclose(f);
    return true;
}

bool sd_load(const char* filename,
             std::list<std::string>& document,
             std::string& err_msg) {
    if (!s_mounted) { err_msg = "no sd"; return false; }

    char path[128];
    snprintf(path, sizeof(path), "/sd/%.100s.txt", filename);

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
