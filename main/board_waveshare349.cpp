// Board support for Waveshare ESP32-S3-Touch-LCD-3.49
// Backlight: LEDC PWM on GPIO8 (active-low)
// Battery:   ADC1_CH3 / GPIO4 via 1:3 voltage divider, gated by TCA9554 IO1
// Power hold: TCA9554 IO6 keeps the power rail alive on battery

#include "board.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/i2c_master.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "board_ws349";

// ---- Backlight (active-low PWM) -------------------------------------------
static constexpr gpio_num_t          PIN_BACKLIGHT  = GPIO_NUM_8;
static constexpr ledc_timer_t        LEDC_TIMER     = LEDC_TIMER_0;
static constexpr ledc_channel_t      LEDC_CHANNEL   = LEDC_CHANNEL_0;
static constexpr ledc_mode_t         LEDC_MODE      = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_bit_t    LEDC_RESOLUTION = LEDC_TIMER_8_BIT;
static constexpr uint32_t            LEDC_FREQ_HZ   = 50000;

static void backlight_init() {
    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_MODE;
    timer.duty_resolution = LEDC_RESOLUTION;
    timer.timer_num       = LEDC_TIMER;
    timer.freq_hz         = LEDC_FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {};
    ch.gpio_num   = PIN_BACKLIGHT;
    ch.speed_mode = LEDC_MODE;
    ch.channel    = LEDC_CHANNEL;
    ch.timer_sel  = LEDC_TIMER;
    ch.duty       = 255;    // full off (active-low: duty 255 = 0% output = off)
    ch.hpoint     = 0;
    ledc_channel_config(&ch);
}

// ---- TCA9554 I/O expander --------------------------------------------------
static constexpr gpio_num_t     PIN_SDA          = GPIO_NUM_47;
static constexpr gpio_num_t     PIN_SCL          = GPIO_NUM_48;
static constexpr uint8_t        TCA9554_ADDR     = 0x20;
static constexpr uint8_t        TCA9554_OUT_REG  = 0x01;
static constexpr uint8_t        TCA9554_CFG_REG  = 0x03;
static constexpr uint8_t        TCA9554_SYS_EN   = 6;   // IO6: power hold
static constexpr uint8_t        TCA9554_BAT_GATE = 1;   // IO1: battery ADC gate

static i2c_master_bus_handle_t  s_i2c_bus  = nullptr;
static i2c_master_dev_handle_t  s_tca_dev  = nullptr;

static bool tca_write(uint8_t reg, uint8_t val) {
    if (!s_tca_dev) return false;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_tca_dev, buf, 2, 20) == ESP_OK;
}

static bool tca_read(uint8_t reg, uint8_t &val) {
    if (!s_tca_dev) return false;
    return i2c_master_transmit_receive(s_tca_dev, &reg, 1, &val, 1, 20) == ESP_OK;
}

static bool tca_set_output_pin(uint8_t pin, bool high) {
    uint8_t out = 0;
    if (!tca_read(TCA9554_OUT_REG, out)) return false;
    if (high) out |=  (1u << pin);
    else      out &= ~(1u << pin);
    if (!tca_write(TCA9554_OUT_REG, out)) return false;

    uint8_t cfg = 0xFF;
    if (!tca_read(TCA9554_CFG_REG, cfg)) return false;
    cfg &= ~(1u << pin);  // set as output
    return tca_write(TCA9554_CFG_REG, cfg);
}

static void tca9554_init() {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port     = I2C_NUM_1;
    bus_cfg.sda_io_num   = PIN_SDA;
    bus_cfg.scl_io_num   = PIN_SCL;
    bus_cfg.clk_source   = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = TCA9554_ADDR;
    dev_cfg.scl_speed_hz    = 300000;

    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_tca_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 device add failed: %s", esp_err_to_name(err));
        return;
    }

    // Hold system power rail (critical for battery operation)
    if (!tca_set_output_pin(TCA9554_SYS_EN, true)) {
        ESP_LOGW(TAG, "TCA9554 power hold not configured (OK on USB)");
    } else {
        ESP_LOGI(TAG, "TCA9554 power hold enabled");
    }

    // Disable battery ADC gate (active-low: high = disabled)
    tca_set_output_pin(TCA9554_BAT_GATE, true);
}

