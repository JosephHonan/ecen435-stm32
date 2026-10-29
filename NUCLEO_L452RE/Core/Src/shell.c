/**
 * shell.c - interactive command shell over USB CDC (virtual COM port)
 *
 * ECEN-435 Lab 5 - J. Honan
 *
 * Any terminal works (PuTTY, Tera Term, Arduino Serial Monitor, VS Code Serial
 * Monitor). Baud rate is ignored by USB CDC. Lines end on CR or LF.
 */
#include "shell.h"
#include "main.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "vcnl4040.h"
#include "oled_term.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern USBD_HandleTypeDef hUsbDeviceFS;
extern I2C_HandleTypeDef  hi2c1;
extern SPI_HandleTypeDef  hspi1;

/* ------------------------------------------------------------------ config */

#define RX_RING_SIZE     512u        /* power of two */
#define LINE_MAX         80u
#define TX_BUF_SIZE      512u
#define TX_TIMEOUT_MS    50u
#define MAX_ARGS         8
#define MAX_USER_CMDS    16
#define PROMPT           "> "

/* ------------------------------------------------------------------ state */

/* RX ring: written by the USB interrupt, read by the main loop */
static volatile uint8_t  rxRing[RX_RING_SIZE];
static volatile uint16_t rxHead = 0, rxTail = 0;
static volatile bool     portOpenedFlag = false;

/* TX double buffer: accumulate in txAcc, hand txSend to the USB stack */
static uint8_t  txAcc[TX_BUF_SIZE];
static uint16_t txAccLen = 0;
static uint8_t  txSend[TX_BUF_SIZE];
static bool     txStalled = false;   /* host stopped reading -> drop output quickly */

/* line editor */
static char     line[LINE_MAX + 1];
static uint8_t  lineLen = 0;
static char     lastLine[LINE_MAX + 1];
static uint8_t  escState = 0;        /* 0 idle, 1 got ESC, 2 got ESC [ */
static uint8_t  prevChar = 0;        /* to treat CR+LF as one Enter */

/* banner after the terminal opens the port */
static bool     bannerPending = false;
static uint32_t bannerAt = 0;

/* proximity watch mode */
static bool     watching = false;
static uint32_t watchPeriod = 200;
static uint32_t watchNext = 0;
static uint16_t watchPeak = 0;

/* mirror output to the OLED? (off while streaming watch data) */
static bool     mirrorOled = true;

/* ------------------------------------------------------------------ commands */

typedef struct {
    const char     *name;
    const char     *help;     /* keep name+help <= 21 chars so it fits the OLED */
    ShellHandler_t  fn;
} ShellCmd_t;

static void cmdHelp(int argc, char **argv);
static void cmdProx(int argc, char **argv);
static void cmdWatch(int argc, char **argv);
static void cmdLight(int argc, char **argv);
static void cmdVcnl(int argc, char **argv);
static void cmdI2c(int argc, char **argv);
static void cmdOled(int argc, char **argv);
static void cmdCls(int argc, char **argv);
static void cmdInfo(int argc, char **argv);
static void cmdReboot(int argc, char **argv);
static void cmdCam(int argc, char **argv);

static const ShellCmd_t builtinCmds[] = {
    { "help",   "this list",      cmdHelp   },
    { "prox",   "read proximity", cmdProx   },
    { "watch",  "live prox [ms]", cmdWatch  },
    { "light",  "ambient light",  cmdLight  },
    { "vcnl",   "sensor regs",    cmdVcnl   },
    { "i2c",    "scan I2C bus",   cmdI2c    },
    { "oled",   "show <text>",    cmdOled   },
    { "cls",    "clear OLED",     cmdCls    },
    { "cam",    "camera self-test", cmdCam  },
    { "info",   "board info",     cmdInfo   },
    { "reboot", "reset MCU",      cmdReboot },
};
#define NUM_BUILTIN (sizeof(builtinCmds) / sizeof(builtinCmds[0]))

static ShellCmd_t userCmds[MAX_USER_CMDS];
static uint8_t    numUserCmds = 0;

/* ================================================================== USB TX */

static bool usbConfigured(void)
{
    return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) &&
           (hUsbDeviceFS.pClassData != NULL);
}

