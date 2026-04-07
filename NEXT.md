# Next steps

---

## ✅ Config file (`/sd/config.txt`) — done

Properties `layout: colemak|qwerty` and `brightness: 1-100`.  Applied at startup
and re-applied on every BLE reconnect (so `:q` + reconnect picks up edits).
Version `0.1.0` is shown on the splash screen below the title in the small bitmap font.

---

## 1. `:e` with no argument → show file listing

When the user types `:e` and presses Enter without a filename, show all non-hidden
`.txt` files on the SD card so they know what to open.

### sd_storage.h — add

```cpp
// Returns filenames (without .txt extension) for all non-hidden .txt files
// in /sd/.  Returns empty vector if SD is not mounted or directory is empty.
std::vector<std::string> sd_list();
```

### sd_storage.cpp — add

```cpp
#include <dirent.h>

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
```

### editor.cpp — update `:e` handler

Replace the current `command.rfind(":e ", 0) == 0` block with two cases:

```cpp
// :e <filename> — load file
if (command.rfind(":e ", 0) == 0) {
    auto arg = command.substr(3);
    while (!arg.empty() && arg[0] == ' ') arg.erase(0, 1);
    std::string err;
    if (sd_load(arg.c_str(), document_, err)) {
        filename_ = arg;
        row_ = document_.begin();
        current_line_ = *row_;
        ncolumn_ = 0;
        mode_ = EditorMode::kNormal;
        output_->Emit(current_line_, ncolumn_, mode_);
        output_->CommandLine("loaded: " + filename_);
    } else {
        output_->CommandLine("error: " + err);
    }
}

// :e alone — show directory listing
if (command == ":e") {
    auto files = sd_list();
    if (files.empty()) {
        output_->CommandLine(": (no files on sd)");
    } else {
        std::string listing = ":";
        for (const auto& f : files) listing += " " + f;
        output_->CommandLine(listing);
    }
    // Stay in command-line mode so user can type :e <filename>
    mode_ = EditorMode::kCommandLineMode;
    std::list<char> prompt = {':', 'e', ' '};
    command_line_ = prompt;
    output_->CommandLine(command_line_);
}
```

The listing is shown in the status bar (`CommandLine`), then the cursor drops into a
fresh `:e ` prompt so the user can immediately type the filename.

---

## 2. Config file on startup

Read `/sd/config.txt` after `sd_init()` and apply `layout` and `brightness` settings.
Re-read on every BLE reconnect so that `:q` → reconnect picks up any edits.

### File format

```
# foowrite config
layout: colemak     # can also be qwerty
brightness: 50      # 0-100  (never actually set to 0)
```

Rules: lines starting with `#` or blank are ignored.  Each property line is
`key: value` optionally followed by `# comment`.  Leading/trailing whitespace
around both key and value is stripped.

### sd_storage.h — add

```cpp
struct FooConfig {
    bool qwerty     = false;  // false = Colemak (default)
    int  brightness = 50;     // 0-100
};

// Load /sd/config.txt and return parsed settings.
// Returns defaults if the file does not exist or the SD is not mounted.
FooConfig sd_load_config();
```

### sd_storage.cpp — add

```cpp
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

FooConfig sd_load_config() {
    FooConfig cfg;
    if (!s_mounted) return cfg;

    FILE* f = fopen("/sd/config.txt", "r");
    if (!f) return cfg;

    char buf[128];
    while (fgets(buf, sizeof(buf), f)) {
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#') continue;

        // Strip inline comment
        auto comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon + 1));

        if (key == "layout") {
            cfg.qwerty = (val == "qwerty");
        } else if (key == "brightness") {
            int v = atoi(val.c_str());
            cfg.brightness = v < 1 ? 1 : v > 100 ? 100 : v;
        }
    }
    fclose(f);
    return cfg;
}
```

### main.cpp — apply config at startup and on reconnect

```cpp
static void apply_config() {
    FooConfig cfg = sd_load_config();
    g_use_qwerty = cfg.qwerty;
    axp192_set_lcd_backlight((cfg.brightness * 255) / 100);
}
```

Call `apply_config()` once after `sd_init()`, and again inside
`draw_connected_screen()` (which runs on every BLE connect event, including after
`:q` + reconnect):

```cpp
static void draw_connected_screen() {
    apply_config();          // ← re-read config on every connect
    draw_bt_icon(&display, TFT_GREEN);
    vTaskDelay(pdMS_TO_TICKS(800));
    display.fillScreen(TFT_BLACK);
    g_editor.Refresh();
}
```

### Autodocumented default config

Ship a `/sd/config.txt` with this content so users have a template:

```
# foowrite config — edit and reconnect to apply

layout: colemak     # can also be: qwerty
brightness: 50      # 0-100 (0 is treated as 1 to keep backlight on)
```

---

## Future (not blocking usability)

WiFi / WPA for wireless document transfer would be convenient (no SD swapping), but
the WPA supplicant stack is large enough to eat meaningfully into PSRAM headroom.
Not worth pursuing until the document editing experience is solid.
