### Design Document: Vim-Like Editor for M5Stack Core2 (ESP-IDF Native)

You can pass this directly to Claude to establish the architecture and requirements for the code generation.

#### 1. System Architecture & Concurrency
The system will run bare-metal C/C++ on the ESP-IDF framework, utilizing FreeRTOS for task management to ensure the UI remains responsive while handling Bluetooth events.
* **Core 0 (Protocol Core):** Dedicated to the NimBLE Bluetooth stack and background Wi-Fi/Radio tasks.
* **Core 1 (Application Core):** Dedicated to the editor logic, PMU management, and display rendering.
* **Inter-Process Communication:** A FreeRTOS Queue (`xQueueCreate`) will pass `hid_keyboard_report_t` structs or parsed `char` data from the BLE callback function on Core 0 to the main editor task on Core 1.

#### 2. Bluetooth Stack (NimBLE HID Host)
* **Library:** ESP-IDF native `NimBLE` stack (significantly lighter on memory than Bluedroid).
* **Role:** BLE HID Central/Host.
* **Implementation Details:**
    * Initialize NimBLE and start scanning for devices exposing the HID service (UUID `0x1812`).
    * Connect and pair (handle SMP security if the keyboard requires bonding).
    * Register a notification callback on the HID Report characteristic.
    * Parse the modifier keys (Shift, Ctrl, Alt) and keycodes from the standard 8-byte HID report, map them to ASCII/control characters, and push them to the FreeRTOS Queue.

#### 3. Hardware & Power Management (AXP192)
The M5Stack Core2 requires explicit I2C initialization to power its internal components. This must be handled before bringing up the display.
* **Internal I2C Pins:** SDA = 21, SCL = 22.
* **Initialization:** The AXP192 PMU must be configured on boot to enable the LCD backlight (LDO2 or LDO3 depending on exact board revision) and logic power. Without this, the screen will remain black.

#### 4. Display & UI Rendering
* **Driver:** The Core2 uses an ILI9342C display controller (SPI: MOSI = 23, MISO = 38, CLK = 18, CS = 5, DC = 15).
* **Library:** `LovyanGFX` (highly optimized for the ESP32 and supports ESP-IDF natively) or `LVGL` (for a more complex widget-based UI). For a raw terminal/vim-like interface, `LovyanGFX` acting as a fast framebuffer for text rendering is recommended.

#### 5. Editor Core Logic
* Ported directly from the Pi Pico C++ source.
* Replace standard I/O polling with a blocking FreeRTOS queue read (`xQueueReceive(queue, &char_buffer, portMAX_DELAY)`) in the main editor loop. This ensures the loop yields CPU time to other tasks when the user is not actively typing.

---

### Development Setup Instructions

Run the following commands to install the native ESP-IDF toolchain. 

**1. Install Prerequisites (requires Homebrew)**
```bash
brew install cmake ninja dfu-util ccache
```

**2. Download ESP-IDF**
```bash
mkdir -p ~/esp
cd ~/esp
# Cloning the stable v5.3 branch
git clone -b v5.3.5 --recursive https://github.com/espressif/esp-idf.git
```

**3. Install the Toolchain for the Core2**
```bash
cd ~/esp/esp-idf
# The Core2 uses the standard ESP32 
./install.sh esp32
```

**4. Set Up Environment Variables**
You must run this command to add the tools to your path every time you open a new session to work on this project (or you can add it as an alias to your `~/.zshrc`).
```bash
. ./export.sh
```

**5. Start Your Project**
Once the environment is sourced, you can copy a template to start building the editor:
```bash
cd ~/esp
cp -r esp-idf/examples/get-started/hello_world ./vim_editor
cd vim_editor
idf.py set-target esp32
idf.py menuconfig

# Build, flash, and monitor (replace /dev/cu.usbserial-XXXX with your specific port)
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```