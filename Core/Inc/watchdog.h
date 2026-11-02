// watchdog.h
// Unabhaengiger Watchdog (IWDG): startet das Board neu, wenn die Hauptschleife haengt.
// Einmal gestartet laeuft er bis zum naechsten Reset weiter. Ein Software-Reset (z.B. "dfu")
// stoppt ihn, deshalb laeuft der DFU-Bootloader ohne Watchdog.
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

#define WATCHDOG_TIMEOUT_MS 4000 // nominal; der LSI-Takt schwankt, im schlechtesten Fall ~2.7 s

// Liest den Grund des letzten Resets und loescht die Reset-Flags. Ganz am Anfang aufrufen.
void Watchdog_ReadResetCause(void);
// Startet den Watchdog. Erst aufrufen, wenn die Initialisierung fertig ist.
void Watchdog_Start(void);
// Watchdog zuruecksetzen, mindestens alle WATCHDOG_TIMEOUT_MS / 2 aufrufen
void Watchdog_Feed(void);

uint8_t Watchdog_IsRunning(void);
uint8_t Watchdog_WasWatchdogReset(void);
const char *Watchdog_ResetCauseName(void);

#endif // WATCHDOG_H