static void usbFlush(void)
{
    if (txAccLen == 0) return;

    if (!usbConfigured() || txStalled) {
        txAccLen = 0;                    /* nobody listening - drop it */
        return;
    }

    /* wait for the previous IN transfer to finish before reusing txSend */
    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    uint32_t t0 = HAL_GetTick();
    while (hcdc->TxState != 0) {
        if ((HAL_GetTick() - t0) > TX_TIMEOUT_MS) {
            txStalled = true;            /* terminal closed or not reading */
            txAccLen = 0;
            return;
        }
    }

    memcpy(txSend, txAcc, txAccLen);
    CDC_Transmit_FS(txSend, txAccLen);
    txAccLen = 0;
}

static void usbPutc(char c)
{
    if (txAccLen >= TX_BUF_SIZE) usbFlush();
    txAcc[txAccLen++] = (uint8_t)c;
}

/* raw: goes to USB only, no newline translation */
static void usbPutsRaw(const char *s)
{
    while (*s) usbPutc(*s++);
}

/* ================================================================== output */

void Shell_Puts(const char *s)
{
    for (const char *p = s; *p; p++) {
        if (*p == '\n') usbPutc('\r');   /* terminals want CRLF */
        usbPutc(*p);
    }
    if (mirrorOled) OLEDTerm_Write(s);
}

void Shell_Printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Shell_Puts(buf);
}

void Shell_Raw(const char *s)
{
    usbPutsRaw(s);
}

static void printPrompt(void)
{
    usbPutsRaw(PROMPT);
}

static void printBanner(void)
{
    usbPutsRaw("\r\n\r\n");
    Shell_Puts("ECEN-435 L452 shell\n");
    usbPutsRaw("Type 'help' for commands.\r\n");
    printPrompt();
    OLEDTerm_Flush();
}

/* ================================================================== RX (ISR) */

void Shell_RxISR(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((rxHead + 1u) & (RX_RING_SIZE - 1u));
        if (next == rxTail) break;       /* full - drop the rest */
        rxRing[rxHead] = buf[i];
        rxHead = next;
    }
}

void Shell_PortOpenedISR(bool dtr)
{
    if (dtr) portOpenedFlag = true;
}

static bool rxGet(uint8_t *c)
{
    if (rxTail == rxHead) return false;
    *c = rxRing[rxTail];
    rxTail = (uint16_t)((rxTail + 1u) & (RX_RING_SIZE - 1u));
    return true;
}

/* ================================================================== dispatch */

static const ShellCmd_t *findCmd(const char *name)
{
    for (size_t i = 0; i < NUM_BUILTIN; i++)
        if (strcmp(name, builtinCmds[i].name) == 0) return &builtinCmds[i];
    for (uint8_t i = 0; i < numUserCmds; i++)
        if (strcmp(name, userCmds[i].name) == 0) return &userCmds[i];
    return NULL;
}

bool Shell_Register(const char *name, const char *help, ShellHandler_t fn)
{
    if (numUserCmds >= MAX_USER_CMDS || findCmd(name) != NULL) return false;
    userCmds[numUserCmds].name = name;
    userCmds[numUserCmds].help = help;
    userCmds[numUserCmds].fn   = fn;
    numUserCmds++;
    return true;
}

static void runLine(char *text)
{
    char *argv[MAX_ARGS];
    int   argc = 0;

    /* split on spaces */
    char *p = text;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    if (argc == 0) return;

    /* lower-case the command word so "HELP" works too */
    for (char *q = argv[0]; *q; q++)
        if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);

    const ShellCmd_t *cmd = findCmd(argv[0]);
    if (cmd) {
        cmd->fn(argc, argv);
    } else {
        Shell_Printf("unknown: %s\n", argv[0]);
        Shell_Puts("type 'help'\n");
    }
}

static void eraseLine(void)
{
    while (lineLen) {
        usbPutsRaw("\b \b");
        lineLen--;
    }
}

