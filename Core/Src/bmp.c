
#include "bmp.h"

// BMP180
#define BMP180_REG_CALIB 0xAA
#define BMP180_REG_CTRL 0xF4
#define BMP180_REG_DATA 0xF6
#define BMP180_CMD_TEMP 0x2E
#define BMP180_CMD_PRESS 0x34
#define BMP180_OSS 3            // Oversampling 0..3, 3 = genaueste Stufe
#define BMP180_TEMP_WAIT_MS 6   // Datenblatt: 4.5 ms
#define BMP180_PRESS_WAIT_MS 27 // Datenblatt: 25.5 ms bei OSS = 3

// BMP280 / BME280
#define BMX280_REG_CALIB 0x88
#define BME280_REG_H1 0xA1
#define BME280_REG_H2 0xE1
#define BMX280_REG_RESET 0xE0
#define BME280_REG_CTRL_HUM 0xF2
#define BMX280_REG_CTRL_MEAS 0xF4
#define BMX280_REG_CONFIG 0xF5
#define BMX280_REG_DATA 0xF7

enum
{
    BMP180_IDLE,
    BMP180_WAIT_TEMP,
    BMP180_WAIT_PRESS
};

static HAL_StatusTypeDef readRegs(BMP_Handle *dev, uint8_t reg, uint8_t *data, uint16_t size)
{
    return HAL_I2C_Mem_Read(dev->hi2c, dev->address, reg, I2C_MEMADD_SIZE_8BIT, data, size, 100);
}

static HAL_StatusTypeDef writeReg(BMP_Handle *dev, uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(dev->hi2c, dev->address, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100);
}

/* ---------- BMP180 ---------- */
static HAL_StatusTypeDef bmp180Init(BMP_Handle *dev)
{
    uint8_t c[22];
    if (readRegs(dev, BMP180_REG_CALIB, c, sizeof(c)) != HAL_OK)
        return HAL_ERROR;

    // Big-Endian
#define BE16(i) ((int16_t)(((uint16_t)c[i] << 8) | c[(i) + 1]))
    dev->ac1 = BE16(0);
    dev->ac2 = BE16(2);
    dev->ac3 = BE16(4);
    dev->ac4 = (uint16_t)BE16(6);
    dev->ac5 = (uint16_t)BE16(8);
    dev->ac6 = (uint16_t)BE16(10);
    dev->b1 = BE16(12);
    dev->b2 = BE16(14);
    dev->mb = BE16(16);
    dev->mc = BE16(18);
    dev->md = BE16(20);
#undef BE16

    dev->state = BMP180_IDLE;
    return HAL_OK;
}

