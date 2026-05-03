// Waveshare ESP32-S3-Touch-LCD-3.49 display context.
//
// LovyanGFX is used purely as a software renderer into a 640x172 LGFX_Sprite
// in PSRAM.  On display_commit() the full sprite is rotated into native panel
// orientation and pushed to the AXS15231B over QSPI in row bands.
//
// Rotation (uiRotated180=true, matching rsvpnano):
//   logicalX = nativeY
//   logicalY = (DISP_H - 1) - nativeX

#include "display_context.h"
#include "axs15231b.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <algorithm>

static const char *TAG = "disp_ws349";

static constexpr int PANEL_W    = 172;    // native portrait width
static constexpr int PANEL_H    = 640;    // native portrait height
static constexpr int DISP_W     = 640;    // logical landscape width
static constexpr int DISP_H     = 172;    // logical landscape height
static constexpr int SEND_BUF_PX = 0x4000; // max pixels per SPI transaction

static lgfx::LGFX_Sprite  s_canvas;
static uint16_t           *s_dma_buf = nullptr;

void display_init() {
    axs15231b_init();

    s_canvas.setColorDepth(16);
    s_canvas.setPsram(true);
    if (!s_canvas.createSprite(DISP_W, DISP_H)) {
        ESP_LOGE(TAG, "Failed to allocate canvas sprite (%dx%d)", DISP_W, DISP_H);
    }
    s_canvas.fillSprite(TFT_BLACK);

    s_dma_buf = static_cast<uint16_t *>(
        heap_caps_malloc(SEND_BUF_PX * sizeof(uint16_t),
                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!s_dma_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer");
    }

    ESP_LOGI(TAG, "Display context ready (%dx%d logical)", DISP_W, DISP_H);
}

lgfx::LovyanGFX& display_get() {
    return s_canvas;
}

void display_set_rotation(int /*rot*/) {
    // Rotation is fixed to landscape via the commit transform; ignore.
}

void display_commit() {
    if (!s_dma_buf) return;

    const uint16_t *src = static_cast<const uint16_t *>(s_canvas.getBuffer());
    if (!src) return;

    const int rows_per_chunk = SEND_BUF_PX / PANEL_W;

    for (int nativeYStart = 0; nativeYStart < PANEL_H; nativeYStart += rows_per_chunk) {
        const int nativeRows = std::min(rows_per_chunk, PANEL_H - nativeYStart);

        for (int localNativeY = 0; localNativeY < nativeRows; ++localNativeY) {
            const int nativeY = nativeYStart + localNativeY;
            uint16_t *dstRow = s_dma_buf + localNativeY * PANEL_W;

            for (int nativeX = 0; nativeX < PANEL_W; ++nativeX) {
                int logicalX = nativeY;
                int logicalY = (DISP_H - 1) - nativeX;
                dstRow[nativeX] = src[logicalY * DISP_W + logicalX];
            }
        }

        axs15231b_push_colors(0, nativeYStart, PANEL_W, nativeRows, s_dma_buf);
    }
}

void display_commit_partial(int /*lx*/, int /*ly*/, int /*lw*/, int /*lh*/) {
    display_commit();
}
