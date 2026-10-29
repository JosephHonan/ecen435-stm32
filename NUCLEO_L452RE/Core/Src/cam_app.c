/* cam_app.c - ArduCAM Mini 2MP bring-up and JPEG capture for the shell
 *
 * ECEN-435 Lab 5 - J. Honan
 *
 * Init sequence and capture flow follow the lab handout:
 *   delay_init(); ArduCAM_CS_init(); sensor_model = OV2640; set_format(JPEG);
 *   ArduCAM_Init(sensor_model); OV2640_set_JPEG_size(OV2640_160x120);
 *   SingleCapTransfer(); while (!receive_OK); receive_OK = false;
 * then 'I','M','G' + JPEG goes out UART4 to the ESP32-C6 sender.
 */
#include "cam_app.h"
#include "ArduCAM.h"
#include "spi.h"
#include "shell.h"
#include "oled_term.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;
extern SPI_HandleTypeDef hspi1;

static bool camReady = false;

bool Camera_IsReady(void)
{
    return camReady;
}

bool Camera_Init(void)
{
    camReady = false;

    /* don't spend seconds writing ~600 registers into an empty bus */
    if (HAL_I2C_IsDeviceReady(&hi2c1, OV2640_I2C_ADDR << 1, 2, 10) != HAL_OK)
        return false;

    delay_init();
    ArduCAM_CS_init();

    /* SPI sanity check on the ArduChip test register */
    write_reg(ARDUCHIP_TEST1, 0x55);
    if (read_reg(ARDUCHIP_TEST1) != 0x55)
        return false;

    sensor_addr  = OV2640_I2C_ADDR << 1;
    sensor_model = OV2640;
    set_format(JPEG);
    ArduCAM_Init(sensor_model);
    OV2640_set_JPEG_size(OV2640_160x120);   /* receiver expects 160x120 */
    HAL_Delay(100);                         /* let auto-exposure settle a bit */

    clear_fifo_flag();
    camReady = true;
    return true;
}

bool Camera_Capture(uint32_t *start, uint32_t *len, uint32_t *fifoLen)
{
    SingleCapTransfer();

    uint32_t t0 = HAL_GetTick();
    while (!receive_OK) {
        if ((HAL_GetTick() - t0) > 1000) {   /* DMA never finished */
            HAL_SPI_DMAStop(&hspi1);
            CS_HIGH();
            return false;
        }
    }
    receive_OK = false;

    *fifoLen = length;
    *start   = jpegstart;
    *len     = jpeglen;
    return jpeglen > 0;
}

/* ------------------------------------------------------------------ shell */

static void cmdSnap(int argc, char **argv)
{
    bool hex = (argc > 1 && strcmp(argv[1], "hex") == 0);
    uint32_t start, len, fifo;

    if (!camReady && !Camera_Init()) {
        Shell_Puts("camera not ready\nrun 'cam' to test\n");
        return;
    }

    uint32_t t0 = HAL_GetTick();
    if (!Camera_Capture(&start, &len, &fifo)) {
        Shell_Printf("capture failed\nfifo %lu bytes\n", (unsigned long)fifo);
        return;
    }
    uint32_t tCap = HAL_GetTick() - t0;
    Shell_Printf("JPEG %lu B %lums\n", (unsigned long)len, (unsigned long)tCap);

    if (hex) {
        /* dump the JPEG to the USB terminal as hex, 32 bytes per line -
         * paste into a hex-to-file tool to view it on the PC */
        char lineBuf[3 * 32 + 3];
        Shell_Raw("---- JPEG HEX BEGIN ----\r\n");
        for (uint32_t i = 0; i < len; i += 32) {
            uint32_t n = (len - i < 32) ? (len - i) : 32, k = 0;
            for (uint32_t j = 0; j < n; j++)
                k += (uint32_t)snprintf(&lineBuf[k], sizeof(lineBuf) - k, "%02X ", Buf1[start + i + j]);
            snprintf(&lineBuf[k], sizeof(lineBuf) - k, "\r\n");
            Shell_Raw(lineBuf);
        }
        Shell_Raw("---- JPEG HEX END ----\r\n");
        return;   /* hex mode: don't also send to the ESP */
    }

    t0 = HAL_GetTick();
    SendImageToESP(&Buf1[start], len);
    Shell_Printf("sent to ESP %lums\n", (unsigned long)(HAL_GetTick() - t0));
}

void Camera_RegisterCommands(void)
{
    Shell_Register("snap", "photo -> ESP [hex]", cmdSnap);
}
