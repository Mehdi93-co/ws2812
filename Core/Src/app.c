
#include "app.h"
#include "i2c_bus.h"
#include "lps22.h"
#include "bmp.h"
#include "htu21.h"
#include "sh1107.h"
#include "buttons.h"
#include "log.h"
#include "console.h"
#include "bootloader.h"
#include "settings.h"
#include "lueft.h"
#include "schimmel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Einstellungen ---------- */
// Temperatur-Ampel in Grad C
#define TEMP_LOW_DEFAULT 18.0f
#define TEMP_HIGH_DEFAULT 26.0f
#define TEMP_HYST 0.5f // Hysterese, damit die Ampel an der Grenze nicht flackert

#define BRIGHTNESS_DEFAULT 128     // LED-Helligkeit 0..255
#define SETTINGS_SAVE_DELAY_MS 3000 // erst speichern, wenn sich 3 s nichts mehr geaendert hat

#define FILTER_ALPHA 0.2f // Glaettung 0..1, kleiner = ruhiger, aber traeger

#define MEASURE_INTERVAL_MS 200
#define DISPLAY_INTERVAL_MS 500
#define DISPLAY_POWERUP_MS 250 // Wartezeit nach dem Einschalten, wie in den Adafruit-Beispielen
#define DISPLAY_SELFTEST 1     // 1 = Testbild beim Start (zur Fehlersuche)

// Bildschirmschoner (schuetzt das OLED vor dem Einbrennen), Zeiten per "set dim/off" aenderbar
#define SAVER_DIM_MIN_DEFAULT 2  // nach 2 min ohne Tastendruck dimmen
#define SAVER_OFF_MIN_DEFAULT 10 // nach 10 min ausschalten

// Schimmelrisiko: angenommene Abkuehlung der Wand gegenueber der Raumluft, per "set wall" aenderbar.
// Typisch: Aussenwand im Winter 3-5 C, schlecht gedaemmt bis 8 C, Innenwand/Sommer 0-1 C
#define WALL_DELTA_DEFAULT 4.0f
#define DISPLAY_DIM_CONTRAST 0x05
#define PRINT_INTERVAL_MS 1000
#define BUTTON_INTERVAL_MS 10
#define SENSOR_RETRY_MS 2000
#define SENSOR_MAX_ERRORS 3
#define BMP_INTERVAL_MS 1000 // zweiter Sensor (BMP180/BMP280/BME280)
#define HTU_INTERVAL_MS 2000 // Feuchtesensor HTU2x

// Bewertung der Luftfeuchte in %rH
#define HUM_DRY 30.0f
#define HUM_WET_DEFAULT 60.0f // darueber: Lueften-Anzeige (Regenbogen-Lauflicht)
#define HUM_HYST 2.0f // Regenbogen geht erst unter cfg.humWet - HUM_HYST wieder aus

// Regenbogen-Lauflicht
#define RAINBOW_FRAME_MS 30 // Bildwechsel, kleiner = schneller
#define RAINBOW_STEP 4      // Farbverschiebung pro Bild (0..255 = ein Umlauf)
#define BREATH_PERIOD_MS 3000 // Lueften laeuft: blaues "Atmen", Dauer eines Atemzugs
#define DONE_BLINK_MS 500     // Fenster kann zu: gruenes Blinken
#define MOLD_PULSE_PERIOD_MS 4000 // Schimmel-Warnung: violettes Pulsieren

// Verlaufsdiagramm: ein Punkt pro Pixelspalte
#define GRAPH_INTERVAL_MS 10000UL // 128 Punkte x 10 s = ca. 21 min

// Wettertrend: Druckaenderung ueber 3 Stunden
#define TREND_INTERVAL_MS 600000UL // alle 10 min ein Wert
#define TREND_SAMPLES 19           // 18 x 10 min = 3 h
#define TREND_MIN_SAMPLES 4        // Trend erst nach 30 min anzeigen
#define TREND_THRESHOLD 1.6f       // hPa pro 3 h

/* ---------- Typen ---------- */
typedef enum
{
    AMPEL_GELB,  // zu kalt
    AMPEL_GRUEN, // ok
    AMPEL_ROT,   // zu warm
    AMPEL_FEHLER // Sensor weg
} AmpelState;

typedef enum
{
    TREND_UNBEKANNT,
    TREND_FALLEND,
    TREND_STABIL,
    TREND_STEIGEND
} Trend;

typedef enum
{
    PAGE_WERTE,
    PAGE_SENSOR2,
    PAGE_FEUCHTE,
    PAGE_VERLAUF,
    PAGE_MINMAX,
    PAGE_COUNT
} Page;

typedef struct
{
    float *data;
    uint8_t size;
    uint8_t head;
    uint8_t count;
} RingBuffer;

/* ---------- Zustand ---------- */
static WS2812Controller *leds;
static LPS22_Handle lps22;

// Einstellungen, per USB-Befehl "set" aenderbar und im Flash gespeichert
static const Settings cfgDefaults = {
    .tempLow = TEMP_LOW_DEFAULT,
    .tempHigh = TEMP_HIGH_DEFAULT,
    .humWet = HUM_WET_DEFAULT,
    .brightness = BRIGHTNESS_DEFAULT,
    .page = 0,
    .dimMinutes = SAVER_DIM_MIN_DEFAULT,
    .offMinutes = SAVER_OFF_MIN_DEFAULT,
    .wallDelta = WALL_DELTA_DEFAULT,
    .wallSensor = 1, // BMP an der Wand nutzen, wenn vorhanden
};
static Settings cfg;
static uint8_t cfgDirty;  // geaendert, aber noch nicht gespeichert
static uint8_t cfgLoaded; // beim Start aus dem Flash geladen
static uint32_t tCfgChanged;

static uint8_t displayOk;
static uint8_t displayOn = 1;
static uint8_t displayDirty = 1;
static Page page = PAGE_WERTE;

static uint8_t sensorOk;
static uint8_t sensorErrors;
static uint8_t haveValues;
static float pressure, temperature; // geglaettet
static float tempMin, tempMax, pressMin, pressMax;

static BMP_Handle bmp;
static uint8_t bmpOk;
static uint8_t bmpErrors;
static uint8_t bmpHaveValues;
static uint8_t bmpRequest; // Messung laeuft
static float bmpTemp, bmpPress, bmpHum;
static uint32_t tBmp, tBmpRetry;

static HTU21_Handle htu;
static uint8_t htuOk;
static uint8_t htuErrors;
static uint8_t htuHaveValues;
static uint8_t htuRequest; // Messung laeuft
static float htuTemp, htuHum;
static uint32_t tHtu, tHtuRetry;

static AmpelState ampel = AMPEL_GRUEN;
static AmpelState ampelShown = (AmpelState)0xFF; // was zuletzt auf den LEDs war

static uint8_t rainbowOffset;
static uint32_t tRainbow; // letztes Bild der Lueft-Animation

