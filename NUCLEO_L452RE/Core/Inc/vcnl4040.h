/**
 * vcnl4040.h - Vishay VCNL4040 proximity / ambient light sensor driver (STM32 HAL, I2C)
 *
 * ECEN-435 Lab 5 - J. Honan
 *
 * Register map and bit fields from the Vishay VCNL4040 datasheet (doc 84274) and
 * application note "Designing the VCNL4040 into an Application" (doc 84307).
 * API modeled on the Adafruit_VCNL4040 Arduino library.
 *
 * Every register is 16 bits, sent low byte first. 7-bit I2C address is fixed at 0x60.
 */
#ifndef VCNL4040_H
#define VCNL4040_H

#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define VCNL4040_I2C_ADDR       (0x60 << 1)   /* HAL wants the 8-bit (shifted) address */
#define VCNL4040_DEVICE_ID      0x0186

/* Command codes (register addresses) */
#define VCNL4040_REG_ALS_CONF   0x00  /* L: ALS_CONF          H: reserved      */
#define VCNL4040_REG_ALS_THDH   0x01
#define VCNL4040_REG_ALS_THDL   0x02
#define VCNL4040_REG_PS_CONF12  0x03  /* L: PS_CONF1          H: PS_CONF2      */
#define VCNL4040_REG_PS_CONF3MS 0x04  /* L: PS_CONF3          H: PS_MS         */
#define VCNL4040_REG_PS_CANC    0x05
#define VCNL4040_REG_PS_THDL    0x06
#define VCNL4040_REG_PS_THDH    0x07
#define VCNL4040_REG_PS_DATA    0x08
#define VCNL4040_REG_ALS_DATA   0x09
#define VCNL4040_REG_WHITE_DATA 0x0A
#define VCNL4040_REG_INT_FLAG   0x0B  /* H byte holds the flags */
#define VCNL4040_REG_ID         0x0C

/* IR LED drive current (PS_MS bits 2:0) */
typedef enum {
    VCNL4040_LED_50MA  = 0,
    VCNL4040_LED_75MA  = 1,
    VCNL4040_LED_100MA = 2,
    VCNL4040_LED_120MA = 3,
    VCNL4040_LED_140MA = 4,
    VCNL4040_LED_160MA = 5,
    VCNL4040_LED_180MA = 6,
    VCNL4040_LED_200MA = 7,
} VCNL4040_LEDCurrent_t;

/* IR LED on/off duty ratio (PS_CONF1 bits 7:6) - lower duty = faster updates, more power */
typedef enum {
    VCNL4040_DUTY_1_40  = 0,
    VCNL4040_DUTY_1_80  = 1,
    VCNL4040_DUTY_1_160 = 2,
    VCNL4040_DUTY_1_320 = 3,
} VCNL4040_LEDDuty_t;

/* Proximity integration time (PS_CONF1 bits 3:1) - longer = more range/counts */
typedef enum {
    VCNL4040_PS_IT_1T   = 0,
    VCNL4040_PS_IT_1_5T = 1,
    VCNL4040_PS_IT_2T   = 2,
    VCNL4040_PS_IT_2_5T = 3,
    VCNL4040_PS_IT_3T   = 4,
    VCNL4040_PS_IT_3_5T = 5,
    VCNL4040_PS_IT_4T   = 6,
    VCNL4040_PS_IT_8T   = 7,
} VCNL4040_ProxIT_t;

/* Ambient light integration time (ALS_CONF bits 7:6) */
typedef enum {
    VCNL4040_ALS_IT_80MS  = 0,   /* 0.10   lux/count, 6553 lux max */
    VCNL4040_ALS_IT_160MS = 1,   /* 0.05   lux/count */
    VCNL4040_ALS_IT_320MS = 2,   /* 0.025  lux/count */
    VCNL4040_ALS_IT_640MS = 3,   /* 0.0125 lux/count, 819 lux max */
} VCNL4040_ALSIT_t;

/* Setup / identification */
HAL_StatusTypeDef VCNL4040_Init(I2C_HandleTypeDef *hi2c);   /* detects, enables PS+ALS+white */
bool              VCNL4040_IsConnected(void);
HAL_StatusTypeDef VCNL4040_ReadID(uint16_t *id);

/* Raw register access (16-bit, little endian) */
HAL_StatusTypeDef VCNL4040_ReadReg(uint8_t reg, uint16_t *value);
HAL_StatusTypeDef VCNL4040_WriteReg(uint8_t reg, uint16_t value);

/* Proximity */
HAL_StatusTypeDef VCNL4040_EnableProximity(bool enable);
HAL_StatusTypeDef VCNL4040_SetProximityLEDCurrent(VCNL4040_LEDCurrent_t current);
HAL_StatusTypeDef VCNL4040_SetProximityLEDDuty(VCNL4040_LEDDuty_t duty);
HAL_StatusTypeDef VCNL4040_SetProximityIntegration(VCNL4040_ProxIT_t it);
HAL_StatusTypeDef VCNL4040_SetProximityHighRes(bool sixteenBit);
HAL_StatusTypeDef VCNL4040_ReadProximity(uint16_t *counts);   /* bigger = closer */

/* Ambient / white light */
HAL_StatusTypeDef VCNL4040_EnableAmbientLight(bool enable);
HAL_StatusTypeDef VCNL4040_SetAmbientIntegration(VCNL4040_ALSIT_t it);
HAL_StatusTypeDef VCNL4040_EnableWhiteLight(bool enable);
HAL_StatusTypeDef VCNL4040_ReadAmbientRaw(uint16_t *counts);
HAL_StatusTypeDef VCNL4040_ReadLux_x10(uint32_t *luxTimes10);  /* 1234 -> 123.4 lux */
HAL_StatusTypeDef VCNL4040_ReadWhite(uint16_t *counts);

#endif /* VCNL4040_H */
