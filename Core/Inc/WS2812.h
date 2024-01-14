// WS2812Controller.h
#ifndef WS2812CONTROLLER_H
#define WS2812CONTROLLER_H

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

#define NUM_LEDS 12
#define HIGH_BIT 61
#define LOW_BIT 31
#define TRUE 1
#define FALSE 0
#define DELAY 60
extern const float _PI;
typedef struct
{
    TIM_HandleTypeDef *htim;
    UART_HandleTypeDef *huart;
    uint32_t channel;
    uint16_t numLeds;
    uint8_t ledData[NUM_LEDS * 3];
    uint8_t brightness;
} WS2812Controller;

void calculatePulseWidth(uint8_t value, uint16_t *pulseWith);
void WS2812_Init(WS2812Controller *controller, TIM_HandleTypeDef *htim, UART_HandleTypeDef *huart, uint32_t channel, uint16_t numLeds);
void WS2812_SetColor(WS2812Controller *controller, uint16_t index, uint8_t red, uint8_t green, uint8_t blue);
void WS2812_SetBrightness(WS2812Controller *controller, uint8_t brightness);
void WS2812_Update(WS2812Controller *controller);
void WS2812_Reset(WS2812Controller *controller);
void WS2812_Print(WS2812Controller *controller);
void UART_NewLine(WS2812Controller *controller);

#endif // WS2812CONTROLLER_H