// Wer die LEDs gerade steuert, nach Wichtigkeit:
// 1. Lueft-Assistent (der Nutzer soll etwas tun), 2. Schimmel-Warnung, 3. Temperatur-Ampel
typedef enum
{
    LED_AMPEL,
    LED_LUEFT,
    LED_SCHIMMEL
} LedMode;

static LedMode ledMode(void)
{
    if (Lueft_GetState() != LUEFT_AUS)
        return LED_LUEFT;
    if (Schimmel_IsWarning())
        return LED_SCHIMMEL;
    return LED_AMPEL;
}

static float graphData[SH1107_WIDTH];
static RingBuffer graph = {graphData, SH1107_WIDTH, 0, 0};
static float trendData[TREND_SAMPLES];
static RingBuffer trend = {trendData, TREND_SAMPLES, 0, 0};

static uint32_t tDisplayRetry;
static uint32_t tMeasure, tDisplay, tPrint, tButtons, tSensorRetry, tGraph, tTrend;

/* ---------- Hilfsfunktionen ---------- */
// Gespeichert wird verzoegert (SETTINGS_SAVE_DELAY_MS), damit schnelles Blaettern nur einen Eintrag erzeugt
static void markCfgChanged(void)
{
    cfgDirty = 1;
    tCfgChanged = HAL_GetTick();
}

/* ---------- Bildschirmschoner ---------- */
typedef enum
{
    SAVER_AKTIV,   // normale Helligkeit
    SAVER_GEDIMMT, // dunkel, aber ablesbar
    SAVER_AUS      // Display aus
} SaverState;

static SaverState saver = SAVER_AKTIV;
static uint32_t tActivity; // letzter Tastendruck oder wichtiges Ereignis

// Sichtbar = nicht per Taster C ausgeschaltet und nicht vom Bildschirmschoner abgeschaltet
static uint8_t displayVisible(void)
{
    return displayOn && saver != SAVER_AUS;
}

static void applyDisplayPower(void)
{
    if (!displayOk)
        return;
    SH1107_SetContrast(saver == SAVER_GEDIMMT ? DISPLAY_DIM_CONTRAST : SH1107_CONTRAST_NORMAL);
    SH1107_SetDisplayOn(displayVisible());
}

// Taster oder wichtiges Ereignis: Bildschirmschoner beenden
static void wakeDisplay(void)
{
    tActivity = HAL_GetTick();
    if (saver == SAVER_AKTIV)
        return;
    saver = SAVER_AKTIV;
    applyDisplayPower();
    displayDirty = 1;
    Log_Print("Bildschirmschoner: Display aufgeweckt\r\n");
}

static void updateScreensaver(uint32_t now)
{
    // Waehrend gelueftet wird (Timer) und bei "Fenster kann zu" wach bleiben. Diese Zustaende dauern
    // nur wenige Minuten. "Bitte lueften" weckt nur einmal, sonst wuerde das Display bei
    // dauerhaft hoher Feuchte nie geschont (der Regenbogen zeigt die Aufforderung ja weiter an).
    LueftState lueftState = Lueft_GetState();
    if (lueftState == LUEFT_LAEUFT || lueftState == LUEFT_FERTIG)
        tActivity = now;

    uint32_t idleMinutes = (now - tActivity) / 60000UL;
    SaverState wanted = SAVER_AKTIV;

    if (cfg.offMinutes && idleMinutes >= cfg.offMinutes)
        wanted = SAVER_AUS;
    else if (cfg.dimMinutes && idleMinutes >= cfg.dimMinutes)
        wanted = SAVER_GEDIMMT;

    if (wanted == saver)
        return;
    saver = wanted;
    applyDisplayPower();
    displayDirty = 1;
    if (saver == SAVER_AUS)
        Log_Print("Bildschirmschoner: Display aus\r\n");
    else if (saver == SAVER_GEDIMMT)
        Log_Print("Bildschirmschoner: Display gedimmt\r\n");
    else
        Log_Print("Bildschirmschoner: Display normal\r\n");
}

static const char *saverName(void)
{
    switch (saver)
    {
    case SAVER_GEDIMMT:
        return "gedimmt";
    case SAVER_AUS:
        return "aus";
    default:
        return "normal";
    }
}

// Float mit einer Nachkommastelle formatieren (nano-libc kann ohne Zusatzflag kein %f)
static void formatFixed1(char *out, float value)
{
    int32_t v = (int32_t)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    const char *sign = (v < 0) ? "-" : "";
    if (v < 0)
        v = -v;
    sprintf(out, "%s%ld.%ld", sign, (long)(v / 10), (long)(v % 10));
}

static void ringPush(RingBuffer *rb, float value)
{
    rb->data[rb->head] = value;
    rb->head = (rb->head + 1) % rb->size;
    if (rb->count < rb->size)
        rb->count++;
}

// i = 0 ist der aelteste Wert
static float ringGet(const RingBuffer *rb, uint8_t i)
{
    return rb->data[(rb->head + rb->size - rb->count + i) % rb->size];
}

static void resetMinMax(void)
{
    tempMin = tempMax = temperature;
    pressMin = pressMax = pressure;
}

/* ---------- Ampel ---------- */
static AmpelState nextAmpelState(AmpelState state, float t)
{
    // Aus Rot/Gelb erst zurueck, wenn die Grenze um TEMP_HYST unterschritten ist
    if (state == AMPEL_ROT && t < cfg.tempHigh - TEMP_HYST)
        state = AMPEL_GRUEN;
    else if (state == AMPEL_GELB && t > cfg.tempLow + TEMP_HYST)
        state = AMPEL_GRUEN;
    else if (state == AMPEL_FEHLER)
        state = AMPEL_GRUEN;

    if (state == AMPEL_GRUEN)
    {
        if (t >= cfg.tempHigh)
            state = AMPEL_ROT;
        else if (t <= cfg.tempLow)
            state = AMPEL_GELB;
    }
    return state;
}

static void showAmpel(AmpelState state)
{
    uint8_t red = 0, green = 0, blue = 0;
    // Waehrend des Lueftens gehoeren die LEDs dem Lueft-Assistenten
    if (ledMode() != LED_AMPEL || state == ampelShown)
        return;
    ampelShown = state;

    switch (state)
    {
    case AMPEL_ROT:
        red = 255;
        break;
    case AMPEL_GELB:
        red = 255;
        green = 180;
        break;
    case AMPEL_GRUEN:
        green = 255;
        break;
    case AMPEL_FEHLER:
        blue = 255;
        break;
    }
    for (uint16_t i = 0; i < leds->numLeds; i++)
    {
        WS2812_SetColor(leds, i, red, green, blue);
    }
    WS2812_Update(leds);
}

