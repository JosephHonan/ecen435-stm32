/**
 * shell.h - interactive command shell over USB CDC (virtual COM port)
 *
 * ECEN-435 Lab 5 - J. Honan
 *
 * Data flow:
 *   USB OUT packet -> CDC_Receive_FS() [interrupt] -> Shell_RxISR() copies bytes
 *   into a ring buffer and returns immediately.
 *   main loop -> Shell_Poll() drains the ring, echoes keystrokes, runs commands,
 *   and sends output with CDC_Transmit_FS() (never called from the interrupt).
 *
 * Every command line and its output is mirrored to the SSD1306 (oled_term).
 */
#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include <stdbool.h>

void Shell_Init(void);
void Shell_Poll(void);                                  /* call every loop pass */

/* Called from usbd_cdc_if.c, interrupt context - keep minimal */
void Shell_RxISR(const uint8_t *buf, uint32_t len);
void Shell_PortOpenedISR(bool dtr);                     /* terminal opened/closed */

/* Output helpers (USB + OLED mirror) */
void Shell_Printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void Shell_Puts(const char *s);
void Shell_Raw(const char *s);                          /* USB only, no OLED, no CRLF fix-up */

/* Hook for later labs: register extra commands at runtime */
typedef void (*ShellHandler_t)(int argc, char **argv);
bool Shell_Register(const char *name, const char *help, ShellHandler_t fn);

#endif /* SHELL_H */
