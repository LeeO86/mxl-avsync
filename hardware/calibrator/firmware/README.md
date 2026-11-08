# Calibrator firmware

**Status: builds and passes host tests. Not tested on hardware yet.**

Firmware for the AV Sync calibrator board (PUYA PY32F002AF15P6TU, 24 MHz internal clock).
The signal contract (what the app sees and must do) is in [`docs/calibrator.md`](../../../docs/calibrator.md).

## What it does

The board picks its mode from what the two light sensors see. No button.

| Mode | When | What the board does |
| --- | --- | --- |
| Idle | power-up, or sensors lit but no swaps | nothing |
| Camera | both sensors dark for 1 s | pulse train: LED flash + buzzer click, started by one register write |
| Display | the comparator sees a real swap between sensor A and B | clicks at once on every swap, then ignores the sensors for 200 ms |

Camera train: 3 start pulses at 0, 125 and 250 ms, then 20 timing pulses every 500 ms
(from 1000 ms), then 2 s pause, then it repeats. LED on 1.5 ms, click = 3 buzzer cycles at 2.7 kHz.
All numbers are fields of `cal_cfg_t` in `src/cal_logic.c` (`cal_default_cfg`), so they are easy to change.

A swap only counts when an ADC sample in the last 100 ms showed the two sensors clearly
different (at least 80 mV and 25 % apart, not dark) and the comparator now flips the other way.
This stops noise near zero (dark screen, OLED PWM off-phase) from making clicks.

## Pins

| Pin | Name | Use |
| --- | --- | --- |
| 7 | PA5 | LED (high = on) |
| 6 | PA6 | BUZ (high = transistor on) |
| 14 | PA3 | sensor A: COMP2 + input, ADC_IN3 |
| 13 | PA2 | sensor B: COMP2 − input, ADC_IN2 |
| 2, 3 | PA13, PA14 | SWD |
| 5 | PB6/BOOT0 | tied to GND on the board, never driven |

All other pins are analog (off). A watchdog (about 250 ms) resets the chip if the code hangs,
so the buzzer and LED cannot stay on.

## Files

- `src/cal_logic.c/.h` – all mode and timing logic, plain C, no hardware. Same code runs in the tests.
- `src/main.c` – hardware: clock, GPIO, comparator interrupt, ADC, timer, watchdog.
- `src/startup.c`, `src/py32f002a.ld` – our own startup code and linker script.
- `tests/test_cal_logic.c` – host unit tests.
- `vendor/` – PY32 and Arm CMSIS headers (BSD-3-Clause and Apache-2.0, see `vendor/README.md`).

## Build and test

Needs `arm-none-eabi-gcc` in `PATH` (Ubuntu: `apt install gcc-arm-none-eabi`, or the xPack build)
and a normal `cc` for the tests.

```sh
make          # build/calibrator.elf, .bin, .hex, prints the size
make test     # host unit tests
make clean
```

## Flash

The board has SWD test pads on its flat bottom side: VCC (5 V), GND, SWDIO, SWCLK, NRST.
Any CMSIS-DAP probe works in principle, for example with pyOCD and the Puya PY32F0xx pack:

```sh
pyocd pack install py32f002a
pyocd flash -t py32f002ax5 build/calibrator.hex
```

The pack and target names above are a guess and the flow is not tested yet. For a batch, a pogo-pin jig on the bottom pads is the plan.
