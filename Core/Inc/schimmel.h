// schimmel.h
// Schimmelrisiko: schaetzt die Feuchte an einer kalten Wandflaeche aus Raumtemperatur und -feuchte
// und zaehlt, wie lange sie in den letzten 24 h kritisch war.
#ifndef SCHIMMEL_H
#define SCHIMMEL_H

#include <stdint.h>

typedef enum
{
    SCHIMMEL_GERING,   // Wandfeuchte < 70 %
    SCHIMMEL_ERHOEHT,  // 70..80 %
    SCHIMMEL_HOCH,     // >= 80 %: Schimmel kann wachsen, wenn das lange anhaelt
    SCHIMMEL_KONDENSAT // >= 100 %: Wasser schlaegt sich an der Wand nieder
} SchimmelLevel;

void Schimmel_Init(void);
// Regelmaessig mit den aktuellen Raumwerten aufrufen.
// wallDelta = so viel kaelter als die Raumluft wird die Wand angenommen (Grad C).
// Gibt 1 zurueck, wenn die Warnung neu ausgeloest oder beendet wurde.
uint8_t Schimmel_Update(uint32_t now, float temperature, float humidity, float wallDelta);
void Schimmel_Reset(void);

SchimmelLevel Schimmel_GetLevel(void);
float Schimmel_GetWallHumidity(void); // geschaetzte relative Feuchte an der Wand in %
uint16_t Schimmel_GetCriticalMinutes(void); // Minuten mit Wandfeuchte >= 80 % in den letzten 24 h
uint8_t Schimmel_IsWarning(void);           // kritische Zeit ueberschritten
const char *Schimmel_LevelName(SchimmelLevel level);

#endif // SCHIMMEL_H
