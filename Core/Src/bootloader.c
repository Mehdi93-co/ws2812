
#include "bootloader.h"
#include "stm32f4xx_hal.h"

#define SYSTEM_MEMORY_ADDR 0x1FFF0000UL // ST-Bootloader im System-Speicher (STM32F405)
#define BOOTLOADER_MAGIC 0xB007D0F0UL

// Das Backup-Register RTC_BKP0R ueberlebt einen Software-Reset
static void enableBackupAccess(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
}

void Bootloader_CheckAndJump(void)
{
    enableBackupAccess();
    if (RTC->BKP0R != BOOTLOADER_MAGIC)
        return;
    RTC->BKP0R = 0; // nur einmal springen, nach dem Update startet wieder die Firmware

    // Direkt nach dem Reset: Takt ist HSI, keine Interrupts aktiv, keine Peripherie initialisiert
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

    uint32_t stack = *(volatile uint32_t *)SYSTEM_MEMORY_ADDR;
    void (*bootloader)(void) = (void (*)(void))(*(volatile uint32_t *)(SYSTEM_MEMORY_ADDR + 4));
    __set_MSP(stack);
    bootloader();

    while (1)
    {
    }
}

void Bootloader_Request(void)
{
    enableBackupAccess();
    RTC->BKP0R = BOOTLOADER_MAGIC;
    NVIC_SystemReset();
}
