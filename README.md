# ECEN-435 — STM32 WiFi-Linked Controller (Lab 3+)

This repo tracks the STM32-based portion of ECEN-435, which begins at Lab 3
with a new custom PCB on a different microcontroller family. It picks up
where the earlier 8051 project left off and connects to it over WiFi.

## Background: the previous project

Labs 1–2 (and the prior course, ECEN-433) were built around a custom 8051
system:

- MCU: AT89C51RC (8051-based), 24 MHz crystal, external ROM/SRAM, PLD-based
  address decoding
- Peripherals: ILI9341 TFT LCD, 4x4 matrix keypad, RTC, 7-segment display
- Toolchain: SDCC

Lab 1 added configurable UART (baud rate/parity/data bits via Timer 2,
interrupt-driven RX) to that board. Lab 2 built on it to add WiFi
connectivity, laying the groundwork for the new board to talk to the old
one. That codebase and its history live separately — this repo starts fresh
at Lab 3.

## This project: Lab 3 onward

- New custom PCB built around an **STM32** microcontroller (different family
  from the 8051 board)
- Communicates with the original 8051-based system over **WiFi**, using the
  serial/WiFi link established in Labs 1–2 as the bridge between the two
  boards

*(Fill in as the design solidifies: specific STM32 part number, WiFi
module/interface, toolchain — CubeIDE/HAL vs. bare-metal register-level, and
any new peripherals on this board.)*

## Repo layout

```
src/      source files
include/  headers
docs/     lab reports and design notes
build/    build output (git-ignored)
```

## Lab progress

| Lab | Status | Notes |
|-----|--------|-------|
| Lab 1 — UART (old 8051 board) | Done | Prior project — history not in this repo |
| Lab 2 — WiFi (old 8051 board) | Done | Prior project — history not in this repo |
| Lab 3 — New STM32 PCB | In progress | Start of this repo |
