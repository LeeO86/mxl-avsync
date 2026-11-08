# AV Sync calibrator board

<!-- SPDX-License-Identifier: CERN-OHL-P-2.0 -->

**Status: v1 proposal, in development. Not ordered, not built, not tested.**

A small giveaway board (60 × 22 mm) for the AVsync app. The phone powers it over a short
USB-C cable. It calibrates:

- **camera mode**: a phone's camera against its microphone (LED flash + click together),
- **display mode**: a sending screen against its speaker (the board clicks when the
  screen pattern swaps).

The board picks the mode from its two light sensors. What the app must do is in
[`docs/calibrator.md`](../../docs/calibrator.md).

![top](renders/calibrator-top.png)
![bottom](renders/calibrator-bottom.png)

Black solder mask, ENIG finish: the orange parts of the AVsync logo (waveform and the
ring in the middle) are bare gold copper; the rest of the logo is white silkscreen. The
bottom has a QR code to <https://leeo86.github.io/mxl-avsync/>.

Schematic: [`renders/calibrator-schematic.pdf`](renders/calibrator-schematic.pdf)
(also [SVG](renders/calibrator.svg)). 3D view: [`renders/calibrator-3d.png`](renders/calibrator-3d.png).

## How it works (short)

- USB-C receptacle, power only: 5.1 kΩ on CC1/CC2 so the phone turns on 5 V. D+/D− are
  not connected, so the phone's audio route does not change. No battery.
- PY32F002A runs straight from 5 V (its range is 1.7–5.5 V, absolute max 6.25 V); 10 Ω +
  1 µF filter spikes. No regulator.
- White LED (PA5, ~12 mA) and buzzer transistor (PA6) are on the same port, so one register
  write starts both.
- Two phototransistors feed comparator 2 (PA3 +, PA2 −) and the ADC. The comparator flips
  at the crossing point of a black/white swap; the ADC decides dark/light and checks that a
  flip is real.

## Mechanics: why holes through the board

The LED must touch the camera lens and the sensors must lie flat on the screen, but the
buzzer (3 mm) and the USB-C receptacle (3.2 mm) are much taller than an LED.

| Option | Cost per order | Result |
| --- | --- | --- |
| **Chosen: all parts on top, LED and sensors look down through holes, bottom side flat** | 0 extra; sensors cost ~$0.40/board more than normal ones | flat bottom touches lens and screen; one-side Economic PCBA |
| Two-sided assembly (LED + sensors on the bottom) | Standard PCBA: setup $51.12 instead of $8.18, stencil $16.42, fixtures $41 (100 pcs) to $164 (500 pcs), $0.48/board minimum | ≈ +$1.00/board at 100, +$0.45 at 500 |
| LED and sensors on the top, board lies parts-down | 0 | does not lie flat: buzzer and plug are 3 mm tall |
| Mid-mount USB-C | small | buzzer still 3 mm; cable plug sticks out both sides anyway |

Parts that make the hole idea work, all in JLC stock:

- **LED**: XINGLIGHT XL-3216UWC-FB, a white *reverse-mount* 1206 LED. Its lens sits in a
  2.1 × 1.7 mm cut-out and shines through the board. Lite-On LTW-C230DS (C575513) fits the
  same footprint as a second source.
- **Sensors**: Vishay VEMT2500X01, a *reverse-gullwing* phototransistor (visible + IR,
  ±15°). The body sits on top, the dome lens looks down through a 2.3 mm hole. With a
  **1.2 mm board** the lens ends flush with the bottom. VEMT2000X01 is the same package.
  JLC has no normal-price reverse-mount sensor, so these are the most expensive small parts.
- **USB-C at the far end**: the cable plug is thicker than the board and sticks out
  ~0.5 mm below the bottom. It sits 50 mm away from the LED and sensors, so the board tilts
  less than 1° and the LED end stays flat on the lens or screen.

Other notes:

- **Look (it is merch)**: AVsync logo on both sides, from `assets/app-icon.svg`. Cyan and
  white strokes are silkscreen. The orange parts are copper with a solder-mask opening and no
  paste, so with ENIG they look gold. That copper has no net; keep-outs keep tracks and vias
  away from it. On the bottom the logo and QR are mirrored so they read right from below.
- **QR code** (bottom): `https://leeo86.github.io/mxl-avsync/` (the site has no calibrator
  section yet, so the root URL). Version 3-M, 0.45 mm modules (3× JLC's 0.153 mm minimum
  line), 4-module quiet zone, white silkscreen on black mask (light background, dark
  modules = normal polarity). Checked: zxing-cpp decodes the bottom render, also at 1/3
  size. The matrix is in `scripts/qr_matrix.txt` (made by `scripts/gen_qr.py`, needs `segno`).
- **Text**: one font, three sizes: name 1.8–2.0 mm, sub-lines 1.0–1.2 mm, and JLC's minimum
  1.0 mm / 0.153 mm stroke for "not a toy", the licence and the pad labels.
