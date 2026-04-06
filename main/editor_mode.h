// Copyright 2025 Ruben Berenguel

#pragma once

// Editor modes — kept separate to avoid circular dependency between
// editor.h and output.hpp
enum class EditorMode { kNormal, kInsert, kCommandLineMode };

// Events the editor can receive (BLE connect/disconnect, save trigger)
typedef enum {
    EV_BT_ON,
    EV_BT_OFF,
    EV_SAVE
} EventType;
