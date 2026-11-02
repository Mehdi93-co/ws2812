
#include "settings.h"
#include <stddef.h>
#include <string.h>

// Sektor 11 ist im Linker-Skript vom Programmcode ausgenommen (FLASH LENGTH = 896K)
#define SETTINGS_SECTOR FLASH_SECTOR_11
#define SETTINGS_ADDR 0x080E0000UL
#define SETTINGS_SIZE (128UL * 1024UL)

#define SETTINGS_MAGIC 0x5E771A9CUL
#define SETTINGS_VERSION 1 // erhoehen, wenn sich der Aufbau von Record aendert

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    float tempLow;
    float tempHigh;
    float humWet;
    uint8_t brightness;
    uint8_t page;
    uint8_t dimMinutes; // ab FLAG_SCREENSAVER, vorher reserviert (0)
    uint8_t offMinutes;
    uint32_t flags; // welche spaeter hinzugekommenen Felder gueltig sind, vorher reserviert (0)
    uint32_t crc;   // ueber alle Bytes davor
} Record;

// Neue Felder werden ueber Flags markiert statt SETTINGS_VERSION zu erhoehen,
// so bleiben aeltere Eintraege (mit 0 in den reservierten Bytes) weiter lesbar
#define FLAG_SCREENSAVER 0x00000001UL

_Static_assert(sizeof(Record) == 32, "Record muss 32 Byte gross sein");

#define RECORD_WORDS (sizeof(Record) / sizeof(uint32_t))
#define SLOT_COUNT (SETTINGS_SIZE / sizeof(Record)) // 4096 Speichervorgaenge bis zum Loeschen

static uint32_t crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (len--)
    {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
    return ~crc;
}

static const Record *slot(uint32_t index)
{
    return (const Record *)(SETTINGS_ADDR + index * sizeof(Record));
}

static uint8_t isEmpty(const Record *r)
{
    const uint32_t *words = (const uint32_t *)r;
    for (uint32_t i = 0; i < RECORD_WORDS; i++)
    {
        if (words[i] != 0xFFFFFFFFUL)
            return 0;
    }
    return 1;
}

static uint8_t isValid(const Record *r)
{
    return r->magic == SETTINGS_MAGIC && r->version == SETTINGS_VERSION &&
           r->crc == crc32((const uint8_t *)r, offsetof(Record, crc));
}

// Sucht den letzten gueltigen Eintrag (-1 = keiner) und den ersten freien Platz (SLOT_COUNT = voll)
static void scan(int32_t *last, uint32_t *nextFree)
{
    *last = -1;
    *nextFree = SLOT_COUNT;
    for (uint32_t i = 0; i < SLOT_COUNT; i++)
    {
        const Record *r = slot(i);
        if (isEmpty(r))
        {
            *nextFree = i;
            return;
        }
        if (isValid(r))
            *last = (int32_t)i;
    }
}

// Nach dem Schreiben kann der Daten-Cache noch alte Flash-Inhalte liefern
static void flushDataCache(void)
{
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

static HAL_StatusTypeDef eraseSector(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sectorError;
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = SETTINGS_SECTOR;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3; // 2.7..3.6 V
    return HAL_FLASHEx_Erase(&erase, &sectorError);
}

static HAL_StatusTypeDef writeRecord(uint32_t index, const Record *rec)
{
    uint32_t addr = SETTINGS_ADDR + index * sizeof(Record);
    const uint32_t *words = (const uint32_t *)rec;

    // Magic zuletzt schreiben: ein halb geschriebener Eintrag (Stromausfall) ist so nie gueltig
    for (uint32_t i = 1; i < RECORD_WORDS; i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i * 4, words[i]) != HAL_OK)
            return HAL_ERROR;
    }
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, words[0]) != HAL_OK)
        return HAL_ERROR;

    flushDataCache();
    return isValid(slot(index)) ? HAL_OK : HAL_ERROR;
}

uint8_t Settings_Load(Settings *s)
{
    int32_t last;
    uint32_t nextFree;
    scan(&last, &nextFree);
    if (last < 0)
        return 0;

    const Record *r = slot((uint32_t)last);
    s->tempLow = r->tempLow;
    s->tempHigh = r->tempHigh;
    s->humWet = r->humWet;
    s->brightness = r->brightness;
    s->page = r->page;
    if (r->flags & FLAG_SCREENSAVER)
    {
        s->dimMinutes = r->dimMinutes;
        s->offMinutes = r->offMinutes;
    }
    return 1;
}

HAL_StatusTypeDef Settings_Save(const Settings *s)
{
    Record rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = SETTINGS_MAGIC;
    rec.version = SETTINGS_VERSION;
    rec.tempLow = s->tempLow;
    rec.tempHigh = s->tempHigh;
    rec.humWet = s->humWet;
    rec.brightness = s->brightness;
    rec.page = s->page;
    rec.dimMinutes = s->dimMinutes;
    rec.offMinutes = s->offMinutes;
    rec.flags = FLAG_SCREENSAVER;
    rec.crc = crc32((const uint8_t *)&rec, offsetof(Record, crc));

    int32_t last;
    uint32_t next;
    scan(&last, &next);

    // Nichts geaendert: Flash nicht unnoetig beschreiben
    if (last >= 0 && memcmp(slot((uint32_t)last), &rec, sizeof(rec)) == 0)
        return HAL_OK;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    HAL_StatusTypeDef status = HAL_ERROR;
    for (uint8_t attempt = 0; attempt < 2 && status != HAL_OK; attempt++)
    {
        if (next >= SLOT_COUNT)
        {
            // Alle Plaetze belegt: einmal loeschen (dauert 1-2 s, die CPU steht in der Zeit)
            if (eraseSector() != HAL_OK)
                break;
            next = 0;
        }
        status = writeRecord(next, &rec);
        next++; // bei Fehler den naechsten Platz versuchen
    }

    HAL_FLASH_Lock();
    return status;
}
