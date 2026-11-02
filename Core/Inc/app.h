// app.h
// Anwendung: LPS22 messen, auf dem SH1107 anzeigen, Temperatur-Ampel auf den WS2812
#ifndef APP_H
#define APP_H

#include "WS2812.h"

void App_Init(WS2812Controller *leds, UART_HandleTypeDef *huart);
// In der Hauptschleife so oft wie moeglich aufrufen, blockiert nicht
void App_Process(void);

#endif // APP_H
