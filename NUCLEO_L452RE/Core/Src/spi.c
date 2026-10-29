/* spi.c - ArduCAM FIFO transfer over SPI1 (DMA) and JPEG hand-off to the ESP32-C6
 *
 * ECEN-435 Lab 5 handout version, adapted for the L452 by J. Honan:
 *  - huart2 -> huart4 (PA0/PA1 to the ESP32-C6 sender)
 *  - SingleCapTransfer() caps the read at the FIFO length and the buffer size
 *  - the RX-complete callback finds both the SOI (FFD8) and EOI (FFD9) markers,
 *    so only a clean JPEG is forwarded (the sender ESP reads until FFD9)
 */
#include "spi.h"
#include "ArduCAM.h"

extern SPI_HandleTypeDef  hspi1;
extern UART_HandleTypeDef huart4;

#define ESP_UART huart4

uint8_t *picbuf       = 0;
bool     receive_OK   = false;
bool     USB_sendOK   = true;
bool     send_OK      = true;
uint32_t sendlen      = 0;
uint32_t jpeglen      = 0;
uint32_t jpegstart    = 0;
uint32_t haveRev      = 0;
uint32_t noRev        = 0;
uint8_t  EP2_SendFinish = 1;
uint8_t  Buf1[BUFFER_MAX_SIZE] = {0};
uint8_t  imageHeader[3] = {'I', 'M', 'G'};

u8 SPI1_ReadWriteByte(u8 TxData)
{
    uint8_t RxData = 0;
    /* HAL_SPI_TransmitReceive handles the TXE/RXNE polling internally */
    if (HAL_SPI_TransmitReceive(&hspi1, &TxData, &RxData, 1, HAL_MAX_DELAY) != HAL_OK) {
        return 0;
    }
    return RxData;
}

void DMA1_RX(uint8_t *p, uint32_t len)
{
    CS_LOW();
    set_fifo_burst();
    HAL_SPI_Receive_DMA(&hspi1, p, (uint16_t)len);
}

void DMA1_SendtoUsart(uint8_t *p, uint32_t len)
{
    HAL_UART_Transmit_DMA(&ESP_UART, p, (uint16_t)len);
}

/* Sender ESP protocol: 'I' 'M' 'G' then the JPEG, ending with FF D9 */
void SendImageToESP(uint8_t *p, uint32_t len)
{
    HAL_UART_Transmit(&ESP_UART, imageHeader, 3, HAL_MAX_DELAY);
    HAL_UART_Transmit(&ESP_UART, p, (uint16_t)len, HAL_MAX_DELAY);
}

void SendBufferAsHex(uint8_t *p, uint32_t len)
{
    static const char hexDigits[] = "0123456789ABCDEF";
    char out[3];
    out[2] = ' ';

    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = p[i];
        out[0] = hexDigits[b >> 4];
        out[1] = hexDigits[b & 0x0F];
        HAL_UART_Transmit(&ESP_UART, (uint8_t *)out, 3, HAL_MAX_DELAY);
    }
    uint8_t nl = '\n';
    HAL_UART_Transmit(&ESP_UART, &nl, 1, HAL_MAX_DELAY);
}

void SingleCapTransfer(void)
{
    picbuf     = Buf1;
    haveRev    = 0;
    jpeglen    = 0;
    jpegstart  = 0;
    sendlen    = 0;
    receive_OK = false;

    flush_fifo();
    clear_fifo_flag();
    start_capture();

    /* the handout waits forever here; give up after 2 s so a dead camera
     * can't hang the whole board */
    uint32_t t0 = HAL_GetTick();
    while (!get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
        if ((HAL_GetTick() - t0) > 2000) { receive_OK = true; return; }
    }

    length  = read_fifo_length();
    if (length == 0 || length >= MAX_FIFO_SIZE) { receive_OK = true; return; }
    sendlen = (length >= BUFFER_MAX_SIZE) ? BUFFER_MAX_SIZE : length;
    DMA1_RX(picbuf, sendlen);
}

/* Called by HAL when SPI1 RX over DMA completes */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1) {
        CS_HIGH();

        /* start of image - the first burst byte can be a dummy */
        jpegstart = 0;
        for (uint32_t i = 0; i + 1 < sendlen; i++) {
            if (Buf1[i] == 0xFF && Buf1[i + 1] == 0xD8) { jpegstart = i; break; }
        }

        /* end of image, searched backwards (FIFO length is padded) */
        jpeglen = 0;
        for (int32_t i = (int32_t)sendlen - 2; i >= (int32_t)jpegstart; i--) {
            if (Buf1[i] == 0xFF && Buf1[i + 1] == 0xD9) {
                jpeglen = (uint32_t)(i + 2) - jpegstart;   /* include the EOI marker */
                break;
            }
        }
        /* jpeglen stays 0 if no EOI was found (frame too big / corrupted) */

        receive_OK = true;
    }
}

/* Called by HAL when UART4 TX over DMA completes */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &ESP_UART) {
        send_OK = true;
    }
}
