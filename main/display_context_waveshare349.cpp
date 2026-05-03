// Waveshare ESP32-S3-Touch-LCD-3.49 display context.
//
// LovyanGFX is used purely as a software renderer into a 640x172 LGFX_Sprite
// in PSRAM.  On display_commit_partial() the requested logical rectangle is
// rotated into native panel orientation and pushed to the AXS15231B over QSPI.
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
    display_commit_partial(0, 0, DISP_W, DISP_H);
}

void display_blit(const uint16_t* src,
                  int src_x, int src_y, int src_w,
                  int dst_x, int dst_y, int w, int h) {
    uint16_t* dst = static_cast<uint16_t*>(s_canvas.getBuffer());
    if (!dst) return;
    for (int y = 0; y < h; ++y) {
        const uint16_t* s = src + (src_y + y) * src_w + src_x;
        uint16_t* d = dst + (dst_y + y) * DISP_W + dst_x;
        for (int x = 0; x < w; ++x) {
            d[x] = s[x];
        }
    }
}

void display_commit_partial(int lx, int ly, int lw, int lh) {
    if (!s_dma_buf) return;

    if (lx < 0) { lw += lx; lx = 0; }
    if (ly < 0) { lh += ly; ly = 0; }
    if (lx + lw > DISP_W) lw = DISP_W - lx;
    if (ly + lh > DISP_H) lh = DISP_H - ly;
    if (lw <= 0 || lh <= 0) return;

    const uint16_t *src = static_cast<const uint16_t *>(s_canvas.getBuffer());
    if (!src) return;

    // Rotation: logicalX = nativeY, logicalY = (DISP_H - 1) - nativeX
    // Map logical rect (lx,ly,lw,lh) → native rect (nx0,ny0,nw,nh)
    const int nx0 = DISP_H - ly - lh;  // native x start
    const int ny0 = lx;                  // native y start
    const int nw  = lh;                  // native width  (= logical height)
    const int nh  = lw;                  // native height (= logical width)

    // Chunk along native rows (panel scan order).  Each chunk is nw × rows.
    const int rows_per_chunk = SEND_BUF_PX / nw;

    for (int nativeY = ny0; nativeY < ny0 + nh; nativeY += rows_per_chunk) {
        const int rows = std::min(rows_per_chunk, ny0 + nh - nativeY);

        for (int localY = 0; localY < rows; ++localY) {
            const int ny = nativeY + localY;
            uint16_t *dstRow = s_dma_buf + localY * nw;

            for (int nx = 0; nx < nw; ++nx) {
                int logicalX = ny;
                int logicalY = (DISP_H - 1) - (nx0 + nx);
                dstRow[nx] = src[logicalY * DISP_W + logicalX];
            }
        }

        axs15231b_push_colors(
            static_cast<uint16_t>(nx0),
            static_cast<uint16_t>(nativeY),
            static_cast<uint16_t>(nw),
            static_cast<uint16_t>(rows),
            s_dma_buf);
    }
}
