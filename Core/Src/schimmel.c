
#include "schimmel.h"
#include <math.h>

/* ---------- Einstellungen ---------- */
#define WALL_RH_ELEVATED 70.0f
#define WALL_RH_CRITICAL 80.0f // ab hier kann Schimmel wachsen (gaengige Bauphysik-Grenze)
#define WALL_RH_CONDENSATE 100.0f
#define WARN_MINUTES (6U * 60U)    // Warnung ab 6 h kritischer Wandfeuchte in 24 h
#define WARN_CLEAR_MINUTES (4U * 60U) // Warnung endet unter 4 h (Hysterese)

// 24 Stunden-Faecher mit den kritischen Minuten je Stunde
#define HOURS 24
#define SAMPLE_INTERVAL_MS 60000UL // jede Minute bewerten
#define SAMPLES_PER_HOUR 60

static uint8_t criticalPerHour[HOURS];
static uint8_t hourIndex;
static uint8_t samplesThisHour;
static uint32_t tSample;
static uint8_t started;

static float wallHumidity;
static SchimmelLevel level;
static uint8_t warning;

// Saettigungsdampfdruck in hPa (Magnus-Formel)
static float saturationPressure(float t)
{
    return 6.112f * expf(17.62f * t / (243.12f + t));
}

static uint16_t criticalMinutes(void)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < HOURS; i++)
        sum += criticalPerHour[i];
    return sum;
}

void Schimmel_Init(void)
{
    Schimmel_Reset();
}

void Schimmel_Reset(void)
{
    for (uint8_t i = 0; i < HOURS; i++)
        criticalPerHour[i] = 0;
    hourIndex = 0;
    samplesThisHour = 0;
    started = 0;
    wallHumidity = 0.0f;
    level = SCHIMMEL_GERING;
    warning = 0;
}

uint8_t Schimmel_Update(uint32_t now, float temperature, float humidity, float wallDelta)
{
    // Gleicher Wasserdampf-Partialdruck wie im Raum, aber bei der kaelteren Wandtemperatur
    float vapour = humidity / 100.0f * saturationPressure(temperature);
    wallHumidity = 100.0f * vapour / saturationPressure(temperature - wallDelta);

    if (wallHumidity >= WALL_RH_CONDENSATE)
        level = SCHIMMEL_KONDENSAT;
    else if (wallHumidity >= WALL_RH_CRITICAL)
        level = SCHIMMEL_HOCH;
    else if (wallHumidity >= WALL_RH_ELEVATED)
        level = SCHIMMEL_ERHOEHT;
    else
        level = SCHIMMEL_GERING;

    if (started && now - tSample < SAMPLE_INTERVAL_MS)
        return 0;
    started = 1;
    tSample = now;

    // Minute zaehlen
    if (level >= SCHIMMEL_HOCH && criticalPerHour[hourIndex] < SAMPLES_PER_HOUR)
        criticalPerHour[hourIndex]++;
    if (++samplesThisHour >= SAMPLES_PER_HOUR)
    {
        // naechste Stunde, die aelteste (vor 24 h) faellt heraus
        samplesThisHour = 0;
        hourIndex = (hourIndex + 1) % HOURS;
        criticalPerHour[hourIndex] = 0;
    }

    uint16_t minutes = criticalMinutes();
    uint8_t before = warning;
    if (!warning && minutes >= WARN_MINUTES)
        warning = 1;
    else if (warning && minutes < WARN_CLEAR_MINUTES)
        warning = 0;
    return warning != before;
}

SchimmelLevel Schimmel_GetLevel(void)
{
    return level;
}

float Schimmel_GetWallHumidity(void)
{
    return wallHumidity;
}

uint16_t Schimmel_GetCriticalMinutes(void)
{
    return criticalMinutes();
}

uint8_t Schimmel_IsWarning(void)
{
    return warning;
}

const char *Schimmel_LevelName(SchimmelLevel l)
{
    switch (l)
    {
    case SCHIMMEL_ERHOEHT:
        return "erhoeht";
    case SCHIMMEL_HOCH:
        return "hoch";
    case SCHIMMEL_KONDENSAT:
        return "Kondensat";
    default:
        return "gering";
    }
}