static void handleChar(uint8_t c)
{
    /* any key stops watch mode */
    if (watching) {
        watching = false;
        mirrorOled = true;
        usbPutsRaw("\r\n");
        Shell_Puts("watch stopped\n");
        OLEDTerm_Flush();
        printPrompt();
        if (c == '\r' || c == '\n' || c == 0x03) return;
    }

    /* arrow keys arrive as ESC [ A/B/C/D */
    if (escState == 1) { escState = (c == '[') ? 2 : 0; return; }
    if (escState == 2) {
        escState = 0;
        if (c == 'A' && lastLine[0]) {           /* up arrow: recall last command */
            eraseLine();
            strcpy(line, lastLine);
            lineLen = (uint8_t)strlen(line);
            usbPutsRaw(line);
        }
        return;
    }

    /* CRLF line endings: the LF right after a CR is the same Enter */
    if (c == '\n' && prevChar == '\r') { prevChar = c; return; }
    prevChar = c;

    switch (c) {
    case 0x1B:                                   /* ESC */
        escState = 1;
        break;

    case '\r':
    case '\n':
        usbPutsRaw("\r\n");
        if (lineLen > 0) {
            line[lineLen] = '\0';
            strcpy(lastLine, line);

            /* fresh OLED page per command: the echoed command on top,
             * its output underneath (lab sign-off requirement) */
            OLEDTerm_Clear();
            OLEDTerm_Write("> ");
            OLEDTerm_Write(line);
            OLEDTerm_Write("\n");

            runLine(line);
            lineLen = 0;
            OLEDTerm_Flush();
        }
        if (!watching) printPrompt();
        break;

    case 0x08:                                   /* backspace */
    case 0x7F:                                   /* DEL (PuTTY default) */
        if (lineLen > 0) {
            lineLen--;
            usbPutsRaw("\b \b");
        }
        break;

    case 0x03:                                   /* Ctrl-C: discard line */
        usbPutsRaw("^C\r\n");
        lineLen = 0;
        printPrompt();
        break;

    default:
        if (c >= ' ' && c <= '~' && lineLen < LINE_MAX) {
            line[lineLen++] = (char)c;
            usbPutc((char)c);                    /* echo so the user sees it */
        }
        break;
    }
}

/* ================================================================== public */

void Shell_Init(void)
{
    rxHead = rxTail = 0;
    lineLen = 0;
    lastLine[0] = '\0';
}

void Shell_Poll(void)
{
    uint8_t c;

    /* terminal just opened the port: greet it (short delay - some hosts
     * raise DTR a moment before they start reading) */
    if (portOpenedFlag) {
        portOpenedFlag = false;
        txStalled = false;
        bannerPending = true;
        bannerAt = HAL_GetTick() + 100;
    }
    if (bannerPending && (int32_t)(HAL_GetTick() - bannerAt) >= 0) {
        bannerPending = false;
        printBanner();
    }

    while (rxGet(&c)) {
        txStalled = false;                       /* host is clearly there */
        handleChar(c);
    }

    if (watching && (int32_t)(HAL_GetTick() - watchNext) >= 0) {
        watchNext = HAL_GetTick() + watchPeriod;

        uint16_t prox;
        if (VCNL4040_ReadProximity(&prox) == HAL_OK) {
            if (prox > watchPeak) watchPeak = prox;
            uint32_t scale = (watchPeak < 1000) ? 1000 : watchPeak;

            /* one live line, redrawn in place: value + 20-char bar */
            char bar[21];
            uint32_t n = ((uint32_t)prox * 20u) / scale;
            for (uint32_t i = 0; i < 20; i++) bar[i] = (i < n) ? '#' : ' ';
            bar[20] = '\0';

            char out[64];
            snprintf(out, sizeof(out), "\rprox 0x%04X %5u |%s|", prox, prox, bar);
            usbPutsRaw(out);
            OLEDTerm_ShowGauge("Proximity", prox, scale);
        } else {
            usbPutsRaw("\rprox read failed     ");
        }
    }

    usbFlush();
}

/* ================================================================== handlers */

static bool ensureVcnl(void)
{
    if (VCNL4040_IsConnected()) return true;
    if (VCNL4040_Init(&hi2c1) == HAL_OK) return true;   /* hot-plug retry */
    Shell_Puts("VCNL4040 not found\ncheck I2C wiring\n");
    return false;
}

