#include "ble_hid_host.h"

#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_hid";

#define HID_SVC_UUID    0x1812
#define REPORT_CHR_UUID 0x2A4D
#define CCCD_UUID       0x2902

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static QueueHandle_t        s_key_queue   = NULL;
static ble_hid_status_cb_t  s_status_cb   = NULL;
static uint8_t              s_own_addr_type;

static uint16_t s_conn_handle     = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_hid_svc_start   = 0;
static uint16_t s_hid_svc_end     = 0;
static uint16_t s_report_val_hdl  = 0;  // Report characteristic value handle
static uint16_t s_report_def_hdl  = 0;  // Report characteristic definition handle
static uint16_t s_cccd_handle     = 0;  // stored for retry after pairing
static uint8_t  s_last_keycode    = 0;  // for key-down edge detection

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void     start_scan(void);
static void     write_cccd(uint16_t conn_handle);
static int      gap_event_cb(struct ble_gap_event *event, void *arg);
static int      svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                             const struct ble_gatt_svc *service, void *arg);
static int      chr_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg);
static int      dsc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                             uint16_t chr_def_handle, const struct ble_gatt_dsc *dsc, void *arg);
static int      cccd_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr, void *arg);

// ---------------------------------------------------------------------------
// HID report parsing
// ---------------------------------------------------------------------------

static void handle_hid_report(const uint8_t *data, uint16_t len) {
    // Standard HID keyboard report:
    //   Byte 0 : modifier mask
    //   Byte 1 : reserved
    //   Bytes 2-7 : up to 6 simultaneous keycodes
    // Some keyboards prefix with a 1-byte report ID → 9 bytes total.
    if (len < 8) return;
    const uint8_t *rpt = (len == 9) ? data + 1 : data;

    uint8_t modifier = rpt[0];
    KeyModifiers mods = {};
    mods.ctrl  = (modifier & 0x01) || (modifier & 0x10);
    mods.shift = (modifier & 0x02) || (modifier & 0x20);
    mods.alt   = (modifier & 0x04) || (modifier & 0x40);
    mods.meta  = (modifier & 0x08) || (modifier & 0x80);

    // Find the first pressed keycode (ignore rollover error 0x01)
    uint8_t keycode = 0;
    for (int i = 2; i < 8; i++) {
        if (rpt[i] > 0x01) { keycode = rpt[i]; break; }
    }

    // Only push on key-down edge (new key, not key held)
    if (keycode != 0 && keycode != s_last_keycode) {
        key_event_t evt = {keycode, mods};
        xQueueSend(s_key_queue, &evt, 0);
    }
    s_last_keycode = keycode;
}

// ---------------------------------------------------------------------------
// GATT discovery chain
// ---------------------------------------------------------------------------

static void start_svc_disc(uint16_t conn_handle) {
    ESP_LOGI(TAG, "Discovering services...");
    s_hid_svc_start = s_hid_svc_end = 0;
    const ble_uuid16_t uuid = BLE_UUID16_INIT(HID_SVC_UUID);
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &uuid.u, svc_disc_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "disc_svc_by_uuid rc=%d", rc);
}

