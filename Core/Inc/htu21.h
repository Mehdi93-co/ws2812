// htu21.h
// Treiber fuer HTU21D / HTU2x Feuchte- und Temperatursensor (I2C 0x40)
// Kompatibel mit SHT21 / Si7021 (gleiche Befehle)
#ifndef HTU21_H
#define HTU21_H

#include "stm32f4xx_hal.h"

#define HTU21_ADDR 0x40

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint8_t state;
    uint32_t tStart;
    float temperature; // Zwischenwert, wird fuer die Feuchte-Kompensation gebraucht
} HTU21_Handle;

HAL_StatusTypeDef HTU21_Init(HTU21_Handle *dev, I2C_HandleTypeDef *hi2c);
// Nicht blockierend: HAL_OK = neue Werte, HAL_BUSY = Messung laeuft noch, erneut aufrufen.
// temperature in Grad C, humidity in %rH
HAL_StatusTypeDef HTU21_Read(HTU21_Handle *dev, float *temperature, float *humidity);

#endif // HTU21_H
