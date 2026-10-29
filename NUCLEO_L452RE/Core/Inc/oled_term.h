/**
 * oled_term.h - scrolling text terminal on the SSD1306 128x64 OLED
 *
 * ECEN-435 Lab 5 - J. Honan
 *
 * Uses Font_6x8 -> 21 columns x 8 rows. Text wraps and scrolls like a serial
 * terminal, so shell commands and their output can be mirrored onto the display.
 * Drawing goes to the SSD1306 frame buffer; call OLEDTerm_Flush() to push it to
 * the panel (about 25 ms over 400 kHz I2C).
 */
#ifndef OLED_TERM_H
#define OLED_TERM_H

#include <stdint.h>
#include <stdbool.h>

#define OLED_TERM_COLS 21
#define OLED_TERM_ROWS 8

bool OLEDTerm_Init(void);                 /* false if no display answers on I2C */
bool OLEDTerm_IsPresent(void);
void OLEDTerm_Clear(void);
void OLEDTerm_Write(const char *s);       /* handles '\n', wraps, scrolls */
void OLEDTerm_Flush(void);                /* redraw only if something changed */

/* Full-screen gauge for live readings (e.g. proximity watch mode).
 * Leaves terminal text intact; next OLEDTerm_Flush() after new text restores it. */
void OLEDTerm_ShowGauge(const char *title, uint32_t value, uint32_t fullScale);

#endif /* OLED_TERM_H */
