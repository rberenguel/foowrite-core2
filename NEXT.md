# Next steps

Scaffold is done: display works, BLE keyboard connects and delivers keycodes.
What follows is porting the editor from `../foowrite` and wiring everything together.

---

## 1. Keymap ✅

Ported as `main/keymap.h` + `main/keymap.cpp`.
- Both Colemak and QWERTY tables compiled in; runtime switch via `g_use_qwerty` bool (default Colemak)
- `KeyModifiers` consolidated here; `key_queue.h` includes `keymap.h`

---

## 2. Editor core ✅

Ported as `main/editor.h` + `main/editor.cpp`, with `main/editor_mode.h` and `main/output.hpp`.
- `Output` is a concrete stub (printf to serial) in `main/output.cpp`; display renderer replaces this in step 4
- `Editor::Init(Output*)` wired in `app_main`; key loop in `main.cpp` calls `ProcessKey` with batching
- SD save/load (`:w`, `:e`) stubbed with log messages until step 6
- `screendiffer.cc` not ported — not needed until display renderer

---

## 3. Test suite

Port `tests/` from foowrite (Google Test, builds on Mac with `-DBUILD_FOR_PICO=off`).
- `editor_stubs.cc` / `editor_stubs.hpp` — mock Output, `SendString()` helper
- `layout_map.h` — char-to-keycode table used by tests
- `test_movements.cpp`, `test_basic_writing.cc`

Tests run natively on the Mac; no flashing required. Add a `tests/` directory here and
a `build.mac` script mirroring foowrite's.

---

## 4. Display renderer (Core2 Output implementation)

Implement `Output` for the ILI9342C via LovyanGFX — this replaces `src/pico/gfx/gfx.cc`.

Key things to port/adapt:
- **`text()` two-pass algorithm** from `gfx.cc:27-223`:
  - Pass 1: measure word widths, compute line breaks, find cursor line, handle pagination
  - Pass 2: render text and cursor (underline in Normal mode, bar in Insert mode)
- **Word wrap** at 320px width (vs 128px on the GFX pack) — more lines per screen
- **Multiple fonts**: small (editor body), large (optional), monospace preferred
  - Suggested: `fonts::DejaVu18` for body, `fonts::DejaVu12` for status bar
- **Pagination**: `N_LINES_PER_SCREEN` will be larger (~10-12 lines); arrow keys navigate
- **Cursor styles**: underline (Normal), vertical bar (Insert), block optional
- **Backlight color** via `axp192_set_lcd_backlight()` — replaces the RGB LED:
  - Blue-ish tint: scanning / disconnected (not possible with single-channel DCDC3,
    so use screen background color instead: navy fill when disconnected)
  - Screen tint for modes: can use a colored status bar rather than backlight color
- **`Emit()`**: clear + redraw on every non-batched keypress
- **`CommandLine()`**: draw `:command` text at bottom of screen (y ≈ 220)
- **`ProcessEvent()`**: handle BLE connect/disconnect → update screen tint
- **`CurrentTimeInMillis()`**: use `esp_timer_get_time() / 1000`

No physical buttons on the Core2 (no GFX pack), so `ProcessHandlers()` can be a no-op
for now, or later wired to the capacitive touch buttons on the front.

---

## 5. FreeRTOS task wiring

Replace the `app_main` while-loop with a proper editor task pinned to Core 1:

```cpp
xTaskCreatePinnedToCore(editor_task, "editor", 8192, NULL, 5, NULL, 1);
```

The editor task:
```cpp
void editor_task(void *) {
    Editor editor(&output);
    editor.Load();  // :e equivalent on boot if file exists on SD
    while (true) {
        key_event_t evt;
        bool batched = uxQueueMessagesWaiting(g_key_queue) > 1;
        xQueueReceive(g_key_queue, &evt, portMAX_DELAY);
        char c = get_char_from_key(evt.keycode, &evt.modifiers);
        editor.ProcessKey(c, &evt.modifiers, batched);
    }
}
```

BLE task stays on Core 0 (already pinned via sdkconfig).

---

## 6. SD card file save/load

The Core2 SD slot shares the SPI bus with the display (MOSI=23, MISO=38, CLK=18, CS=4).
LovyanGFX sets `bus_shared=true` in `lgfx_config.h` for this reason.

Use the ESP-IDF SDMMC / SPI SD driver:
- Mount with `esp_vfs_fat_sdspi_mount()` at `/sd`
- Save: `:w filename` → open `/sd/filename.txt`, write document lines
- Load: `:e filename` → read `/sd/filename.txt`, parse by `\n`
- Replace foowrite's interrupt-safety wrappers with a FreeRTOS mutex

Leave for after the editor is working on screen.

---

## 7. Nice-to-haves (later)

- **Bonding**: optionally store bond keys in NVS so the keyboard reconnects without
  re-pairing. Controlled by a `:bond` command.
- **Multiple files**: `:ls` to list `/sd/*.txt`, tab completion on `:e`
- **Battery display**: read AXP192 ADC registers for battery voltage, show in status bar
- **Touch buttons**: the three capacitive buttons on the Core2 front panel could handle
  brightness, Bluetooth re-pair, or splash screen (mirrors foowrite's GFX pack buttons)
- **Word count**: `:wc` already in foowrite's editor, bring it along
