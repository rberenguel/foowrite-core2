#include "sd_storage.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

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

std::vector<std::string> sd_list() {
    std::vector<std::string> files;
    if (!s_mounted) return files;
    DIR* dir = opendir("/sd");
    if (!dir) return files;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.empty() || name[0] == '.') continue;      // skip hidden
        auto dot = name.rfind(".txt");
        if (dot == std::string::npos) continue;            // .txt files only
        files.push_back(name.substr(0, dot));
    }
    closedir(dir);
    return files;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

static std::string cfg_trim(const std::string& s) {
    const char* ws = " \t\r\n";
    size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

FooConfig sd_load_config() {
    FooConfig cfg;
    if (!s_mounted) return cfg;

    FILE* f = fopen("/sd/config.txt", "r");
    if (!f) return cfg;

    char buf[128];
    while (fgets(buf, sizeof(buf), f)) {
        std::string line = cfg_trim(buf);
        if (line.empty() || line[0] == '#') continue;

        // Strip inline comment
        auto comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = cfg_trim(line.substr(0, colon));
        std::string val = cfg_trim(line.substr(colon + 1));
        if (key.empty() || val.empty()) continue;

        if (key == "layout") {
            cfg.qwerty = (val == "qwerty");
        } else if (key == "brightness") {
            int v = atoi(val.c_str());
            cfg.brightness = v < 1 ? 1 : v > 100 ? 100 : v;
        } else if (key == "rotation") {
            cfg.rotation = atoi(val.c_str());
        }
    }
    fclose(f);
    return cfg;
}
