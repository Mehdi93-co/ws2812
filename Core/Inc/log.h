// log.h
// Text-Ausgabe ueber USB-CDC (virtueller COM-Port), optional zusaetzlich ueber UART.
// Gesendet wird nur, solange ein Terminal geoeffnet ist (DTR gesetzt).
#ifndef LOG_H
#define LOG_H

#include "stm32f4xx_hal.h"

// 1 = Ausgaben zusaetzlich auf dem UART (USART3, 115200 Baud)
#define LOG_ALSO_UART 0

void Log_Init(UART_HandleTypeDef *huart);
// Haengt Text an den Sendepuffer an, blockiert nicht
void Log_Print(const char *str);
// In der Hauptschleife aufrufen: schickt den Pufferinhalt per USB
void Log_Process(void);
// Liefert einmalig 1, nachdem am PC ein Terminal geoeffnet wurde
uint8_t Log_TerminalOpened(void);
// Wird von usbd_cdc_if.c aufgerufen (SET_CONTROL_LINE_STATE)
void Log_SetDtr(uint8_t dtr);

#endif // LOG_H