// Umrechnung nach Datenblatt BMP180, Kapitel 3.5
static void bmp180Compute(BMP_Handle *dev, int32_t ut, int32_t up, float *temperature, float *pressure)
{
    int32_t x1 = ((ut - (int32_t)dev->ac6) * (int32_t)dev->ac5) >> 15;
    int32_t x2 = ((int32_t)dev->mc << 11) / (x1 + dev->md);
    int32_t b5 = x1 + x2;
    *temperature = ((b5 + 8) >> 4) / 10.0f;

    int32_t b6 = b5 - 4000;
    x1 = (dev->b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = (dev->ac2 * b6) >> 11;
    int32_t x3 = x1 + x2;
    int32_t b3 = ((((int32_t)dev->ac1 * 4 + x3) << BMP180_OSS) + 2) / 4;
    x1 = (dev->ac3 * b6) >> 13;
    x2 = (dev->b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    uint32_t b4 = ((uint32_t)dev->ac4 * (uint32_t)(x3 + 32768)) >> 15;
    uint32_t b7 = ((uint32_t)up - b3) * (uint32_t)(50000 >> BMP180_OSS);

    int32_t p;
    if (b7 < 0x80000000UL)
        p = (b7 * 2) / b4;
    else
        p = (b7 / b4) * 2;
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p += (x1 + x2 + 3791) >> 4;

    *pressure = p / 100.0f; // Pa -> hPa
}

static HAL_StatusTypeDef bmp180Read(BMP_Handle *dev, float *temperature, float *pressure)
{
    uint8_t c[3];
    uint32_t now = HAL_GetTick();

    switch (dev->state)
    {
    case BMP180_IDLE:
        if (writeReg(dev, BMP180_REG_CTRL, BMP180_CMD_TEMP) != HAL_OK)
            return HAL_ERROR;
        dev->tStart = now;
        dev->state = BMP180_WAIT_TEMP;
        return HAL_BUSY;

    case BMP180_WAIT_TEMP:
        if (now - dev->tStart < BMP180_TEMP_WAIT_MS)
            return HAL_BUSY;
        if (readRegs(dev, BMP180_REG_DATA, c, 2) != HAL_OK ||
            writeReg(dev, BMP180_REG_CTRL, BMP180_CMD_PRESS | (BMP180_OSS << 6)) != HAL_OK)
        {
            dev->state = BMP180_IDLE;
            return HAL_ERROR;
        }
        dev->ut = ((int32_t)c[0] << 8) | c[1];
        dev->tStart = now;
        dev->state = BMP180_WAIT_PRESS;
        return HAL_BUSY;

    case BMP180_WAIT_PRESS:
    default:
        if (now - dev->tStart < BMP180_PRESS_WAIT_MS)
            return HAL_BUSY;
        dev->state = BMP180_IDLE;
        if (readRegs(dev, BMP180_REG_DATA, c, 3) != HAL_OK)
            return HAL_ERROR;
        int32_t up = (((int32_t)c[0] << 16) | ((int32_t)c[1] << 8) | c[2]) >> (8 - BMP180_OSS);
        bmp180Compute(dev, dev->ut, up, temperature, pressure);
        return HAL_OK;
    }
}

/* ---------- BMP280 / BME280 ---------- */
static HAL_StatusTypeDef bmx280Init(BMP_Handle *dev)
{
    uint8_t c[24];

    if (writeReg(dev, BMX280_REG_RESET, 0xB6) != HAL_OK)
        return HAL_ERROR;
    HAL_Delay(3); // Startzeit nach Reset: 2 ms

    if (readRegs(dev, BMX280_REG_CALIB, c, sizeof(c)) != HAL_OK)
        return HAL_ERROR;

    // Little-Endian
#define LE16(i) ((int16_t)(((uint16_t)c[(i) + 1] << 8) | c[i]))
    dev->t1 = (uint16_t)LE16(0);
    dev->t2 = LE16(2);
    dev->t3 = LE16(4);
    dev->p1 = (uint16_t)LE16(6);
    dev->p2 = LE16(8);
    dev->p3 = LE16(10);
    dev->p4 = LE16(12);
    dev->p5 = LE16(14);
    dev->p6 = LE16(16);
    dev->p7 = LE16(18);
    dev->p8 = LE16(20);
    dev->p9 = LE16(22);
#undef LE16

    if (dev->type == BMP_TYPE_BME280)
    {
        uint8_t e[7];
        if (readRegs(dev, BME280_REG_H1, &dev->h1, 1) != HAL_OK ||
            readRegs(dev, BME280_REG_H2, e, sizeof(e)) != HAL_OK)
            return HAL_ERROR;
        dev->h2 = (int16_t)(((uint16_t)e[1] << 8) | e[0]);
        dev->h3 = e[2];
        dev->h4 = (int16_t)((int8_t)e[3] * 16 | (e[4] & 0x0F));
        dev->h5 = (int16_t)((int8_t)e[5] * 16 | (e[4] >> 4));
        dev->h6 = (int8_t)e[6];

        // Feuchte-Oversampling x1, muss vor CTRL_MEAS geschrieben werden
        if (writeReg(dev, BME280_REG_CTRL_HUM, 0x01) != HAL_OK)
            return HAL_ERROR;
    }

    // CONFIG: Standby 1000 ms (101), IIR-Filter x4 (010)
    if (writeReg(dev, BMX280_REG_CONFIG, 0xA8) != HAL_OK)
        return HAL_ERROR;
    // CTRL_MEAS: Temperatur x2 (010), Druck x16 (101), Normal-Modus (11)
    return writeReg(dev, BMX280_REG_CTRL_MEAS, 0x57);
}

// Umrechnung nach Datenblatt BME280, Kapitel 4.2.3 (Integer-Variante)
static int32_t bmx280CompensateT(BMP_Handle *dev, int32_t adcT, int32_t *tFine)
{
    int32_t var1 = ((((adcT >> 3) - ((int32_t)dev->t1 << 1))) * (int32_t)dev->t2) >> 11;
    int32_t var2 = (((((adcT >> 4) - (int32_t)dev->t1) * ((adcT >> 4) - (int32_t)dev->t1)) >> 12) *
                    (int32_t)dev->t3) >> 14;
    *tFine = var1 + var2;
    return (*tFine * 5 + 128) >> 8; // 0.01 Grad C
}

static uint32_t bmx280CompensateP(BMP_Handle *dev, int32_t adcP, int32_t tFine)
{
    int64_t var1 = (int64_t)tFine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dev->p6;
    var2 = var2 + ((var1 * (int64_t)dev->p5) << 17);
    var2 = var2 + (((int64_t)dev->p4) << 35);
    var1 = ((var1 * var1 * (int64_t)dev->p3) >> 8) + ((var1 * (int64_t)dev->p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dev->p1) >> 33;
    if (var1 == 0)
        return 0; // Division durch 0 vermeiden

    int64_t p = 1048576 - adcP;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dev->p9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dev->p8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dev->p7) << 4);
    return (uint32_t)p; // Pa * 256
}

static uint32_t bme280CompensateH(BMP_Handle *dev, int32_t adcH, int32_t tFine)
{
    int32_t v = tFine - 76800;
    v = (((((adcH << 14) - (((int32_t)dev->h4) << 20) - (((int32_t)dev->h5) * v)) + 16384) >> 15) *
         (((((((v * ((int32_t)dev->h6)) >> 10) * (((v * ((int32_t)dev->h3)) >> 11) + 32768)) >> 10) + 2097152) *
               ((int32_t)dev->h2) + 8192) >> 14));
    v = (v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)dev->h1)) >> 4));
    if (v < 0)
        v = 0;
    if (v > 419430400)
        v = 419430400;
    return (uint32_t)(v >> 12); // %rH * 1024
}

