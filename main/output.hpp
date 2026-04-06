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
    void Init(Editor*);
    void ProcessHandlers();
    void ProcessEvent(EventType ev);

 private:
    int   prev_line_start_ = -1;
    int   next_line_start_ = -1;
    Editor* editor_ = nullptr;
};