static void cmdHelp(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* USB gets the full list with descriptions... */
    mirrorOled = false;
    Shell_Puts("commands:\n");
    for (size_t i = 0; i < NUM_BUILTIN; i++)
        Shell_Printf("  %-8s%s\n", builtinCmds[i].name, builtinCmds[i].help);
    for (uint8_t i = 0; i < numUserCmds; i++)
        Shell_Printf("  %-8s%s\n", userCmds[i].name, userCmds[i].help);
    mirrorOled = true;

    /* ...the 21x8 OLED gets a compact 3-per-row grid so it all fits
     * under the echoed "> help" line */
    char cell[8];
    int  n = 0;
    OLEDTerm_Write("commands:\n");
    for (size_t i = 0; i < NUM_BUILTIN + numUserCmds; i++) {
        const char *name = (i < NUM_BUILTIN) ? builtinCmds[i].name
                                             : userCmds[i - NUM_BUILTIN].name;
        snprintf(cell, sizeof(cell), "%-7s", name);
        OLEDTerm_Write(cell);
        if (++n % 3 == 0) OLEDTerm_Write("\n");
    }
    if (n % 3) OLEDTerm_Write("\n");
}

static void cmdProx(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint16_t prox;
    if (!ensureVcnl()) return;
    if (VCNL4040_ReadProximity(&prox) == HAL_OK)
        Shell_Printf("prox: 0x%04X (%u)\n", prox, prox);
    else
        Shell_Puts("prox read failed\n");
}

static void cmdWatch(int argc, char **argv)
{
    if (!ensureVcnl()) return;

    watchPeriod = 200;
    if (argc > 1) {
        long ms = strtol(argv[1], NULL, 10);
        if (ms >= 20 && ms <= 5000) watchPeriod = (uint32_t)ms;
    }
    Shell_Printf("streaming every %lums\n", (unsigned long)watchPeriod);
    Shell_Puts("press any key to stop\n");
    OLEDTerm_Flush();

    mirrorOled = false;      /* OLED shows the gauge instead of scrolling text */
    watchPeak  = 0;
    watching   = true;
    watchNext  = HAL_GetTick();
}

static void cmdLight(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t lux10;
    uint16_t white;
    if (!ensureVcnl()) return;
    if (VCNL4040_ReadLux_x10(&lux10) == HAL_OK && VCNL4040_ReadWhite(&white) == HAL_OK) {
        Shell_Printf("lux:   %lu.%lu\n", (unsigned long)(lux10 / 10), (unsigned long)(lux10 % 10));
        Shell_Printf("white: %u\n", white);
    } else {
        Shell_Puts("light read failed\n");
    }
}

static void cmdVcnl(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint16_t id = 0, als = 0, ps12 = 0, ps3ms = 0;
    if (!ensureVcnl()) return;
    VCNL4040_ReadID(&id);
    VCNL4040_ReadReg(VCNL4040_REG_ALS_CONF,   &als);
    VCNL4040_ReadReg(VCNL4040_REG_PS_CONF12,  &ps12);
    VCNL4040_ReadReg(VCNL4040_REG_PS_CONF3MS, &ps3ms);
    Shell_Printf("ID       0x%04X\n", id);
    Shell_Printf("ALS_CONF 0x%04X\n", als);
    Shell_Printf("PS_CONF  0x%04X\n", ps12);
    Shell_Printf("PS_MS    0x%04X\n", ps3ms);
}

static void cmdI2c(int argc, char **argv)
{
    (void)argc; (void)argv;
    int found = 0;
    Shell_Puts("I2C1 devices:\n");
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1, 2) == HAL_OK) {
            const char *name = "";
            if (addr == 0x60) name = " VCNL4040";
            else if (addr == 0x3C || addr == 0x3D) name = " SSD1306";
            else if (addr == 0x30) name = " OV2640";
            Shell_Printf(" 0x%02X%s\n", addr, name);
            found++;
        }
    }
    if (!found) Shell_Puts(" none - check wiring\n");
}

static void cmdOled(int argc, char **argv)
{
    if (!OLEDTerm_IsPresent() && !OLEDTerm_Init()) {
        Shell_Puts("OLED not found\n");
        return;
    }
    if (argc < 2) {
        Shell_Puts("usage: oled <text>\n");
        return;
    }
    /* rebuild the text the user typed (argv split it on spaces) */
    for (int i = 1; i < argc; i++) {
        Shell_Puts(argv[i]);
        Shell_Puts(i < argc - 1 ? " " : "\n");
    }
}

