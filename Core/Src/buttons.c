
#include "buttons.h"

#define DEBOUNCE_COUNT 3 // 3 x 10 ms stabil = gedrueckt

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} ButtonPin;

static const ButtonPin pins[] = {
    {GPIOB, GPIO_PIN_8}, // A
    {GPIOC, GPIO_PIN_6}, // B
    {GPIOC, GPIO_PIN_7}, // C
};
#define NUM_BUTTONS (sizeof(pins) / sizeof(pins[0]))

static uint8_t stable;                // entprellter Zustand, Bit gesetzt = gedrueckt
static uint8_t counter[NUM_BUTTONS];

void Buttons_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // Am FeatherWing haben nicht alle Taster einen Pull-up, deshalb intern aktivieren
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    {
        GPIO_InitStruct.Pin = pins[i].pin;
        HAL_GPIO_Init(pins[i].port, &GPIO_InitStruct);
    }
}

uint8_t Buttons_Poll(void)
{
    uint8_t pressedEvents = 0;
    for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    {
        uint8_t raw = (HAL_GPIO_ReadPin(pins[i].port, pins[i].pin) == GPIO_PIN_RESET);
        uint8_t current = (stable >> i) & 1;

        if (raw == current)
        {
            counter[i] = 0;
            continue;
        }
        if (++counter[i] >= DEBOUNCE_COUNT)
        {
            counter[i] = 0;
            stable ^= (1 << i);
            if (raw)
                pressedEvents |= (1 << i);
        }
    }
    return pressedEvents;
}
