# foowrite-core2

Vim-like editor for the M5Stack Core2, built with ESP-IDF (native C/C++).

Straight port (via Claude and Gemini) of the original [foowrite](https://github.com/rberenguel/foowrite) I wrote for the Pi Pico

> [!WARNING]
> Still not functional.
> - Splash screen ✓
> - BLE pairing ✓
> - VIM-like editor wired ✓
> - Text on screen ⤬
> - Word wrap, pagination ⤬
> - Saving to SD ⤬
> - Settings, controls, touch ⤬

## Hardware

- M5Stack Core2 (ESP32, ILI9342C 320×240 display, AXP192 PMU)
- Bluetooth LE keyboard (pairs with any HID keyboard on boot)
- Micro-SD card for file storage (TODO)

## Toolchain Setup (macOS)

Do this once.

**1. Prerequisites**

```bash
brew install cmake ninja dfu-util ccache
```

**2. Download ESP-IDF v5.3**

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.3.5 --recursive https://github.com/espressif/esp-idf.git
```

**3. Install the ESP32 toolchain**

```bash
cd ~/esp/esp-idf
./install.sh esp32
```

**4. Add to shell (optional but convenient)**

Add this alias to `~/.zshrc` so you can activate the environment easily:

```zsh
alias get_idf='. ~/esp/esp-idf/export.sh'
```

Then activate with `get_idf` whenever you open a new terminal to work on this project.

## Building

```bash
# Activate ESP-IDF environment first (or run get_idf if you set the alias)
. ~/esp/esp-idf/export.sh

cd /path/to/foowrite-core2
idf.py set-target esp32   # only needed once
idf.py build
```

## Flashing

Plug in the Core2 via USB-C, find its port:

```bash
ls /dev/cu.*
```

Then flash and open the serial monitor:

```bash
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

## Project Structure

```
foowrite-core2/
├── main/
│   ├── main.cpp          # Entry point
│   ├── axp192.h/cpp      # PMU init (powers display, backlight)
│   └── lgfx_config.h     # LovyanGFX display configuration
└── components/
    └── LovyanGFX/        # Display library (git submodule)
```
