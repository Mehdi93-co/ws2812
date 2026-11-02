// lps22.h
// Treiber fuer den LPS22HB / LPS22HH Drucksensor (I2C)
#ifndef LPS22_H
#define LPS22_H

#include "stm32f4xx_hal.h"

#define LPS22_ADDR_DEFAULT 0x5D // SDO = High (Adafruit-Breakout Standard)
#define LPS22_ADDR_ALT 0x5C     // SDO = Low

#define LPS22_WHO_AM_I 0x0F
#define LPS22_CTRL_REG1 0x10
#define LPS22_CTRL_REG2 0x11
#define LPS22_PRESS_OUT_XL 0x28

#define LPS22_ID_HB 0xB1
#define LPS22_ID_HH 0xB3

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint16_t address; // 8-Bit HAL-Adresse (7-Bit << 1)
    uint8_t id;       // WHO_AM_I-Wert
} LPS22_Handle;

// Sucht den Sensor auf 0x5D und 0x5C und startet die Messung mit 10 Hz
HAL_StatusTypeDef LPS22_Init(LPS22_Handle *dev, I2C_HandleTypeDef *hi2c);
// pressure in hPa, temperature in Grad C
HAL_StatusTypeDef LPS22_Read(LPS22_Handle *dev, float *pressure, float *temperature);

#endif // LPS22_H