/* ---------- Lueften: Regenbogen-Lauflicht ---------- */
// Farbrad: hue 0..255 -> rot, gelb, gruen, cyan, blau, magenta, zurueck zu rot
static void hueToRgb(uint8_t hue, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    uint8_t region = hue / 43;
    uint8_t rise = (uint8_t)((hue - region * 43) * 6);
    uint8_t fall = 255 - rise;

    switch (region)
    {
    case 0:
        *red = 255, *green = rise, *blue = 0;
        break;
    case 1:
        *red = fall, *green = 255, *blue = 0;
        break;
    case 2:
        *red = 0, *green = 255, *blue = rise;
        break;
    case 3:
        *red = 0, *green = fall, *blue = 255;
        break;
    case 4:
        *red = rise, *green = 0, *blue = 255;
        break;
    default:
        *red = 255, *green = 0, *blue = fall;
        break;
    }
}

static void drawRainbow(void)
{
    uint8_t red, green, blue;
    for (uint16_t i = 0; i < leds->numLeds; i++)
    {
        // Ein voller Farbkreis ueber alle LEDs verteilt, der sich jedes Bild weiterdreht
        uint8_t hue = rainbowOffset + (uint8_t)(i * 256 / leds->numLeds);
        hueToRgb(hue, &red, &green, &blue);
        WS2812_SetColor(leds, i, red, green, blue);
    }
    WS2812_Update(leds);
    rainbowOffset += RAINBOW_STEP;
}

static void drawSolid(uint8_t red, uint8_t green, uint8_t blue)
{
    for (uint16_t i = 0; i < leds->numLeds; i++)
    {
        WS2812_SetColor(leds, i, red, green, blue);
    }
    WS2812_Update(leds);
}

// 0..1..0 im Takt von periodMs, fuer "atmende" bzw. pulsierende LEDs
static float pulseLevel(uint32_t now, uint32_t periodMs)
{
    float phase = (float)(now % periodMs) / periodMs;
    return 0.5f - 0.5f * cosf(6.2831853f * phase);
}

// LED-Animationen von Lueft-Assistent und Schimmel-Warnung, sonst die Ampel
//   Lueften noetig = Regenbogen, laeuft = blau atmend, fertig = gruen blinkend
//   Schimmel-Warnung = violett pulsierend
static void animateLeds(uint32_t now)
{
    static LedMode lastMode = LED_AMPEL;
    LedMode mode = ledMode();

    if (mode != lastMode)
    {
        lastMode = mode;
        if (mode == LED_AMPEL)
        {
            // Animation vorbei: Ampel neu zeichnen, die LEDs zeigen noch das letzte Bild
            ampelShown = (AmpelState)0xFF;
            showAmpel(ampel);
            return;
        }
    }
    if (mode == LED_AMPEL || now - tRainbow < RAINBOW_FRAME_MS)
        return;
    tRainbow = now;

    if (mode == LED_SCHIMMEL)
    {
        float level = pulseLevel(now, MOLD_PULSE_PERIOD_MS);
        drawSolid((uint8_t)(20.0f + 140.0f * level), 0, (uint8_t)(30.0f + 225.0f * level));
        return;
    }

    switch (Lueft_GetState())
    {
    case LUEFT_NOETIG:
        drawRainbow();
        break;
    case LUEFT_LAEUFT:
        drawSolid(0, 0, (uint8_t)(40.0f + 215.0f * pulseLevel(now, BREATH_PERIOD_MS)));
        break;
    case LUEFT_FERTIG:
        if ((now / DONE_BLINK_MS) & 1)
            drawSolid(0, 0, 0);
        else
            drawSolid(0, 255, 0);
        break;
    default:
        break;
    }
}