static int svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *service, void *arg) {
    if (error->status == BLE_HS_EDONE) {
        if (s_hid_svc_end != 0) {
            ESP_LOGI(TAG, "HID svc handles %d–%d, discovering chars...",
                     s_hid_svc_start, s_hid_svc_end);
            s_report_val_hdl = s_report_def_hdl = 0;
            ble_gattc_disc_all_chrs(conn_handle, s_hid_svc_start, s_hid_svc_end,
                                    chr_disc_cb, NULL);
        } else {
            ESP_LOGE(TAG, "HID service not found, disconnecting");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    if (error->status != 0) { return error->status; }

    if (ble_uuid_u16(&service->uuid.u) == HID_SVC_UUID) {
        s_hid_svc_start = service->start_handle;
        s_hid_svc_end   = service->end_handle;
    }
    return 0;
}

static int chr_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr, void *arg) {
    if (error->status == BLE_HS_EDONE) {
        if (s_report_val_hdl != 0) {
            ESP_LOGI(TAG, "Report chr val_hdl=%d, discovering descriptors...",
                     s_report_val_hdl);
            // Descriptors live between val_handle+1 and service end
            ble_gattc_disc_all_dscs(conn_handle,
                                    s_report_val_hdl + 1, s_hid_svc_end,
                                    dsc_disc_cb, NULL);
        } else {
            ESP_LOGE(TAG, "Report characteristic not found, disconnecting");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    if (error->status != 0) { return error->status; }

    // Take the first Report characteristic found
    if (ble_uuid_u16(&chr->uuid.u) == REPORT_CHR_UUID && s_report_val_hdl == 0) {
        s_report_def_hdl = chr->def_handle;
        s_report_val_hdl = chr->val_handle;
        ESP_LOGI(TAG, "Report chr found def=%d val=%d", s_report_def_hdl, s_report_val_hdl);
    }
    return 0;
}

static int dsc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        uint16_t chr_def_handle, const struct ble_gatt_dsc *dsc, void *arg) {
    if (error->status == BLE_HS_EDONE) {
        if (s_cccd_handle == 0) {
            ESP_LOGE(TAG, "CCCD not found");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        // If s_cccd_handle != 0, write_cccd() was already called during iteration.
        return 0;
    }
    if (error->status != 0) { return error->status; }

    if (ble_uuid_u16(&dsc->uuid.u) == CCCD_UUID) {
        ESP_LOGI(TAG, "CCCD found at handle %d, enabling notifications...", dsc->handle);
        s_cccd_handle = dsc->handle;
        write_cccd(conn_handle);
    }
    return 0;
}

static void write_cccd(uint16_t conn_handle) {
    uint16_t val = htole16(0x0001);
    int rc = ble_gattc_write_flat(conn_handle, s_cccd_handle,
                                  &val, sizeof(val), cccd_write_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "CCCD write rc=%d", rc);
}

static int cccd_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr, void *arg) {
    if (error->status == 0) {
        ESP_LOGI(TAG, "Notifications enabled — keyboard ready");
        if (s_status_cb) s_status_cb(BLE_HID_CONNECTED);
        return 0;
    }

    // Keyboard requires encryption before allowing CCCD writes.
    // Initiate pairing — on success BLE_GAP_EVENT_ENC_CHANGE fires and we retry.
    if (error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
        error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC)) {
        ESP_LOGI(TAG, "Auth required (err %d), initiating security...", error->status);
        int rc = ble_gap_security_initiate(conn_handle);
        if (rc == BLE_HS_EALREADY) {
            // Keyboard already initiated security — just wait for BLE_GAP_EVENT_ENC_CHANGE
            ESP_LOGI(TAG, "Security already in progress, waiting...");
        } else if (rc != 0) {
            ESP_LOGE(TAG, "security_initiate rc=%d", rc);
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }

    ESP_LOGE(TAG, "CCCD write failed: %d", error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

// ---------------------------------------------------------------------------
// Advertisement parsing
// ---------------------------------------------------------------------------

static bool adv_has_hid_uuid(const struct ble_gap_disc_desc *disc) {
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return false;
    }
    for (int i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == HID_SVC_UUID) return true;
    }
    // Appearance 0x03C1 = keyboard (fallback for keyboards that don't list UUID)
    if (fields.appearance_is_present && fields.appearance == 0x03C1) return true;
    return false;
}

// ---------------------------------------------------------------------------
// GAP event handler
// ---------------------------------------------------------------------------

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {

    case BLE_GAP_EVENT_DISC: {
        if (!adv_has_hid_uuid(&event->disc)) break;
        const uint8_t *a = event->disc.addr.val;
        ESP_LOGI(TAG, "HID device found: %02x:%02x:%02x:%02x:%02x:%02x, connecting...",
                 a[5], a[4], a[3], a[2], a[1], a[0]);
        ble_gap_disc_cancel();
        int rc = ble_gap_connect(s_own_addr_type, &event->disc.addr,
                                 30000, NULL, gap_event_cb, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "Connect failed rc=%d, resuming scan", rc);
            start_scan();
        }
        break;
    }

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_last_keycode = 0;
            ESP_LOGI(TAG, "Connected, handle=%d", s_conn_handle);
            start_svc_disc(s_conn_handle);
        } else {
            ESP_LOGE(TAG, "Connect failed status=%d", event->connect.status);
            start_scan();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected reason=%d", event->disconnect.reason);
        s_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
        s_cccd_handle    = 0;
        s_report_val_hdl = 0;
        s_report_def_hdl = 0;
        s_last_keycode   = 0;
        if (s_status_cb) s_status_cb(BLE_HID_SCANNING);
        start_scan();
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == s_report_val_hdl) {
            uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
            uint8_t  buf[16];
            if (len > sizeof(buf)) len = sizeof(buf);
            ble_hs_mbuf_to_flat(event->notify_rx.om, buf, len, NULL);
            handle_hid_report(buf, len);
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            ESP_LOGI(TAG, "Encryption established, retrying CCCD write...");
            write_cccd(event->enc_change.conn_handle);
        } else {
            ESP_LOGE(TAG, "Encryption failed: %d", event->enc_change.status);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        // Just Works: respond with action type, no passkey needed
        struct ble_sm_io pkey = {};
        pkey.action = event->passkey.params.action;
        ESP_LOGI(TAG, "Passkey action=%d (Just Works)", pkey.action);
        ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        break;
    }

    default:
        break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

static void start_scan(void) {
    struct ble_gap_disc_params params = {};
    params.filter_duplicates = 1;
    params.passive           = 0; // active: get scan response (some KBs put UUID there)
    params.itvl              = 0;
    params.window            = 0;
    params.filter_policy     = 0;
    params.limited           = 0;

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "disc failed rc=%d", rc);
    else ESP_LOGI(TAG, "Scanning for HID keyboard...");
}

// ---------------------------------------------------------------------------
// NimBLE host callbacks
// ---------------------------------------------------------------------------

static void on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) { ESP_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc); return; }
    start_scan();
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();          // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------

void ble_hid_init(QueueHandle_t key_queue, ble_hid_status_cb_t status_cb) {
    s_key_queue  = key_queue;
    s_status_cb  = status_cb;

    nimble_port_init();

    // Security: no bonding, Just Works, Secure Connections enabled
    // (MX Keys and many modern keyboards require SC for encryption)
    ble_hs_cfg.sm_io_cap          = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding         = 0;
    ble_hs_cfg.sm_mitm            = 0;
    ble_hs_cfg.sm_sc              = 1;  // Secure Connections
    ble_hs_cfg.sm_our_key_dist    = 0;
    ble_hs_cfg.sm_their_key_dist  = 0;
    ble_hs_cfg.sync_cb            = on_sync;
    ble_hs_cfg.reset_cb           = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("foowrite");

    // Starts NimBLE host task pinned to the core set in sdkconfig
    nimble_port_freertos_init(nimble_host_task);
}
