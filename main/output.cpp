// Copyright 2025 Ruben Berenguel

// LovyanGFX display renderer for M5Stack Core2 (ILI9342C, 320x240).
// Solarized dark palette; sprite-based rendering for flicker-free updates.

#include "output.hpp"

#include <list>
#include <string>

#include "editor_mode.h"
#include "esp_timer.h"
#include "lgfx_config.h"
#include "splash.h"

extern LGFX display;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static constexpr int32_t SCREEN_W = 320;
static constexpr int32_t SCREEN_H = 240;
static constexpr int32_t TEXT_X   = 3;
static constexpr int32_t TEXT_Y   = 3;
static constexpr int32_t STATUS_H = 20;
static constexpr int32_t STATUS_Y = SCREEN_H - STATUS_H;
static constexpr int32_t WRAP_W   = SCREEN_W - TEXT_X * 2;
static constexpr int32_t TEXT_H   = STATUS_Y - TEXT_Y;

// ---------------------------------------------------------------------------
// Solarized dark palette — pure black background
// ---------------------------------------------------------------------------
static constexpr uint32_t COL_BG        = 0x000000;  // pure black
static constexpr uint32_t COL_TEXT      = 0x839496;  // base0  — body text (light grey)
static constexpr uint32_t COL_CURSOR_N  = 0x268bd2;  // blue   — Normal underline
static constexpr uint32_t COL_CURSOR_I  = 0x2aa198;  // cyan   — Insert bar
static constexpr uint32_t COL_STATUS_BG = 0x073642;  // base02 — status bar background
static constexpr uint32_t COL_LABEL_N   = 0x859900;  // green  — "NORMAL" label
static constexpr uint32_t COL_LABEL_I   = 0x268bd2;  // blue   — "INSERT" label
static constexpr uint32_t COL_LABEL_C   = 0x2aa198;  // cyan   — "COMMAND" label

// ---------------------------------------------------------------------------
// Double-buffer sprite pair — allocated from PSRAM in Init.
// s_buf[s_curr]       = render target for the current frame
// s_buf[1 - s_curr]   = previous frame (used for dirty-rect diff)
// Size: 2 × WRAP_W × TEXT_H × 2 bytes ≈ 2 × 136 KB = ~272 KB PSRAM.
// ---------------------------------------------------------------------------
static lgfx::LGFX_Sprite s_buf[2];
static bool               s_bufs_ready = false;
static int                s_curr       = 0;

static constexpr int MAX_VL = 128;

// ---------------------------------------------------------------------------
// Helpers — operate on any LovyanGFX canvas (sprite or display)
// ---------------------------------------------------------------------------

static void set_body_font(lgfx::LovyanGFX& g) {
    g.setFont(&fonts::DejaVu18);
    g.setTextSize(1);
    g.setTextDatum(lgfx::top_left);
}

static inline int32_t cw(lgfx::LovyanGFX& g, char c) {
    char buf[2] = {c, '\0'};
    return g.textWidth(buf);
}

// Draw cursor at canvas-relative position (ox = left edge of text area in canvas coords).
// ox lets us clamp the Insert bar so it never goes left of the text margin.
static void draw_cursor(lgfx::LovyanGFX& g, int32_t x, int32_t y,
                        int32_t w, int32_t line_h, EditorMode mode, int32_t ox) {
    if (mode == EditorMode::kNormal) {
        g.fillRect(x, y + line_h - 3, w > 0 ? w : 8, 2, COL_CURSOR_N);
    } else {
        // Insert bar — one pixel to the left of character, clamped to ox.
        int32_t bx = x > ox ? x - 1 : ox;
        g.fillRect(bx, y, 2, line_h - 2, COL_CURSOR_I);
    }
}

