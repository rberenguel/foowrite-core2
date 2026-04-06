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

#define HID_SVC_UUID        0x1812
#define REPORT_CHR_UUID     0x2A4D
#define BOOT_KB_IN_UUID     0x2A22  // Boot Keyboard Input Report
#define PROTOCOL_MODE_UUID  0x2A4E
#define CCCD_UUID           0x2902
#define MAX_CHRS            24
#define MAX_CCCDS            8

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static QueueHandle_t       s_key_queue  = NULL;
static ble_hid_status_cb_t s_status_cb  = NULL;
static uint8_t             s_own_addr_type;

static uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_hid_svc_start = 0;
static uint16_t s_hid_svc_end   = 0;
static uint8_t  s_last_keycode  = 0;

// All characteristics in the HID service
static uint16_t s_chr_def_hdls[MAX_CHRS];
static uint16_t s_chr_val_hdls[MAX_CHRS];
static uint16_t s_chr_uuids[MAX_CHRS];
static int      s_num_chrs = 0;

// Protocol Mode characteristic value handle (0 if not found)
static uint16_t s_proto_mode_hdl = 0;

// CCCDs found that belong to notifiable HID characteristics
static uint16_t s_cccd_handles[MAX_CCCDS];
static int      s_num_cccds          = 0;
static bool     s_security_pending   = false;
static bool     s_notified_connected = false;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void start_scan(void);
static void write_all_cccds(uint16_t conn_handle);
static void start_dsc_disc(uint16_t conn_handle);
static int  gap_event_cb(struct ble_gap_event *event, void *arg);
static int  svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         const struct ble_gatt_svc *service, void *arg);
static int  chr_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         const struct ble_gatt_chr *chr, void *arg);
static int  dsc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         uint16_t chr_def_handle, const struct ble_gatt_dsc *dsc, void *arg);
static int  cccd_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg);
static int  proto_mode_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                 struct ble_gatt_attr *attr, void *arg);

// ---------------------------------------------------------------------------
// HID report parsing
// ---------------------------------------------------------------------------

static void handle_hid_report(uint16_t attr_handle, const uint8_t *data, uint16_t len) {
    // Log every notification raw — critical for diagnosing keyboard format
    char hex[64] = {};
    int  pos = 0;
    for (int i = 0; i < len && pos < (int)sizeof(hex) - 3; i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x ", data[i]);
    }
    ESP_LOGD(TAG, "NOTIFY hdl=%d len=%d [%s]", attr_handle, len, hex);

    // Standard HID keyboard report:
    //   Byte 0   : modifier mask
    //   Byte 1   : reserved (0x00)
    //   Bytes 2-7: up to 6 keycodes
    // With report ID prefix (len >= 9, byte 0 = ID, bytes 1-8 = report)
    const uint8_t *rpt = data;
    uint16_t       rlen = len;

    if (len >= 9) {
        // Could be report-ID-prefixed — byte 1 is usually 0x00 (reserved) in raw reports
        // If byte 0 looks like a report ID (small, non-zero) and byte 2 is 0x00, skip byte 0
        if (data[0] != 0 && data[2] == 0x00) {
            rpt  = data + 1;
            rlen = len - 1;
        }
    }

    if (rlen < 3) return;

    uint8_t modifier = rpt[0];
    KeyModifiers mods = {};
    mods.ctrl  = (modifier & 0x01) || (modifier & 0x10);
    mods.shift = (modifier & 0x02) || (modifier & 0x20);
    mods.alt   = (modifier & 0x04) || (modifier & 0x40);
    mods.meta  = (modifier & 0x08) || (modifier & 0x80);

    // Find first pressed keycode
    uint8_t keycode = 0;
    int     key_start = (rlen >= 8) ? 2 : 1;
    for (int i = key_start; i < (int)rlen && i < key_start + 6; i++) {
        if (rpt[i] > 0x01) { keycode = rpt[i]; break; }
    }

    if (keycode != 0 && keycode != s_last_keycode) {
        key_event_t evt = {keycode, mods};
        xQueueSend(s_key_queue, &evt, 0);
    }
    s_last_keycode = keycode;
}

