// settings.h
// Einstellungen dauerhaft im Flash speichern (Sektor 11, 0x080E0000).
// Jeder Speichervorgang haengt einen neuen Eintrag an, der letzte gueltige gilt.
// Geloescht wird der Sektor erst, wenn alle Plaetze belegt sind (schont den Flash).
#ifndef SETTINGS_H
#define SETTINGS_H

#include "stm32f4xx_hal.h"

typedef struct
{
    float tempLow;      // Ampel gelb ab dieser Temperatur (und darunter)
    float tempHigh;     // Ampel rot ab dieser Temperatur
    float humWet;       // Regenbogen / Lueften ab dieser Luftfeuchte
    uint8_t brightness; // LED-Helligkeit 0..255
    uint8_t page;       // zuletzt gewaehlte Display-Seite
} Settings;

// Laedt den letzten gueltigen Eintrag. Gibt 0 zurueck (und laesst *s unveraendert), wenn keiner existiert.
uint8_t Settings_Load(Settings *s);
HAL_StatusTypeDef Settings_Save(const Settings *s);

#endif // SETTINGS_H
