
#include "console.h"
#include "log.h"
#include "stm32f4xx_hal.h"

#define RX_BUFFER_SIZE 256
// Terminals wie der VS-Code-Serial-Monitor schicken den ganzen Befehl am Stueck, oft ohne Zeilenende.
// Kommt nach so einem Block CONSOLE_IDLE_MS lang nichts mehr, gilt die Zeile als fertig.
#define CONSOLE_IDLE_MS 50

// Ringpuffer: der USB-Interrupt schreibt, die Hauptschleife liest
static volatile uint8_t rx[RX_BUFFER_SIZE];
static volatile uint16_t rxHead;
static volatile uint16_t rxTail;
static volatile uint32_t tLastRx;
static volatile uint8_t lastPacketWasBlock; // letztes USB-Paket hatte mehr als 1 Zeichen

static char line[CONSOLE_LINE_MAX];
static uint8_t lineLen;
static Console_Handler lineHandler;

void Console_Init(Console_Handler handler)
{
    lineHandler = handler;
    rxHead = rxTail = 0;
    lineLen = 0;
}

void Console_Receive(const uint8_t *data, uint32_t len)
{
    tLastRx = HAL_GetTick();
    lastPacketWasBlock = (len > 1);
    for (uint32_t i = 0; i < len; i++)
    {
        uint16_t next = (rxHead + 1) % RX_BUFFER_SIZE;
        if (next == rxTail)
            return; // Puffer voll, Rest verwerfen
        rx[rxHead] = data[i];
        rxHead = next;
    }
}

static void executeLine(void)
{
    line[lineLen] = '\0';
    lineLen = 0;
#if CONSOLE_ECHO
    Log_Print("\r\n");
#endif
    if (lineHandler)
        lineHandler(line);
}

void Console_Process(void)
{
    while (rxTail != rxHead)
    {
        char c = (char)rx[rxTail];
        rxTail = (rxTail + 1) % RX_BUFFER_SIZE;

        if (c == '\r' || c == '\n')
        {
            // Bei "\r\n" ist die zweite Zeile leer und wird uebersprungen
            if (lineLen)
                executeLine();
            continue;
        }

        if (c == '\b' || c == 0x7F) // Backspace / Delete
        {
            if (lineLen)
            {
                lineLen--;
#if CONSOLE_ECHO
                Log_Print("\b \b");
#endif
            }
            continue;
        }

        if (c < 0x20 || c > 0x7E || lineLen >= CONSOLE_LINE_MAX - 1)
            continue;

        line[lineLen++] = c;
#if CONSOLE_ECHO
        char echo[2] = {c, '\0'};
        Log_Print(echo);
#endif
    }

    // Befehl ohne Zeilenende: nur bei Bloecken, sonst wuerde beim Tippen in PuTTY
    // eine kurze Pause schon einen halben Befehl ausfuehren
    if (lineLen && lastPacketWasBlock && HAL_GetTick() - tLastRx >= CONSOLE_IDLE_MS)
        executeLine();
}
