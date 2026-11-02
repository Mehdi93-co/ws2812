// buttons.h
// Taster A/B/C des OLED FeatherWing: A = D9 (PB8), B = D6 (PC6), C = D5 (PC7), aktiv Low
#ifndef BUTTONS_H
#define BUTTONS_H

#include "stm32f4xx_hal.h"

#define BUTTON_A 0x01
#define BUTTON_B 0x02
#define BUTTON_C 0x04

void Buttons_Init(void);
// Alle ~10 ms aufrufen. Gibt die seit dem letzten Aufruf neu gedrueckten Taster als Bitmaske zurueck.
uint8_t Buttons_Poll(void);

#endif // BUTTONS_H