- iPhone camera bumps: the lens rings stand higher than the bump plate. The board rests on
  the ring of the lens under the LED circle; hold it lightly or tape it. The buzzer sits
  next to the LED (5 mm), with its side sound port pointing at the LED, so it is close to the
  rear microphone, which sits on the bump on most phones.
- Alignment marks on **both** sides: LED circle, `A`/`B`, the split line (on the top it
  runs from the notch past the LED and again behind the buzzer, so you can see the line
  while the board lies face down) and a half-round notch in the board edge on the split line.
- No company name on the board.
- Ground copper on both layers stops light going sideways through the board. No vias at
  the optics end.
- Programming pads (5V, GND, SWDIO, SWCLK, NRST) are bare copper on the flat bottom, for a
  pogo-pin jig.

## BOM (JLCPCB/LCSC, live stock and prices checked 2026-11-08)

USD per part at the order quantity of ~100 and ~500 boards. Ext = extended part
($3.07 feeder fee per type, Economic PCBA).

| Ref | Part | LCSC | Lib | Qty | Stock | $ @100 | $ @500 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| U1 | PY32F002AF15P6TU, TSSOP-20 | C5292059 | Ext | 1 | 50,312 | 0.1668 | 0.1163 |
| Q2, Q3 | VEMT2500X01 phototransistor, reverse gullwing | C3210911 | Ext | 2 | **782** ⚠ | 0.2306 | 0.1979 |
| BZ1 | MLT-8530 magnetic buzzer 8.5 × 8.5 × 3 mm | C94599 | Ext | 1 | 137,398 | 0.158 | 0.1369 |
| J1 | TYPE-C 6P receptacle (SHOU HAN), power only | C456012 | Ext | 1 | 1,000,308 | 0.0315 | 0.0277 |
| D1 | XL-3216UWC-FB white LED, reverse mount | C3646935 | Ext | 1 | 111,067 | 0.0164 | 0.0128 |
| Q1 | S8050 NPN, SOT-23 | C2146 | Basic | 1 | 1,454,339 | 0.0141 | 0.0109 |
| D2 | 1N4148WS, SOD-323 | C2128 | Basic | 1 | 2,842,054 | 0.011 | 0.0086 |
| C1 | 10 µF 25 V 0805 | C15850 | Basic | 1 | 8,661,703 | 0.0359 | 0.028 |
| C2 | 1 µF 0402 | C52923 | Basic | 1 | 11.5 M | 0.0058 | 0.0058 |
| C3 | 100 nF 0402 | C1525 | Basic | 1 | 34.2 M | 0.0031 | 0.0031 |
| C4, C5 | 10 nF 0402 | C15195 | Basic | 2 | 5.2 M | 0.0038 | 0.0029 |
| R1, R2 | 5.1 kΩ 0402 | C25905 | Basic | 2 | 4.5 M | 0.0023 | 0.0017 |
| R3 | 10 Ω 0402 | C25077 | Basic | 1 | 3.6 M | 0.001 | 0.001 |
| R4 | 150 Ω 0603 | C22808 | Basic | 1 | 1.3 M | 0.0015 | 0.0015 |
| R5 | 1 kΩ 0402 | C11702 | Basic | 1 | 33.7 M | 0.0013 | 0.0013 |
| R6 | 100 kΩ 0402 | C25741 | Basic | 1 | 8.8 M | 0.0012 | 0.0012 |
| R7, R8 | 22 kΩ 0402 | C25768 | Basic | 2 | 1.0 M | 0.0012 | 0.0009 |
| TP1–TP5 | bare pads (bottom) | – | – | – | – | – | – |

**Parts per board: $0.94 @100, $0.76 @500.**

⚠ 500 boards need 1000 VEMT2500X01, JLC has 782. Either reserve them now and fill up with
VEMT2000X01 (C143697, same package, 843 in stock, $0.42), or wait for a restock.

## Cost per board (JLCPCB Economic PCBA, one side)

Fees from the JLC price page (updated 2026-10-22): setup $8.18, stencil $1.53,
5 extended types × $3.07, 71 joints × $0.0016.

| Item | 100 pcs | 500 pcs |
| --- | --- | --- |
| Parts | $94 | $382 |
| Setup + stencil | $9.71 | $9.71 |
| Extended feeders (5 × $3.07) | $15.35 | $15.35 |
| SMT joints | $11.36 | $56.80 |
| PCB 2-layer 60 × 22 mm, 1.2 mm, green, lead-free HASL [estimate] | ~$20 | ~$60 |
| ENIG instead of HASL (gold logo) [estimate] | ~+$20 | ~+$55 |
| Black mask instead of green [not verified] | $0 expected | $0 expected |
| **Total ex. shipping, VAT, programming** | **≈ $170 → $1.70/board** | **≈ $580 → $1.15/board** |