// Text fuer die Statuszeile, NULL wenn der Lueft-Assistent nichts zu melden hat
static const char *lueftStatusText(uint32_t now, char *buf)
{
    uint32_t s;
    switch (Lueft_GetState())
    {
    case LUEFT_NOETIG:
        return "Bitte lueften!";
    case LUEFT_LAEUFT:
        s = Lueft_GetDurationS(now);
        sprintf(buf, "Lueften %02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
        return buf;
    case LUEFT_FERTIG:
        return (Lueft_GetGrund() == LUEFT_GRUND_ZU_KALT) ? "Zu kalt: Fenster zu!" : "Fenster kann zu!";
    default:
        return NULL;
    }
}

static void updateLueften(uint32_t now)
{
    uint8_t changed;
    if (!htuOk || !htuHaveValues)
        changed = Lueft_Reset();
    else
        changed = Lueft_Update(now, htuTemp, htuHum, cfg.humWet);
    if (!changed)
        return;

    switch (Lueft_GetState())
    {
    case LUEFT_NOETIG:
        Log_Print("Luftfeuchte hoch: bitte lueften!\r\n");
        break;
    case LUEFT_LAEUFT:
        Log_Print("Fenster offen erkannt, Lueften laeuft\r\n");
        break;
    case LUEFT_FERTIG:
        switch (Lueft_GetGrund())
        {
        case LUEFT_GRUND_FEUCHTE_OK:
            Log_Print("Luftfeuchte ok: Fenster kann zu\r\n");
            break;
        case LUEFT_GRUND_ZU_KALT:
            Log_Print("Raum zu stark abgekuehlt: Fenster schliessen\r\n");
            break;
        default:
            Log_Print("Genug gelueftet: Fenster kann zu\r\n");
            break;
        }
        break;
    default:
        Log_Print("Lueft-Assistent: alles ok\r\n"); // Ampel zeichnet animateLeds() neu
        break;
    }
    // Hinweise zum Lueften sollen sichtbar sein, auch wenn der Bildschirmschoner aktiv ist
    if (Lueft_GetState() != LUEFT_AUS)
        wakeDisplay();
    displayDirty = 1;
}

/* ---------- Schimmelrisiko ---------- */
// Zeile fuer das Display, max. 21 Zeichen
static void schimmelText(char *out)
{
    uint16_t minutes = Schimmel_GetCriticalMinutes();
    char duration[12];

    if (minutes >= 60)
        sprintf(duration, "%uh", minutes / 60);
    else
        sprintf(duration, "%umin", minutes);

    if (Schimmel_IsWarning())
        sprintf(out, "Schimmel-WARNUNG %s", duration);
    else if (Schimmel_GetLevel() == SCHIMMEL_KONDENSAT)
        strcpy(out, "Schimmel: Kondensat!");
    else if (Schimmel_GetLevel() == SCHIMMEL_HOCH)
        sprintf(out, "Schimmel: hoch %s", duration);
    else
        sprintf(out, "Schimmel: %s", Schimmel_LevelName(Schimmel_GetLevel()));
}

// Wandtemperatur gemessen, wenn der BMP als Wandsensor eingestellt ist und Werte liefert
static uint8_t wallMeasured(void)
{
    return cfg.wallSensor && bmpOk && bmpHaveValues;
}

// Gemessen vom BMP an der Wand, sonst geschaetzt aus Raumtemperatur minus "set wall"
static float wallTemperature(void)
{
    return wallMeasured() ? bmpTemp : htuTemp - cfg.wallDelta;
}

static void updateSchimmel(uint32_t now)
{
    static int8_t lastMeasured = -1;

    // Ohne Feuchtesensor nichts bewerten, die 24-h-Historie bleibt fuer kurze Ausfaelle erhalten
    if (!htuOk || !htuHaveValues)
        return;

    // Wechsel zwischen gemessen und geschaetzt melden (z.B. BMP abgesteckt)
    uint8_t measured = wallMeasured();
    if (measured != lastMeasured)
    {
        Log_Print(measured ? "Wandtemperatur: gemessen mit dem BMP-Sensor\r\n"
                           : "Wandtemperatur: geschaetzt (Raumtemperatur - set wall)\r\n");
        lastMeasured = measured;
        displayDirty = 1;
    }

    if (Schimmel_Update(now, htuTemp, htuHum, wallTemperature()))
    {
        if (Schimmel_IsWarning())
        {
            Log_Print("Schimmel-Warnung: Wand war lange zu feucht, oefter lueften und heizen!\r\n");
            wakeDisplay();
        }
        else
        {
            Log_Print("Schimmel-Warnung beendet\r\n");
        }
        displayDirty = 1;
    }
}

/* ---------- Wettertrend ---------- */
static Trend getTrend(void)
{
    if (trend.count < TREND_MIN_SAMPLES)
        return TREND_UNBEKANNT;

    float oldest = ringGet(&trend, 0);
    float newest = ringGet(&trend, trend.count - 1);
    float spanMinutes = (trend.count - 1) * (TREND_INTERVAL_MS / 60000.0f);
    float per3h = (newest - oldest) * (180.0f / spanMinutes);

    if (per3h >= TREND_THRESHOLD)
        return TREND_STEIGEND;
    if (per3h <= -TREND_THRESHOLD)
        return TREND_FALLEND;
    return TREND_STABIL;
}

/* ---------- Messung ---------- */
static void sensorLost(void)
{
    sensorOk = 0;
    Log_Print("LPS22 verloren, I2C wird neu gestartet\r\n");
    wakeDisplay(); // Fehler soll sichtbar sein
    // Haengt der Bus, hilft oft nur ein Neustart der I2C-Peripherie
    HAL_I2C_DeInit(&hi2c1);
    I2C_Bus_Init();
    ampel = AMPEL_FEHLER;
    showAmpel(ampel);
    displayDirty = 1;
}

static void measure(uint32_t now)
{
    float p, t;

    if (!sensorOk)
    {
        if (now - tSensorRetry >= SENSOR_RETRY_MS)
        {
            tSensorRetry = now;
            if (LPS22_Init(&lps22, &hi2c1) == HAL_OK)
            {
                sensorOk = 1;
                sensorErrors = 0;
                Log_Print("LPS22 gefunden\r\n");
            }
        }
        return;
    }

    if (LPS22_Read(&lps22, &p, &t) != HAL_OK)
    {
        if (++sensorErrors >= SENSOR_MAX_ERRORS)
            sensorLost();
        return;
    }
    sensorErrors = 0;

    if (!haveValues)
    {
        // Erster Wert: Filter, Min/Max und Verlauf damit starten
        pressure = p;
        temperature = t;
        haveValues = 1;
        resetMinMax();
        ringPush(&graph, temperature);
        ringPush(&trend, pressure);
        tGraph = tTrend = now;
    }
    else
    {
        pressure += FILTER_ALPHA * (p - pressure);
        temperature += FILTER_ALPHA * (t - temperature);
    }

    if (temperature < tempMin)
        tempMin = temperature;
    if (temperature > tempMax)
        tempMax = temperature;
    if (pressure < pressMin)
        pressMin = pressure;
    if (pressure > pressMax)
        pressMax = pressure;

    ampel = nextAmpelState(ampel, temperature);
    showAmpel(ampel);
}

static void bmpMeasure(uint32_t now)
{
    char line[48];
    float t, p, h;

    if (!bmpOk)
    {
        if (now - tBmpRetry >= SENSOR_RETRY_MS)
        {
            tBmpRetry = now;
            if (BMP_Init(&bmp, &hi2c1) == HAL_OK)
            {
                bmpOk = 1;
                bmpErrors = 0;
                bmpRequest = 0;
                sprintf(line, "%s gefunden\r\n", BMP_TypeName(bmp.type));
                Log_Print(line);
                displayDirty = 1;
            }
        }
        return;
    }

    if (!bmpRequest)
    {
        if (now - tBmp < BMP_INTERVAL_MS)
            return;
        tBmp = now;
        bmpRequest = 1;
    }

    // Der BMP180 braucht ~30 ms pro Messung, BMP_Read meldet solange HAL_BUSY
    HAL_StatusTypeDef status = BMP_Read(&bmp, &t, &p, &h);
    if (status == HAL_BUSY)
        return;
    bmpRequest = 0;

    if (status != HAL_OK)
    {
        if (++bmpErrors >= SENSOR_MAX_ERRORS)
        {
            bmpOk = 0;
            sprintf(line, "%s verloren\r\n", BMP_TypeName(bmp.type));
            Log_Print(line);
            displayDirty = 1;
        }
        return;
    }
    bmpErrors = 0;
    bmpTemp = t;
    bmpPress = p;
    bmpHum = h;
    bmpHaveValues = 1;
}

static void htuMeasure(uint32_t now)
{
    float t, h;

    if (!htuOk)
    {
        if (now - tHtuRetry >= SENSOR_RETRY_MS)
        {
            tHtuRetry = now;
            if (HTU21_Init(&htu, &hi2c1) == HAL_OK)
            {
                htuOk = 1;
                htuErrors = 0;
                htuRequest = 0;
                Log_Print("HTU2x gefunden\r\n");
                displayDirty = 1;
            }
        }
        return;
    }

    if (!htuRequest)
    {
        if (now - tHtu < HTU_INTERVAL_MS)
            return;
        tHtu = now;
        htuRequest = 1;
    }

    // Temperatur + Feuchte dauern zusammen ~70 ms, HTU21_Read meldet solange HAL_BUSY
    HAL_StatusTypeDef status = HTU21_Read(&htu, &t, &h);
    if (status == HAL_BUSY)
        return;
    htuRequest = 0;

    if (status != HAL_OK)
    {
        if (++htuErrors >= SENSOR_MAX_ERRORS)
        {
            htuOk = 0;
            Log_Print("HTU2x verloren\r\n");
            displayDirty = 1;
        }
        return;
    }
    htuErrors = 0;
    htuTemp = t;
    htuHum = h;
    htuHaveValues = 1;
}

/* ---------- Taster ---------- */
static void handleButtons(uint8_t pressed)
{
    if (!pressed)
        return;

    // Ist das Display aus oder gedimmt, weckt der erste Druck es nur auf (blaettert nicht)
    uint8_t wasSleeping = (saver != SAVER_AKTIV) || !displayOn;
    wakeDisplay();
    if (wasSleeping)
    {
        displayOn = 1;
        applyDisplayPower();
        displayDirty = 1;
        return;
    }

    if (pressed & BUTTON_A)
    {
        page = (page + 1) % PAGE_COUNT;
    }
    if (pressed & BUTTON_B)
    {
        if (page == PAGE_MINMAX)
            resetMinMax();
        else
            page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
    }
    if (pressed & BUTTON_C)
    {
        displayOn = 0;
        applyDisplayPower();
        Log_Print("Display aus (Taster C), beliebiger Taster schaltet es wieder ein\r\n");
    }
    if (cfg.page != page)
    {
        cfg.page = page; // Seite merken, damit sie nach dem Neustart wieder erscheint
        markCfgChanged();
    }
    displayDirty = 1;
}

/* ---------- Anzeige ---------- */
static void drawHeader(const char *title)
{
    char str[8];
    SH1107_DrawString(0, 0, title, 1);
    sprintf(str, "%d/%d", page + 1, PAGE_COUNT);
    SH1107_DrawString(SH1107_WIDTH - 3 * 6, 0, str, 1);
}

static void drawPageWerte(void)
{
    static const char *stateText[] = {"Status: zu kalt", "Status: OK", "Status: zu warm", "Sensor Fehler!"};
    static const char *trendText[] = {
        "Trend --",
        SH1107_ARROW_DOWN " fallend",
        SH1107_ARROW_RIGHT " stabil",
        SH1107_ARROW_UP " steigend",
    };
    char value[16];
    char line[24];

    drawHeader("Druck");
    SH1107_DrawString(36, 0, trendText[getTrend()], 1);
    formatFixed1(value, pressure);
    sprintf(line, "%s hPa", value);
    SH1107_DrawString(0, 9, line, 2);

    SH1107_DrawString(0, 28, "Temperatur", 1);
    formatFixed1(value, temperature);
    sprintf(line, "%s" SH1107_DEG "C", value);
    SH1107_DrawString(0, 37, line, 2);

    const char *lueftText = lueftStatusText(HAL_GetTick(), line);
    if (lueftText)
        SH1107_DrawString(0, 56, lueftText, 1);
    else if (Schimmel_IsWarning())
        SH1107_DrawString(0, 56, "Schimmel-Warnung!", 1);
    else
        SH1107_DrawString(0, 56, stateText[ampel], 1);
}

static void drawPageSensor2(void)
{
    char value[16];
    char line[24];

    if (bmp.type == BMP_TYPE_NONE)
    {
        drawHeader("Sensor 2");
        SH1107_DrawString(0, 20, "Kein BMP/BME gefunden", 1);
        SH1107_DrawString(0, 32, "Adresse 0x76/0x77", 1);
        return;
    }

    if (cfg.wallSensor)
    {
        sprintf(line, "%s (Wand)", BMP_TypeName(bmp.type));
        drawHeader(line);
    }
    else
    {
        drawHeader(BMP_TypeName(bmp.type));
    }
    if (!bmpHaveValues)
    {
        SH1107_DrawString(0, 28, "Warte auf Daten...", 1);
        return;
    }

    formatFixed1(value, bmpTemp);
    sprintf(line, "%s" SH1107_DEG "C", value);
    SH1107_DrawString(0, 10, line, 2);

    formatFixed1(value, bmpPress);
    sprintf(line, "Druck: %s hPa", value);
    SH1107_DrawString(0, 30, line, 1);

    if (wallMeasured() && htuHaveValues)
    {
        // Als Wandsensor: berechnete Feuchte an der Wandoberflaeche
        formatFixed1(value, Schimmel_GetWallHumidity());
        sprintf(line, "Wandfeuchte: %s %%", value);
    }
    else if (BMP_HasHumidity(&bmp))
    {
        formatFixed1(value, bmpHum);
        sprintf(line, "Feuchte: %s %%", value);
    }
    else
    {
        strcpy(line, "Feuchte: n/a");
    }
    SH1107_DrawString(0, 40, line, 1);

    if (!bmpOk)
    {
        SH1107_DrawString(0, 56, "Sensor Fehler!", 1);
    }
    else if (haveValues)
    {
        // Vergleich mit dem LPS22
        float diff = bmpTemp - temperature;
        formatFixed1(value, diff);
        sprintf(line, "dT LPS22: %s%s" SH1107_DEG "C", (diff >= 0.0f) ? "+" : "", value);
        SH1107_DrawString(0, 56, line, 1);
    }
}

// Taupunkt nach der Magnus-Formel
static float dewPoint(float t, float rh)
{
    const float a = 17.62f, b = 243.12f;
    if (rh < 1.0f)
        rh = 1.0f; // log(0) vermeiden
    float gamma = logf(rh / 100.0f) + a * t / (b + t);
    return b * gamma / (a - gamma);
}

static void drawPageFeuchte(void)
{
    char value[16];
    char line[24];

    drawHeader("HTU2x Feuchte");
    if (!htuOk && !htuHaveValues)
    {
        SH1107_DrawString(0, 20, "Kein HTU2x gefunden", 1);
        SH1107_DrawString(0, 32, "Adresse 0x40", 1);
        return;
    }
    if (!htuHaveValues)
    {
        SH1107_DrawString(0, 28, "Warte auf Daten...", 1);
        return;
    }

    formatFixed1(value, htuHum);
    sprintf(line, "%s %%rH", value);
    SH1107_DrawString(0, 10, line, 2);

    formatFixed1(value, htuTemp);
    sprintf(line, "Temp: %s" SH1107_DEG "C", value);
    SH1107_DrawString(0, 28, line, 1);

    formatFixed1(value, dewPoint(htuTemp, htuHum));
    sprintf(line, "Taupunkt: %s" SH1107_DEG "C", value);
    SH1107_DrawString(0, 37, line, 1);

    schimmelText(line);
    SH1107_DrawString(0, 46, line, 1);

    const char *lueftText = lueftStatusText(HAL_GetTick(), line);
    if (!htuOk)
        SH1107_DrawString(0, 56, "Sensor Fehler!", 1);
    else if (lueftText)
        SH1107_DrawString(0, 56, lueftText, 1);
    else if (htuHum < HUM_DRY)
        SH1107_DrawString(0, 56, "Luft: zu trocken", 1);
    else if (htuHum > cfg.humWet)
        SH1107_DrawString(0, 56, "Feucht (Pause)", 1); // nach dem Lueften: 15 min Pause
    else
        SH1107_DrawString(0, 56, "Luft: angenehm", 1);
}

static void drawPageVerlauf(void)
{
    const uint8_t top = 10, bottom = 54;
    char line[24], lo[8], hi[8];

    sprintf(line, "Temp. %lu min", (unsigned long)(SH1107_WIDTH * GRAPH_INTERVAL_MS / 60000UL));
    drawHeader(line);

    if (graph.count < 2)
    {
        SH1107_DrawString(0, 28, "Sammle Daten...", 1);
        return;
    }

    float minV = ringGet(&graph, 0), maxV = minV;
    for (uint8_t i = 1; i < graph.count; i++)
    {
        float v = ringGet(&graph, i);
        if (v < minV)
            minV = v;
        if (v > maxV)
            maxV = v;
    }

    // Mindestens 1 Grad Spanne, sonst wird Rauschen riesig dargestellt
    float scaleLo = minV, scaleHi = maxV;
    if (scaleHi - scaleLo < 1.0f)
    {
        float mid = (scaleHi + scaleLo) / 2.0f;
        scaleLo = mid - 0.5f;
        scaleHi = mid + 0.5f;
    }

    // Neueste Werte rechts
    uint8_t x0 = SH1107_WIDTH - graph.count;
    uint8_t prevY = 0;
    for (uint8_t i = 0; i < graph.count; i++)
    {
        float v = ringGet(&graph, i);
        uint8_t y = bottom - (uint8_t)((v - scaleLo) / (scaleHi - scaleLo) * (bottom - top) + 0.5f);
        if (i == 0)
            SH1107_DrawPixel(x0, y, 1);
        else
            SH1107_DrawVLine(x0 + i, prevY, y);
        prevY = y;
    }

    formatFixed1(lo, minV);
    formatFixed1(hi, maxV);
    sprintf(line, "min %s  max %s", lo, hi);
    SH1107_DrawString(0, 57, line, 1);
}

static void drawPageMinMax(void)
{
    char value[16];
    char line[24];

    drawHeader("Min/Max");

    formatFixed1(value, tempMin);
    sprintf(line, "Temp min: %s" SH1107_DEG "C", value);
    SH1107_DrawString(0, 12, line, 1);
    formatFixed1(value, tempMax);
    sprintf(line, "Temp max: %s" SH1107_DEG "C", value);
    SH1107_DrawString(0, 21, line, 1);

    formatFixed1(value, pressMin);
    sprintf(line, "Druck min: %s", value);
    SH1107_DrawString(0, 33, line, 1);
    formatFixed1(value, pressMax);
    sprintf(line, "Druck max: %s", value);
    SH1107_DrawString(0, 42, line, 1);

    SH1107_DrawString(0, 56, "B = zuruecksetzen", 1);
}

static HAL_StatusTypeDef render(void)
{
    SH1107_Clear();
    if (!haveValues)
    {
        SH1107_DrawString(0, 0, sensorOk ? "Warte auf Daten..." : "Fehler:", 1);
        if (!sensorOk)
            SH1107_DrawString(0, 12, "LPS22 nicht gefunden", 1);
    }
    else
    {
        switch (page)
        {
        case PAGE_WERTE:
            drawPageWerte();
            break;
        case PAGE_SENSOR2:
            drawPageSensor2();
            break;
        case PAGE_FEUCHTE:
            drawPageFeuchte();
            break;
        case PAGE_VERLAUF:
            drawPageVerlauf();
            break;
        case PAGE_MINMAX:
            drawPageMinMax();
            break;
        default:
            break;
        }
    }
    return SH1107_Update();
}

static void printValues(void)
{
    char p[16], t[16], line[128];
    if (!haveValues)
        return;
    formatFixed1(p, pressure);
    formatFixed1(t, temperature);
    sprintf(line, "Druck: %s hPa, Temp: %s C%s\r\n", p, t, sensorOk ? "" : " (Sensor Fehler)");
    Log_Print(line);

    if (bmpHaveValues)
    {
        char h[16];
        formatFixed1(p, bmpPress);
        formatFixed1(t, bmpTemp);
        if (BMP_HasHumidity(&bmp))
            formatFixed1(h, bmpHum);
        else
            strcpy(h, "n/a");
        sprintf(line, "%s: Druck: %s hPa, Temp: %s C, Feuchte: %s\r\n", BMP_TypeName(bmp.type), p, t, h);
        Log_Print(line);
    }

    if (htuHaveValues)
    {
        char h[16];
        formatFixed1(t, htuTemp);
        formatFixed1(h, htuHum);
        sprintf(line, "HTU2x: Temp: %s C, Feuchte: %s %%rH%s\r\n", t, h, htuOk ? "" : " (Sensor Fehler)");
        Log_Print(line);

        formatFixed1(t, wallTemperature());
        formatFixed1(h, Schimmel_GetWallHumidity());
        sprintf(line, "Wand (%s): %s C, Feuchte %s %%rH, Schimmelrisiko %s, kritisch %u min/24h%s\r\n",
                wallMeasured() ? "gemessen" : "geschaetzt", t, h,
                Schimmel_LevelName(Schimmel_GetLevel()), Schimmel_GetCriticalMinutes(),
                Schimmel_IsWarning() ? " WARNUNG" : "");
        Log_Print(line);
    }
}

// Uebersicht ueber alle Geraete, wird beim Oeffnen des Terminals ausgegeben
static void printStatus(void)
{
    char line[64];

    Log_Print("\r\n/* ========== WS2812 Wetterstation ========== */\r\n");
    I2C_Bus_Scan(Log_Print);

    Log_Print(displayOk ? "SH1107 OK\r\n" : "SH1107 nicht gefunden (0x3C)\r\n");

    if (sensorOk)
    {
        sprintf(line, "LPS22 OK, Adresse 0x%02X, ID 0x%02X\r\n", lps22.address >> 1, lps22.id);
        Log_Print(line);
    }
    else
    {
        Log_Print("LPS22 nicht gefunden (0x5C/0x5D)\r\n");
    }

    if (bmpOk)
    {
        sprintf(line, "%s OK, Adresse 0x%02X, ID 0x%02X\r\n", BMP_TypeName(bmp.type), bmp.address >> 1, bmp.id);
        Log_Print(line);
    }
    else
    {
        Log_Print("BMP180/BMP280/BME280 nicht gefunden (0x76/0x77)\r\n");
    }

    Log_Print(htuOk ? "HTU2x OK, Adresse 0x40\r\n" : "HTU2x nicht gefunden (0x40)\r\n");
    sprintf(line, "Lueft-Assistent: %s\r\n", Lueft_StateName(Lueft_GetState()));
    Log_Print(line);
    Log_Print(cfgLoaded ? "Einstellungen aus dem Flash geladen\r\n" : "Werkseinstellungen aktiv\r\n");
    Log_Print("'help' zeigt alle Befehle\r\n");
}

/* ---------- USB-Befehle ---------- */
static void printSettings(void)
{
    char line[64], a[16], b[16];
    formatFixed1(a, cfg.tempLow);
    formatFixed1(b, cfg.tempHigh);
    sprintf(line, "Ampel: gelb <= %s C, rot >= %s C\r\n", a, b);
    Log_Print(line);
    formatFixed1(a, cfg.humWet);
    sprintf(line, "Lueften ab %s %%rH\r\n", a);
    Log_Print(line);
    sprintf(line, "LED-Helligkeit: %u\r\n", cfg.brightness);
    Log_Print(line);
    if (cfg.dimMinutes)
        sprintf(line, "Bildschirmschoner: dimmen nach %u min, ", cfg.dimMinutes);
    else
        strcpy(line, "Bildschirmschoner: nicht dimmen, ");
    Log_Print(line);
    if (cfg.offMinutes)
        sprintf(line, "aus nach %u min\r\n", cfg.offMinutes);
    else
        strcpy(line, "nicht ausschalten\r\n");
    Log_Print(line);
    formatFixed1(a, cfg.wallDelta);
    if (!cfg.wallSensor)
        sprintf(line, "Schimmelrisiko: Wand geschaetzt, %s C kaelter als die Raumluft\r\n", a);
    else if (wallMeasured())
        sprintf(line, "Schimmelrisiko: Wand gemessen mit %s\r\n", BMP_TypeName(bmp.type));
    else
        sprintf(line, "Schimmelrisiko: Wandsensor fehlt, geschaetzt mit %s C\r\n", a);
    Log_Print(line);
    sprintf(line, "Display jetzt: %s, letzter Tastendruck vor %lu s\r\n", saverName(),
            (unsigned long)((HAL_GetTick() - tActivity) / 1000UL));
    Log_Print(line);
    Log_Print(cfgDirty ? "Einstellungen: noch nicht gespeichert\r\n" : "Einstellungen: gespeichert\r\n");
}

static void applyBrightness(void)
{
    WS2812_SetBrightness(leds, cfg.brightness);
    // Ampel mit der neuen Helligkeit neu zeichnen (der Regenbogen aktualisiert sich selbst)
    ampelShown = (AmpelState)0xFF;
    showAmpel(ampel);
}

static uint8_t cfgValid(const Settings *s)
{
    // Positiv formuliert, damit auch NaN aus einem kaputten Eintrag abgelehnt wird
    return s->tempLow >= -40.0f && s->tempLow <= 85.0f &&
           s->tempHigh >= -40.0f && s->tempHigh <= 85.0f && s->tempLow < s->tempHigh &&
           s->humWet > HUM_HYST && s->humWet <= 100.0f &&
           s->page < PAGE_COUNT &&
           s->wallDelta >= 0.0f && s->wallDelta <= 15.0f;
}

static void saveCfgNow(void)
{
    if (!cfgDirty)
        return;
    cfgDirty = 0;
    Log_Print(Settings_Save(&cfg) == HAL_OK ? "Einstellungen gespeichert\r\n"
                                            : "Fehler beim Speichern der Einstellungen\r\n");
}

// Ampel mit den aktuellen Grenzen neu bewerten
static void reevaluateAmpel(void)
{
    if (haveValues && sensorOk)
    {
        ampel = nextAmpelState(AMPEL_GRUEN, temperature);
        showAmpel(ampel);
    }
    displayDirty = 1;
}

static void printHelp(void)
{
    Log_Print("Befehle:\r\n"
              "  help              diese Hilfe\r\n"
              "  status            Sensoren und Einstellungen anzeigen\r\n"
              "  set low <C>       Ampel gelb ab dieser Temperatur (und darunter)\r\n"
              "  set high <C>      Ampel rot ab dieser Temperatur\r\n"
              "  set wet <%>       Regenbogen/Lueften ab dieser Luftfeuchte\r\n"
              "  set bright <0-255> LED-Helligkeit\r\n"
              "  set dim <min>     Display dimmen nach x min ohne Tastendruck (0 = nie)\r\n"
              "  set off <min>     Display aus nach x min ohne Tastendruck (0 = nie)\r\n"
              "  set wall <C>      Schimmelrisiko: Wand so viel kaelter als die Raumluft (0-15)\r\n"
              "  set wallsensor 1|0 BMP-Sensor an der Wand nutzen (1) oder nur schaetzen (0)\r\n"
              "  defaults          alle Einstellungen auf Werkseinstellung\r\n"
              "  test schimmel     Schimmel-Warnung ca. 1 min lang vorfuehren\r\n"
              "  Einstellungen werden automatisch im Flash gespeichert.\r\n"
              "  dfu               in den USB-Bootloader springen (Firmware-Update)\r\n"
              "  reset             Neustart\r\n");
}

// Wartet kurz, damit eine letzte Meldung noch per USB rausgeht
static void flushLog(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < ms)
        Log_Process();
}

