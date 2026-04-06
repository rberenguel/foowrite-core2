#pragma once

#include "key_queue.h"

typedef enum {
    BLE_HID_SCANNING,    // waiting for a keyboard
    BLE_HID_CONNECTED,   // keyboard connected and notifications active
} ble_hid_status_t;

// Called from NimBLE host task (Core 0) when status changes.
// Keep it short — don't call display functions directly from here.
typedef void (*ble_hid_status_cb_t)(ble_hid_status_t status);

// Initialise NimBLE and start scanning.  Must be called once from app_main.
// key_queue : queue to push key_event_t into on each keypress
// status_cb : called on connect/disconnect (may be NULL)
void ble_hid_init(QueueHandle_t key_queue, ble_hid_status_cb_t status_cb);