The JLC PCB price (with ENIG and mask colour) comes from their JavaScript calculator; I could
not get a live quote, so the PCB lines are estimates. JLC usually charges the same for black
as for green on 2-layer boards, but check it in the quote. Run the real quote with
`fab/calibrator-gerbers.zip` (1.2 mm, ENIG, black). Add "Confirm Parts Placement" ($0.45, recommended: the custom footprints are new) and
shipping/import to CH (see the earlier BOM research: about CHF 25–40 at 100 pcs).

**USB-C to USB-C cable, bought separately** (short, USB 2.0, no e-marker needed):
JLC/LCSC do not list one. Retail 15 cm cables cost about $2.50–3 (e.g. The Pi Hut £2.25);
bulk from a cable maker is probably $0.5–1.5 at 100–500 pcs [estimate, no quote].
The cable often costs more than the board.

## Files

| Path | What |
| --- | --- |
| `calibrator.kicad_pro/.kicad_sch/.kicad_pcb` | KiCad 9 project |
| `calibrator.pretty/` | custom footprints (buzzer, reverse LED, reverse-gullwing sensor, USB-C 6P) |
| `3d/` | simple box 3D models for the renders |
| `fab/calibrator-gerbers.zip` | gerbers + drill for JLCPCB (order **1.2 mm**, **ENIG**, **black** mask, white silk) |
| `fab/calibrator-bom-jlc.csv`, `fab/calibrator-cpl-jlc.csv` | JLC BOM and pick-and-place files |
| `renders/` | PNG renders, schematic PDF/SVG |
| `calibrator.kicad_sym` | custom PY32F002A symbol with pins grouped by use |
| `scripts/` | generators: `design.py` (parts, nets, placement) → schematic (`gen_sch.py`, drawn by hand in code), board (`gen_pcb.py`), fab files; `check_netlist.py` checks the drawn schematic against `design.py` |
| `firmware/` | PY32 firmware + host tests ([README](firmware/README.md)) |

### Rebuild

Needs docker only (pinned images `kicad/kicad:9.0.9-full`, `ghcr.io/freerouting/freerouting:2.1.0`).

```sh
scripts/build.sh   # footprints, schematic, placement, autoroute, ground stitching, ERC, DRC
scripts/fab.sh     # gerbers, drill, BOM, CPL, renders, schematic PDF/SVG
```

The autorouter does not give the same tracks every run; the committed board is the
checked one.

### Checks

- ERC: 0 violations. Netlist check: the schematic wires give exactly the nets in `design.py`.
- DRC: 0 violations, 0 unconnected (rules for JLC: 0.15 mm track/space, 0.3/0.6 mm vias,
  0.3 mm copper to board edge).
- CPL rotations are KiCad's. Check every part in JLC's placement preview (especially the
  reverse LED, the sensors and the USB-C) before ordering.

## Production programming

JLC only programs chips on Standard PCBA, so Economic boards arrive blank. Plan: a small
pogo-pin jig on the 5 bottom pads and a CMSIS-DAP probe (pyOCD with the Puya PY32F0xx pack).
About 10 s per board, so ~1.5 h for 500 boards. See the [firmware README](firmware/README.md).

## Risks and open points

1. **Phone powers the board?** A sink with 5.1 kΩ Rd is the standard USB-C way to ask for
   5 V. iPhone 15+ and most Android phones supply it; some ask the user or need OTG on.
   To test with real phones.
2. **Buzzer delay** from coil-on to sound is unknown (expect 0.05–0.3 ms). Measure once,
   then the app subtracts it.
3. **Sensor signal**: on dim screens the 22 kΩ load may give little voltage; room light can
   enter the clear sensor bodies from above (both sensors alike, so it mostly cancels).
   Change R7/R8 or put a sticker over the sensors if needed.
4. **LED fit**: the reverse LED lens (1.86 × 1.4 mm) has only ~0.1 mm room in the routed
   cut-out. Check the first boards.
5. **LED brightness through the lens**: the LED touches the lens and floods the picture;
   1.5 ms at 12 mA should be plenty, but a very short exposure shows only a band of rows.
6. **Sensor stock** (see BOM).
7. **No hardware test yet**: firmware register setup (clock, comparator, ADC) is from the
   datasheet and vendor headers only.

## Licence

- **Board** (KiCad project, footprints, 3D models, scripts, fab files, renders and the logo
  artwork on the board): **CERN-OHL-P-2.0**, see
  [`LICENSE-CERN-OHL-P-2.0.txt`](LICENSE-CERN-OHL-P-2.0.txt).
  `SPDX-License-Identifier: CERN-OHL-P-2.0`. It is printed small on the top silkscreen.
  CERN-OHL-S-2.0 (share-alike: changed boards must stay open) would be the alternative.
- **Firmware** (`firmware/`): MIT, like the rest of the repository. The vendored headers are
  BSD-3-Clause (Puya/ST) and Apache-2.0 (Arm); see [`firmware/vendor/README.md`](firmware/vendor/README.md).
