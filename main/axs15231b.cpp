// AXS15231B QSPI display driver — ESP-IDF native, no Arduino.
// Ported from rsvpnano (ionutdecebal/rsvpnano), Apache-2.0.

#include "axs15231b.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "axs15231b";

// Waveshare ESP32-S3-Touch-LCD-3.49 display pins
static constexpr gpio_num_t PIN_CS   = GPIO_NUM_9;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_10;
static constexpr gpio_num_t PIN_D0   = GPIO_NUM_11;
static constexpr gpio_num_t PIN_D1   = GPIO_NUM_12;
static constexpr gpio_num_t PIN_D2   = GPIO_NUM_13;
static constexpr gpio_num_t PIN_D3   = GPIO_NUM_14;
static constexpr gpio_num_t PIN_RST  = GPIO_NUM_21;

static constexpr int SPI_FREQ_HZ       = 40000000;
static constexpr int SEND_BUF_PIXELS   = 0x4000;  // 16K pixels per DMA chunk

struct LcdCmd {
    uint8_t  cmd;
    uint8_t  data[4];
    uint8_t  len;
    uint16_t delay_ms;
};

// Minimal 5-command init sequence confirmed working on this hardware (from rsvpnano)
static const LcdCmd kInitCmds[] = {
    {0x11, {0x00}, 0, 100},  // SLPOUT
    {0x36, {0x00}, 1,   0},  // MADCTL — no rotation (portrait native)
    {0x3A, {0x55}, 1,   0},  // COLMOD — 16-bit colour
    {0x11, {0x00}, 0, 100},  // SLPOUT (repeated as in reference implementation)
    {0x29, {0x00}, 0, 100},  // DISPON
};

static spi_device_handle_t s_spi      = nullptr;
static bool                s_bus_ready = false;

static void send_cmd(uint8_t cmd, const uint8_t *data, uint32_t len) {
    if (!s_spi) return;
    spi_transaction_t t = {};
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd   = 0x02;
    t.addr  = static_cast<uint32_t>(cmd) << 8;
    if (len) {
        t.tx_buffer = data;
        t.length    = len * 8;
    }
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void set_column_window(uint16_t x1, uint16_t x2) {
    const uint8_t d[] = {
        static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1),
        static_cast<uint8_t>(x2 >> 8), static_cast<uint8_t>(x2),
    };
    send_cmd(0x2A, d, sizeof(d));
}

static void set_row_window(uint16_t y1, uint16_t y2) {
    const uint8_t d[] = {
        static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1),
        static_cast<uint8_t>(y2 >> 8), static_cast<uint8_t>(y2),
    };
    send_cmd(0x2B, d, sizeof(d));
}

void axs15231b_init(void) {
    // Reset sequence
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(30));

    if (!s_bus_ready) {
        spi_bus_config_t bus = {};
        bus.data0_io_num     = PIN_D0;
        bus.data1_io_num     = PIN_D1;
        bus.sclk_io_num      = PIN_SCLK;
        bus.data2_io_num     = PIN_D2;
        bus.data3_io_num     = PIN_D3;
        bus.max_transfer_sz  = SEND_BUF_PIXELS * static_cast<int>(sizeof(uint16_t)) + 8;
        bus.flags            = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;

        spi_device_interface_config_t dev = {};
        dev.command_bits  = 8;
        dev.address_bits  = 24;
        dev.mode          = 3;  // CPOL=1, CPHA=1
        dev.clock_speed_hz = SPI_FREQ_HZ;
        dev.spics_io_num  = PIN_CS;
        dev.flags         = SPI_DEVICE_HALFDUPLEX;
        dev.queue_size    = 10;

        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));
        ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &s_spi));
        s_bus_ready = true;
    }

    for (const auto &c : kInitCmds) {
        send_cmd(c.cmd, c.data, c.len);
        if (c.delay_ms) vTaskDelay(pdMS_TO_TICKS(c.delay_ms));
    }

    ESP_LOGI(TAG, "AXS15231B init complete");
}

void axs15231b_push_colors(uint16_t x, uint16_t y,
                           uint16_t width, uint16_t height,
                           const uint16_t *data) {
    if (!s_spi || !data || !width || !height) return;

    set_column_window(x, x + width - 1);

    size_t         remaining  = static_cast<size_t>(width) * height;
    const uint16_t *cursor    = data;
    bool            first     = true;

    while (remaining > 0) {
        size_t chunk = remaining < static_cast<size_t>(SEND_BUF_PIXELS)
                       ? remaining : static_cast<size_t>(SEND_BUF_PIXELS);

        spi_transaction_ext_t t = {};
        if (first) {
            t.base.flags = SPI_TRANS_MODE_QIO;
            t.base.cmd   = 0x32;
            // y==0 → RAMWR (resets write pointer to row 0 of current CASET window)
            // y!=0 → RAMWR-continue (advances within same window)
            t.base.addr  = (y == 0) ? 0x002C00u : 0x003C00u;
            first = false;
        } else {
            t.base.flags = SPI_TRANS_MODE_QIO
                         | SPI_TRANS_VARIABLE_CMD
                         | SPI_TRANS_VARIABLE_ADDR
                         | SPI_TRANS_VARIABLE_DUMMY;
            t.command_bits = 0;
            t.address_bits = 0;
            t.dummy_bits   = 0;
        }
        t.base.tx_buffer = cursor;
        t.base.length    = chunk * 16;

        ESP_ERROR_CHECK(spi_device_polling_transmit(
            s_spi, reinterpret_cast<spi_transaction_t *>(&t)));

        remaining -= chunk;
        cursor    += chunk;
    }
}
