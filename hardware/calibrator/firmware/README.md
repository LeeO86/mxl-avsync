# Calibrator firmware

**Status: builds and passes host tests. Not tested on hardware yet.**

Firmware for the AV Sync calibrator board (WCH CH32X035G8R6, QSOP-28, RISC-V,
internal 48 MHz clock, no crystal). The signal contract (what the app sees and
must do) is in [`docs/calibrator.md`](../../../docs/calibrator.md).

## What it does

The board picks its mode from what the two light sensors see. No mode button.

| Mode | When | What the board does |
| --- | --- | --- |
| Idle | power-up, or sensors lit but no swaps | nothing |
| Camera | both sensors dark for 1 s | pulse train: LED flash + buzzer click, started by one register write |
| Display | the comparator sees a real swap between sensor A and B | clicks at once on every swap, then ignores the sensors for 200 ms |

Camera train: 3 start pulses at 0, 125 and 250 ms, then 20 timing pulses every 500 ms
(from 1000 ms), then 2 s pause, then it repeats. LED on 1.5 ms, click = 3 buzzer cycles at 2.7 kHz.
All numbers are fields of `cal_cfg_t` in `src/cal_logic.c` (`cal_default_cfg`).

A swap only counts when an ADC sample in the last 100 ms showed the two sensors clearly
different (at least 80 mV and 25 % apart, not dark) and the comparator now flips the other way.
This stops noise near zero (dark screen, OLED PWM off-phase) from making clicks.

## USB stays off

The board has USB data lines only for firmware updates. **This firmware never turns USB on**:
the USB and USB-PD blocks get no clock, the D+/D− pull-ups stay off, and PC16/PC17 stay
floating inputs. A phone therefore never sees a USB device (no pop-ups, no change of the
audio route); it only gives 5 V, because of the 5.1 kΩ resistors on CC1/CC2.
Only the chip's ROM bootloader uses USB, and only in update mode (below).

## Pins

| Pin | Name | Use |
| --- | --- | --- |
| 15 | PA4 | LED (high = on) |
| 16 | PA5 | BUZ (high = transistor on) |
| 5 | PA13 | sensor A → comparator 3 + input (C3P0); bonded to PC15 inside the package |
| 9 | PA0 | sensor A (same net) → ADC channel 0 |
| 11 | PA2 | sensor B → comparator 3 − input (C3N0) and ADC channel 2 |
| 18 | PB3 | comparator 3 output (C3O). Read back for the edge interrupt (EXTI line 3, both edges). **Leave unconnected on the board.** |
| 2, 3 | PC16, PC17 | USB D−, D+. PC17 + button to VDD = update mode at power-on |
| 28, 1 | PC18, PC19 | SDI debug (SWDIO, SWCLK), left at reset default |

Comparator 3 runs with hysteresis. Its output goes to PB3; the EXTI interrupt reads PB3 to know
which sensor is brighter and clicks right away (comparator → pin → interrupt is well under 1 µs).
All other pins are analog (off). A watchdog (about 250 ms) resets the chip if the code hangs, so
the buzzer and LED cannot stay on.

## Files

- `src/cal_logic.c/.h` – all mode and timing logic, plain C, no hardware. Same code runs in the tests.
- `src/main.c` – hardware: clock, GPIO, comparator + EXTI, ADC, SysTick timing, watchdog.
- `src/startup.c`, `src/ch32x035.ld` – our own startup code, vector table and linker script.
- `tests/test_cal_logic.c` – host unit tests.
- `vendor/` – minimal CH32X035 register header from ch32fun (MIT), see `vendor/README.md`.

## Build and test

Needs a RISC-V GCC in `PATH` and a normal `cc` for the tests. Plain RV32IMAC, no C library,
no WCH compiler extensions.

```sh
make                                   # xPack riscv-none-elf-gcc (default)
make CROSS=riscv64-unknown-elf-        # Ubuntu: apt install gcc-riscv64-unknown-elf picolibc-riscv64-unknown-elf
make test                              # host unit tests
make clean
```

Output: `build/calibrator.bin` (flash this), `.hex`, `.elf`.

## Update over USB (update mode)

1. Unplug the board.
2. Hold the button ("hold to update") and plug the board into a PC with a USB-C to USB-C
   or USB-A to USB-C cable.
3. Release the button after about 1 s. The chip's ROM bootloader starts and shows up as a
   WCH ISP device (USB ID `4348:55e0`, on some chips `1a86:55e0`).
4. Flash with an open-source tool:

   ```sh
   wchisp flash build/calibrator.bin          # https://github.com/ch32-rs/wchisp (GPL-2.0)
   # or
   python3 chprog.py build/calibrator.bin     # wagiminator/MCU-Flash-Tools (MIT)
   ```

5. Unplug and plug in again (without the button): the new firmware runs.

The bootloader is only checked at power-on (not on reset): the button must be held while
power comes up. A blank chip (as delivered by JLCPCB) always starts the bootloader, so the
first flash needs no button.

On Linux, allow access to the bootloader without root, e.g. `/etc/udev/rules.d/50-wch-isp.rules`:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="4348", ATTRS{idProduct}=="55e0", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", MODE="0666"
```

then `sudo udevadm control --reload-rules` (or ask your admin). WCH's own WCHISPTool and
WCH-LinkUtility (closed source) work too. A WCH-LinkE probe on PC18/PC19 can be used for
debugging, but the board has no pads for it.

## Licence

MIT, like the rest of the repository (the board itself is CERN-OHL-P-2.0, see `../README.md`).
Vendored header: see `vendor/README.md`.
