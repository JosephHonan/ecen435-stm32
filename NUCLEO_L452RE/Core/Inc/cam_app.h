/* cam_app.h - ArduCAM Mini 2MP bring-up and JPEG capture for the shell
 * ECEN-435 Lab 5 - J. Honan */
#ifndef CAM_APP_H
#define CAM_APP_H

#include <stdint.h>
#include <stdbool.h>

bool Camera_Init(void);          /* handout init sequence, 160x120 JPEG */
bool Camera_IsReady(void);

/* Capture one frame into Buf1. On success start/len describe the JPEG
 * (FFD8..FFD9) inside Buf1. Returns false on timeout / no JPEG found. */
bool Camera_Capture(uint32_t *start, uint32_t *len, uint32_t *fifoLen);

void Camera_RegisterCommands(void);  /* adds "snap" to the USB shell */

#endif