// ---------------------------------------------------------------------------
// Two-pass word-wrap renderer
//
// Renders into canvas `g` starting at (ox, oy).  For the sprite path, ox=oy=0
// and the sprite is later pushed to (TEXT_X, TEXT_Y).  For the direct-display
// fallback, ox=TEXT_X, oy=TEXT_Y.
//
// Pass 1: measure words, build visual-line start list, find cursor vline.
// Pass 2: fill bg, then draw from page start.
// ---------------------------------------------------------------------------
// Returns the height in pixels of the rendered content (clamped to TEXT_H).
// Caller uses this to limit pushSprite to only the rows that matter.
static int32_t render_to(lgfx::LovyanGFX& g, int32_t ox, int32_t oy,
                         const std::string& t, int cursor_pos, EditorMode mode,
                         int* out_prev, int* out_next) {
    set_body_font(g);

    const int32_t line_h  = g.fontHeight() + 2;
    const int32_t space_w = cw(g, ' ');
    const int32_t n_vis   = TEXT_H / line_h;

    *out_prev = -1;
    *out_next = -1;

    // -------------------------------------------------------------------------
    // Pass 1 — build visual-line start list; find cursor visual line
    // -------------------------------------------------------------------------
    int vl[MAX_VL];
    int nvl = 0;
    vl[nvl++] = 0;

    int     cursor_vline = -1;
    int32_t px = 0;

    for (size_t i = 0; i < t.size(); ) {
        char ch = t[i];

        if (ch == '\n') {
            if (cursor_vline < 0 && (int)i >= cursor_pos) cursor_vline = nvl - 1;
            if (nvl < MAX_VL) vl[nvl++] = (int)i + 1;
            px = 0;
            i++;
            continue;
        }
        if (ch == ' ') {
            if (cursor_vline < 0 && (int)i >= cursor_pos) cursor_vline = nvl - 1;
            px += space_w;
            i++;
            continue;
        }

        // Word: measure, then wrap if needed.
        size_t  wstart = i;
        int32_t ww     = 0;
        while (i < t.size() && t[i] != ' ' && t[i] != '\n') { ww += cw(g, t[i]); i++; }

        if (px != 0 && px + ww > WRAP_W) {
            if (nvl < MAX_VL) vl[nvl++] = (int)wstart;
            px = 0;
        }
        if (cursor_vline < 0 &&
            cursor_pos >= (int)wstart && cursor_pos <= (int)i) {
            cursor_vline = nvl - 1;
        }
        px += ww;
    }

    if (cursor_vline < 0) cursor_vline = nvl - 1;

    *out_prev = cursor_vline > 0       ? vl[cursor_vline - 1] : -1;
    *out_next = cursor_vline + 1 < nvl ? vl[cursor_vline + 1] : -1;

    int start_vl = n_vis > 0 ? (cursor_vline / n_vis) * n_vis : 0;
    int start_ch = vl[start_vl];

    // -------------------------------------------------------------------------
    // Pass 2 — clear canvas, render from start_ch
    // -------------------------------------------------------------------------
    g.fillRect(ox, oy, WRAP_W, TEXT_H, COL_BG);
    g.setTextColor(COL_TEXT, COL_BG);

    px = 0;
    int32_t py = 0;
    bool    cursor_drawn = false;

    for (size_t i = (size_t)start_ch; py < TEXT_H; ) {
        if (i >= t.size()) {
            if (!cursor_drawn && cursor_pos >= (int)t.size())
                draw_cursor(g, ox + px, oy + py, 8, line_h, mode, ox);
            break;
        }
        char ch = t[i];

        if (ch == '\n') {
            if (!cursor_drawn && (int)i == cursor_pos) {
                cursor_drawn = true;
                draw_cursor(g, ox + px, oy + py, 8, line_h, mode, ox);
            }
            px = 0; py += line_h; i++;
            continue;
        }
        if (ch == ' ') {
            if (!cursor_drawn && (int)i == cursor_pos) {
                cursor_drawn = true;
                draw_cursor(g, ox + px, oy + py, space_w, line_h, mode, ox);
            }
            px += space_w; i++;
            continue;
        }

        // Word: measure for wrap, then draw char-by-char.
        int32_t ww = 0;
        { size_t j = i;
          while (j < t.size() && t[j] != ' ' && t[j] != '\n') { ww += cw(g, t[j]); j++; } }

        if (px != 0 && px + ww > WRAP_W) {
            px = 0; py += line_h;
            if (py >= TEXT_H) break;
        }

        while (i < t.size() && t[i] != ' ' && t[i] != '\n') {
            int32_t w = cw(g, t[i]);
            if (!cursor_drawn && (int)i == cursor_pos) {
                cursor_drawn = true;
                draw_cursor(g, ox + px, oy + py, w, line_h, mode, ox);
            }
            char buf[2] = {t[i], '\0'};
            g.drawString(buf, ox + px, oy + py);
            px += w; i++;
        }
    }

    if (!cursor_drawn && cursor_pos >= 0)
        draw_cursor(g, ox + px, oy + py, 8, line_h, mode, ox);

    // Return the pixel height of the rendered content.
    // py is the top of the last line drawn; add one line_h to get the bottom.
    int32_t used_h = py + line_h;
    return used_h < TEXT_H ? used_h : TEXT_H;
}

