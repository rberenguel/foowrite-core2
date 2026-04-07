// Copyright 2025 Ruben Berenguel

#pragma once

#include <list>
#include <string>

#include "output.hpp"
#include "keymap.h"
#include "editor_mode.h"

class Editor {
 public:
    Editor() : mode_(EditorMode::kNormal) {}

    // Initialise with a display output; call once before ProcessKey.
    void Init(Output* out);

    void ProcessKey(const uint8_t key, KeyModifiers* modifiers, bool batched);
    void ProcessHandlers();
    void ProcessEvent(EventType ev);
    void ProcessSaving();
    void Refresh();

    // Accessors used by tests
    std::string GetCurrentLine();
    std::string GetDocument();
    int         CountLines();

    void ResetState() {
        current_line_.clear();
        ncolumn_ = 0;
        row_ = document_.end();
        mode_ = EditorMode::kNormal;
    }

 private:
    std::list<std::string>::iterator row_;
    int ncolumn_ = 0;
    int nrow_    = 0;
    int previously_shifted_ = -1;
    int previous_key_       = -1;

    Output* output_ = nullptr;

    void HandleEsc();
    auto InsertOrReplaceLine(std::string new_line,
                             std::list<std::string>::iterator row);
    void UpdateCurrentLine(int direction);
    void WordCount(int* wc, int* cc);
    void HandleEnter();
    void ProcessCommand(char c, KeyModifiers* modifiers);
    void DispatchCommand(std::string command);

    std::string       current_line_;
    std::list<char>   command_line_;
    std::list<std::string> document_;
    EditorMode        mode_;
    bool              should_save_ = false;
    std::string       filename_ = "untitled";
};
