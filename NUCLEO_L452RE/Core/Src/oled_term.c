/**
 * oled_term.c - scrolling text terminal on the SSD1306 128x64 OLED
 *
 * ECEN-435 Lab 5 - J. Honan
 */
#include "oled_term.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;

static char  lines[OLED_TERM_ROWS][OLED_TERM_COLS + 1];
static uint8_t row = 0, col = 0;
static bool  present = false;
static bool  dirty   = false;

static void newLine(void)
{
    col = 0;
    if (row < OLED_TERM_ROWS - 1) {
        row++;
    } else {
        /* scroll everything up one line */
        memmove(lines[0], lines[1], (OLED_TERM_ROWS - 1) * sizeof(lines[0]));
        memset(lines[OLED_TERM_ROWS - 1], 0, sizeof(lines[0]));
    }
}

bool OLEDTerm_Init(void)
{
    /* Probe first: ssd1306_Init() has no error return, and we want the shell
     * to keep working when the display is unplugged. */
    present = (HAL_I2C_IsDeviceReady(&hi2c1, SSD1306_I2C_ADDR, 3, 10) == HAL_OK);
    if (!present) return false;

    ssd1306_Init();
    OLEDTerm_Clear();
    OLEDTerm_Flush();
    return true;
}

bool OLEDTerm_IsPresent(void)
{
    return present;
}

void OLEDTerm_Clear(void)
{
    memset(lines, 0, sizeof(lines));
    row = col = 0;
    dirty = true;          /* caller decides when to OLEDTerm_Flush() */
}

void OLEDTerm_Write(const char *s)
{
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            newLine();
        } else if (c == '\r') {
            /* ignore - '\n' does the line feed */
        } else if (c >= ' ' && c <= '~') {
            if (col >= OLED_TERM_COLS) newLine();
            lines[row][col++] = c;
            lines[row][col] = '\0';
        }
        dirty = true;
    }
}

void OLEDTerm_Flush(void)
{
    if (!present || !dirty) return;

    ssd1306_Fill(Black);
    for (uint8_t r = 0; r < OLED_TERM_ROWS; r++) {
        if (lines[r][0] == '\0') continue;
        ssd1306_SetCursor(0, (uint8_t)(r * 8));
        ssd1306_WriteString(lines[r], Font_6x8, White);
    }
    ssd1306_UpdateScreen();
    dirty = false;
}

void OLEDTerm_ShowGauge(const char *title, uint32_t value, uint32_t fullScale)
{
    char buf[16];
    if (!present) return;

    ssd1306_Fill(Black);

    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString((char *)title, Font_7x10, White);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
    ssd1306_SetCursor(0, 16);
    ssd1306_WriteString(buf, Font_11x18, White);

    snprintf(buf, sizeof(buf), "0x%04lX", (unsigned long)value);
    ssd1306_SetCursor(70, 20);
    ssd1306_WriteString(buf, Font_7x10, White);

    /* bar graph across the bottom */
    if (fullScale == 0) fullScale = 1;
    uint32_t w = (value >= fullScale) ? 127 : (value * 127U) / fullScale;
    ssd1306_DrawRectangle(0, 44, 127, 63, White);
    if (w > 2) ssd1306_FillRectangle(2, 46, (uint8_t)(w - 1), 61, White);

    ssd1306_UpdateScreen();
    dirty = true;   /* terminal text must be redrawn after the gauge */
}
