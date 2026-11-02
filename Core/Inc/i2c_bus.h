// i2c_bus.h
// I2C1 auf dem Feather STM32F405: SCL = PB6, SDA = PB7 (auch am Qwiic/STEMMA-QT-Anschluss)
#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "stm32f4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

void I2C_Bus_Init(void);
// Sucht alle Geraete am Bus und gibt die Adressen ueber die Funktion print aus
void I2C_Bus_Scan(void (*print)(const char *str));

#endif // I2C_BUS_H
