# foowrite-core2

Vim-like editor for the M5Stack Core2, built with ESP-IDF (native C/C++).

Straight port (via Claude and Gemini) of the original [foowrite](https://github.com/rberenguel/foowrite) I wrote for the Pi Pico.

<img src="media/pic.jpg" width="600">

## Hardware

- M5Stack Core2 (ESP32, ILI9342C 320×240 display, AXP192 PMU)
- Bluetooth LE keyboard (pairs with any HID keyboard on boot)
- Micro-SD card for file storage

## Installing a pre-built release

Download the latest `foowrite-core2-VERSION.bin` from the
[Releases](../../releases) page, then flash it with
[esptool](https://github.com/espressif/esptool) — no ESP-IDF required.

**1. Install esptool**

```bash
pip install esptool
```

**2. Find the Core2's serial port**

Plug in the Core2 via USB-C, then:

```bash
# macOS
ls /dev/cu.usbserial-*

# Linux
ls /dev/ttyUSB*
```

**3. Flash**

```bash
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX \
  --baud 921600 write_flash 0x0 foowrite-core2-VERSION.bin
```

Replace `/dev/cu.usbserial-XXXX` with the port found above and `VERSION` with
the release you downloaded.  The device will reboot into foowrite immediately
after flashing.

---

## Building from source

### Toolchain setup (macOS, one-time)

```bash
brew install cmake ninja dfu-util ccache
```

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.3.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32
```

Add to `~/.zshrc` for convenience:

```zsh
alias get_idf='. ~/esp/esp-idf/export.sh'
```

### Build

```bash
get_idf   # activate ESP-IDF environment
cd /path/to/foowrite-core2
idf.py set-target esp32   # only needed once
idf.py build
```

### Flash (with IDF)

```bash
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

### Create a release binary

```bash
./release.sh
# writes ~/Downloads/foowrite-core2-VERSION.bin
```

### Run tests (host, no device needed)

```bash
cd tests && cmake -B build && cmake --build build && ctest --test-dir build
```

## Battery indicator

The battery percentage is shown in two places:

**Splash screen** (top-right corner) — displayed before BLE scanning starts, so the
load is lower and the reading is closer to true state of charge.

**Status bar** (bottom-right, visible while editing) — displayed under full load
(ESP32 + BLE + display), so the reading is typically 10–15% lower than actual.

| Appearance | Meaning |
|------------|---------|
| `82%` green | Charging via USB |
| `~82%` white/grey | On battery, reading is approximate (`~` = under load) |
| `~18%` red | Low battery: ≤ 20% in editor (under load), ≤ 30% on splash |

The voltage-based percentage is a linear approximation of the LiPo discharge curve
and is inherently noisy under varying load.  Use the splash screen value (power on
without connecting the keyboard) for the most consistent readings when tracking
battery drain over time.  Power off via `:qq` or a long press of the physical button.

---

## Project structure

```
foowrite-core2/
├── main/
│   ├── main.cpp          # Entry point, BLE + editor loop
│   ├── editor.h/cpp      # Vim-like editor core
│   ├── output.hpp/cpp    # LovyanGFX display renderer
│   ├── keymap.h/cpp      # Colemak / QWERTY HID keymap
│   ├── axp192.h/cpp      # AXP192 PMU (backlight, battery, power-off)
│   ├── sd_storage.h/cpp  # SD card save / load / config
│   ├── splash.h/cpp      # Generative mountains splash screen
│   ├── version.h         # FOOWRITE_VERSION string
│   └── lgfx_config.h     # LovyanGFX display configuration
├── tests/                # Host-side Google Test suite
├── release.sh            # Produces merged flash binary for distribution
└── components/
    └── LovyanGFX/        # Display library (git submodule)
```
