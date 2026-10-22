# ecen435-stm32

ECEN-435 (Fall 2026) STM32L452 firmware — J. Honan

| Folder | MCU | Purpose |
|---|---|---|
| `NUCLEO_L452RE/` | STM32L452RET6 (LQFP64) | Dev/test on the NUCLEO-L452RE (Lab 5 device libraries) |
| `PCB_L452CE/` | STM32L452CET6 (LQFP48) | Custom PCB target (Lab 4 schematic) |

Both projects use the **same pin map**, so application code in `Core/` moves between them unchanged.

## Pin map

| Function | Pin | NUCLEO header | Notes |
|---|---|---|---|
| I2C1 SCL / SDA | PA9 / PA10 | D8 / D2 | VCNL4040, SSD1306, OV2640 config — 400 kHz |
| SPI1 SCK / MISO / MOSI | PA5 / PA6 / PA7 | D13 / D12 / D11 | ArduCAM, SPI SRAM — /16 = 5 MHz |
| CAM_CS | PB0 | A3 | GPIO out, idle high |
| SRAM_CS | PB1 | CN10 | GPIO out, idle high |
| UART4 TX / RX | PA0 / PA1 | A0 / A1 | ESP32-C6 (GPIO17 / GPIO16), 115200 8N1 |
| USB DM / DP | PA11 / PA12 | CN10 | USB CDC shell |
| SWD | PA13 / PA14 | — | |
| CAN1 RX / TX | PB12 / PB13 | — | PCB only |
| USER_LED1 / LED2 | PA2 / PA3 | — | PCB only (PA2/PA3 = ST-LINK VCP on the NUCLEO) |

## Clocks
MSI 4 MHz → PLL (M/1, N×40, R/2) → **80 MHz SYSCLK**, AHB/APB1/APB2 /1 (matches handout Figure 4).
USB 48 MHz from PLLSAI1Q (MSI × 24 / 2). If USB enumeration is flaky: CLK48 mux → HSI48 and RCC → CRS SYNC Source = USB.
TIM2 prescaler 79 → 1 µs tick (used by `delay_us`).

## Build / flash (VS Code + STM32Cube extension)
Toolchain in CubeMX Project Manager = **Makefile**.

```
cd NUCLEO_L452RE      # or PCB_L452CE
make -j8
STM32_Programmer_CLI -c port=SWD -w build/NUCLEO_L452RE.bin 0x08000000 -v -rst
```
F5 in VS Code builds, flashes, and starts a debug session over ST-LINK.

## Toolchain (Windows)
`make` and `arm-none-eabi-gcc` come from the STM32CubeIDE 2.2.0 plugins; `STM32_Programmer_CLI` from the STM32Cube extension bundle. All three are on the user PATH.
