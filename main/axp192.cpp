#include "axp192.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "axp192";

static i2c_master_dev_handle_t s_dev = NULL;

// ---------------------------------------------------------------------------
// Low-level I2C helpers (new driver API)
// ---------------------------------------------------------------------------

static esp_err_t axp_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t axp_read(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

static esp_err_t axp_read32(uint8_t reg, uint32_t *val) {
    uint8_t buf[4];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, buf, 4, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;
    *val = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
    return ESP_OK;
}

static esp_err_t axp_set_bits(uint8_t reg, uint8_t mask) {
    uint8_t val;
    esp_err_t err = axp_read(reg, &val);
    if (err != ESP_OK) return err;
    return axp_write(reg, val | mask);
}

static esp_err_t axp_clear_bits(uint8_t reg, uint8_t mask) {
    uint8_t val;
    esp_err_t err = axp_read(reg, &val);
    if (err != ESP_OK) return err;
    return axp_write(reg, val & ~mask);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void axp192_init(i2c_master_bus_handle_t *out_bus_handle) {
    // --- I2C bus (new driver) ---
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port            = AXP192_I2C_PORT;
    bus_cfg.sda_io_num          = (gpio_num_t)AXP192_SDA;
    bus_cfg.scl_io_num          = (gpio_num_t)AXP192_SCL;
    bus_cfg.clk_source          = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt   = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    // --- AXP192 device ---
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = AXP192_ADDR;
    dev_cfg.scl_speed_hz    = 400000;

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev));

    ESP_LOGI(TAG, "I2C initialised (new driver), configuring AXP192...");

    // --- DCDC1: 3.35V — ESP32 core VDD (must stay on) ---
    // Voltage: 0.7V + N * 0.025V → 3.35V → N = 106 = 0x6A
    axp_write(AXP192_REG_DCDC1_VOLTAGE, 0x6A);

    // --- LDO2: 3.3V — LCD power rail (must stay on) ---
    // LDO2 voltage reg 0x28 bits[7:4]: 1.8V + N*0.1V → 3.3V → N=15=0xF
    // LDO3 voltage reg 0x28 bits[3:0]: 1.8V + N*0.1V → 3.0V → N=12=0xC (vibration motor)
    axp_write(AXP192_REG_LDO23_VOLTAGE, 0xFC);

    // --- GPIO4: LCD reset line — enable output, hold high ---
    axp_write(AXP192_REG_GPIO4_CTL, 0x84);   // GPIO4 as output
    axp_write(AXP192_REG_GPIO34_STATE, 0x02); // GPIO4 high (release reset)

    // --- Power output: enable DCDC1, LDO2, EXTEN ---
    // 0x45 = 0b01000101: EXTEN(6) | LDO2(2) | DCDC1(0)
    // LDO3 (vibration motor) intentionally OFF.
    // DCDC3 (backlight) enabled separately via axp192_set_lcd_backlight().
    axp_write(AXP192_REG_POWER_OUTPUT_CTL, 0x45);

    // --- Charging: 4.2V target, 360mA ---
    axp_write(AXP192_REG_CHARGE_CTL1, 0xA1);

    // --- PEK: short press 128ms, long press 1.5s ---
    axp_write(AXP192_REG_PEK, 0x0C);

    // --- ADC: enable all voltage/current measurements ---
    axp_write(AXP192_REG_ADC_ENABLE1, 0xFF);

    // --- Coulomb counter: enable ---
    axp_write(0xB8, 0x80);

    // --- VBUS: limit to 500mA ---
    axp_write(AXP192_REG_VBUS_IPSOUT_CTL, 0xA0);

    // Turn on backlight at full brightness
    axp192_set_lcd_backlight(255);

    ESP_LOGI(TAG, "AXP192 configured, LCD power on");

    // Short delay for rails to stabilise before SPI display init
    vTaskDelay(pdMS_TO_TICKS(10));

    if (out_bus_handle) {
        *out_bus_handle = bus_handle;
    }
}

void axp192_set_lcd_backlight(uint8_t brightness) {
    // Backlight on Core2 is DCDC3 (reg 0x27), NOT LDO2.
    // LDO2 is the LCD power rail and must stay on regardless.
    // Formula from M5Stack/LovyanGFX: voltage_reg = (brightness >> 3) + 72
    // brightness=0   → off (disable DCDC3)
    // brightness=255 → reg=0x67 ≈ 3.275V
    if (brightness == 0) {
        axp_clear_bits(AXP192_REG_POWER_OUTPUT_CTL, AXP192_DCDC3_EN);
        return;
    }
    uint8_t val = (uint8_t)((brightness >> 3) + 72);
    axp_write(AXP192_REG_DCDC3_VOLTAGE, val);
    axp_set_bits(AXP192_REG_POWER_OUTPUT_CTL, AXP192_DCDC3_EN);
}

void axp192_set_exten(bool enable) {
    if (enable)
        axp_set_bits(AXP192_REG_POWER_OUTPUT_CTL, AXP192_EXTEN_EN);
    else
        axp_clear_bits(AXP192_REG_POWER_OUTPUT_CTL, AXP192_EXTEN_EN);
}

void axp192_shutdown() {
    axp_set_bits(0x32, 0x80);  // POFF bit — AXP192 cuts all rails immediately
}

bool axp192_is_charging() {
    uint8_t val = 0;
    axp_read(0x00, &val);
    return (val & 0x04) != 0;  // reg 0x00 bit 2: charging indicator (M5Core2 AXP192.cpp)
}

// M5Core2 battery capacity: 390 mAh
#define BAT_CAPACITY_MAH 390.0f

// Coulomb counter: 65536 * 0.5 / 3600 / 25 = 0.3641 mAh per LSB (from M5Core2 AXP192.cpp)
#define COULOMB_LSB_MAH  (65536.0f * 0.5f / 3600.0f / 25.0f)

static bool  s_anchored       = false;
static int   s_bat_pct_anchor = 0;
static float s_coulomb_anchor = 0;
static float s_max_mv         = 0;   // set by axp192_set_bat_max_mv() from main

static float coulomb_net_mah() {
    uint32_t coin = 0, coout = 0;
    axp_read32(0xB0, &coin);
    axp_read32(0xB4, &coout);
    return (float)(coin - coout) * COULOMB_LSB_MAH;
}

float axp192_read_voltage_mv() {
    uint8_t h = 0, l = 0;
    axp_read(0x78, &h);
    axp_read(0x79, &l);
    return ((uint32_t)h << 4 | (l & 0x0F)) * 1.1f;
}

void axp192_set_bat_max_mv(float max_mv) {
    s_max_mv = max_mv;
}

int axp192_get_battery_pct() {
    if (!s_anchored) {
        float cur_mv = axp192_read_voltage_mv();
        int pct;
        if (s_max_mv > 0) {
            pct = (int)(cur_mv / s_max_mv * 100.0f);
        } else {
            pct = (cur_mv <= 3200.0f) ? 0
                : (cur_mv >= 4150.0f) ? 100
                : (int)((cur_mv - 3200.0f) / 9.5f);
        }
        if (pct > 100) pct = 100;
        if (pct < 0)   pct = 0;
        s_bat_pct_anchor = pct;
        s_coulomb_anchor = coulomb_net_mah();
        s_anchored       = true;
    }

    float delta_mah = coulomb_net_mah() - s_coulomb_anchor;
    int pct = s_bat_pct_anchor + (int)(delta_mah / BAT_CAPACITY_MAH * 100.0f);
    if (pct > 100) pct = 100;
    if (pct < 0)   pct = 0;
    return pct;
}
