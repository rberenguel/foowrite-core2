# Next steps

---

## ✅ Config file (`/sd/config.txt`) — done

Properties `layout: colemak|qwerty`, `brightness: 1-100`, `rotation: 1|-1`.
Applied at startup (`apply_config()` in `main.cpp:39`) and re-applied on every BLE
reconnect so that `:q` + reconnect picks up edits.
Version `0.1.0` is shown on the splash screen.

---

## ✅ `:e` with no argument → show file listing — done

Implemented in `editor.cpp:529`.  Replaces the document with a listing of all
`.txt` files on the SD card and drops into a fresh `:e ` prompt so the user can
type the filename to open directly.  (Marked risky in prior notes — verify on device.)

---

## ✅ 1. Dirty flag + filename in the modeline — done

`editor.h`: added `dirty_` field, `IsDirty()` and `GetFilename()` accessors.
`editor.cpp`: `dirty_ = true` on all insert/delete/enter operations; cleared on
successful `:w` and on `:e <file>` load.
`output.cpp`: `draw_status()` now accepts filename + dirty; shows `[+] filename`
centred between the mode label and battery.  `[+]` is rendered in `COL_LABEL_I`
(blue) when dirty.  `Output` caches the last values so the status bar stays correct
after ESC-to-Normal.

---

## ✅ 2. Key repeat for arrows and backspace — done

Root cause was in `ble_hid_host.cpp`: the `keycode != s_last_keycode` guard
swallowed repeat events when a keyboard re-sent the same keycode without an
intervening key-up report.  Fixed by whitelisting
`KEY_LEFT/RIGHT/UP/DOWN/BACKSPACE/DELETE` to bypass the deduplication check, so
all hardware repeats reach the editor queue.

---

## 3. Folders

Create, navigate, go up/back.  Needed once you have more than a handful of files.
Depends on `:e` listing being stable (item above).

**Approach**: extend `sd_list()` to also return subdirectory names (marked with `/`
suffix).  In the `:e` listing view, pressing Enter on a directory item changes the
working directory; pressing Enter on a file opens it.  Keep a path stack for `..`.

---

## 4. 3 buffers mapped to the physical buttons

The M5Stack Core2 has 3 capacitive touch zones at the bottom.

| Button | Buffer |
|--------|--------|
| Left   | `scratch.txt` — permanent scratchpad |
| Middle | current file (whatever was last opened) |
| Right  | `<current>-notes.txt` — auto-created on first tap |

Switching saves the current file if dirty (or to a `.swp` temp file) and loads
the target.  Only one buffer is in RAM at a time; we just track three filenames.

---

## 5. Markdown-ish rendering

Different colours for `#` headers (solarized rainbow by level), bold, italic.
Folded headers show as `>` / `>>` / `>>>` etc.

Requires touching `render_to()` in `output.cpp` to tokenise the line before
drawing it character-by-character (colour state machine).

---

## 6. Outline jumping mode

Jump between `#` headers.  Build on folder/buffer work (item 3/4) so navigation
feels consistent.  Essential for documents over ~5k words.

---

## 7. Non-modal editing (not critical)

Lightweight menu + non-modal option, selectable from config.  Requires a registry
of ex-commands so they work in both modes.  Low priority — the device targets
vim-fluent writers.
