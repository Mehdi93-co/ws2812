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
    uint8_t dimMinutes; // Bildschirmschoner: dimmen nach x min ohne Tastendruck (0 = nie)
    uint8_t offMinutes; // Bildschirmschoner: aus nach x min ohne Tastendruck (0 = nie)
    float wallDelta;    // Schimmelrisiko: so viel kaelter als die Raumluft ist die Wand (Grad C)
    uint8_t wallSensor; // 1 = BMP180/BMP280 sitzt an der Wand und misst die Wandtemperatur
} Settings;

// Laedt den letzten gueltigen Eintrag. Gibt 0 zurueck (und laesst *s unveraendert), wenn keiner existiert.
// Felder, die ein aelterer Eintrag noch nicht kennt, behalten ihren Wert in *s.
uint8_t Settings_Load(Settings *s);
HAL_StatusTypeDef Settings_Save(const Settings *s);

#endif // SETTINGS_H
