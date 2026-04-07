// Stubs for platform-specific dependencies, allowing editor.cpp + keymap.cpp
// to build and run on the host without ESP-IDF or LovyanGFX.

#include "../main/editor.h"
#include "../main/output.hpp"
#include "../main/keymap.h"
#include "../main/axp192.h"
#include "../main/sd_storage.h"
#include "./layout_map.h"
#include "./editor_stubs.hpp"

// Force QWERTY so layout_map characters round-trip correctly through
// get_char_from_key() — the tests were written expecting QWERTY semantics.
namespace {
struct QwertyInit { QwertyInit() { g_use_qwerty = true; } } qwerty_init;
}

void SendString(Editor* editor, std::string s) {
    auto mods = KeyModifiers{};
    for (const auto& ch : s) {
        std::string letter(1, ch);
        auto it = layout_map.find(letter);
        if (it != layout_map.end()) {
            editor->ProcessKey(it->second, &mods, false);
        }
    }
}

// ---------------------------------------------------------------------------
// Output stubs
// ---------------------------------------------------------------------------

// Returns an ever-increasing value so KEY_ENTER debounce (200 ms window) and
// shift debounce (150 ms window) never fire during tests.
int Output::CurrentTimeInMillis() {
    static int t = 0;
    return t += 1000;
}

void Output::Emit(const std::string&, int, EditorMode) {}
void Output::CommandLine(const std::list<char>&) {}
void Output::CommandLine(const std::string&) {}
void Output::Command(const OutputCommands&) {}
int  Output::NextLine() { return -1; }
int  Output::PrevLine() { return -1; }
void Output::ProcessEvent(EventType) {}
void Output::ProcessHandlers() {}
void Output::Init(Editor*) {}

// ---------------------------------------------------------------------------
// AXP192 stubs
// ---------------------------------------------------------------------------
void axp192_init(i2c_master_bus_handle_t*) {}
void axp192_set_lcd_backlight(uint8_t) {}
void axp192_set_exten(bool) {}
int  axp192_get_battery_pct() { return 50; }

// ---------------------------------------------------------------------------
// SD storage stubs
// ---------------------------------------------------------------------------
bool sd_init() { return true; }
bool sd_save(const char*, const std::list<std::string>&, std::string&) { return true; }
bool sd_load(const char*, std::list<std::string>&, std::string&) { return false; }