// ---------------------------------------------------------------------------
// CCCD subscribe
// ---------------------------------------------------------------------------

static void write_all_cccds(uint16_t conn_handle) {
    ESP_LOGD(TAG, "Subscribing to %d CCCD(s)...", s_num_cccds);
    uint16_t val = htole16(0x0001);
    for (int i = 0; i < s_num_cccds; i++) {
        ESP_LOGD(TAG, "  CCCD handle %d", s_cccd_handles[i]);
        ble_gattc_write_flat(conn_handle, s_cccd_handles[i],
                             &val, sizeof(val), cccd_write_cb, NULL);
    }
}

static int cccd_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr, void *arg) {
    if (error->status == 0) {
        if (!s_notified_connected) {
            s_notified_connected = true;
            ESP_LOGI(TAG, "Keyboard ready — notifications active");
            if (s_status_cb) s_status_cb(BLE_HID_CONNECTED);
        }
        return 0;
    }

    if (error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
        error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC)) {
        if (!s_security_pending) {
            s_security_pending = true;
            ESP_LOGD(TAG, "Auth required, initiating security...");
            int rc = ble_gap_security_initiate(conn_handle);
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGE(TAG, "security_initiate rc=%d", rc);
                ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        }
        return 0;
    }

    ESP_LOGW(TAG, "CCCD write err %d (non-fatal)", error->status);
    return 0;
}

// ---------------------------------------------------------------------------
// Protocol Mode write
// ---------------------------------------------------------------------------

static int proto_mode_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                 struct ble_gatt_attr *attr, void *arg) {
    if (error->status == 0) {
        ESP_LOGD(TAG, "Protocol Mode set to Report Protocol (0x01)");
    } else {
        ESP_LOGW(TAG, "Protocol Mode write failed: %d (continuing anyway)", error->status);
    }
    // Proceed to descriptor discovery regardless
    start_dsc_disc(conn_handle);
    return 0;
}

// ---------------------------------------------------------------------------
// GATT discovery
// ---------------------------------------------------------------------------

static void start_dsc_disc(uint16_t conn_handle) {
    // Discover ALL descriptors across the entire HID service.
    // We match CCCDs to notifiable characteristics using chr_def_handle.
    s_num_cccds = 0;
    ESP_LOGD(TAG, "Discovering descriptors in service range [%d-%d]...",
             s_hid_svc_start, s_hid_svc_end);
    int rc = ble_gattc_disc_all_dscs(conn_handle,
                                      s_hid_svc_start + 1, s_hid_svc_end,
                                      dsc_disc_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "disc_all_dscs rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void start_svc_disc(uint16_t conn_handle) {
    s_hid_svc_start = s_hid_svc_end = 0;
    const ble_uuid16_t uuid = BLE_UUID16_INIT(HID_SVC_UUID);
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &uuid.u, svc_disc_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "disc_svc_by_uuid rc=%d", rc);
}

