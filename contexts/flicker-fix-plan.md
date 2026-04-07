# Flicker Fix Plan — Pixel-Level Double Buffer + Dirty Rect

## Root cause

The original foowrite had no flicker because the GFX pack display is 128×64 at
1 bpp = 1 KB.  A full push takes ~1 ms and completes within one 60 Hz scan cycle.
`screendiffer.cc` was only ever used for the Badger e-ink partial-refresh path.

The Core2 display is 320×240 at 16 bpp = 136 KB per frame.  At 40 MHz SPI that is
~27 ms — nearly two full scan cycles.  Any write that takes longer than one scan
cycle produces visible tearing; a full-frame push always does.

The sprite approach (current) eliminates the "black flash" but not the tearing,
because `pushSprite` still sweeps 136 KB top-to-bottom over 27 ms while the
display is scanning the same memory in the other direction.

## Fix: two frame buffers + per-pixel diff → push only the dirty bounding box

For a single typed character the dirty rect is roughly one character cell
(~12 × 22 px = 528 bytes ≈ 85 μs at 40 MHz).  Imperceptible.

### Data structures

```cpp
// In output.cpp, replacing the single s_canvas:

static lgfx::LGFX_Sprite s_buf[2];          // front & back buffers
static bool               s_bufs_ready = false;
static int                s_curr = 0;        // index of buffer to render into
                                              // s_buf[1 - s_curr] is previous frame

// In Output::Init:
s_buf[0].setColorDepth(16);
s_buf[1].setColorDepth(16);
s_bufs_ready = (s_buf[0].createSprite(WRAP_W, TEXT_H) != nullptr)
             && (s_buf[1].createSprite(WRAP_W, TEXT_H) != nullptr);
// Both buffers start zeroed — matches the black-filled display after fillScreen.
```

Memory: 2 × 314 × 217 × 2 = ~272 KB PSRAM.  Fine with 8 MB available.

### Dirty-rect detection

```cpp
// Scan both buffers, find bounding box of differing pixels.
// Returns false if nothing changed (skip the push entirely).
static bool dirty_rect(const uint16_t* front, const uint16_t* back,
                       int32_t w, int32_t h,
                       int32_t* ox, int32_t* oy, int32_t* ow, int32_t* oh) {
    int32_t x0 = w, y0 = h, x1 = -1, y1 = -1;

    // Compare two pixels at a time as uint32 for speed.
    // PSRAM burst reads make sequential access ~4–6 ms for 136 KB worst-case,
    // but typical edits change only a few rows so the inner scan exits early.
    for (int32_t y = 0; y < h; y++) {
        const uint32_t* fp = (const uint32_t*)(front + y * w);
        const uint32_t* bp = (const uint32_t*)(back  + y * w);
        bool row_dirty = false;
        for (int32_t x2 = 0; x2 < w / 2; x2++) {
            if (fp[x2] != bp[x2]) {
                row_dirty = true;
                int32_t x = x2 * 2;
                // Narrow to the exact pixel within the uint32
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

    if (x1 < 0) return false;   // no change
    *ox = x0;  *oy = y0;
    *ow = x1 - x0 + 1;
    *oh = y1 - y0 + 1;
    return true;
}
```

### Emit — updated

```cpp
void Output::Emit(const std::string& s, int cursor_pos, EditorMode mode) {
    if (!s_bufs_ready) {
        // Fallback (should not happen with 8 MB PSRAM)
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
        // Push only the dirty rectangle — setClipRect limits the SPI transfer
        // so LovyanGFX sets a smaller setAddrWindow and sends fewer bytes.
        display.setClipRect(TEXT_X + dx, TEXT_Y + dy, dw, dh);
        front.pushSprite(&display, TEXT_X, TEXT_Y);
        display.clearClipRect();
    }

    // Swap buffers: front becomes the new "previous frame" reference.
    s_curr = 1 - s_curr;

    draw_status(mode);
}
```

No `memcpy` needed — the swap is free (flip one int).  The back buffer now holds
the correct previous frame for the next diff.

### render_to signature

Keep exactly as-is (returns used height, takes `lgfx::LovyanGFX& g`).
`LGFX_Sprite` inherits from `lgfx::LovyanGFX` so it can be passed directly.

### Status bar

`draw_status` always writes directly to `display` (not the sprite), same as now.
It is outside the diffed area so no change needed there.

## Files to change

| File | Change |
|------|--------|
| `main/output.cpp` | Replace `s_canvas`/`s_canvas_ready`/`s_prev_push_h` with `s_buf[2]`/`s_bufs_ready`/`s_curr`; add `dirty_rect()`; update `Emit` and `Init` as above |

No other files need changes.

## Expected performance

| Scenario | Dirty rect | Bytes pushed | Time @ 40 MHz |
|----------|-----------|-------------|---------------|
| Single char typed | ~12 × 22 px | ~528 B | ~85 μs |
| Cursor moved 1 word | ~80 × 22 px | ~3.5 KB | ~560 μs |
| Full page reflow | 314 × 217 px | 136 KB | ~27 ms (same as now) |
| Nothing changed | — | 0 B | 0 μs |

Full page reflow only happens on pagination scroll, which is rare and tolerable.
All common editing operations become imperceptible.

## Notes

- `getBuffer()` is public in LovyanGFX v1 (`LGFX_Sprite::getBuffer()` returns
  `void*`; cast to `uint16_t*` for 16-bpp sprites).
- The dirty-rect scan of 136 KB takes roughly 2–5 ms on ESP32 with PSRAM burst
  reads.  This is paid every keypress regardless of change size, but is far
  cheaper than the 27 ms SPI push it replaces.
- If PSRAM reads prove too slow for the scan, optimise by comparing row sums
  first (cheap), then doing the full comparison only on rows whose sum changed.
