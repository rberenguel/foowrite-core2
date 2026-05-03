// Copyright 2025 Ruben Berenguel

// API for an "output" — some sort of screen/display.
// The reference implementation will be in display.cpp (LovyanGFX / ILI9342C).
// Until then, output.cpp provides a printf-based stub.

#pragma once

#include <list>
#include <string>

#include "editor_mode.h"
#include "keymap.h"

class Editor;  // Forward declaration to avoid circular dependency

enum class OutputCommands { kFlush, kSplash, kCommandMode };

class Output {
 public:
    int  CurrentTimeInMillis();
    int  NextLine();
    int  PrevLine();
    void Emit(const std::string& s, int cursor_pos, EditorMode mode);
    void CommandLine(const std::list<char>& s);
    void CommandLine(const std::string& s);
    void Command(const OutputCommands& command);
    void SetRotation(int rot);
    void Init(Editor*);
    void ProcessHandlers();
    void ProcessEvent(EventType ev);

 private:
    int   prev_line_start_ = -1;
    int   next_line_start_ = -1;
    Editor* editor_ = nullptr;

    // Cached for draw_status calls that happen outside Emit (e.g. ESC to Normal)
    std::string status_filename_;
    bool        status_dirty_ = false;

    // Previous status state — used to skip status redraw when nothing changed
    EditorMode  last_mode_ = EditorMode::kNormal;
    std::string last_filename_;
    bool        last_dirty_ = false;
};
