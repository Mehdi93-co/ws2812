
#include "lueft.h"
#include <math.h>

/* ---------- Einstellungen ---------- */
#define HUM_HYST 2.0f            // NOETIG endet ohne Lueften erst unter humWet - HUM_HYST
#define HUM_TARGET_MARGIN 5.0f   // Lueften fertig, wenn die Feuchte so weit unter humWet liegt
#define OPEN_TEMP_DROP 0.5f      // Fenster offen: Temperatur faellt mindestens so viel ...
#define OPEN_AH_DROP 0.05f       // ... oder die absolute Feuchte um 5 %
#define MAX_TEMP_DROP 3.0f       // Raum zu kalt: Abkuehlung seit Lueftbeginn
#define MAX_DURATION_MS (10UL * 60UL * 1000UL) // Stosslueften: nach 10 min ist es genug
#define DONE_SHOW_MS (60UL * 1000UL)          // "Fenster kann zu" so lange anzeigen
#define PAUSE_MS (15UL * 60UL * 1000UL)       // danach so lange nicht erneut auffordern

// Verlauf fuer die Fenster-Erkennung: alle 10 s ein Wert, 3 min Fenster
#define HIST_INTERVAL_MS 10000UL
#define HIST_SAMPLES 19
#define HIST_MIN_SAMPLES 7 // mindestens 1 min Verlauf, bevor erkannt wird

static LueftState state;
static LueftGrund grund;
static uint32_t tState;      // Beginn des aktuellen Zustands
static uint32_t tPauseUntil; // vorher keine neue Aufforderung
static uint8_t pauseActive;
static float startTemp;      // Temperatur bei Lueftbeginn

static float histTemp[HIST_SAMPLES];
static float histAh[HIST_SAMPLES];
static uint8_t histHead, histCount;
static uint32_t tHist;

// Absolute Luftfeuchte in g/m^3 (Magnus-Formel)
static float absoluteHumidity(float t, float rh)
{
    float saturation = 6.112f * expf(17.67f * t / (t + 243.5f)); // hPa
    return saturation * rh * 2.1674f / (273.15f + t);
}

static void histClear(void)
{
    histHead = histCount = 0;
}

static void histPush(float t, float ah)
{
    histTemp[histHead] = t;
    histAh[histHead] = ah;
    histHead = (histHead + 1) % HIST_SAMPLES;
    if (histCount < HIST_SAMPLES)
        histCount++;
}

static uint8_t histOldest(float *t, float *ah)
{
    if (histCount < HIST_MIN_SAMPLES)
        return 0;
    uint8_t i = (histHead + HIST_SAMPLES - histCount) % HIST_SAMPLES;
    *t = histTemp[i];
    *ah = histAh[i];
    return 1;
}

static uint8_t windowOpened(float t, float ah)
{
    float oldT, oldAh;
    if (!histOldest(&oldT, &oldAh))
        return 0;
    return (oldT - t >= OPEN_TEMP_DROP) || (oldAh > 0.0f && (oldAh - ah) / oldAh >= OPEN_AH_DROP);
}

static uint8_t setState(LueftState newState, uint32_t now)
{
    if (newState == state)
        return 0;
    state = newState;
    tState = now;
    return 1;
}

void Lueft_Init(void)
{
    state = LUEFT_AUS;
    grund = LUEFT_GRUND_KEINER;
    pauseActive = 0;
    histClear();
}

uint8_t Lueft_Update(uint32_t now, float temperature, float humidity, float humWet)
{
    float ah = absoluteHumidity(temperature, humidity);

    if (histCount == 0 || now - tHist >= HIST_INTERVAL_MS)
    {
        tHist = now;
        histPush(temperature, ah);
    }

    if (pauseActive && (int32_t)(now - tPauseUntil) >= 0)
        pauseActive = 0;

    switch (state)
    {
    case LUEFT_AUS:
        if (humidity > humWet && !pauseActive)
            return setState(LUEFT_NOETIG, now);
        break;

    case LUEFT_NOETIG:
        if (humidity < humWet - HUM_HYST)
            return setState(LUEFT_AUS, now); // von selbst trockener geworden
        if (windowOpened(temperature, ah))
        {
            // Bezug ist der Wert vor dem Oeffnen, sonst zaehlt die erste Abkuehlung nicht mit
            float oldAh;
            if (!histOldest(&startTemp, &oldAh))
                startTemp = temperature;
            grund = LUEFT_GRUND_KEINER;
            return setState(LUEFT_LAEUFT, now);
        }
        break;

    case LUEFT_LAEUFT:
        if (humidity < humWet - HUM_TARGET_MARGIN)
            grund = LUEFT_GRUND_FEUCHTE_OK;
        else if (startTemp - temperature >= MAX_TEMP_DROP)
            grund = LUEFT_GRUND_ZU_KALT;
        else if (now - tState >= MAX_DURATION_MS)
            grund = LUEFT_GRUND_ZEIT;
        else
            break;
        return setState(LUEFT_FERTIG, now);

    case LUEFT_FERTIG:
        if (now - tState >= DONE_SHOW_MS)
        {
            // Pause, damit nicht sofort wieder zum Lueften aufgefordert wird
            pauseActive = 1;
            tPauseUntil = now + PAUSE_MS;
            histClear();
            return setState(LUEFT_AUS, now);
        }
        break;
    }
    return 0;
}

uint8_t Lueft_Reset(void)
{
    histClear();
    grund = LUEFT_GRUND_KEINER;
    if (state == LUEFT_AUS)
        return 0;
    state = LUEFT_AUS;
    return 1;
}

LueftState Lueft_GetState(void)
{
    return state;
}

LueftGrund Lueft_GetGrund(void)
{
    return grund;
}

uint32_t Lueft_GetDurationS(uint32_t now)
{
    return (now - tState) / 1000UL;
}

const char *Lueft_StateName(LueftState s)
{
    switch (s)
    {
    case LUEFT_NOETIG:
        return "noetig";
    case LUEFT_LAEUFT:
        return "laeuft";
    case LUEFT_FERTIG:
        return "fertig";
    default:
        return "aus";
    }
}