// ---------------------------------------------------------------------------
// Status bar — always drawn directly to the display
// ---------------------------------------------------------------------------
static void draw_status(EditorMode mode, const char* msg = nullptr) {
    const char* label;
    uint32_t    label_col;
    switch (mode) {
        case EditorMode::kNormal:
            label = " NORMAL";  label_col = COL_LABEL_N; break;
        case EditorMode::kInsert:
            label = " INSERT";  label_col = COL_LABEL_I; break;
        case EditorMode::kCommandLineMode:
            label = " COMMAND"; label_col = COL_LABEL_C; break;
        default:
            label = "";         label_col = COL_TEXT;    break;
    }
    display.setFont(&fonts::DejaVu12);
    display.setTextSize(1);
    display.setTextDatum(lgfx::top_left);
    const int32_t fh = display.fontHeight();
    const int32_t ty = STATUS_Y + (STATUS_H - fh) / 2;

    display.fillRect(0, STATUS_Y, SCREEN_W, STATUS_H, COL_STATUS_BG);
    display.setTextColor(label_col, COL_STATUS_BG);
    display.drawString(label, 4, ty);
    if (msg) {
        display.setTextColor(COL_TEXT, COL_STATUS_BG);
        display.drawString(msg, SCREEN_W / 2, ty);
    }
}

