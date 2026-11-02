// console.h
// Befehlszeile ueber USB-CDC: sammelt empfangene Zeichen zu Zeilen und uebergibt sie an einen Handler
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

#define CONSOLE_LINE_MAX 64
#define CONSOLE_ECHO 1 // 1 = getippte Zeichen zuruecksenden (fuer Terminals ohne lokales Echo)

typedef void (*Console_Handler)(char *line);

void Console_Init(Console_Handler handler);
// Aus CDC_Receive_FS aufrufen (USB-Interrupt)
void Console_Receive(const uint8_t *data, uint32_t len);
// In der Hauptschleife aufrufen: verarbeitet empfangene Zeichen
void Console_Process(void);

#endif // CONSOLE_H