static HAL_StatusTypeDef bmx280Read(BMP_Handle *dev, float *temperature, float *pressure, float *humidity)
{
    uint8_t c[8]; // press[3], temp[3], hum[2]
    uint16_t len = (dev->type == BMP_TYPE_BME280) ? 8 : 6;
    if (readRegs(dev, BMX280_REG_DATA, c, len) != HAL_OK)
        return HAL_ERROR;

    int32_t adcP = ((int32_t)c[0] << 12) | ((int32_t)c[1] << 4) | (c[2] >> 4);
    int32_t adcT = ((int32_t)c[3] << 12) | ((int32_t)c[4] << 4) | (c[5] >> 4);
    if (adcT == 0x80000) // noch keine Messung fertig (Reset-Wert)
        return HAL_BUSY;

    int32_t tFine;
    *temperature = bmx280CompensateT(dev, adcT, &tFine) / 100.0f;
    *pressure = bmx280CompensateP(dev, adcP, tFine) / 25600.0f; // Pa*256 -> hPa

    if (dev->type == BMP_TYPE_BME280)
    {
        int32_t adcH = ((int32_t)c[6] << 8) | c[7];
        *humidity = bme280CompensateH(dev, adcH, tFine) / 1024.0f;
    }
    return HAL_OK;
}

/* ---------- Oeffentliche Funktionen ---------- */
HAL_StatusTypeDef BMP_Init(BMP_Handle *dev, I2C_HandleTypeDef *hi2c)
{
    const uint8_t addresses[] = {BMP_ADDR_PRIMARY, BMP_ADDR_SECONDARY};
    dev->hi2c = hi2c;
    dev->type = BMP_TYPE_NONE;

    for (uint8_t i = 0; i < sizeof(addresses); i++)
    {
        dev->address = addresses[i] << 1;
        if (readRegs(dev, BMP_REG_ID, &dev->id, 1) != HAL_OK)
            continue;

        switch (dev->id)
        {
        case BMP180_ID:
            dev->type = BMP_TYPE_BMP180;
            return bmp180Init(dev);
        case BMP280_ID:
        case 0x56: // BMP280 Vorserie
        case 0x57:
            dev->type = BMP_TYPE_BMP280;
            return bmx280Init(dev);
        case BME280_ID:
            dev->type = BMP_TYPE_BME280;
            return bmx280Init(dev);
        default:
            break;
        }
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef BMP_Read(BMP_Handle *dev, float *temperature, float *pressure, float *humidity)
{
    *humidity = -1.0f;
    switch (dev->type)
    {
    case BMP_TYPE_BMP180:
        return bmp180Read(dev, temperature, pressure);
    case BMP_TYPE_BMP280:
    case BMP_TYPE_BME280:
        return bmx280Read(dev, temperature, pressure, humidity);
    default:
        return HAL_ERROR;
    }
}

const char *BMP_TypeName(BMP_Type type)
{
    switch (type)
    {
    case BMP_TYPE_BMP180:
        return "BMP180";
    case BMP_TYPE_BMP280:
        return "BMP280";
    case BMP_TYPE_BME280:
        return "BME280";
    default:
        return "---";
    }
}
