
#include "WS2812.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "math.h"

const float _PI = 3.14159;

// Define constants to replace magic numbers
#define BITS_PER_LED 24
#define COLOR_DEPTH 8
#define MAX_RGB_VALUE 255
#define MAX_RETRY_COUNT 5
#define RETRY_DELAY_MS 10

// Fixed calculatePulseWidth function (matches header)
void calculatePulseWidth(uint8_t value, uint16_t *pulseWidth)
{
    if (value)
    {
        *pulseWidth = HIGH_BIT;
    }
    else
    {
        *pulseWidth = LOW_BIT;
    }
}

void WS2812_Init(WS2812Controller *controller, TIM_HandleTypeDef *htim, UART_HandleTypeDef *huart, uint32_t channel, uint16_t numLeds)
{
    char str[] = "WS2812_Init\r\n";
    
    // Validate input parameters
    if (!controller || !htim || !huart || numLeds == 0 || numLeds > NUM_LEDS)
    {
#ifdef Debug_Mode
        if (huart && controller)
        {
            HAL_UART_Transmit(huart, (uint8_t *)"WS2812_Init: Invalid parameters\r\n", 33, 100);
        }
#endif
        return;
    }
    
    controller->huart = huart;
    controller->htim = htim;
    controller->channel = channel;
    controller->numLeds = numLeds;
    controller->brightness = MAX_RGB_VALUE; // Full brightness by default
    
    // LED-Array initialisieren
    for (uint16_t i = 0; i < controller->numLeds * 3; i++)
    {
        controller->ledData[i] = 0;
    }
    
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
}

// Improved Update function with better memory management
void WS2812_Update(WS2812Controller *controller)
{
    if (!controller || !controller->htim)
    {
        return;
    }
    
    char str[50];
    uint16_t totalBits = (controller->numLeds * BITS_PER_LED) + DELAY;
    
    // Use static buffer to avoid stack overflow for large LED counts
    static uint16_t pwmData[NUM_LEDS * BITS_PER_LED + DELAY];
    
    // Check if we exceed buffer size
    if (totalBits > sizeof(pwmData) / sizeof(pwmData[0]))
    {
#ifdef Debug_Mode
        sprintf(str, "Error: LED count too large for buffer\r\n");
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif
        return;
    }
    
    // Initialize buffer
    memset(pwmData, 0, totalBits * sizeof(uint16_t));

    uint32_t pwmIndex = 0;
    
    for (uint16_t i = 0; i < controller->numLeds; i++)
    {
        // Extract RGB values
        uint8_t red = controller->ledData[i * 3 + 1];
        uint8_t green = controller->ledData[i * 3];
        uint8_t blue = controller->ledData[i * 3 + 2];
        
        // Convert RGB data to PWM data (GRB format for WS2812)
        uint32_t color = (green << 16) | (red << 8) | (blue << 0);
        
        // Fixed loop with signed counter
        for (int16_t j = BITS_PER_LED - 1; j >= 0; j--)
        {
            uint16_t pulseWidth;
            calculatePulseWidth(((color >> j) & 0x01), &pulseWidth);
            pwmData[pwmIndex++] = pulseWidth;
        }
    }
    
    // Add reset delay
    for (uint8_t delay = 0; delay < DELAY; delay++)
    {
        pwmData[pwmIndex++] = 0;
    }

#ifdef Debug_Mode
    sprintf(str, "Total PWM bits: %ld\r\n", (unsigned long)pwmIndex);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif

    // Improved error handling with exponential backoff
    HAL_StatusTypeDef status;
    uint8_t retry_count = 0;
    uint16_t delay_ms = RETRY_DELAY_MS;
    
    do {
        status = HAL_TIM_PWM_Start_DMA(controller->htim, controller->channel, 
                                       (uint32_t *)pwmData, pwmIndex);
        
        if (status == HAL_OK)
        {
            break;
        }
        
        retry_count++;
        
#ifdef Debug_Mode
        sprintf(str, "DMA start failed (0x%02X), retry %d/%d\r\n", 
                status, retry_count, MAX_RETRY_COUNT);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif
        
        if (retry_count < MAX_RETRY_COUNT)
        {
            HAL_Delay(delay_ms);
            delay_ms *= 2; // Exponential backoff
            if (delay_ms > 100) delay_ms = 100; // Cap at 100ms
        }
        
    } while (retry_count < MAX_RETRY_COUNT);
    
    if (status != HAL_OK)
    {
#ifdef Debug_Mode
        sprintf(str, "DMA start failed permanently after %d retries\r\n", retry_count);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif
    }
}

