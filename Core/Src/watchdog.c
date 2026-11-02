
#include "watchdog.h"
#include "stm32f4xx_hal.h"

// LSI ~32 kHz / 128 = 250 Hz -> 4 ms pro Zaehlschritt
#define IWDG_PRESCALER_128 0x5U
#define IWDG_RELOAD (WATCHDOG_TIMEOUT_MS / 4U) // max. 4095

#define IWDG_KEY_ENABLE 0xCCCCU // startet den Watchdog (und den LSI)
#define IWDG_KEY_ACCESS 0x5555U // erlaubt das Schreiben von PR und RLR
#define IWDG_KEY_RELOAD 0xAAAAU // Zaehler neu laden ("fuettern")

_Static_assert(IWDG_RELOAD <= 0x0FFF, "Watchdog-Timeout zu lang fuer Prescaler 128");

typedef enum
{
    RESET_UNKNOWN,
    RESET_POWER_ON,
    RESET_PIN,
    RESET_SOFTWARE,
    RESET_WATCHDOG,
    RESET_WINDOW_WATCHDOG,
    RESET_BROWNOUT,
    RESET_LOW_POWER
} ResetCause;

static ResetCause resetCause = RESET_UNKNOWN;
static uint8_t running;

void Watchdog_ReadResetCause(void)
{
    uint32_t csr = RCC->CSR;

    // Reihenfolge wichtig: bei Power-on sind auch PIN und BOR gesetzt
    if (csr & RCC_CSR_IWDGRSTF)
        resetCause = RESET_WATCHDOG;
    else if (csr & RCC_CSR_WWDGRSTF)
        resetCause = RESET_WINDOW_WATCHDOG;
    else if (csr & RCC_CSR_LPWRRSTF)
        resetCause = RESET_LOW_POWER;
    else if (csr & RCC_CSR_SFTRSTF)
        resetCause = RESET_SOFTWARE;
    else if (csr & RCC_CSR_PORRSTF)
        resetCause = RESET_POWER_ON;
    else if (csr & RCC_CSR_BORRSTF)
        resetCause = RESET_BROWNOUT;
    else if (csr & RCC_CSR_PINRSTF)
        resetCause = RESET_PIN;

    RCC->CSR |= RCC_CSR_RMVF; // Flags loeschen, damit der naechste Reset eindeutig ist
}

void Watchdog_Start(void)
{
    // Beim Anhalten im Debugger den Watchdog einfrieren, sonst gibt es staendig Resets
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    IWDG->KR = IWDG_KEY_ENABLE;
    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->PR = IWDG_PRESCALER_128;
    IWDG->RLR = IWDG_RELOAD;

    // Warten, bis Prescaler und Reload uebernommen sind (dauert einige LSI-Takte)
    uint32_t start = HAL_GetTick();
    while (IWDG->SR != 0 && HAL_GetTick() - start < 100)
    {
    }

    IWDG->KR = IWDG_KEY_RELOAD;
    running = 1;
}

void Watchdog_Feed(void)
{
    if (running)
        IWDG->KR = IWDG_KEY_RELOAD;
}

uint8_t Watchdog_IsRunning(void)
{
    return running;
}

uint8_t Watchdog_WasWatchdogReset(void)
{
    return resetCause == RESET_WATCHDOG;
}

const char *Watchdog_ResetCauseName(void)
{
    switch (resetCause)
    {
    case RESET_POWER_ON:
        return "Einschalten";
    case RESET_PIN:
        return "Reset-Taster";
    case RESET_SOFTWARE:
        return "Software (reset/dfu)";
    case RESET_WATCHDOG:
        return "WATCHDOG (Firmware hing)";
    case RESET_WINDOW_WATCHDOG:
        return "Window-Watchdog";
    case RESET_BROWNOUT:
        return "Unterspannung";
    case RESET_LOW_POWER:
        return "Low-Power";
    default:
        return "unbekannt";
    }
}
