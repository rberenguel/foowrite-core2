#include <stdio.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "axp192.h"
#include "lgfx_config.h"
#include "key_queue.h"
#include "keymap.h"
#include "ble_hid_host.h"
#include "output.hpp"
#include "editor.h"
#include "editor_mode.h"
#include "sd_storage.h"
#include "splash.h"

static const char *TAG = "main";

// Shared queue — BLE writes, editor reads
QueueHandle_t g_key_queue = NULL;

LGFX   display;   // extern'd by output.cpp for display rendering
static Output g_output;
static Editor g_editor;

// Status updated from BLE task, read from main loop
static std::atomic<ble_hid_status_t> g_ble_status{BLE_HID_SCANNING};

static void on_ble_status(ble_hid_status_t status) {
    g_ble_status.store(status);
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------

static void apply_config() {
    FooConfig cfg = sd_load_config();
    g_use_qwerty = cfg.qwerty;
    axp192_set_lcd_backlight((cfg.brightness * 255) / 100);
}

static void draw_scanning_screen(void) {
    draw_splash(&display);
}

static void draw_connected_screen(void) {
    apply_config();   // re-read config on every connect; :q + reconnect refreshes
    draw_bt_icon(&display, TFT_GREEN);

    vTaskDelay(pdMS_TO_TICKS(800));

    display.fillScreen(TFT_BLACK);
    g_editor.Refresh();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

extern "C" void app_main(void) {
    // 1. Power on peripherals
    axp192_init(nullptr);

    // 2. Display
    display.init();
    display.setRotation(1);

    // 2b. SD card (SPI3_HOST shared with display; must init after display)
    if (!sd_init()) {
        ESP_LOGW(TAG, "SD card not found or mount failed");
    }
    apply_config();

    ESP_LOGI(TAG, "Display initialised: %dx%d", (int)display.width(), (int)display.height());

    // 3. Key event queue
    g_key_queue = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_event_t));

    // 4. Editor
    g_editor.Init(&g_output);

    // 5. BLE — scanning starts immediately on Core 0
    draw_scanning_screen();
    ble_hid_init(g_key_queue, on_ble_status);

    // 6. Main loop (Core 1) — editor loop
    ble_hid_status_t last_status = BLE_HID_SCANNING;

    while (true) {
        ble_hid_status_t status = g_ble_status.load();

        // Update screen on status change
        if (status != last_status) {
            if (status == BLE_HID_CONNECTED) {
                g_editor.ProcessEvent(EV_BT_ON);
                draw_connected_screen();
            } else {
                g_editor.ProcessEvent(EV_BT_OFF);
                draw_scanning_screen();
            }
            last_status = status;
        }

        // Feed queued keypresses to the editor
        key_event_t evt;
        while (xQueueReceive(g_key_queue, &evt, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool batched = uxQueueMessagesWaiting(g_key_queue) > 0;
            g_editor.ProcessKey(evt.keycode, &evt.modifiers, batched);
        }
    }
}