// Liest eine Zahl, gibt 0 zurueck, wenn keine gueltige Zahl folgt
static uint8_t parseFloat(const char *str, float *value)
{
    char *end;
    if (str == NULL)
        return 0;
    *value = strtof(str, &end);
    return end != str;
}

static void handleSet(char *args)
{
    char *name = strtok(args, " ");
    char *valueStr = strtok(NULL, " ");
    float value;

    if (name == NULL || !parseFloat(valueStr, &value))
    {
        Log_Print("Verwendung: set low|high|wet|bright|dim|off|wall|wallsensor <wert>\r\n");
        return;
    }

    if (strcmp(name, "low") == 0)
    {
        if (value >= cfg.tempHigh)
        {
            Log_Print("Fehler: low muss kleiner als high sein\r\n");
            return;
        }
        cfg.tempLow = value;
    }
    else if (strcmp(name, "high") == 0)
    {
        if (value <= cfg.tempLow)
        {
            Log_Print("Fehler: high muss groesser als low sein\r\n");
            return;
        }
        cfg.tempHigh = value;
    }
    else if (strcmp(name, "wet") == 0)
    {
        if (value <= HUM_HYST || value > 100.0f)
        {
            Log_Print("Fehler: wet muss zwischen 3 und 100 liegen\r\n");
            return;
        }
        cfg.humWet = value;
    }
    else if (strcmp(name, "bright") == 0)
    {
        if (value < 0.0f || value > 255.0f)
        {
            Log_Print("Fehler: bright muss zwischen 0 und 255 liegen\r\n");
            return;
        }
        cfg.brightness = (uint8_t)value;
        applyBrightness();
    }
    else if (strcmp(name, "dim") == 0 || strcmp(name, "off") == 0)
    {
        if (value < 0.0f || value > 255.0f)
        {
            Log_Print("Fehler: Minuten muessen zwischen 0 und 255 liegen (0 = nie)\r\n");
            return;
        }
        if (name[0] == 'd')
            cfg.dimMinutes = (uint8_t)value;
        else
            cfg.offMinutes = (uint8_t)value;
        wakeDisplay(); // neue Zeiten gelten ab jetzt
    }
    else if (strcmp(name, "wall") == 0)
    {
        if (value < 0.0f || value > 15.0f)
        {
            Log_Print("Fehler: wall muss zwischen 0 und 15 liegen\r\n");
            return;
        }
        cfg.wallDelta = value;
    }
    else if (strcmp(name, "wallsensor") == 0)
    {
        if (value != 0.0f && value != 1.0f)
        {
            Log_Print("Fehler: wallsensor muss 0 oder 1 sein\r\n");
            return;
        }
        cfg.wallSensor = (uint8_t)value;
    }
    else
    {
        Log_Print("Unbekannter Wert, erlaubt: low, high, wet, bright, dim, off, wall, wallsensor\r\n");
        return;
    }

    reevaluateAmpel();
    markCfgChanged();
    printSettings();
}

