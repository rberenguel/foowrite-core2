// Copyright 2025 Ruben Berenguel

// Generative-mountains splash screen, adapted from foowrite (Pico/GFX pack).
// Original: 128×64 mono.  This version: 320×240 colour (ILI9342C / LovyanGFX).

#include "splash.h"
#include "version.h"
#include "axp192.h"

#include <math.h>
#include <vector>

#include "esp_random.h"
#include "esp_timer.h"

// ---------------------------------------------------------------------------
// PRNG — same LCG as the original; seeded from hardware RNG at call time
// ---------------------------------------------------------------------------

static int s_seed = 42;

static int fastrand(int x) {
    s_seed = s_seed * 0x343fd + 0x269ec3;
    return (int)(((s_seed >> 16) & 0x7fff) * x >> 15);
}

static int hrand(int n) {
    if (n <= 0) return 0;
    return (int)(esp_random() % (unsigned)n);
}

// ---------------------------------------------------------------------------
// Geometry helpers (identical to original)
// ---------------------------------------------------------------------------

struct Pt { int x, y; };

static Pt r45(Pt pt) {
    return { (int)((pt.x - pt.y) / 1.4142f),
             (int)((pt.x + pt.y) / 1.4142f) };
}

static Pt ir45(Pt pt) {
    return { (int)((pt.x + pt.y) / 1.4142f),
             (int)((pt.y - pt.x) / 1.4142f) };
}

// ---------------------------------------------------------------------------
// Coordinate scaling: original 128×64 → Core2 320×240
//
// Mountains occupy the top GROUND_Y pixels; bottom 50px are left for status.
// ---------------------------------------------------------------------------

static const int SCREEN_W  = 320;
static const int SCREEN_H  = 240;
static const int GROUND_Y  = 190;   // y-coordinate of mountain base line
static const int STATUS_Y  = 198;   // first line available for caller text

static const float SX = SCREEN_W / 128.0f;        // ≈ 2.5
static const float SY = GROUND_Y / 64.0f;         // ≈ 2.97

static inline int sx(int x) { return (int)(x * SX + 0.5f); }
static inline int sy(int y) { return (int)(y * SY + 0.5f); }

// ---------------------------------------------------------------------------
// Main draw function
// ---------------------------------------------------------------------------

