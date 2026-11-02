
#include "WS2812.h"
#include <stdlib.h>
#include "math.h"

const float _PI = 3.14159;
void calculatePulseWidth(uint8_t value, uint16_t *pulseWith)
{
    if (value)
    {
        *pulseWith = HIGH_BIT; // Adjust based on your timing requirements
    }
    else
    {
        *pulseWith = LOW_BIT; // Adjust based on your timing requirements
    }
}


void WS2812_Init(WS2812Controller *controller, TIM_HandleTypeDef *htim, UART_HandleTypeDef *huart, uint32_t channel, uint16_t numLeds)
{
    char str[] = "WS2812_Init\r\n";
    controller->huart = huart;
    controller->htim = htim;
    controller->channel = channel;
    controller->numLeds = (numLeds > NUM_LEDS) ? NUM_LEDS : numLeds; // ledData hat nur Platz fuer NUM_LEDS
    controller->brightness = 255;
    for (uint16_t i = 0; i < controller->numLeds * 3; i++)
    {
        controller->ledData[i] = 0;
    }
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
}

void WS2812_SetColor(WS2812Controller *controller, uint16_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    char str[80];
    if (index < controller->numLeds)
    {
        /*        float angle = 90 - controller->brightness; // in Grad
               angle = angle * _PI / 180;                 // in Rad
               // Apply brightness
               red = red / tan(angle);
               green = green / tan(angle);
               blue = blue / tan(angle); */
        float brightnessFactor = controller->brightness / 255.0f; // brightness: 0-255
        red = (uint8_t)(red * brightnessFactor);
        green = (uint8_t)(green * brightnessFactor);
        blue = (uint8_t)(blue * brightnessFactor);
        // Gamma-Korrektur anwenden
        red = gammaCorrect(red);
        green = gammaCorrect(green);
        blue = gammaCorrect(blue);
#ifdef Debug_Mode
        sprintf(str, "\tRED   [%u]\r\n", red);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
        sprintf(str, "\tGREEN [%u]\r\n", green);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
        sprintf(str, "\tBLUE  [%u]\r\n", blue);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif
        controller->ledData[index * 3] = green;
        controller->ledData[index * 3 + 1] = red;
        controller->ledData[index * 3 + 2] = blue;
#ifdef Debug_Mode
        sprintf(str, "\tred   [%u]\r\n", controller->ledData[index * 3 + 1]);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
        sprintf(str, "\tgreen [%u]\r\n", controller->ledData[index * 3]);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
        sprintf(str, "\tblue  [%u]\r\n", controller->ledData[index * 3 + 2]);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif
    }
}

void WS2812_SetBrightness(WS2812Controller *controller, uint8_t brightness)
{
    if (brightness > 255)
        controller->brightness = 255;
    else
        controller->brightness = brightness;
}

void WS2812_Update(WS2812Controller *controller)
{
    // Debug string
    char str[50];

    // static: die DMA liest den Puffer noch, nachdem die Funktion zurueckgekehrt ist
    static uint16_t pwmData[(NUM_LEDS * 24) + DELAY]; // 24 bit/LED + Reset-Pause

    // Warten, bis der vorherige DMA-Transfer fertig ist, bevor der Puffer ueberschrieben wird
    uint32_t start = HAL_GetTick();
    while (HAL_TIM_GetChannelState(controller->htim, controller->channel) != HAL_TIM_CHANNEL_STATE_READY)
    {
        if (HAL_GetTick() - start > 100)
            return;
    }

    // Convert RGB data to PWM data
    for (uint16_t index = 0; index < ((controller->numLeds * 24) + DELAY); index++)
    {
        pwmData[index] = 0;
    }

    uint32_t pwmIndex = 0;
    uint32_t color = 0;
    for (uint16_t i = 0; i < (controller->numLeds); i++)
    {
        // Extract RGB values
        uint8_t red = controller->ledData[i * 3 + 1];
        uint8_t green = controller->ledData[i * 3];
        uint8_t blue = controller->ledData[i * 3 + 2];
        // Convert RGB data to PWM data
        color = (green << 16) | (red << 8) | (blue << 0);
        for (uint16_t j = 24; j >= 1; j--)
        {
            uint16_t pulseWith = 0;
            // calculatePulseWidth(((color >> (j - 1)) & 0x01), &pulseWith);
            if (((color >> (j - 1)) & 0x01))
                pulseWith = HIGH_BIT; // Adjust based on your timing requirements
            else
                pulseWith = LOW_BIT; // Adjust based on your timing requirements
            pwmData[pwmIndex++] = pulseWith;
        }

        // Add 50-bit delay
    }
    for (uint8_t delay = 0; delay < DELAY; delay++)
    {
        pwmData[pwmIndex++] = 0;
    }
#ifdef Debug_Mode
    for (uint16_t i = 0; i < pwmIndex; i++)
    {
        sprintf(str, "pwmData[%u] = %u\r\n", i, pwmData[i]);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    }
#endif

    if (HAL_TIM_PWM_Start_DMA(controller->htim, controller->channel, (uint32_t *)pwmData, pwmIndex) != HAL_OK)
    {
        sprintf(str, "WS2812_Update: DMA start failed\r\n");
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    }
}

void WS2812_Reset(WS2812Controller *controller)
{
    // Debug string
    char str[32];
    WS2812_SetBrightness(controller, 1);
    for (uint16_t i = 0; i < (controller->numLeds); ++i)
    {
        sprintf(str, "Reset LED [%u]\r\n", i);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
        WS2812_SetColor(controller, i, 0, 0, 0);
    }
    WS2812_Update(controller);
}

void WS2812_Deinit(WS2812Controller *controller)
{
}

void WS2812_Print(WS2812Controller *controller)
{
    char str[80];
    sprintf(str, "Channel: %lu\r\n", controller->channel);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    sprintf(str, "numLeds: %d\r\n", controller->numLeds);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    sprintf(str, "brightness: %d\r\n", controller->brightness);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    for (uint16_t i = 0; i < controller->numLeds * 3; i++)
    {
        sprintf(str, "[%d] ", controller->ledData[i]);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    }
    HAL_UART_Transmit(controller->huart, (uint8_t *)"\r\n", 2, 100);
}

void UART_NewLine(WS2812Controller *controller)
{
    //  HAL_UART_Transmit(controller->huart, "\r\n", strlen("\r\n"), 100);
}

uint8_t gammaCorrect(uint8_t value)
{
    float gamma = 2.8; // Typischer Gamma-Wert für LEDs
    return (uint8_t)(pow((float)value / 255.0, gamma) * 255.0);
}