// ---------------------------------------------------------------------------
// Dirty-rect detection — compare front vs back buffer pixel-by-pixel.
// Returns false if nothing changed (push can be skipped entirely).
// ---------------------------------------------------------------------------
static bool dirty_rect(const uint16_t* front, const uint16_t* back,
                       int32_t w, int32_t h,
                       int32_t* ox, int32_t* oy, int32_t* ow, int32_t* oh) {
    int32_t x0 = w, y0 = h, x1 = -1, y1 = -1;

    for (int32_t y = 0; y < h; y++) {
        const uint32_t* fp = (const uint32_t*)(front + y * w);
        const uint32_t* bp = (const uint32_t*)(back  + y * w);
        bool row_dirty = false;
        for (int32_t x2 = 0; x2 < w / 2; x2++) {
            if (fp[x2] != bp[x2]) {
                row_dirty = true;
                int32_t x  = x2 * 2;
                int32_t xa = (front[y * w + x]     != back[y * w + x])     ? x     : x + 1;
                int32_t xb = (front[y * w + x + 1] != back[y * w + x + 1]) ? x + 1 : x;
                if (xa < x0) x0 = xa;
                if (xb > x1) x1 = xb;
            }
        }
        if (row_dirty) {
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }

    if (x1 < 0) return false;
    *ox = x0;  *oy = y0;
    *ow = x1 - x0 + 1;
    *oh = y1 - y0 + 1;
    return true;
}

// ---------------------------------------------------------------------------
// Output interface
// ---------------------------------------------------------------------------

int Output::CurrentTimeInMillis() {
    return static_cast<int>(esp_timer_get_time() / 1000);
}
int Output::NextLine() { return next_line_start_; }
int Output::PrevLine() { return prev_line_start_; }

void Output::Init(Editor* ed) {
    editor_ = ed;
    // Allocate both frame buffers from PSRAM. Both start zeroed, matching the
    // black-filled display. Emit falls back to direct rendering on failure.
    s_buf[0].setColorDepth(16);
    s_buf[1].setColorDepth(16);
    s_buf[0].setPsram(true);
    s_buf[1].setPsram(true);
    s_bufs_ready = (s_buf[0].createSprite(WRAP_W, TEXT_H) != nullptr)
                && (s_buf[1].createSprite(WRAP_W, TEXT_H) != nullptr);

    if (s_bufs_ready) {
        s_buf[0].fillSprite(COL_BG);
        s_buf[1].fillSprite(COL_BG);
    }

    display.fillScreen(COL_BG);
    draw_status(EditorMode::kNormal);
}

void Output::Emit(const std::string& s, int cursor_pos, EditorMode mode) {
    if (!s_bufs_ready) {
        // Fallback: render directly to display (should not happen with 8 MB PSRAM).
        display.startWrite();
        render_to(display, TEXT_X, TEXT_Y, s, cursor_pos, mode,
                  &prev_line_start_, &next_line_start_);
        display.endWrite();
        draw_status(mode);
        return;
    }

    lgfx::LGFX_Sprite& front = s_buf[s_curr];
    lgfx::LGFX_Sprite& back  = s_buf[1 - s_curr];

    // Render new frame into front buffer (pure RAM, no SPI).
    render_to(front, 0, 0, s, cursor_pos, mode,
              &prev_line_start_, &next_line_start_);

    // Find the bounding box of pixels that changed vs the previous frame.
    int32_t dx, dy, dw, dh;
    bool changed = dirty_rect(
        (const uint16_t*)front.getBuffer(),
        (const uint16_t*)back.getBuffer(),
        WRAP_W, TEXT_H, &dx, &dy, &dw, &dh);

    if (changed) {
        // Push only the dirty rectangle — setClipRect limits the SPI transfer.
        display.setClipRect(TEXT_X + dx, TEXT_Y + dy, dw, dh);
        front.pushSprite(&display, TEXT_X, TEXT_Y);
        display.clearClipRect();
    }

    // Swap buffers: front becomes the new "previous frame" reference.
    s_curr = 1 - s_curr;

    draw_status(mode);
}

void Output::CommandLine(const std::list<char>& s) {
    std::string str(s.begin(), s.end());
    CommandLine(str);
}

void Output::CommandLine(const std::string& s) {
    display.setFont(&fonts::DejaVu12);
    display.setTextSize(1);
    display.setTextDatum(lgfx::top_left);
    const int32_t fh = display.fontHeight();
    const int32_t ty = STATUS_Y + (STATUS_H - fh) / 2;
    display.fillRect(0, STATUS_Y, SCREEN_W, STATUS_H, COL_STATUS_BG);
    display.setTextColor(COL_LABEL_C, COL_STATUS_BG);
    display.drawString(s.c_str(), 4, ty);
}

void Output::Command(const OutputCommands& cmd) {
    switch (cmd) {
        case OutputCommands::kSplash:
            draw_splash(&display);
            break;
        case OutputCommands::kCommandMode:
            draw_status(EditorMode::kNormal);
            break;
        case OutputCommands::kFlush:
            break;
    }
}

void Output::ProcessHandlers() {}
void Output::ProcessEvent(EventType ev) { (void)ev; }
