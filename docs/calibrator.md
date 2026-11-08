# AV Sync calibrator: signal contract

**v1 proposal — app side not built yet. The board is not built or tested yet either.**

The calibrator is a small board (56 × 22 mm) that the phone powers over a short USB-C cable.
It has a white LED, a buzzer and two light sensors. It picks its mode by itself from
what the sensors see. There is no button and no battery.

Hardware files: [`hardware/calibrator/`](../hardware/calibrator/README.md).
Firmware: [`hardware/calibrator/firmware/`](../hardware/calibrator/firmware/README.md).
All numbers below are defaults in `cal_default_cfg()` (`firmware/src/cal_logic.c`).

## Words

- **Bottom side**: the flat side with no parts. It touches the phone or the screen.
  The LED and both sensors look out through holes in the board on this side.
- **Sensor A / B**: the two light sensors, 9 mm apart, marked `A` and `B` on both sides.
- **Split line**: the line half-way between A and B. It runs along the board and ends in
  the half-round notch at the LED end. It is printed on both sides.
- **Click**: 3 cycles of a 2.7 kHz square wave on the buzzer (about 1.1 ms). The first
  cycle starts with the buzzer coil switched on.

## Power

- Only USB-C phones. The board is a USB-C *sink*: 5.1 kΩ on CC1 and CC2. D+ and D− are
  not connected, so the phone does not see an audio or data device and keeps its own
  speaker and microphone.
- The phone must supply 5 V on USB-C (iPhone 15 and later, most Android phones with OTG).
- The board needs about 10 mA, plus short peaks of about 100 mA during a click.
- It starts in **Idle** about 1 ms after power-up.

## Modes

| Mode | Enters when | Does |
| --- | --- | --- |
| Idle | power-up; sensors see light but nothing changes | nothing (silent, LED off) |
| Camera | both sensors dark (below 150 mV) for 1000 ms | pulse train (below) |
| Display | a valid swap is seen (below) | one click on every swap |

- Camera → Idle: the sensors see light for 300 ms (the board was lifted).
- Display → Idle: no valid swap for 1500 ms.
- Idle or Camera → Display: at the first valid swap. That swap already gets a click.
  A camera train stops at once.

## Camera mode (calibrates a phone's camera against its microphone)

**Placement**: the phone lies screen down. The board lies on the camera bump, bottom side
down, with the LED circle on the main rear camera lens. The buzzer sits next to the LED,
so it is close to the rear microphone on most phones. The sensors then face the phone's
back and see dark, which starts camera mode after 1 s.

**Pulse**: the LED turns on and the click starts with **one register write** (LED and
buzzer pins on the same port, same store), so they start less than 1 µs apart.
LED on time: 1.5 ms (`led_pulse_us`, can be 0.5–5 ms). The buzzer needs some time to move
air after its coil is switched on; that delay is the same for every board of a type and
still has to be measured once (open point).

**Train** (times from the start of the train):

| Pulses | Times |
| --- | --- |
| 3 start pulses | 0, 125, 250 ms |
| 20 timing pulses | 1000, 1500, …, 10500 ms (every 500 ms) |
| pause | 2000 ms after the last pulse (until 12500 ms) |

Then the train starts again with the 3 start pulses, as long as the board stays in
camera mode. The 125 ms spacing never happens elsewhere, so the app can find the start.

**What the app must do**

1. Record video (rear camera) and audio (built-in microphone) for at least one train (~13 s).
2. Find each flash: the LED floods the whole picture (no focus needed). With a short
   exposure only some rows see it (rolling shutter); use the row timing as the existing
   calibration does (`makeCalibration`, `docs/calibration.md`). Use the **start** of the
   flash.
3. Find each click onset in the audio.
4. Pair flashes and clicks by the train pattern. `offset = click time − flash time`,
   per pulse; use the median of the 20 timing pulses.
5. Subtract the fixed buzzer delay and the sound travel time from buzzer to microphone
   (about 0.03 ms per cm; the buzzer is about 1–2 cm from the mic on most phones).
6. Save it like today's calibration record (per model and OS version).

The MCU clock is only good to about 2 %, so the 500 ms spacing is not a time reference.
Only "flash and click start together" is.

## Display mode (calibrates a sender screen against its speaker; "transponder")

**Pattern** (shown by the sending phone, on its own screen):