static void handleCommand(char *line)
{
    char *cmd = strtok(line, " ");
    char *args = strtok(NULL, "");

    if (cmd == NULL)
        return;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
    {
        printHelp();
    }
    else if (strcmp(cmd, "status") == 0)
    {
        printStatus();
        printSettings();
    }
    else if (strcmp(cmd, "set") == 0)
    {
        handleSet(args);
    }
    else if (strcmp(cmd, "test") == 0 && args && strcmp(args, "schimmel") == 0)
    {
        Schimmel_TestWarning();
        wakeDisplay();
        displayDirty = 1;
        Log_Print("Test: Schimmel-Warnung fuer ca. 1 min eingeschaltet\r\n");
    }
    else if (strcmp(cmd, "defaults") == 0)
    {
        uint8_t keepPage = cfg.page;
        cfg = cfgDefaults;
        cfg.page = keepPage;
        applyBrightness();
        reevaluateAmpel();
        markCfgChanged();
        Log_Print("Werkseinstellungen geladen\r\n");
        printSettings();
    }
    else if (strcmp(cmd, "dfu") == 0)
    {
        saveCfgNow();
        Log_Print("Springe in den DFU-Bootloader, jetzt flash_dfu.bat starten...\r\n");
        flushLog(200);
        Bootloader_Request();
    }
    else if (strcmp(cmd, "reset") == 0)
    {
        saveCfgNow();
        Log_Print("Neustart...\r\n");
        flushLog(200);
        NVIC_SystemReset();
    }
    else
    {
        Log_Print("Unbekannter Befehl, 'help' zeigt alle Befehle\r\n");
    }
}