void draw_splash(LGFX* display) {
    // Seed from hardware RNG + timer for variety
    s_seed = (int)((esp_timer_get_time() ^ (int64_t)esp_random()) & 0x7FFFFFFF);
    fastrand(hrand(100) + 1);  // jiggle

    display->fillScreen(TFT_BLACK);

    // -----------------------------------------------------------------------
    // Mountain silhouette
    // -----------------------------------------------------------------------
    std::vector<Pt> peaks;
    int x1 = 15 + fastrand(20);
    int y1 = 5  + fastrand(10);
    int xS = fastrand(20);
    Pt peak1 = {x1,              y1};
    Pt peak2 = {x1 + 50 - xS,   y1 - 7};
    Pt peak3 = {peak2.x + 35,    peak2.y + xS};
    peaks.push_back(peak1);
    peaks.push_back(peak2);
    peaks.push_back(peak3);

    // Build the ground-level range that forms the silhouette outline
    std::vector<Pt> range;
    for (int i = 0; i < (int)peaks.size(); i++) {
        Pt pt = peaks[i];

        if (i == 0) {
            Pt p = {pt.x - 64 + pt.y, 64};
            if (p.x < 0) { p.y += p.x; p.x = 1; }
            range.push_back(p);
        }
        if (i > 0) {
            Pt prev  = peaks[i - 1];
            Pt r1    = r45(prev);
            Pt r2    = r45(pt);
            Pt inter = ir45({r1.x, r2.y});
            range.push_back(inter);
            // Small descent line from peak (scaled up)
            display->drawLine(sx(pt.x), sy(pt.y),
                              sx(pt.x + 3), sy(pt.y + 3), TFT_WHITE);
        }
        range.push_back(pt);

        if (i == (int)peaks.size() - 1) {
            Pt p = {pt.x + 64 - pt.y, 64};
            if (p.x > 128) { p.y += (127 - p.x); p.x = 127; }
            display->drawPixel(sx(pt.x),     sy(pt.y),     TFT_WHITE);
            display->drawPixel(sx(pt.x),     sy(pt.y) + 1, TFT_WHITE);
            range.push_back(p);
        }
    }

    // Draw the outline (double-width for readability on larger screen)
    for (int i = 1; i < (int)range.size(); i++) {
        Pt p = range[i - 1];
        Pt q = range[i];
        display->drawLine(sx(p.x),     sy(p.y), sx(q.x),     sy(q.y), TFT_WHITE);
        display->drawLine(sx(p.x) + 1, sy(p.y), sx(q.x) + 1, sy(q.y), TFT_WHITE);
    }

    // Sub-peaks hanging below each main peak
    for (const Pt& pt : peaks) {
        Pt sp1  = {pt.x - 6,       pt.y + 18};
        Pt sp2  = {sp1.x - 6,      sp1.y + 9};
        Pt sp2w = {sp1.x + 4,      sp1.y + 3};
        Pt sp3  = {sp2.x - 2,      sp2.y + 6};
        Pt sp3w = {sp2.x + 3,      sp2.y + 2};
        display->drawLine(sx(pt.x),   sy(pt.y),   sx(sp1.x),  sy(sp1.y),  TFT_WHITE);
        display->drawLine(sx(sp1.x),  sy(sp1.y),  sx(sp2.x),  sy(sp2.y),  TFT_WHITE);
        display->drawLine(sx(sp1.x),  sy(sp1.y),  sx(sp2w.x), sy(sp2w.y), TFT_WHITE);
        display->drawLine(sx(sp2.x),  sy(sp2.y),  sx(sp3w.x), sy(sp3w.y), TFT_WHITE);
        display->drawLine(sx(sp2.x),  sy(sp2.y),  sx(sp3.x),  sy(sp3.y),  TFT_WHITE);
    }

    // -----------------------------------------------------------------------
    // "foowrite" title — centred, just above half-screen height
    // -----------------------------------------------------------------------
    display->setFont(&fonts::FreeSansBold12pt7b);
    display->setTextDatum(lgfx::baseline_left);
    int tw = display->textWidth("foowrite");
    int tx = (SCREEN_W - tw) / 2;
    int ty = 95;                      // top of box; baseline at ty+20 ≈ y=115
    display->fillRect(tx - 6, ty, tw + 12, 26, TFT_BLACK);
    display->setTextColor(TFT_WHITE, TFT_BLACK);
    display->setCursor(tx, ty + 20);
    display->print("foowrite");

    // Version — smaller font, centred directly below the title
    display->setFont(&fonts::Font0);
    display->setTextSize(1);
    display->setTextDatum(lgfx::top_center);
    display->setTextColor(TFT_WHITE, TFT_BLACK);
    display->drawString(FOOWRITE_VERSION, SCREEN_W / 2, ty + 28);

    // -----------------------------------------------------------------------
    // Random inspirational quote — black box overlaid on lower mountains
    // -----------------------------------------------------------------------
    int threshold = hrand(10);

    display->setFont(&fonts::FreeSans9pt7b);  // ~14px line height
    const int QL = 155;    // top of quote area
    const int LS = 17;     // line spacing

    if (threshold < 6) {
        display->fillRect(0, QL, SCREEN_W, LS * 2 + 4, TFT_BLACK);
        display->setTextColor(TFT_WHITE, TFT_BLACK);
        display->setCursor(24, QL + 13);
        display->print("The mountains are calling");
        display->setCursor(24, QL + 13 + LS);
        display->print("    and I must write");
    } else if (threshold < 8) {
        display->fillRect(0, QL - LS, SCREEN_W, LS * 4 + 4, TFT_BLACK);
        display->setTextColor(TFT_WHITE, TFT_BLACK);
        display->setCursor(24, QL - LS + 13);
        display->print("Ever tried. Ever failed.");
        display->setCursor(24, QL - LS + 13 + LS);
        display->print(" No matter.");
        display->setCursor(24, QL - LS + 13 + LS * 2);
        display->print("Try Again. Fail again.");
        display->setCursor(24, QL - LS + 13 + LS * 3);
        display->print(" Fail better.");
    } else {
        display->fillRect(0, QL, SCREEN_W, LS * 2 + 4, TFT_BLACK);
        display->setTextColor(TFT_WHITE, TFT_BLACK);
        display->setCursor(24, QL + 13);
        display->print("Not all those who wander");
        display->setCursor(24, QL + 13 + LS);
        display->print("                are lost");
    }

    // -----------------------------------------------------------------------
    // Bluetooth logo — lower-right corner, blue
    // The symbol is a vertical stem with two right-pointing chevrons:
    //   top → upper-right → mid → lower-right → bottom  (and back through mid)
    // -----------------------------------------------------------------------
    const uint32_t BLUE = 0x5599FF;  // light cornflower blue (RGB888)
    draw_bt_icon(display, BLUE);

    // Battery percentage — top-right, small font
    // Green when charging, red when ≤30% (lower threshold than editor: less
    // load here so the voltage reading is closer to true state of charge),
    // white otherwise.  ~ prefix indicates approximate reading under load.
    int  bat_pct  = axp192_get_battery_pct();
    bool charging = axp192_is_charging();
    char bat_str[8];
    if (bat_pct < 0)
        snprintf(bat_str, sizeof(bat_str), charging ? "?%%" : "~?%%");
    else
        snprintf(bat_str, sizeof(bat_str), charging ? "%d%%" : "~%d%%", bat_pct);
    uint32_t bat_col = charging                        ? 0x859900   // solarized green
                     : (bat_pct >= 0 && bat_pct <= 30) ? 0xDC322F   // solarized red
                                                       : 0xFFFFFF;  // white
    display->setFont(&fonts::Font0);
    display->setTextSize(1);
    display->setTextDatum(lgfx::top_right);
    display->setTextColor(bat_col, TFT_BLACK);
    display->drawString(bat_str, SCREEN_W - 4, 4);
}

void draw_bt_icon(LGFX* display, uint32_t color) {
    const int BX = 306;   // stem x
    const int BY = 216;   // top of logo
    const int BH = 16;    // total height
    const int BW = 8;     // arm reach to the right
    const int BM = BY + BH / 2;  // mid y

    // Stem
    display->drawLine(BX, BY, BX, BY + BH, color);
    display->drawLine(BX + 1, BY, BX + 1, BY + BH, color);
    // Upper chevron: top → upper-right → mid
    display->drawLine(BX, BY,  BX + BW, BM - BH / 4, color);
    display->drawLine(BX + BW, BM - BH / 4, BX, BM,  color);
    // Lower chevron: mid → lower-right → bottom
    display->drawLine(BX, BM,  BX + BW, BM + BH / 4, color);
    display->drawLine(BX + BW, BM + BH / 4, BX, BY + BH, color);
}