```
 ┌──────────────┬──────────────┐
 │              │              │
 │    half L    │    half R    │    each half at least 20 × 20 mm
 │              │              │    (the board needs 2 mm margin around each sensor)
 │              │              │
 └──────────────┴──────────────┘
                ^ pattern line = where the board's split line goes
```

- Two rectangles side by side, sharing one straight edge (the **pattern line**).
- State 1: L white, R black. State 2: L black, R white. Pure white (full brightness) and
  pure black, nothing else on the halves.
- **Swap** between state 1 and state 2 every **500 ms** (allowed: 300–1000 ms). Both halves
  change in the same frame.
- The pattern line must run **across the display's scan rows** so that both sensors are on
  the same row: for a phone in portrait, put the halves left and right of each other
  (vertical pattern line). Otherwise one sensor updates later and the crossing moves.
- Draw a marker where the notch goes, e.g. a short tick on the pattern line.
- Set the screen to full brightness while calibrating (this also keeps OLED flicker low).

**Placement**: bottom side down on the screen. The split line lies on the pattern line
(the notch on the line), sensor A over one half and sensor B over the other. Up to ±2 mm
off is fine.

**How the board decides**: the two sensors drive the two inputs of the MCU comparator.
The comparator flips at the moment the two sensors see the same light, i.e. half-way
through the swap, where the rising half and the falling half cross. Slow LCD pixels,
backlight changes, OLED flicker and room light hit both sensors the same way and mostly
cancel. A flip counts as a **valid swap** only if, in the 100 ms before, the ADC saw the
two sensors clearly different (at least 80 mV and 25 % apart, not dark) the *other* way
round. Noise around zero therefore never clicks.

**Click**: at a valid swap the board clicks **at once** (about 5 µs after the comparator
flips, no timed delay). Then it ignores the sensors for **200 ms** (lock-out).

**What the app must do**

1. Run the pattern. For each swap, plan it on the phone's display clock (CADisplayLink /
   Choreographer) and note its planned presentation time `t_swap`.
2. Play a short sound on the built-in speaker at exactly `t_swap + 100 ms` on the same
   precise clock (100 ms = `D`; any fixed value 50–200 ms works, it must be longer than the
   expected display delay and shorter than the lock-out gap to the next swap).
3. Record the phone's own microphone. For every swap it hears the board's click first,
   then the speaker sound.
4. Measure `gap = speaker sound onset − click onset` per swap (both through the same
   microphone, so the microphone delay cancels).
5. `screen-vs-speaker offset = gap − D` = (speaker output delay) − (display light delay).
   Positive = the sound comes out later than the picture. Use the median.
6. Use both swap directions equally (state 1→2 and 2→1). A small mismatch between the two
   sensors moves the crossing early in one direction and late in the other, so the
   average cancels it.
7. Correct for the fixed buzzer delay and the sound path difference: the board sits on
   the screen, a few cm from the microphone, while the speaker is in the phone body. Put
   the pattern near the microphone end of the phone to keep that path short; about
   0.03 ms per cm.
8. If no click comes for 1.5 s, the board has fallen back to idle: tell the user to
   check the placement.

## Timing summary

| Item | Value |
| --- | --- |
| LED on time | 1.5 ms |
| Click | 3 × 2.7 kHz (≈1.1 ms), first half-cycle = coil on |
| LED to click start | < 1 µs (same register write) |
| Camera train | 3 start pulses (0/125/250 ms), 20 pulses every 500 ms from 1000 ms, 2 s pause |
| Swap period (app) | 500 ms (300–1000 ms) |
| Comparator flip to click | ≈ 5 µs |
| Lock-out after click | 200 ms |
| Valid swap needs | ADC difference ≥ 80 mV and ≥ 25 %, opposite sign, within 100 ms |
| Dark | both sensors < 150 mV |
| Enter camera | dark for 1000 ms |
| Leave camera | light for 300 ms |
| Leave display | no valid swap for 1500 ms |
| Ignore sensors after a camera pulse | 5 ms |

## Open points

- The buzzer's delay from coil-on to sound must be measured once (lab: reference
  microphone + photodiode). It enters both modes as a constant.
- Whether every phone powers a passive USB-C sink without asking the user. Expected yes
  for iPhone 15 and later and most Android phones; to check on real phones.
- Sensor signal on dim screens and room light leaking into the clear sensor bodies from
  above: to check on the first boards.
