// bootloader.h
// Sprung in den ST-Bootloader (USB-DFU) ohne BOOT0-Pin
#ifndef BOOTLOADER_H
#define BOOTLOADER_H

// Ganz am Anfang von main() aufrufen, vor HAL_Init().
// Springt in den Bootloader, wenn vorher Bootloader_Request() aufgerufen wurde.
void Bootloader_CheckAndJump(void);

// Merkt sich den Wunsch im Backup-Register und startet neu (kehrt nicht zurueck)
void Bootloader_Request(void);

#endif // BOOTLOADER_H
