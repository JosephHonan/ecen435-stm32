/* spi.h - ArduCAM FIFO transfer over SPI1 (DMA) and JPEG hand-off to the ESP32-C6
 * ECEN-435 Lab 5 handout version, adapted for the L452 (UART4 to the ESP). */
#ifndef __SPI_H
#define __SPI_H

#include "main.h"
#include "sys.h"

/* A 160x120 OV2640 JPEG is usually 2-6 KB; leave headroom. */
#define BUFFER_MAX_SIZE 12288

extern uint32_t sendlen;      /* bytes pulled from the camera FIFO          */
extern uint32_t jpeglen;      /* bytes from SOI (FFD8) through EOI (FFD9)   */
extern uint32_t jpegstart;    /* offset of the FFD8 marker inside Buf1      */
extern uint32_t haveRev;
extern uint32_t noRev;
extern uint8_t *picbuf;
extern bool receive_OK;
extern bool send_OK;
extern uint8_t Buf1[BUFFER_MAX_SIZE];
extern uint8_t EP2_SendFinish;

u8   SPI1_ReadWriteByte(u8 TxData);
void DMA1_RX(uint8_t *p, uint32_t len);
void DMA1_SendtoUsart(uint8_t *p, uint32_t len);
void SendImageToESP(uint8_t *p, uint32_t len);
void SendBufferAsHex(uint8_t *p, uint32_t len);
void SingleCapTransfer(void);

#endif
