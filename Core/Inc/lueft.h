// lueft.h
// Lueft-Assistent: erkennt, wann gelueftet werden muss, ob das Fenster offen ist
// und wann es wieder zu kann. Reine Logik, Anzeige und LEDs macht app.c.
#ifndef LUEFT_H
#define LUEFT_H

#include <stdint.h>

typedef enum
{
    LUEFT_AUS,    // alles ok
    LUEFT_NOETIG, // Luftfeuchte zu hoch, bitte lueften
    LUEFT_LAEUFT, // Fenster offen erkannt
    LUEFT_FERTIG  // Fenster kann wieder zu
} LueftState;

typedef enum
{
    LUEFT_GRUND_KEINER,
    LUEFT_GRUND_FEUCHTE_OK, // Ziel-Feuchte erreicht
    LUEFT_GRUND_ZEIT,       // maximale Lueftdauer erreicht
    LUEFT_GRUND_ZU_KALT     // Raum zu stark abgekuehlt
} LueftGrund;

void Lueft_Init(void);
// Regelmaessig mit den aktuellen Werten des Feuchtesensors aufrufen.
// humWet = Feuchtegrenze aus den Einstellungen. Gibt 1 zurueck, wenn sich der Zustand geaendert hat.
uint8_t Lueft_Update(uint32_t now, float temperature, float humidity, float humWet);
// Sensor ausgefallen: Assistent ausschalten
uint8_t Lueft_Reset(void);

LueftState Lueft_GetState(void);
LueftGrund Lueft_GetGrund(void);
// Sekunden seit Beginn des Lueftens (nur in LUEFT_LAEUFT sinnvoll)
uint32_t Lueft_GetDurationS(uint32_t now);
const char *Lueft_StateName(LueftState state);

#endif // LUEFT_H
