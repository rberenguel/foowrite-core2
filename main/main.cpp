#include <stdio.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_pm.h"

#include "board.h"
#include "display_context.h"
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

// ---------------------------------------------------------------------------
// Software key-repeat
// Keys that should repeat when held (arrows, backspace, delete).
// The BLE host sends one event per physical press; the OS-side repeat that
// desktop systems provide simply doesn't exist here, so we synthesise it.
// ---------------------------------------------------------------------------
static constexpr uint8_t k_repeat_keys[] = {
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_BACKSPACE, KEY_DELETE
};
static constexpr int RPT_DELAY_MS    = 400;  // hold duration before first repeat
static constexpr int RPT_INTERVAL_MS = 60;   // interval between subsequent repeats

static uint8_t  s_rpt_key      = 0;
static int64_t  s_rpt_first_ms = 0;
static int64_t  s_rpt_last_ms  = 0;
static bool     s_rpt_active   = false;

static bool is_repeat_key(uint8_t kc) {
    for (auto k : k_repeat_keys) if (k == kc) return true;
    return false;
}

static Output g_output;
static Editor g_editor;

static void check_soft_repeat() {
    uint8_t held = ble_hid_current_keycode();
    int64_t now  = esp_timer_get_time() / 1000;  // µs → ms

    if (held != s_rpt_key) {
        // Key changed (new press or released) — reset repeat state
        s_rpt_key      = held;
        s_rpt_first_ms = now;
        s_rpt_last_ms  = now;
        s_rpt_active   = false;
        return;
    }
    if (held == 0 || !is_repeat_key(held)) return;

    if (!s_rpt_active) {
        if (now - s_rpt_first_ms >= RPT_DELAY_MS) {
            s_rpt_active  = true;
            s_rpt_last_ms = now;
            KeyModifiers no_mods = {};
            g_editor.ProcessKey(held, &no_mods, false);
        }
    } else if (now - s_rpt_last_ms >= RPT_INTERVAL_MS) {
        s_rpt_last_ms = now;
        KeyModifiers no_mods = {};
        g_editor.ProcessKey(held, &no_mods, false);
    }
}

// ---------------------------------------------------------------------------
// PWR button (GPIO16) — hold 1 s to shut down
// ---------------------------------------------------------------------------
static constexpr int64_t PWR_HOLD_MS = 1000;

static void check_pwr_button() {
    static int64_t s_low_since_ms = 0;
    static bool    s_armed        = true;

    bool pressed = (gpio_get_level(GPIO_NUM_16) == 0);
    int64_t now  = esp_timer_get_time() / 1000;

    if (!pressed) {
        s_low_since_ms = now;
        s_armed        = true;
        return;
    }
    if (!s_armed) return;
    if ((now - s_low_since_ms) >= PWR_HOLD_MS) {
        s_armed = false;
        board_shutdown();
    }
}

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
    board_set_backlight((cfg.brightness * 255) / 100);
    display_set_rotation(cfg.rotation == -1 ? 3 : 1);
}

static void draw_scanning_screen(void) {
    draw_splash(&display_get());
    display_commit();
}

static void draw_connected_screen(void) {
    apply_config();   // re-read config on every connect; :q + reconnect refreshes
    draw_bt_icon(&display_get(), TFT_GREEN);
    display_commit();

    vTaskDelay(pdMS_TO_TICKS(800));

    display_get().fillScreen(TFT_BLACK);
    display_commit();
    g_editor.Refresh();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

extern "C" void app_main(void) {
    // 0. Power management — allow CPU to drop to 80MHz and light-sleep between ticks.
    //    With xQueueReceive(50ms) the CPU is idle ~99% of the time; without this it
    //    spins at 160MHz burning ~80mA extra for no reason.
    esp_pm_config_t pm = {};
    pm.max_freq_mhz  = 80;
    pm.min_freq_mhz  = 80;   // keep stable for BLE timing; no dynamic scaling
    pm.light_sleep_enable = true;
    esp_pm_configure(&pm);

    // 1. Power on peripherals
    board_init();

    // 2. Display
    display_init();

    // 2b. SD card (SPI3_HOST shared with display; must init after display)
    if (!sd_init()) {
        ESP_LOGW(TAG, "SD card not found or mount failed");
    }

    // Load max battery voltage for calibration; update if this boot is a new high.
    // Pre-BLE is the least-loaded voltage reading we get.
    // Skip update when charging — charger elevates voltage artificially.
    {
        float max_mv   = sd_load_bat_max_mv();
        float cur_mv   = board_read_voltage_mv();
        bool  charging = board_is_charging();
        ESP_LOGI(TAG, "bat cal: max_mv=%.1f cur_mv=%.1f charging=%d", max_mv, cur_mv, charging);
        board_set_bat_max_mv(max_mv);
        if (!charging) {
            if (cur_mv > max_mv) {
                board_set_bat_max_mv(cur_mv);
                sd_save_bat_max_mv(cur_mv);
                ESP_LOGI(TAG, "bat cal: saved new max %.1f mV", cur_mv);
            }
        } else {
            ESP_LOGI(TAG, "bat cal: skipping save (charging)");
        }
    }

    apply_config();

    ESP_LOGI(TAG, "Display initialised: %dx%d",
             (int)display_get().width(), (int)display_get().height());

    // 3. Key event queue
    g_key_queue = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_event_t));

    // 4. Editor
    g_editor.Init(&g_output);

    // 5. BLE — scanning starts immediately on Core 0
    draw_scanning_screen();
    ble_hid_init(g_key_queue, on_ble_status);

    // 6. Main loop (Core 1) — editor loop
    ble_hid_status_t last_status   = BLE_HID_SCANNING;
    int64_t          last_splash_ms = 0;
    static constexpr int64_t SPLASH_REFRESH_MS = 30000;  // refresh battery every 30s

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
                last_splash_ms = esp_timer_get_time() / 1000;
            }
            last_status = status;
        }

        // Periodically refresh the splash while scanning so the battery
        // reading stays current without needing a keyboard to be attached.
        if (status == BLE_HID_SCANNING) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - last_splash_ms >= SPLASH_REFRESH_MS) {
                draw_scanning_screen();
                last_splash_ms = now_ms;
            }
        }

        // Feed queued keypresses to the editor
        key_event_t evt;
        while (xQueueReceive(g_key_queue, &evt, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool batched = uxQueueMessagesWaiting(g_key_queue) > 0;
            g_editor.ProcessKey(evt.keycode, &evt.modifiers, batched);
        }

        // Synthesise key-repeat for navigation/delete keys
        check_soft_repeat();

        // Power button — 1 s hold shuts down
        check_pwr_button();
    }
}