/* ---------- Oeffentliche Funktionen ---------- */
void App_Init(WS2812Controller *ledController, UART_HandleTypeDef *huart)
{
    leds = ledController;
    Log_Init(huart);
    Console_Init(handleCommand);
    Buttons_Init();
    Lueft_Init();
    Schimmel_Init();

    // Einstellungen aus dem Flash, bei fehlendem oder ungueltigem Eintrag Werkseinstellung
    cfg = cfgDefaults;
    cfgLoaded = Settings_Load(&cfg);
    if (!cfgValid(&cfg))
    {
        cfg = cfgDefaults;
        cfgLoaded = 0;
    }
    page = (Page)cfg.page;
    WS2812_SetBrightness(leds, cfg.brightness);

    // Das OLED braucht nach dem Einschalten Zeit, sonst ignoriert es die Init-Befehle
    if (HAL_GetTick() < DISPLAY_POWERUP_MS)
        HAL_Delay(DISPLAY_POWERUP_MS - HAL_GetTick());
    displayOk = (SH1107_Init(&hi2c1) == HAL_OK);
    tDisplayRetry = HAL_GetTick();
#if DISPLAY_SELFTEST
    if (displayOk)
    {
        // Testbild: zuerst 1 s alles weiss (Panel-Test), danach Text aus dem RAM
        SH1107_SelfTest(1000);
        SH1107_Clear();
        SH1107_DrawString(0, 0, "Display Test", 2);
        SH1107_DrawString(0, 24, "Startet...", 1);
        SH1107_Update();
        HAL_Delay(1000);
    }
#endif

    sensorOk = (LPS22_Init(&lps22, &hi2c1) == HAL_OK);
    if (!sensorOk)
    {
        ampel = AMPEL_FEHLER;
        showAmpel(ampel);
    }
    tSensorRetry = HAL_GetTick();
    tActivity = HAL_GetTick(); // Bildschirmschoner startet ab jetzt

    bmpOk = (BMP_Init(&bmp, &hi2c1) == HAL_OK);
    tBmpRetry = HAL_GetTick();

    htuOk = (HTU21_Init(&htu, &hi2c1) == HAL_OK);
    tHtuRetry = HAL_GetTick();

    // Geht nur raus, wenn LOG_ALSO_UART aktiv ist. Per USB kommt der Status beim Oeffnen des Terminals.
    printStatus();
}