// Enhanced SetColor function with better validation
void WS2812_SetColor(WS2812Controller *controller, uint16_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!controller)
    {
        return;
    }
    
    char str[80];
    
    // Bounds checking
    if (index >= controller->numLeds)
    {
#ifdef Debug_Mode
        sprintf(str, "Index %d out of bounds (max: %d)\r\n", index, controller->numLeds - 1);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif
        return;
    }

    // Apply brightness
    float brightnessFactor = (float)controller->brightness / MAX_RGB_VALUE;
    red = (uint8_t)(red * brightnessFactor);
    green = (uint8_t)(green * brightnessFactor);
    blue = (uint8_t)(blue * brightnessFactor);
    
    // Apply gamma correction
    red = gammaCorrect(red);
    green = gammaCorrect(green);
    blue = gammaCorrect(blue);

#ifdef Debug_Mode
    sprintf(str, "LED[%d] R:%d G:%d B:%d (brightness: %.2f)\r\n", 
            index, red, green, blue, brightnessFactor);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif

    // Store in GRB format for WS2812
    controller->ledData[index * 3] = green;
    controller->ledData[index * 3 + 1] = red;
    controller->ledData[index * 3 + 2] = blue;
}

void WS2812_SetBrightness(WS2812Controller *controller, uint8_t brightness)
{
    if (!controller)
    {
        return;
    }
    
    controller->brightness = (brightness > MAX_RGB_VALUE) ? MAX_RGB_VALUE : brightness;
    
#ifdef Debug_Mode
    char str[50];
    sprintf(str, "Brightness set to: %d\r\n", controller->brightness);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif
}

// Enhanced gamma correction with bounds checking
uint8_t gammaCorrect(uint8_t value)
{
    static const uint8_t gamma_table[256] = {
        // Pre-calculated gamma table (Gamma = 2.8)
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
        2,   3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   5,   5,   5,
        5,   6,   6,   6,   6,   7,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,
       10,  10,  11,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  16,  16,
       17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  24,  24,  25,
       25,  26,  27,  27,  28,  29,  29,  30,  31,  32,  32,  33,  34,  35,  35,  36,
       37,  38,  39,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  50,
       51,  52,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  66,  67,  68,
       69,  70,  72,  73,  74,  75,  77,  78,  79,  81,  82,  83,  85,  86,  87,  89,
       90,  92,  93,  95,  96,  98,  99, 101, 102, 104, 105, 107, 109, 110, 112, 114,
      115, 117, 119, 120, 122, 124, 126, 127, 129, 131, 133, 135, 137, 138, 140, 142,
      144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 167, 169, 171, 173, 175,
      177, 180, 182, 184, 186, 189, 191, 193, 196, 198, 200, 203, 205, 208, 210, 213,
      215, 218, 220, 223, 225, 228, 231, 233, 236, 239, 241, 244, 247, 249, 252, 255
    };
    
    return gamma_table[value];
}

// Enhanced Reset function
void WS2812_Reset(WS2812Controller *controller)
{
    if (!controller)
    {
        return;
    }
    
#ifdef Debug_Mode
    char str[50];
    sprintf(str, "Resetting %d LEDs\r\n", controller->numLeds);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
#endif
    
    // Clear LED data directly for efficiency
    memset(controller->ledData, 0, controller->numLeds * 3);
    WS2812_Update(controller);
}

// Enhanced Deinit function
void WS2812_Deinit(WS2812Controller *controller)
{
    if (!controller)
    {
        return;
    }
    
    // Stop DMA if running
    HAL_TIM_PWM_Stop_DMA(controller->htim, controller->channel);
    
    // Reset all LEDs
    WS2812_Reset(controller);
    
    // Clear controller data
    controller->htim = NULL;
    controller->huart = NULL;
    controller->channel = 0;
    controller->numLeds = 0;
    controller->brightness = 0;
    
#ifdef Debug_Mode
    if (controller->huart) // Check if UART is still valid
    {
        HAL_UART_Transmit(controller->huart, (uint8_t *)"WS2812 Deinitialized\r\n", 22, 100);
    }
#endif
}

