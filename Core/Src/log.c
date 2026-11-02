
#include "log.h"
#include "usbd_cdc_if.h"
#include <string.h>

#define LOG_BUFFER_SIZE 2048
#define LOG_CHUNK_SIZE 256

extern USBD_HandleTypeDef hUsbDeviceFS;

static UART_HandleTypeDef *logUart;

// Ringpuffer, wird nur aus der Hauptschleife beschrieben und gelesen
static char ring[LOG_BUFFER_SIZE];
static uint16_t head; // Schreibposition
static uint16_t tail; // Leseposition

// CDC_Transmit_FS kopiert nicht: der Puffer muss bis zum Ende der Uebertragung gueltig bleiben
static uint8_t txBuffer[LOG_CHUNK_SIZE];

static volatile uint8_t dtr;         // Terminal offen (aus dem USB-Interrupt gesetzt)
static volatile uint8_t dtrRisingEdge;

void Log_Init(UART_HandleTypeDef *huart)
{
    logUart = huart;
    head = tail = 0;
}

void Log_Print(const char *str)
{
#if LOG_ALSO_UART
    if (logUart)
        HAL_UART_Transmit(logUart, (uint8_t *)str, strlen(str), 100);
#endif
    // Ohne offenes Terminal wuerde der Puffer nur volllaufen
    if (!dtr)
        return;

    while (*str)
    {
        uint16_t next = (head + 1) % LOG_BUFFER_SIZE;
        if (next == tail)
            return; // Puffer voll, Rest verwerfen
        ring[head] = *str++;
        head = next;
    }
}

void Log_Process(void)
{
    if (!dtr)
    {
        tail = head; // Terminal zu: alte Daten verwerfen
        return;
    }
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
        return;

    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hcdc == NULL || hcdc->TxState != 0)
        return; // vorherige Uebertragung laeuft noch

    uint16_t len = 0;
    while (tail != head && len < LOG_CHUNK_SIZE)
    {
        txBuffer[len++] = ring[tail];
        tail = (tail + 1) % LOG_BUFFER_SIZE;
    }
    if (len)
        CDC_Transmit_FS(txBuffer, len);
}

uint8_t Log_TerminalOpened(void)
{
    if (!dtrRisingEdge)
        return 0;
    dtrRisingEdge = 0;
    return 1;
}

void Log_SetDtr(uint8_t value)
{
    if (value && !dtr)
        dtrRisingEdge = 1;
    dtr = value;
}