void App_Process(void)
{
    uint32_t now = HAL_GetTick();

    if (now - tButtons >= BUTTON_INTERVAL_MS)
    {
        tButtons = now;
        handleButtons(Buttons_Poll());
    }

    if (now - tMeasure >= MEASURE_INTERVAL_MS)
    {
        tMeasure = now;
        measure(now);
    }

    bmpMeasure(now);
    htuMeasure(now);
    updateLueften(now);
    updateSchimmel(now);
    animateLeds(now);

    if (haveValues && now - tGraph >= GRAPH_INTERVAL_MS)
    {
        tGraph = now;
        ringPush(&graph, temperature);
    }

    if (haveValues && now - tTrend >= TREND_INTERVAL_MS)
    {
        tTrend = now;
        ringPush(&trend, pressure);
    }

    if (Log_TerminalOpened())
        printStatus();

    if (now - tPrint >= PRINT_INTERVAL_MS)
    {
        tPrint = now;
        printValues();
    }
    if (cfgDirty && now - tCfgChanged >= SETTINGS_SAVE_DELAY_MS)
        saveCfgNow();

    Console_Process();
    Log_Process();

    updateScreensaver(now);

    if (!displayOk)
    {
        // Display neu suchen, z.B. nach Wackelkontakt oder zu fruehem Start
        if (now - tDisplayRetry >= SENSOR_RETRY_MS)
        {
            tDisplayRetry = now;
            if (SH1107_Init(&hi2c1) == HAL_OK)
            {
                displayOk = 1;
                displayDirty = 1;
                applyDisplayPower(); // Init setzt Kontrast und "an" zurueck
                Log_Print("SH1107 gefunden\r\n");
            }
        }
    }
    else if (displayVisible() && (displayDirty || now - tDisplay >= DISPLAY_INTERVAL_MS))
    {
        tDisplay = now;
        displayDirty = 0;
        if (render() != HAL_OK)
        {
            displayOk = 0;
            tDisplayRetry = now;
            Log_Print("SH1107 Fehler beim Senden, wird neu initialisiert\r\n");
        }
    }
}