// ---- Battery ADC -----------------------------------------------------------
static constexpr adc_channel_t BAT_ADC_CHANNEL = ADC_CHANNEL_3;  // GPIO4

static adc_oneshot_unit_handle_t s_adc_handle  = nullptr;
static adc_cali_handle_t         s_adc_cali    = nullptr;
static float                     s_bat_max_mv  = 4200.0f;

static void adc_init() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc_handle) != ESP_OK) {
        ESP_LOGW(TAG, "ADC unit init failed");
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten    = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    adc_oneshot_config_channel(s_adc_handle, BAT_ADC_CHANNEL, &chan_cfg);

    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id  = ADC_UNIT_1;
    cali_cfg.atten    = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration not available");
    }
}

// Reads battery voltage: ADC pin sees battery/3 (1:3 resistor divider)
static float read_bat_voltage_v() {
    if (!s_adc_handle) return -1.0f;

    tca_set_output_pin(TCA9554_BAT_GATE, false);  // enable gate
    vTaskDelay(pdMS_TO_TICKS(3));

    int32_t mv_total = 0;
    int     samples  = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, BAT_ADC_CHANNEL, &raw) != ESP_OK) continue;
        int mv = 0;
        if (s_adc_cali) {
            adc_cali_raw_to_voltage(s_adc_cali, raw, &mv);
        } else {
            mv = raw * 3300 / 4095;
        }
        mv_total += mv;
        samples++;
        vTaskDelay(1);
    }

    tca_set_output_pin(TCA9554_BAT_GATE, true);   // disable gate

    if (samples == 0) return -1.0f;
    // Voltage divider factor: battery = pin * 3
    return static_cast<float>(mv_total) * 0.003f / samples;
}

static uint8_t voltage_to_pct(float v) {
    struct Pt { float v; uint8_t p; };
    static constexpr Pt kCurve[] = {
        {3.30f,  0}, {3.50f,  5}, {3.60f, 10}, {3.70f, 20},
        {3.75f, 30}, {3.80f, 40}, {3.85f, 50}, {3.90f, 60},
        {3.95f, 70}, {4.00f, 80}, {4.10f, 90}, {4.20f,100},
    };
    constexpr int N = sizeof(kCurve) / sizeof(kCurve[0]);
    if (v <= kCurve[0].v)   return kCurve[0].p;
    if (v >= kCurve[N-1].v) return kCurve[N-1].p;
    for (int i = 1; i < N; i++) {
        if (v <= kCurve[i].v) {
            float span  = kCurve[i].v - kCurve[i-1].v;
            float ratio = (span > 0) ? (v - kCurve[i-1].v) / span : 0.0f;
            int   pct   = (int)(kCurve[i-1].p
                              + (kCurve[i].p - kCurve[i-1].p) * ratio + 0.5f);
            if (pct < 0)   pct = 0;
            if (pct > 100) pct = 100;
            return (uint8_t)pct;
        }
    }
    return 0;
}

// ---- Public board API ------------------------------------------------------

void board_init() {
    // Pull both buttons up immediately, matching rsvpnano's begin().
    // GPIO16 (PWR) pull-up is required: the power-hold MOSFET gate needs it
    // HIGH while the firmware is running so TCA9554 IO6 can take over the latch.
    gpio_config_t btn = {};
    btn.pin_bit_mask = (1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_16);
    btn.mode         = GPIO_MODE_INPUT;
    btn.pull_up_en   = GPIO_PULLUP_ENABLE;
    btn.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&btn);

    backlight_init();
    tca9554_init();
    adc_init();
}

void board_set_backlight(uint8_t brightness) {
    // Active-low: duty 255 = off, duty 0 = full brightness
    uint32_t duty = 255u - brightness;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

bool board_is_charging() {
    return false;  // Waveshare has no charging indicator accessible via software
}

float board_read_voltage_mv() {
    return read_bat_voltage_v() * 1000.0f;
}

void board_set_bat_max_mv(float max_mv) {
    s_bat_max_mv = max_mv;
}

int board_get_battery_pct() {
    float v = read_bat_voltage_v();
    if (v < 2.5f || v > 4.6f) return -1;
    return voltage_to_pct(v);
}

void board_shutdown() {
    // Release power hold — device shuts off on battery
    tca_set_output_pin(TCA9554_SYS_EN, false);
}
