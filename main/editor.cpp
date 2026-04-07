// Copyright 2025 Ruben Berenguel

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>

#include <list>
#include <string>

#include "editor.h"
#include "editor_mode.h"
#include "keymap.h"
#include "output.hpp"
#include "axp192.h"
#include "sd_storage.h"

[[maybe_unused]] static std::string ModeString(EditorMode mode) {
    switch (mode) {
        case EditorMode::kNormal:          return "N";
        case EditorMode::kInsert:          return "I";
        case EditorMode::kCommandLineMode: return "L";
        default:                           return "?";
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void Editor::Init(Output* out) {
    output_ = out;
    row_ = document_.begin();
    output_->Init(this);
}

// ---------------------------------------------------------------------------
// Document helpers
// ---------------------------------------------------------------------------

auto Editor::InsertOrReplaceLine(std::string new_line,
                                 std::list<std::string>::iterator row) {
    if (row == document_.end()) {
        document_.push_back(new_line);
        return --document_.end();
    } else {
        *row = new_line;
        return row;
    }
}

void Editor::UpdateCurrentLine(int direction) {
    auto at_end   = row_ == document_.end();
    auto at_begin = row_ == document_.begin();
    row_ = InsertOrReplaceLine(current_line_, row_);
    if (at_end && !at_begin) {
        --row_;
    }
    if (direction == -1 && row_ != document_.begin()) {
        --row_;
    }
    if (!at_end && direction == 1 && row_ != document_.end()) {
        ++row_;
        if (row_ == document_.end()) {
            --row_;
        }
    }
    current_line_ = *row_;
    ncolumn_ = fmin(ncolumn_, (int)current_line_.size());
}

void Editor::HandleEnter() {
    std::string remaining = current_line_.substr(0, ncolumn_);
    current_line_ = current_line_.substr(ncolumn_);
    row_ = InsertOrReplaceLine(remaining, row_);
    row_ = document_.insert(++row_, "");
    ncolumn_ = 0;
}

// ---------------------------------------------------------------------------
// Public accessors (used by tests)
// ---------------------------------------------------------------------------

std::string Editor::GetCurrentLine() { return current_line_; }
int         Editor::CountLines()     { return (int)document_.size(); }

std::vector<std::string> Editor::GetFollowingLines(int n) const {
    std::vector<std::string> result;
    if (row_ == document_.end()) return result;
    auto it = row_;
    ++it;
    for (int i = 0; i < n && it != document_.end(); ++i, ++it) {
        result.push_back(*it);
    }
    return result;
}

std::string Editor::GetDocument() {
    std::string full;
    for (const auto& line : document_) {
        full += line + "\n";
    }
    return full;
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

void Editor::ProcessSaving() {
    // SD card save — implemented in step 6.
    // should_save_ is set by :w; check it here and write /sd/<file>.
    should_save_ = false;
    printf("[editor] save not yet implemented\n");
}

// ---------------------------------------------------------------------------
// Event / handler pass-through
// ---------------------------------------------------------------------------

void Editor::ProcessHandlers() {
    output_->ProcessHandlers();
}

void Editor::ProcessEvent(EventType ev) {
    switch (ev) {
        case EV_BT_OFF: output_->ProcessEvent(EV_BT_OFF); break;
        case EV_BT_ON:  output_->ProcessEvent(EV_BT_ON);  break;
        case EV_SAVE:   ProcessSaving(); break;
    }
}

void Editor::Refresh() {
    output_->Emit(current_line_, ncolumn_, mode_);
}

// ---------------------------------------------------------------------------
// Command mode
// ---------------------------------------------------------------------------

void Editor::DispatchCommand(std::string command) {
    command_line_.clear();
    auto dummy = KeyModifiers{};
    for (const auto& c : command) {
        ProcessCommand(c, &dummy);
    }
}

void Editor::ProcessCommand(char c, KeyModifiers* modifiers) {
    auto emits = true;
    std::string command_line_str(command_line_.begin(), command_line_.end());

    switch (c) {
        case '\0':
            break;

        case ':':
            mode_ = EditorMode::kCommandLineMode;
            command_line_.emplace_back(':');
            output_->CommandLine(command_line_);
            break;

        case 'b':
            command_line_.clear();
            {
                ncolumn_ = fmax(--ncolumn_, 0);
                ncolumn_ = fmax(--ncolumn_, 0);
                bool advance = (current_line_[ncolumn_] != ' ');
                while (ncolumn_ >= 0 && current_line_[ncolumn_] != ' ')
                    --ncolumn_;
                if (advance) {
                    if (ncolumn_ <= (int)current_line_.size() - 1) {
                        ++ncolumn_;
                        if (ncolumn_ <= (int)current_line_.size() - 1)
                            ++ncolumn_;
                    }
                }
            }
            break;

        case 'w':
            if (command_line_str == "da") {
                command_line_.clear();
                int start = ncolumn_, end = ncolumn_;
                while (start > 0 && current_line_[start] != ' ') --start;
                while (end <= (int)current_line_.size() - 1 && current_line_[end] != ' ') ++end;
                current_line_.erase(start, end - start);
                ncolumn_ = fmin(ncolumn_, (int)current_line_.size());
                break;
            }
            if (command_line_str == "di") {
                command_line_.clear();
                int start = ncolumn_, end = ncolumn_;
                while (start > 0 && current_line_[start] != ' ') --start;
                if (start < (int)current_line_.size() - 1) ++start;
                while (end <= (int)current_line_.size() - 1 && current_line_[end] != ' ') ++end;
                current_line_.erase(start, end - start);
                break;
            }
            if (command_line_str == "ca") {
                if (ncolumn_ == (int)current_line_.size())
                    DispatchCommand("dawa");
                else
                    DispatchCommand("dawi");
                break;
            }
            if (command_line_str == "ci") {
                if (ncolumn_ == (int)current_line_.size())
                    DispatchCommand("diwa");
                else
                    DispatchCommand("diwi");
                break;
            }
            command_line_.clear();
            while (ncolumn_ <= (int)current_line_.size() - 1 && current_line_[ncolumn_] != ' ')
                ++ncolumn_;
            if (ncolumn_ <= (int)current_line_.size() - 1) {
                ++ncolumn_;
                if (ncolumn_ <= (int)current_line_.size() - 1)
                    ++ncolumn_;
            } else {
                ncolumn_ = current_line_.size();
            }
            break;

        case 'c':
            if (command_line_str == "c") {
                // TODO: change full line
            } else {
                command_line_.emplace_back('c');
            }
            break;

        case 'd':
            if (command_line_str == "d") {
                if (document_.size() > 1 && row_ != document_.end()) {
                    row_ = document_.erase(row_);
                    if (row_ == document_.end()) --row_;
                    current_line_ = *row_;
                } else {
                    current_line_.clear();
                    if (row_ != document_.end()) *row_ = "";
                }
                ncolumn_ = 0;
                command_line_ = {};
            } else {
                command_line_.emplace_back('d');
            }
            break;

        case '$':
            if (command_line_str == "d") {
                ncolumn_ = fmax(--ncolumn_, 0);
                current_line_.erase(ncolumn_, current_line_.size() - ncolumn_);
                command_line_.clear();
            } else if (command_line_str == "c") {
                DispatchCommand("d$a");
            } else if (command_line_str == "") {
                ncolumn_ = current_line_.size();
            }
            output_->Emit(current_line_, ncolumn_, mode_);
            break;

        case '^':
            if (command_line_str == "") {
                int start = 0;
                while (start < (int)current_line_.size() && current_line_[start] == ' ')
                    ++start;
                ncolumn_ = fmin((int)current_line_.size(), start + 1);
            }
            output_->Emit(current_line_, ncolumn_, mode_);
            break;

        case '0':
            if (command_line_str == "") {
                ncolumn_ = fmin(1, (int)current_line_.size());
            }
            output_->Emit(current_line_, ncolumn_, mode_);
            break;

        case 'a':
            if (modifiers->ctrl) {
                ncolumn_ = 1;
            } else if (command_line_str == "d" || command_line_str == "c") {
                command_line_.emplace_back('a');
            } else {
                mode_ = EditorMode::kInsert;
            }
            output_->Emit(current_line_, ncolumn_, mode_);
            break;

        case 'i':
            if (modifiers->meta) {
                if (row_ != document_.begin()) --row_;
                else row_ = document_.begin();
            } else if (command_line_str == "d" || command_line_str == "c") {
                command_line_.emplace_back('i');
            } else {
                ncolumn_ = fmax(--ncolumn_, 0);
                mode_ = EditorMode::kInsert;
            }
            break;

        case 'n':
            if (modifiers->meta) {
                ncolumn_ = fmax(0, --ncolumn_);
            }
            break;

        case 'e':
            if (modifiers->meta) break;
            if (modifiers->ctrl) { ncolumn_ = current_line_.size(); break; }
            break;

        case 'o':
            if (modifiers->meta) {
                ncolumn_ = fmin(0, ++ncolumn_);
            }
            break;
    }

    if (emits) {
        output_->Emit(current_line_, ncolumn_, mode_);
    }
}

// ---------------------------------------------------------------------------
// Key processing — main entry point
// ---------------------------------------------------------------------------

void Editor::ProcessKey(const uint8_t key, KeyModifiers* modifiers,
                        bool batched) {
    if (key == 0) {
        output_->Emit(current_line_, ncolumn_, mode_);
        return;
    }

    char c = get_char_from_key(key, modifiers);

    int now = output_->CurrentTimeInMillis();
    // Debounce KEY_ENTER specifically (some keyboards bounce / send duplicates on ESP32 BLE)
    static int previously_entered = -1;
    if (key == KEY_ENTER) {
        if (now - previously_entered < 200) return;
        previously_entered = now;
    }

    if ((now - previously_shifted_ < 150) && key == (uint8_t)previous_key_) {
        return;
    }
    if (modifiers->shift) {
        previously_shifted_ = now;
        previous_key_ = key;
    } else {
        previously_shifted_ = -1;
        previous_key_ = -1;
    }

    // Modeless keys: arrows
    switch (key) {
        case KEY_LEFT:
            ncolumn_ = fmax(--ncolumn_, 0);
            if (!batched) output_->Emit(current_line_, ncolumn_, mode_);
            return;
        case KEY_RIGHT:
            ncolumn_ = fmin(++ncolumn_, (int)current_line_.size());
            if (!batched) output_->Emit(current_line_, ncolumn_, mode_);
            return;
        case KEY_UP:
            if (output_->PrevLine() >= 0) {
                ncolumn_ = output_->PrevLine();
                output_->Emit(current_line_, ncolumn_, mode_);
                return;
            }
            if (row_ == document_.begin()) {
                ncolumn_ = 0;
                UpdateCurrentLine(-1);
                output_->Emit(current_line_, ncolumn_, mode_);
                return;
            }
            UpdateCurrentLine(-1);
            if (!batched) output_->Emit(current_line_, ncolumn_, mode_);
            return;
        case KEY_DOWN:
            if (output_->NextLine() >= 0) {
                ncolumn_ = output_->NextLine();
                output_->Emit(current_line_, ncolumn_, mode_);
                return;
            }
            UpdateCurrentLine(+1);
            if (!batched) output_->Emit(current_line_, ncolumn_, mode_);
            return;
        default:
            break;
    }

    // Normal mode
    if (mode_ == EditorMode::kNormal) {
        ProcessCommand(c, modifiers);
        return;
    }

    // Insert mode
    if (mode_ == EditorMode::kInsert) {
        auto begin_line = current_line_.begin();
        switch (key) {
            case KEY_ENTER:
                HandleEnter();
                output_->Emit(current_line_, ncolumn_, mode_);
                break;
            case KEY_ESC:
                HandleEsc();
                break;
            case KEY_CAPSLOCK:
                HandleEsc();
                break;
            case KEY_BACKSPACE:
                if (current_line_.size() < (size_t)ncolumn_) break;
                if (current_line_.size() == 0) break;
                ncolumn_ = fmax(0, --ncolumn_);
                std::advance(begin_line, ncolumn_);
                current_line_.erase(begin_line);
                if (!batched) output_->Emit(current_line_, ncolumn_, mode_);
                break;
            case KEY_SPACE:
                c = ' ';
                // fall through
            default:
                if (c == '\0') break;
                current_line_.insert(ncolumn_, 1, c);
                ncolumn_++;
                if (!batched) output_->Emit(current_line_, ncolumn_, mode_);
        }
        return;
    }

    // Command-line mode
    if (mode_ == EditorMode::kCommandLineMode) {
        switch (key) {
            case KEY_ESC:
                command_line_.clear();
                mode_ = EditorMode::kNormal;
                output_->Command(OutputCommands::kCommandMode);
                output_->Emit(current_line_, ncolumn_, mode_);
                return;
            case KEY_BACKSPACE:
                if (!command_line_.empty()) command_line_.pop_back();
                output_->CommandLine(command_line_);
                break;
            case KEY_ENTER: {
                auto command = std::string(command_line_.begin(), command_line_.end());
                command_line_.clear();

                if (command == ":q") {
                    mode_ = EditorMode::kNormal;
                    output_->Command(OutputCommands::kSplash);
                    return;
                }
                if (command == ":wc") {
                    int wc = 0, cc = 0;
                    WordCount(&wc, &cc);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "w: %d c: %d l: %d", wc, cc, CountLines());
                    output_->CommandLine(buf);
                }
                if (command.find(":br ") == 0 || command.find(":brightness ") == 0) {
                    int val = 0;
                    if (command.find(":br ") == 0) val = atoi(command.c_str() + 4);
                    else val = atoi(command.c_str() + 12);
                    val = fmax(0, fmin(100, val));
                    axp192_set_lcd_backlight((val * 255) / 100);
                    output_->CommandLine("brightness set");
                }
                if (command == ":lorem") {
                    current_line_.assign(
                        "The treeship Yggdrasill was a marvel of bio-engineering, a "
                        "living vessel grown from the heartwood of a world tree. Its "
                        "hull was a tapestry of interwoven bark and leaves, its decks a "
                        "network of branches and vines. Sunlight filtered through the "
                        "canopy, casting dappled shadows on the mossy floor.");
                    UpdateCurrentLine(-1);
                    output_->Emit(current_line_, ncolumn_, mode_);
                }
                if (command.rfind(":w", 0) == 0) {
                    if (!current_line_.empty()) UpdateCurrentLine(-1);
                    auto arg = command.substr(2);
                    // trim leading space
                    while (!arg.empty() && arg[0] == ' ') arg.erase(0, 1);
                    if (!arg.empty()) filename_ = arg;

                    std::string err;
                    if (sd_save(filename_.c_str(), document_, err)) {
                        output_->CommandLine("saved: " + filename_);
                    } else {
                        output_->CommandLine("error: " + err);
                    }
                }
                if (command.rfind(":e ", 0) == 0) {
                    auto arg = command.substr(3);
                    std::string err;
                    if (sd_load(arg.c_str(), document_, err)) {
                        filename_ = arg;
                        row_ = document_.begin();
                        current_line_ = *row_;
                        ncolumn_ = 0;
                        output_->Emit(current_line_, ncolumn_, mode_);
                        output_->CommandLine("loaded: " + filename_);
                    } else {
                        output_->CommandLine("error: " + err);
                    }
                }
                if (command == ":ps") {
                    printf("\n--- document ---\n");
                    for (const auto& line : document_) printf("%s\n", line.c_str());
                    printf("--- end ---\n");
                    output_->CommandLine("sent");
                }
                mode_ = EditorMode::kNormal;
                return;
            }
            case KEY_SPACE:
                c = ' ';
                // fall through
            default:
                switch (c) {
                    case '\0': break;
                    default:
                        command_line_.emplace_back(c);
                        output_->CommandLine(command_line_);
                        return;
                }
        }
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Editor::HandleEsc() {
    if (mode_ == EditorMode::kInsert || mode_ == EditorMode::kCommandLineMode) {
        command_line_.clear();
        mode_ = EditorMode::kNormal;
        output_->Command(OutputCommands::kCommandMode);
        output_->Emit(current_line_, ncolumn_, mode_);
    }
}

void Editor::WordCount(int* wc, int* cc) {
    *wc = 0; *cc = 0;
    for (const std::string& line : document_) {
        bool in_word = false;
        for (char ch : line) {
            (*cc)++;
            if (std::isspace(ch)) { if (in_word) (*wc)++; in_word = false; }
            else                  { in_word = true; }
        }
        if (in_word) (*wc)++;
    }
}
