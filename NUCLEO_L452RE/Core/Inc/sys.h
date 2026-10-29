/* sys.h - compatibility shim for the ArduCAM STM32F103 driver on STM32L4 HAL.
 * The original file held F1 bit-band GPIO macros (unused here) and relied on
 * the F1 Standard Peripheral Library for the u8/u16/u32 typedefs. */
#ifndef __SYS_H
#define __SYS_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#endif
