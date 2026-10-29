/**
 * vcnl4040.c - Vishay VCNL4040 proximity / ambient light sensor driver (STM32 HAL, I2C)
 *
 * ECEN-435 Lab 5 - J. Honan
 *
 * Polling (blocking) I2C. Each call is a few hundred microseconds at 400 kHz.
 */
#include "vcnl4040.h"

#define VCNL4040_TIMEOUT_MS 10

static I2C_HandleTypeDef *vcnlI2c = NULL;
static VCNL4040_ALSIT_t   alsIt   = VCNL4040_ALS_IT_80MS;

/* ---------------------------------------------------------------- raw access */

HAL_StatusTypeDef VCNL4040_ReadReg(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    if (vcnlI2c == NULL) return HAL_ERROR;

    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(vcnlI2c, VCNL4040_I2C_ADDR, reg,
                                             I2C_MEMADD_SIZE_8BIT, data, 2,
                                             VCNL4040_TIMEOUT_MS);
    if (ret == HAL_OK) {
        *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);   /* LSB first */
    }
    return ret;
}

HAL_StatusTypeDef VCNL4040_WriteReg(uint8_t reg, uint16_t value)
{
    uint8_t data[2] = { (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    if (vcnlI2c == NULL) return HAL_ERROR;

    return HAL_I2C_Mem_Write(vcnlI2c, VCNL4040_I2C_ADDR, reg,
                             I2C_MEMADD_SIZE_8BIT, data, 2, VCNL4040_TIMEOUT_MS);
}

/* Read-modify-write a bit field inside one 16-bit register.
 * mask/value are already shifted into position. */
static HAL_StatusTypeDef updateBits(uint8_t reg, uint16_t mask, uint16_t value)
{
    uint16_t cur;
    HAL_StatusTypeDef ret = VCNL4040_ReadReg(reg, &cur);
    if (ret != HAL_OK) return ret;

    cur = (uint16_t)((cur & ~mask) | (value & mask));
    return VCNL4040_WriteReg(reg, cur);
}

/* ---------------------------------------------------------------- setup */

HAL_StatusTypeDef VCNL4040_ReadID(uint16_t *id)
{
    return VCNL4040_ReadReg(VCNL4040_REG_ID, id);
}

bool VCNL4040_IsConnected(void)
{
    uint16_t id = 0;
    return (VCNL4040_ReadID(&id) == HAL_OK) && (id == VCNL4040_DEVICE_ID);
}

HAL_StatusTypeDef VCNL4040_Init(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef ret;
    vcnlI2c = hi2c;

    if (!VCNL4040_IsConnected()) {
        return HAL_ERROR;
    }

    /* Same defaults as the Adafruit library, tuned for hand-over-sensor demos:
     * max LED current, 8T integration, 16-bit output -> widest usable range. */
    if ((ret = VCNL4040_SetProximityLEDCurrent(VCNL4040_LED_200MA)) != HAL_OK) return ret;
    if ((ret = VCNL4040_SetProximityLEDDuty(VCNL4040_DUTY_1_40))   != HAL_OK) return ret;
    if ((ret = VCNL4040_SetProximityIntegration(VCNL4040_PS_IT_8T)) != HAL_OK) return ret;
    if ((ret = VCNL4040_SetProximityHighRes(true))                 != HAL_OK) return ret;
    if ((ret = VCNL4040_EnableProximity(true))                     != HAL_OK) return ret;

    if ((ret = VCNL4040_SetAmbientIntegration(VCNL4040_ALS_IT_80MS)) != HAL_OK) return ret;
    if ((ret = VCNL4040_EnableAmbientLight(true))                  != HAL_OK) return ret;
    if ((ret = VCNL4040_EnableWhiteLight(true))                    != HAL_OK) return ret;

    HAL_Delay(10);   /* let the first conversion finish */
    return HAL_OK;
}

/* ---------------------------------------------------------------- proximity */

/* PS_CONF1 (low byte of 0x03): bit0 PS_SD = 1 shuts the proximity sensor down */
HAL_StatusTypeDef VCNL4040_EnableProximity(bool enable)
{
    return updateBits(VCNL4040_REG_PS_CONF12, 0x0001, enable ? 0x0000 : 0x0001);
}

/* PS_MS (high byte of 0x04): bits 2:0 LED_I */
HAL_StatusTypeDef VCNL4040_SetProximityLEDCurrent(VCNL4040_LEDCurrent_t current)
{
    return updateBits(VCNL4040_REG_PS_CONF3MS, 0x0700, (uint16_t)current << 8);
}

/* PS_CONF1 bits 7:6 PS_Duty */
HAL_StatusTypeDef VCNL4040_SetProximityLEDDuty(VCNL4040_LEDDuty_t duty)
{
    return updateBits(VCNL4040_REG_PS_CONF12, 0x00C0, (uint16_t)duty << 6);
}

/* PS_CONF1 bits 3:1 PS_IT */
HAL_StatusTypeDef VCNL4040_SetProximityIntegration(VCNL4040_ProxIT_t it)
{
    return updateBits(VCNL4040_REG_PS_CONF12, 0x000E, (uint16_t)it << 1);
}

/* PS_CONF2 (high byte of 0x03): bit3 PS_HD, 1 = 16-bit output, 0 = 12-bit */
HAL_StatusTypeDef VCNL4040_SetProximityHighRes(bool sixteenBit)
{
    return updateBits(VCNL4040_REG_PS_CONF12, 0x0800, sixteenBit ? 0x0800 : 0x0000);
}

HAL_StatusTypeDef VCNL4040_ReadProximity(uint16_t *counts)
{
    return VCNL4040_ReadReg(VCNL4040_REG_PS_DATA, counts);
}

/* ---------------------------------------------------------------- ambient / white */

/* ALS_CONF (low byte of 0x00): bit0 ALS_SD = 1 shuts ALS down */
HAL_StatusTypeDef VCNL4040_EnableAmbientLight(bool enable)
{
    return updateBits(VCNL4040_REG_ALS_CONF, 0x0001, enable ? 0x0000 : 0x0001);
}

/* ALS_CONF bits 7:6 ALS_IT */
HAL_StatusTypeDef VCNL4040_SetAmbientIntegration(VCNL4040_ALSIT_t it)
{
    HAL_StatusTypeDef ret = updateBits(VCNL4040_REG_ALS_CONF, 0x00C0, (uint16_t)it << 6);
    if (ret == HAL_OK) alsIt = it;
    return ret;
}

/* PS_MS (high byte of 0x04): bit7 WHITE_EN, 0 = enabled */
HAL_StatusTypeDef VCNL4040_EnableWhiteLight(bool enable)
{
    return updateBits(VCNL4040_REG_PS_CONF3MS, 0x8000, enable ? 0x0000 : 0x8000);
}

HAL_StatusTypeDef VCNL4040_ReadAmbientRaw(uint16_t *counts)
{
    return VCNL4040_ReadReg(VCNL4040_REG_ALS_DATA, counts);
}

/* Resolution is 0.1 lux/count at 80 ms and halves with each doubling of IT */
HAL_StatusTypeDef VCNL4040_ReadLux_x10(uint32_t *luxTimes10)
{
    uint16_t raw;
    HAL_StatusTypeDef ret = VCNL4040_ReadAmbientRaw(&raw);
    if (ret == HAL_OK) {
        *luxTimes10 = (uint32_t)raw >> (uint32_t)alsIt;
    }
    return ret;
}

HAL_StatusTypeDef VCNL4040_ReadWhite(uint16_t *counts)
{
    return VCNL4040_ReadReg(VCNL4040_REG_WHITE_DATA, counts);
}
