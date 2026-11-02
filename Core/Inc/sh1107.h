// sh1107.h
// Treiber fuer das Adafruit OLED FeatherWing 128x64 (SH1107, I2C 0x3C)
#ifndef SH1107_H
#define SH1107_H

#include "stm32f4xx_hal.h"

#define SH1107_ADDR 0x3C
#define SH1107_WIDTH 128 // logische Breite (quer)
#define SH1107_HEIGHT 64 // logische Hoehe

#define SH1107_CONTRAST_NORMAL 0x4F // Wert aus der Adafruit-Init-Sequenz

// Sonderzeichen in der Font, Verwendung: "23.5" SH1107_DEG "C"
#define SH1107_DEG "\x7F"
#define SH1107_ARROW_UP "\x80"
#define SH1107_ARROW_DOWN "\x81"
#define SH1107_ARROW_RIGHT "\x82"

HAL_StatusTypeDef SH1107_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef SH1107_SetDisplayOn(uint8_t on);
// Helligkeit 0..255 (SH1107_CONTRAST_NORMAL nach dem Init)
HAL_StatusTypeDef SH1107_SetContrast(uint8_t contrast);
// Schaltet fuer durationMs alle Pixel ein (Test, ob das Panel selbst funktioniert)
HAL_StatusTypeDef SH1107_SelfTest(uint32_t durationMs);
void SH1107_Clear(void);
void SH1107_DrawPixel(uint8_t x, uint8_t y, uint8_t on);
void SH1107_DrawVLine(uint8_t x, uint8_t y0, uint8_t y1);
// size = 1: 6x8 Pixel pro Zeichen, size = 2: 12x16 Pixel
void SH1107_DrawString(uint8_t x, uint8_t y, const char *str, uint8_t size);
// Framebuffer an das Display uebertragen
HAL_StatusTypeDef SH1107_Update(void);

#endif // SH1107_H