static int svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *service, void *arg) {
    if (error->status == BLE_HS_EDONE) {
        if (s_hid_svc_end != 0) {
            ESP_LOGD(TAG, "HID svc [%d-%d], discovering all chrs...",
                     s_hid_svc_start, s_hid_svc_end);
            s_num_chrs = 0;
            ble_gattc_disc_all_chrs(conn_handle, s_hid_svc_start, s_hid_svc_end,
                                    chr_disc_cb, NULL);
        } else {
            ESP_LOGE(TAG, "HID service not found");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    if (error->status != 0) return error->status;
    if (ble_uuid_u16(&service->uuid.u) == HID_SVC_UUID) {
        s_hid_svc_start = service->start_handle;
        s_hid_svc_end   = service->end_handle;
    }
    return 0;
}

static int chr_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr, void *arg) {
    if (error->status == BLE_HS_EDONE) {
        ESP_LOGD(TAG, "Found %d chr(s) in HID service", s_num_chrs);

        // If Protocol Mode characteristic exists, switch to Report Protocol (0x01).
        // This ensures the keyboard sends Report (0x2A4D) notifications, not boot-mode ones.
        if (s_proto_mode_hdl != 0) {
            ESP_LOGD(TAG, "Writing Protocol Mode = Report (0x01)...");
            uint8_t report_mode = 0x01;
            int rc = ble_gattc_write_flat(conn_handle, s_proto_mode_hdl,
                                          &report_mode, 1, proto_mode_write_cb, NULL);
            if (rc != 0) {
                ESP_LOGW(TAG, "Protocol Mode write failed rc=%d, continuing", rc);
                start_dsc_disc(conn_handle);
            }
        } else {
            start_dsc_disc(conn_handle);
        }
        return 0;
    }
    if (error->status != 0) return error->status;

    if (s_num_chrs < MAX_CHRS) {
        uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);
        s_chr_def_hdls[s_num_chrs] = chr->def_handle;
        s_chr_val_hdls[s_num_chrs] = chr->val_handle;
        s_chr_uuids[s_num_chrs]    = uuid16;
        ESP_LOGD(TAG, "  chr 0x%04x def=%d val=%d", uuid16,
                 chr->def_handle, chr->val_handle);
        s_num_chrs++;

        if (uuid16 == PROTOCOL_MODE_UUID) {
            s_proto_mode_hdl = chr->val_handle;
        }
    }
    return 0;
}

// Is this chr's def_handle one of our notifiable characteristics?
static bool chr_is_notifiable(uint16_t def_handle) {
    for (int i = 0; i < s_num_chrs; i++) {
        if (s_chr_def_hdls[i] == def_handle) {
            uint16_t u = s_chr_uuids[i];
            return (u == REPORT_CHR_UUID || u == BOOT_KB_IN_UUID);
        }
    }
    return false;
}