// Enhanced Print function with better formatting
void WS2812_Print(WS2812Controller *controller)
{
    if (!controller || !controller->huart)
    {
        return;
    }
    
    char str[100];
    sprintf(str, "=== WS2812 Controller Status ===\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    sprintf(str, "Channel: %ld\r\n", controller->channel);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    sprintf(str, "LED Count: %d\r\n", controller->numLeds);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    sprintf(str, "Brightness: %d (%.1f%%)\r\n", 
            controller->brightness, 
            (float)controller->brightness / MAX_RGB_VALUE * 100.0f);
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    sprintf(str, "LED Data (GRB format):\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    for (uint16_t i = 0; i < controller->numLeds; i++)
    {
        sprintf(str, "LED[%2d]: G=%3d R=%3d B=%3d\r\n", 
                i,
                controller->ledData[i * 3],     // Green
                controller->ledData[i * 3 + 1], // Red
                controller->ledData[i * 3 + 2]  // Blue
        );
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    }
    
    sprintf(str, "================================\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
}

void UART_NewLine(WS2812Controller *controller)
{
    if (controller && controller->huart)
    {
        HAL_UART_Transmit(controller->huart, (uint8_t *)"\r\n", 2, 100);
    }
}


// Einfacher Basis-Test
void WS2812_Test_Basic(WS2812Controller *controller)
{
    char str[80];
    sprintf(str, "=== WS2812 Basic Test Start ===\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    // Test 1: Alle LEDs rot
    sprintf(str, "Test 1: All LEDs RED\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    WS2812_SetBrightness(controller, 50); // 20% Helligkeit für Test
    
    for (uint16_t i = 0; i < controller->numLeds; i++)
    {
        WS2812_SetColor(controller, i, 255, 0, 0); // Rot
    }
    WS2812_Update(controller);
    HAL_Delay(2000);
    
    // Test 2: Alle LEDs grün
    sprintf(str, "Test 2: All LEDs GREEN\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    for (uint16_t i = 0; i < controller->numLeds; i++)
    {
        WS2812_SetColor(controller, i, 0, 255, 0); // Grün
    }
    WS2812_Update(controller);
    HAL_Delay(2000);
    
    // Test 3: Alle LEDs blau
    sprintf(str, "Test 3: All LEDs BLUE\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    for (uint16_t i = 0; i < controller->numLeds; i++)
    {
        WS2812_SetColor(controller, i, 0, 0, 255); // Blau
    }
    WS2812_Update(controller);
    HAL_Delay(2000);
    
    // Test 4: Weiß
    sprintf(str, "Test 4: All LEDs WHITE\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    for (uint16_t i = 0; i < controller->numLeds; i++)
    {
        WS2812_SetColor(controller, i, 255, 255, 255); // Weiß
    }
    WS2812_Update(controller);
    HAL_Delay(2000);
    
    // Reset
    WS2812_Reset(controller);
    
    sprintf(str, "=== Basic Test Complete ===\r\n\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
}

// Lauflicht-Test
void WS2812_Test_RunningLight(WS2812Controller *controller)
{
    char str[80];
    sprintf(str, "=== Running Light Test ===\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    WS2812_SetBrightness(controller, 100);
    
    // 3 Durchgänge
    for (uint8_t cycle = 0; cycle < 3; cycle++)
    {
        sprintf(str, "Cycle %d/3\r\n", cycle + 1);
        HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
        
        for (uint16_t i = 0; i < controller->numLeds; i++)
        {
            // Alle LEDs aus
            WS2812_Reset(controller);
            
            // Aktuelle LED an (rot)
            WS2812_SetColor(controller, i, 255, 0, 0);
            
            // Nächste LED schwach grün (falls vorhanden)
            if (i + 1 < controller->numLeds)
            {
                WS2812_SetColor(controller, i + 1, 0, 50, 0);
            }
            
            WS2812_Update(controller);
            HAL_Delay(200);
        }
    }
    
    WS2812_Reset(controller);
    sprintf(str, "=== Running Light Complete ===\r\n\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
}

// Regenbogen-Test
void WS2812_Test_Rainbow(WS2812Controller *controller)
{
    char str[80];
    sprintf(str, "=== Rainbow Test ===\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    WS2812_SetBrightness(controller, 80);
    
    // Statischer Regenbogen
    for (uint16_t i = 0; i < controller->numLeds; i++)
    {
        float hue = (float)i / controller->numLeds * 360.0f; // 0-360 Grad
        uint8_t r, g, b;
        HSV_to_RGB(hue, 1.0f, 1.0f, &r, &g, &b);
        WS2812_SetColor(controller, i, r, g, b);
    }
    WS2812_Update(controller);
    HAL_Delay(3000);
    
    // Rotierender Regenbogen
    sprintf(str, "Rotating rainbow...\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    for (uint16_t offset = 0; offset < 360; offset += 10)
    {
        for (uint16_t i = 0; i < controller->numLeds; i++)
        {
            float hue = ((float)i / controller->numLeds * 360.0f + offset);
            if (hue >= 360.0f) hue -= 360.0f;
            
            uint8_t r, g, b;
            HSV_to_RGB(hue, 1.0f, 1.0f, &r, &g, &b);
            WS2812_SetColor(controller, i, r, g, b);
        }
        WS2812_Update(controller);
        HAL_Delay(100);
    }
    
    WS2812_Reset(controller);
    sprintf(str, "=== Rainbow Complete ===\r\n\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
}

// Brightness-Test
void WS2812_Test_Brightness(WS2812Controller *controller)
{
    char str[80];
    sprintf(str, "=== Brightness Test ===\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    // Alle LEDs weiß setzen
    for (uint16_t i = 0; i < controller->numLeds; i++)
    {
        WS2812_SetColor(controller, i, 255, 255, 255);
    }
    
    // Helligkeit von 0 bis 255 hochfahren
    sprintf(str, "Fade up...\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    for (uint16_t brightness = 0; brightness <= 255; brightness += 5)
    {
        WS2812_SetBrightness(controller, brightness);
        
        // Farben neu setzen (da Brightness in SetColor angewendet wird)
        for (uint16_t i = 0; i < controller->numLeds; i++)
        {
            WS2812_SetColor(controller, i, 255, 255, 255);
        }
        
        WS2812_Update(controller);
        HAL_Delay(50);
    }
    
    HAL_Delay(1000);
    
    // Helligkeit von 255 bis 0 runterfahren
    sprintf(str, "Fade down...\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    for (int16_t brightness = 255; brightness >= 0; brightness -= 5)
    {
        WS2812_SetBrightness(controller, brightness);
        
        // Farben neu setzen
        for (uint16_t i = 0; i < controller->numLeds; i++)
        {
            WS2812_SetColor(controller, i, 255, 255, 255);
        }
        
        WS2812_Update(controller);
        HAL_Delay(50);
    }
    
    WS2812_Reset(controller);
    sprintf(str, "=== Brightness Complete ===\r\n\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
}

// Vollständiger Test
void WS2812_Test_Complete(WS2812Controller *controller)
{
    char str[80];
    sprintf(str, "\r\n============================\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    sprintf(str, "  WS2812 COMPLETE TEST SUITE\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    sprintf(str, "============================\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    
    // Controller-Info ausgeben
    WS2812_Print(controller);
    HAL_Delay(2000);
    
    // Alle Tests durchführen
    WS2812_Test_Basic(controller);
    HAL_Delay(1000);
    
    WS2812_Test_RunningLight(controller);
    HAL_Delay(1000);
    
    WS2812_Test_Rainbow(controller);
    HAL_Delay(1000);
    
    WS2812_Test_Brightness(controller);
    HAL_Delay(1000);
    
    sprintf(str, "============================\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    sprintf(str, "  ALL TESTS COMPLETED!\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
    sprintf(str, "============================\r\n\r\n");
    HAL_UART_Transmit(controller->huart, (uint8_t *)str, strlen(str), 100);
}

// HSV zu RGB Konvertierung für Regenbogen-Effekt
void HSV_to_RGB(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int i;
    float f, p, q, t;
    
    if (s == 0.0f)
    {
        // Graustufen
        *r = *g = *b = (uint8_t)(v * 255);
        return;
    }
    
    h /= 60.0f; // Sektor 0 bis 5
    i = (int)h;
    f = h - i;  // Nachkommaanteil
    p = v * (1.0f - s);
    q = v * (1.0f - s * f);
    t = v * (1.0f - s * (1.0f - f));
    
    switch (i)
    {
        case 0:
            *r = (uint8_t)(v * 255);
            *g = (uint8_t)(t * 255);
            *b = (uint8_t)(p * 255);
            break;
        case 1:
            *r = (uint8_t)(q * 255);
            *g = (uint8_t)(v * 255);
            *b = (uint8_t)(p * 255);
            break;
        case 2:
            *r = (uint8_t)(p * 255);
            *g = (uint8_t)(v * 255);
            *b = (uint8_t)(t * 255);
            break;
        case 3:
            *r = (uint8_t)(p * 255);
            *g = (uint8_t)(q * 255);
            *b = (uint8_t)(v * 255);
            break;
        case 4:
            *r = (uint8_t)(t * 255);
            *g = (uint8_t)(p * 255);
            *b = (uint8_t)(v * 255);
            break;
        default: // case 5:
            *r = (uint8_t)(v * 255);
            *g = (uint8_t)(p * 255);
            *b = (uint8_t)(q * 255);
            break;
    }
}