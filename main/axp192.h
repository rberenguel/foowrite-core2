#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

// AXP192 PMU — M5Stack Core2
// I2C address 0x34, bus on SDA=21 SCL=22

#define AXP192_ADDR     0x34
#define AXP192_I2C_PORT I2C_NUM_0
#define AXP192_SDA      21
#define AXP192_SCL      22

// Key registers
#define AXP192_REG_POWER_OUTPUT_CTL  0x12
#define AXP192_REG_DCDC1_VOLTAGE     0x26
#define AXP192_REG_DCDC3_VOLTAGE     0x27  // LCD backlight brightness on Core2
#define AXP192_REG_LDO23_VOLTAGE     0x28  // LDO2 = LCD power rail (3.3V, keep on)
#define AXP192_REG_GPIO0_MODE        0x90
#define AXP192_REG_GPIO0_LDO_VOLTAGE 0x91
#define AXP192_REG_GPIO4_CTL         0x95  // GPIO4 = LCD reset
#define AXP192_REG_GPIO34_STATE      0x96  // GPIO3/4 output state
#define AXP192_REG_CHARGE_CTL1       0x33
#define AXP192_REG_PEK               0x36
#define AXP192_REG_ADC_ENABLE1       0x82
#define AXP192_REG_VBUS_IPSOUT_CTL   0x30

// Power output control bits (register 0x12)
#define AXP192_DCDC1_EN  (1 << 0)
#define AXP192_DCDC3_EN  (1 << 1)  // LCD backlight
#define AXP192_LDO2_EN   (1 << 2)  // LCD power rail
#define AXP192_LDO3_EN   (1 << 3)
#define AXP192_DCDC2_EN  (1 << 4)
#define AXP192_EXTEN_EN  (1 << 6)

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the AXP192 and power on all Core2 peripherals.
// Must be called before bringing up the display.
// Exposes the bus handle so LovyanGFX can share it later if needed.
void axp192_init(i2c_master_bus_handle_t *out_bus_handle);

// Set LCD backlight brightness 0–255 (controls DCDC3 voltage).
// 0 = off, 255 = full brightness (~3.3V).
void axp192_set_lcd_backlight(uint8_t brightness);

// Enable/disable the 5V boost (bus power, speaker, etc.)
void axp192_set_exten(bool enable);

#ifdef __cplusplus
}
#endif
