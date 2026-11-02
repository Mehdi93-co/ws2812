
#include "htu21.h"

#define CMD_TEMP_NO_HOLD 0xF3
#define CMD_HUM_NO_HOLD 0xF5
#define CMD_READ_USER_REG 0xE7
#define CMD_SOFT_RESET 0xFE

#define TEMP_WAIT_MS 52 // Datenblatt: max. 50 ms bei 14 Bit
#define HUM_WAIT_MS 18  // Datenblatt: max. 16 ms bei 12 Bit
#define RESET_WAIT_MS 15

enum
{
    HTU21_IDLE,
    HTU21_WAIT_TEMP,
    HTU21_WAIT_HUM
};

static HAL_StatusTypeDef sendCommand(HTU21_Handle *dev, uint8_t cmd)
{
    return HAL_I2C_Master_Transmit(dev->hi2c, HTU21_ADDR << 1, &cmd, 1, 100);
}

// CRC-8, Polynom x^8 + x^5 + x^4 + 1 (0x31), Startwert 0
static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

// Liest MSB, LSB, CRC und gibt den Rohwert ohne Statusbits zurueck
static HAL_StatusTypeDef readRaw(HTU21_Handle *dev, uint16_t *raw)
{
    uint8_t c[3];
    if (HAL_I2C_Master_Receive(dev->hi2c, HTU21_ADDR << 1, c, sizeof(c), 100) != HAL_OK)
        return HAL_ERROR;
    if (crc8(c, 2) != c[2])
        return HAL_ERROR;
    *raw = (((uint16_t)c[0] << 8) | c[1]) & 0xFFFC;
    return HAL_OK;
}

HAL_StatusTypeDef HTU21_Init(HTU21_Handle *dev, I2C_HandleTypeDef *hi2c)
{
    uint8_t userReg;
    dev->hi2c = hi2c;
    dev->state = HTU21_IDLE;

    if (HAL_I2C_IsDeviceReady(dev->hi2c, HTU21_ADDR << 1, 2, 10) != HAL_OK)
        return HAL_ERROR;
    if (sendCommand(dev, CMD_SOFT_RESET) != HAL_OK)
        return HAL_ERROR;
    HAL_Delay(RESET_WAIT_MS);

    // User-Register lesen, um zu pruefen, dass wirklich ein HTU2x antwortet
    if (sendCommand(dev, CMD_READ_USER_REG) != HAL_OK ||
        HAL_I2C_Master_Receive(dev->hi2c, HTU21_ADDR << 1, &userReg, 1, 100) != HAL_OK)
        return HAL_ERROR;
    return HAL_OK;
}

HAL_StatusTypeDef HTU21_Read(HTU21_Handle *dev, float *temperature, float *humidity)
{
    uint16_t raw;
    uint32_t now = HAL_GetTick();

    switch (dev->state)
    {
    case HTU21_IDLE:
        if (sendCommand(dev, CMD_TEMP_NO_HOLD) != HAL_OK)
            return HAL_ERROR;
        dev->tStart = now;
        dev->state = HTU21_WAIT_TEMP;
        return HAL_BUSY;

    case HTU21_WAIT_TEMP:
        if (now - dev->tStart < TEMP_WAIT_MS)
            return HAL_BUSY;
        if (readRaw(dev, &raw) != HAL_OK || sendCommand(dev, CMD_HUM_NO_HOLD) != HAL_OK)
        {
            dev->state = HTU21_IDLE;
            return HAL_ERROR;
        }
        dev->temperature = -46.85f + 175.72f * raw / 65536.0f;
        dev->tStart = now;
        dev->state = HTU21_WAIT_HUM;
        return HAL_BUSY;

    case HTU21_WAIT_HUM:
    default:
        if (now - dev->tStart < HUM_WAIT_MS)
            return HAL_BUSY;
        dev->state = HTU21_IDLE;
        if (readRaw(dev, &raw) != HAL_OK)
            return HAL_ERROR;

        float rh = -6.0f + 125.0f * raw / 65536.0f;
        // Temperaturkompensation laut Datenblatt: -0.15 %rH pro Grad Abweichung von 25 Grad C
        rh += (25.0f - dev->temperature) * -0.15f;
        if (rh < 0.0f)
            rh = 0.0f;
        if (rh > 100.0f)
            rh = 100.0f;

        *temperature = dev->temperature;
        *humidity = rh;
        return HAL_OK;
    }
}