static void cmdCls(int argc, char **argv)
{
    (void)argc; (void)argv;
    OLEDTerm_Clear();      /* Enter handler flushes after the command runs */
    usbPutsRaw("OLED cleared\r\n");
}

static void cmdInfo(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t up = HAL_GetTick() / 1000;
    Shell_Printf("SYSCLK %lu MHz\n", (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000));
    Shell_Printf("uptime %lus\n", (unsigned long)up);
    Shell_Printf("VCNL   %s\n", VCNL4040_IsConnected() ? "ok" : "missing");
    Shell_Printf("OLED   %s\n", OLEDTerm_IsPresent() ? "ok" : "missing");
    usbPutsRaw("built  " __DATE__ " " __TIME__ "\r\n");
}

static void cmdReboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    Shell_Puts("rebooting...\n");
    OLEDTerm_Flush();
    usbFlush();
    HAL_Delay(50);
    NVIC_SystemReset();
}

/* ------------------------------------------------------------------ camera self-test
 * Checks both camera buses without the ArduCAM library:
 *  - SPI to the ArduChip (FIFO controller): write/read the test register,
 *    read the chip version.
 *  - I2C (SCCB) to the OV2640 sensor: read the product ID, expect 0x26 / 0x4x.
 */
#define OV2640_SCCB_ADDR  (0x30 << 1)
#define ARDUCHIP_TEST1    0x00
#define ARDUCHIP_VER      0x40

static uint8_t camSpiRead(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), 0x00 }, rx[2] = { 0, 0 };
    HAL_GPIO_WritePin(CAM_CS_GPIO_Port, CAM_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, 10);
    HAL_GPIO_WritePin(CAM_CS_GPIO_Port, CAM_CS_Pin, GPIO_PIN_SET);
    return rx[1];
}

static void camSpiWrite(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg | 0x80), val };
    HAL_GPIO_WritePin(CAM_CS_GPIO_Port, CAM_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, tx, 2, 10);
    HAL_GPIO_WritePin(CAM_CS_GPIO_Port, CAM_CS_Pin, GPIO_PIN_SET);
}

static void cmdCam(int argc, char **argv)
{
    (void)argc; (void)argv;
    bool spiOk = true, i2cOk = true;

    /* SPI: the test register must echo back what we write */
    const uint8_t pattern[] = { 0x55, 0xAA, 0x3C };
    for (size_t i = 0; i < sizeof(pattern); i++) {
        camSpiWrite(ARDUCHIP_TEST1, pattern[i]);
        uint8_t got = camSpiRead(ARDUCHIP_TEST1);
        if (got != pattern[i]) {
            Shell_Printf("SPI wr %02X rd %02X\n", pattern[i], got);
            spiOk = false;
        }
    }
    Shell_Printf("SPI  %s\n", spiOk ? "ok" : "FAIL");
    if (spiOk)
        Shell_Printf("chip ver 0x%02X\n", camSpiRead(ARDUCHIP_VER));
    else
        Shell_Puts("check SCK D13 MISO\nD12 MOSI D11 CS A3\n");

    /* I2C: select sensor register bank (0xFF = 1), read PID/VER */
    uint8_t bank = 0x01, pid = 0, ver = 0;
    if (HAL_I2C_Mem_Write(&hi2c1, OV2640_SCCB_ADDR, 0xFF, I2C_MEMADD_SIZE_8BIT, &bank, 1, 10) != HAL_OK ||
        HAL_I2C_Mem_Read (&hi2c1, OV2640_SCCB_ADDR, 0x0A, I2C_MEMADD_SIZE_8BIT, &pid, 1, 10) != HAL_OK ||
        HAL_I2C_Mem_Read (&hi2c1, OV2640_SCCB_ADDR, 0x0B, I2C_MEMADD_SIZE_8BIT, &ver, 1, 10) != HAL_OK) {
        i2cOk = false;
    }
    if (i2cOk && pid == 0x26) {
        Shell_Printf("OV2640 id %02X%02X ok\n", pid, ver);
    } else if (i2cOk) {
        Shell_Printf("sensor id %02X%02X ?\n", pid, ver);
        i2cOk = false;
    } else {
        Shell_Puts("OV2640 no I2C reply\n");
    }

    Shell_Puts((spiOk && i2cOk) ? "camera PASS\n" : "camera FAIL\n");
}

