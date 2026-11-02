
#include "lps22.h"

static HAL_StatusTypeDef readReg(LPS22_Handle *dev, uint8_t reg, uint8_t *data, uint16_t size)
{
    return HAL_I2C_Mem_Read(dev->hi2c, dev->address, reg, I2C_MEMADD_SIZE_8BIT, data, size, 100);
}

static HAL_StatusTypeDef writeReg(LPS22_Handle *dev, uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(dev->hi2c, dev->address, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100);
}

HAL_StatusTypeDef LPS22_Init(LPS22_Handle *dev, I2C_HandleTypeDef *hi2c)
{
    const uint8_t addresses[] = {LPS22_ADDR_DEFAULT, LPS22_ADDR_ALT};
    dev->hi2c = hi2c;
    dev->id = 0;

    for (uint8_t i = 0; i < sizeof(addresses); i++)
    {
        dev->address = addresses[i] << 1;
        if (readReg(dev, LPS22_WHO_AM_I, &dev->id, 1) == HAL_OK &&
            (dev->id == LPS22_ID_HB || dev->id == LPS22_ID_HH))
        {
            // CTRL_REG1: ODR = 10 Hz (Bits 6:4 = 010), EN_LPFP = 1 (Tiefpass, Bit 3), BDU = 1 (Bit 1)
            if (writeReg(dev, LPS22_CTRL_REG1, 0x2A) != HAL_OK)
                return HAL_ERROR;
            // CTRL_REG2: IF_ADD_INC = 1, damit mehrere Register am Stueck gelesen werden koennen
            return writeReg(dev, LPS22_CTRL_REG2, 0x10);
        }
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef LPS22_Read(LPS22_Handle *dev, float *pressure, float *temperature)
{
    uint8_t data[5]; // PRESS_OUT_XL, _L, _H, TEMP_OUT_L, _H
    HAL_StatusTypeDef status = readReg(dev, LPS22_PRESS_OUT_XL, data, sizeof(data));
    if (status != HAL_OK)
        return status;

    int32_t pressRaw = ((int32_t)data[2] << 16) | ((int32_t)data[1] << 8) | data[0];
    if (pressRaw & 0x00800000) // 24-Bit Zweierkomplement
        pressRaw |= 0xFF000000;
    int16_t tempRaw = (int16_t)(((uint16_t)data[4] << 8) | data[3]);

    *pressure = (float)pressRaw / 4096.0f;   // hPa
    *temperature = (float)tempRaw / 100.0f;  // Grad C
    return HAL_OK;
}
