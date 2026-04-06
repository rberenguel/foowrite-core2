// Copyright 2025 Ruben Berenguel

// Stub Output implementation — printf to serial.
// Replace with display.cpp (LovyanGFX renderer) in step 4.

#include "output.hpp"

#include <stdio.h>
#include <string>
#include <list>

#include "esp_timer.h"
#include "editor_mode.h"

int Output::CurrentTimeInMillis() {
    return static_cast<int>(esp_timer_get_time() / 1000);
}

// Return -1 = no pagination override; display layer will set these when
// word-wrapped text spans multiple screen pages.
int Output::NextLine() { return -1; }
int Output::PrevLine() { return -1; }

void Output::Init(Editor* ed) {
    editor_ = ed;
    printf("[output] init\n");
}

void Output::Emit(const std::string& s, int cursor_pos, EditorMode mode) {
    const char* mode_str = "N";
    if (mode == EditorMode::kInsert)          mode_str = "I";
    if (mode == EditorMode::kCommandLineMode) mode_str = "L";
    printf("[%s] col=%d | %s\n", mode_str, cursor_pos, s.c_str());
}

void Output::CommandLine(const std::list<char>& s) {
    std::string str(s.begin(), s.end());
    printf("[cmd] %s\n", str.c_str());
}

void Output::CommandLine(const std::string& s) {
    printf("[cmd] %s\n", s.c_str());
}

void Output::Command(const OutputCommands& cmd) {
    switch (cmd) {
        case OutputCommands::kFlush:       printf("[cmd] flush\n");   break;
        case OutputCommands::kSplash:      printf("[cmd] splash\n");  break;
        case OutputCommands::kCommandMode: printf("[cmd] normal\n");  break;
    }
}

void Output::ProcessHandlers() {
    // No physical buttons yet; capacitive touch wired here later.
}

void Output::ProcessEvent(EventType ev) {
    switch (ev) {
        case EV_BT_ON:  printf("[output] BT connected\n");    break;
        case EV_BT_OFF: printf("[output] BT disconnected\n"); break;
        case EV_SAVE:   break;
    }
}
