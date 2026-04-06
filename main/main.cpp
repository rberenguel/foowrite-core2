#include <stdio.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "axp192.h"
#include "lgfx_config.h"
#include "key_queue.h"
#include "ble_hid_host.h"

static const char *TAG = "main";

// Shared queue — BLE writes, editor reads
QueueHandle_t g_key_queue = NULL;

static LGFX display;

// Status updated from BLE task, read from main loop
static std::atomic<ble_hid_status_t> g_ble_status{BLE_HID_SCANNING};

static void on_ble_status(ble_hid_status_t status) {
    g_ble_status.store(status);
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------

static void draw_scanning_screen(void) {
    display.fillScreen(TFT_NAVY);
    display.setFont(&fonts::FreeSansBold12pt7b);
    display.setTextColor(TFT_WHITE, TFT_NAVY);
    display.setCursor(10, 30);
    display.print("foowrite");

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_CYAN, TFT_NAVY);
    display.setCursor(10, 65);
    display.print("Waiting for keyboard...");
    display.setCursor(10, 90);
    display.print("(pair any BLE keyboard)");
}

// Cursor position for the key echo area
static int32_t s_echo_x = 10;
static int32_t s_echo_y = 10;
static const int32_t ECHO_LINE_H = 20;
static const int32_t ECHO_MARGIN = 10;

static void draw_connected_screen(void) {
    display.fillScreen(TFT_DARKGREEN);
    display.setFont(&fonts::FreeSansBold12pt7b);
    display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    display.setCursor(10, 30);
    display.print("foowrite");

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    display.setCursor(10, 65);
    display.print("Keyboard connected!");

    vTaskDelay(pdMS_TO_TICKS(800));

    display.fillScreen(TFT_BLACK);
    s_echo_x = ECHO_MARGIN;
    s_echo_y = ECHO_MARGIN;
}

static void echo_key(const key_event_t &evt) {
    // Show raw HID keycode as hex — keymap translation comes with the editor
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X ", evt.keycode);

    display.setFont(&fonts::DejaVu18);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setCursor(s_echo_x, s_echo_y);
    display.print(buf);

    s_echo_x += display.textWidth(buf);
    if (s_echo_x > display.width() - 40) {
        s_echo_x = ECHO_MARGIN;
        s_echo_y += ECHO_LINE_H;
        if (s_echo_y > display.height() - ECHO_LINE_H) {
            // Scroll: clear and start over
            display.fillScreen(TFT_BLACK);
            s_echo_x = ECHO_MARGIN;
            s_echo_y = ECHO_MARGIN;
        }
    }
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

    ESP_LOGI(TAG, "Display initialised: %dx%d", (int)display.width(), (int)display.height());

    // 3. Key event queue
    g_key_queue = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_event_t));

    // 4. BLE — scanning starts immediately on Core 0
    draw_scanning_screen();
    ble_hid_init(g_key_queue, on_ble_status);

    // 5. Main loop (Core 1) — will become the editor loop
    ble_hid_status_t last_status = BLE_HID_SCANNING;

    while (true) {
        ble_hid_status_t status = g_ble_status.load();

        // Update screen on status change
        if (status != last_status) {
            if (status == BLE_HID_CONNECTED) {
                draw_connected_screen();
            } else {
                draw_scanning_screen();
            }
            last_status = status;
        }

        // Drain the key queue and echo raw keycodes to screen
        key_event_t evt;
        while (xQueueReceive(g_key_queue, &evt, pdMS_TO_TICKS(50)) == pdTRUE) {
            ESP_LOGI(TAG, "Key: 0x%02X shift=%d ctrl=%d alt=%d meta=%d",
                     evt.keycode, evt.modifiers.shift, evt.modifiers.ctrl,
                     evt.modifiers.alt, evt.modifiers.meta);
            if (last_status == BLE_HID_CONNECTED) {
                echo_key(evt);
            }
        }
    }
}
