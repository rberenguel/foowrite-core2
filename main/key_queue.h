#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Key modifiers — mirrors foowrite's KeyModifiers
typedef struct {
    bool shift;
    bool ctrl;
    bool meta;
    bool alt;
} KeyModifiers;

// One keypress event passed from BLE task (Core 0) to editor task (Core 1)
typedef struct {
    uint8_t      keycode;   // USB HID keycode
    KeyModifiers modifiers;
} key_event_t;

// Shared queue — created in main, written by BLE, read by editor
extern QueueHandle_t g_key_queue;
#define KEY_QUEUE_DEPTH 64
