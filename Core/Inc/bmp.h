// bmp.h
// Treiber fuer Bosch BMP180 / BMP280 / BME280 (I2C), Typ wird ueber das ID-Register erkannt
#ifndef BMP_H
#define BMP_H

#include "stm32f4xx_hal.h"

#define BMP_ADDR_PRIMARY 0x77
#define BMP_ADDR_SECONDARY 0x76
#define BMP_REG_ID 0xD0

#define BMP180_ID 0x55
#define BMP280_ID 0x58
#define BME280_ID 0x60

typedef enum
{
    BMP_TYPE_NONE,
    BMP_TYPE_BMP180, // Temperatur + Druck
    BMP_TYPE_BMP280, // Temperatur + Druck
    BMP_TYPE_BME280  // Temperatur + Druck + Feuchte
} BMP_Type;

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint16_t address; // 8-Bit HAL-Adresse (7-Bit << 1)
    BMP_Type type;
    uint8_t id;

    // Kalibrierung BMP180
    int16_t ac1, ac2, ac3;
    uint16_t ac4, ac5, ac6;
    int16_t b1, b2, mb, mc, md;
    // Ablauf BMP180 (Messung wird gestartet, Ergebnis spaeter abgeholt)
    uint8_t state;
    uint32_t tStart;
    int32_t ut;

    // Kalibrierung BMP280 / BME280
    uint16_t t1;
    int16_t t2, t3;
    uint16_t p1;
    int16_t p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1, h3;
    int16_t h2, h4, h5;
    int8_t h6;
} BMP_Handle;

// Sucht den Sensor auf 0x77 und 0x76, liest die Kalibrierung und startet die Messung
HAL_StatusTypeDef BMP_Init(BMP_Handle *dev, I2C_HandleTypeDef *hi2c);
// Nicht blockierend: HAL_OK = neue Werte, HAL_BUSY = Messung laeuft noch, erneut aufrufen.
// temperature in Grad C, pressure in hPa, humidity in %rH (nur BME280, sonst -1)
HAL_StatusTypeDef BMP_Read(BMP_Handle *dev, float *temperature, float *pressure, float *humidity);
const char *BMP_TypeName(BMP_Type type);

static inline uint8_t BMP_HasHumidity(const BMP_Handle *dev)
{
    return dev->type == BMP_TYPE_BME280;
}

#endif // BMP_H