static int dsc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        uint16_t chr_def_handle, const struct ble_gatt_dsc *dsc, void *arg) {
    if (error->status == BLE_HS_EDONE) {
        if (s_num_cccds == 0) {
            // No CCCDs found for known notifiable chrs — subscribe to ALL CCCDs anyway
            // (fallback: some keyboards don't lay out handles as expected)
            ESP_LOGW(TAG, "No CCCDs matched known chrs, re-scanning for any CCCD...");
            // We already ran through all descriptors. Try a broader collect by
            // re-running disc but storing all CCCDs regardless of chr.
            // To avoid infinite loop, check if we already fell back.
            if ((intptr_t)arg == 0) {
                s_num_cccds = 0;
                ble_gattc_disc_all_dscs(conn_handle,
                                         s_hid_svc_start + 1, s_hid_svc_end,
                                         dsc_disc_cb, (void*)1 /* fallback flag */);
            } else {
                ESP_LOGE(TAG, "Still no CCCDs found, disconnecting");
                ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        } else {
            write_all_cccds(conn_handle);
        }
        return 0;
    }
    if (error->status != 0) {
        ESP_LOGW(TAG, "dsc_disc err %d", error->status);
        return 0;
    }

    uint16_t uuid16 = ble_uuid_u16(&dsc->uuid.u);
    bool fallback = ((intptr_t)arg == 1);
    ESP_LOGD(TAG, "  dsc 0x%04x hdl=%d chr_def=%d%s",
             uuid16, dsc->handle, chr_def_handle, fallback ? " [fallback]" : "");

    if (uuid16 == CCCD_UUID && s_num_cccds < MAX_CCCDS) {
        // Normal pass: only collect CCCDs from notifiable characteristics
        // Fallback pass: collect all CCCDs
        if (fallback || chr_is_notifiable(chr_def_handle)) {
            s_cccd_handles[s_num_cccds++] = dsc->handle;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Advertisement filtering
// ---------------------------------------------------------------------------

static bool adv_has_hid_uuid(const struct ble_gap_disc_desc *disc) {
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return false;
    }
    for (int i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == HID_SVC_UUID) return true;
    }
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
        ESP_LOGI(TAG, "HID device: %02x:%02x:%02x:%02x:%02x:%02x — connecting",
                 a[5], a[4], a[3], a[2], a[1], a[0]);
        ble_gap_disc_cancel();
        if (ble_gap_connect(s_own_addr_type, &event->disc.addr,
                            30000, NULL, gap_event_cb, NULL) != 0) {
            start_scan();
        }
        break;
    }

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle        = event->connect.conn_handle;
            s_last_keycode       = 0;
            s_security_pending   = false;
            s_notified_connected = false;
            s_proto_mode_hdl     = 0;
            ESP_LOGI(TAG, "Connected, handle=%d", s_conn_handle);
            start_svc_disc(s_conn_handle);
        } else {
            ESP_LOGE(TAG, "Connect failed, scanning again");
            start_scan();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected reason=%d", event->disconnect.reason);
        s_conn_handle        = BLE_HS_CONN_HANDLE_NONE;
        s_num_cccds          = 0;
        s_num_chrs           = 0;
        s_security_pending   = false;
        s_notified_connected = false;
        s_last_keycode       = 0;
        if (s_status_cb) s_status_cb(BLE_HID_SCANNING);
        start_scan();
        break;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        uint8_t  buf[20];
        if (len > sizeof(buf)) len = sizeof(buf);
        ble_hs_mbuf_to_flat(event->notify_rx.om, buf, len, NULL);
        handle_hid_report(event->notify_rx.attr_handle, buf, len);
        break;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            ESP_LOGI(TAG, "Encryption established, retrying CCCD writes...");
            s_security_pending = false;
            write_all_cccds(event->enc_change.conn_handle);
        } else if (event->enc_change.status == BLE_HS_ENOTCONN) {
            // Connection already gone before encryption completed — disconnect
            // event will handle re-scan, nothing to do here.
            ESP_LOGD(TAG, "Encryption event on dead connection, ignoring");
        } else {
            ESP_LOGW(TAG, "Encryption failed: %d", event->enc_change.status);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey = {};
        pkey.action = event->passkey.params.action;
        ESP_LOGD(TAG, "Passkey action=%d (Just Works)", pkey.action);
        ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        break;
    }

    case BLE_GAP_EVENT_MTU:
        ESP_LOGD(TAG, "MTU=%d", event->mtu.value);
        break;

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
    params.passive           = 0;
    // Scan 50ms every 500ms — enough to find a keyboard without hammering the radio.
    // Units are 0.625ms: 800 * 0.625 = 500ms interval, 80 * 0.625 = 50ms window.
    params.itvl              = 800;
    params.window            = 80;
    params.filter_policy     = 0;
    params.limited           = 0;

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "disc rc=%d", rc);
    else ESP_LOGI(TAG, "Scanning for HID keyboard...");
}

// ---------------------------------------------------------------------------
// NimBLE host
// ---------------------------------------------------------------------------

static void on_sync(void) {
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    start_scan();
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

static void nimble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------

void ble_hid_init(QueueHandle_t key_queue, ble_hid_status_cb_t status_cb) {
    s_key_queue = key_queue;
    s_status_cb = status_cb;

    // Suppress the NimBLE port layer's own verbose HCI logs
    // Suppress also ourselves if not warn.
    esp_log_level_set("NimBLE", ESP_LOG_WARN);
    esp_log_level_set("ble_hid", ESP_LOG_WARN);

    nimble_port_init();

    ble_hs_cfg.sm_io_cap         = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding        = 0;
    ble_hs_cfg.sm_mitm           = 0;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_our_key_dist   = 0;
    ble_hs_cfg.sm_their_key_dist = 0;
    ble_hs_cfg.sync_cb           = on_sync;
    ble_hs_cfg.reset_cb          = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("foowrite");

    nimble_port_freertos_init(nimble_host_task);
}
