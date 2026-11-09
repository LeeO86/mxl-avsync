# AV Sync calibrator: signal contract

**v1 proposal. The app side is built (iOS app and container generator, see [App side](#app-side-built)), but only tested on synthetic signals. The board is not built or tested yet.**

The calibrator is a small board (66 × 22 mm) that the phone powers over a USB-C to USB-C cable.
The cable is not included: the user brings their own.
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
  the half-round notch at the LED end. It is printed on both sides; on the top it shows
  next to the LED and again behind the buzzer, so it can be lined up while the board lies
  face down.
- **Click**: 3 cycles of a 2.7 kHz square wave on the buzzer (about 1.1 ms). The first
  cycle starts with the buzzer coil switched on.

## Power

- Only USB-C phones. The board is a USB-C *sink*: 5.1 kΩ on CC1 and CC2. D+ and D− go to
  the MCU but are only used for firmware updates: the firmware never turns USB on, so the
  phone does not see an audio or data device and keeps its own speaker and microphone.
- Firmware update (not with a phone): hold the board's BOOT button while plugging it into
  a PC; see `hardware/calibrator/README.md`.
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

**Click**: at a valid swap the board clicks **at once** (a few µs after the comparator
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
| Light change to comparator flip | ~15 µs filter (22 kΩ + 1 nF) plus sensor response (estimate) |
| Comparator flip to click | a few µs (estimate) |
| Lock-out after click | 200 ms |
| Valid swap needs | ADC difference ≥ 80 mV and ≥ 25 %, opposite sign, within 100 ms |
| Dark | both sensors < 150 mV |
| Enter camera | dark for 1000 ms |
| Leave camera | light for 300 ms |
| Leave display | no valid swap for 1500 ms |
| Ignore sensors after a camera pulse | 5 ms |

## App side (built)

Code: `core/include/avsync/calibrator.hpp` (all analysis, shared by every app),
`PhoneAnalyzer::begin_calibrator` (camera take), the iPhone generator's
calibration mode and the container's `AVSYNC_PATTERN=calibration`. User guide:
[calibration.md](calibration.md#with-the-calibrator).

**Finding the sounds.** High-pass at 300 Hz, then the RMS over the last 0.25 ms.
A sound starts where this envelope first reaches 25 % of its peak. The same
detector times the board's click and the generator's beep, so in display mode
its small delay cancels.

**Click or beep?** First by time order (click, then the beep about D later),
then by sound: the energy at 2.7 kHz (click) against 1.5 kHz (beep) in the
3 ms after the onset. The generator's beep is 6 cycles of 1.5 kHz (4 ms), with a
sharp start and a soft end, peak at the chirp level.

**Camera mode.** The analyser keeps the mean brightness of 32 row bands per
frame. The flash start is where a band reaches half its flash level, on the
row time the analyser already uses (`frame start + readout × row`): that row's
exposure ended half an LED pulse after the flash started. iOS gives no readout
time and the analyser exposes for the whole frame interval; then the readout
is fitted over the pulses: the board's clock drifts against the camera, so the
flash lands on different rows, and click − flash grows with the row by exactly
the readout time. Flashes and clicks are paired by the most common difference
(±12 ms around it). Offset = median of the pairs after dropping outliers
(beyond 3 σ, at least 0.5 ms); uncertainty = standard error of the median.
For the camera take the iPhone locks the exposure at the lowest ISO.

**Display mode.** Every click is paired with the next beep less than 450 ms
later. Offset = beep − click − D, the median per swap direction (every other
swap), and the mean of the two directions. The app does not need the swap
times, so the same analysis works for a TV fed by the container generator.

**Pattern.** Every generator uses the same core functions: period k lasts
500 ms on its frame grid (for 59.94 fps the swap is the first frame at or
after k × 500 ms), left half white in even periods, the beep D after each
swap. The iPhone draws the halves in portrait near the bottom microphone,
about 62 mm tall, with a red outline of the board (sensor end down, notch
tick). The container fills the whole picture and draws a grey tick at both
ends of the pattern line.

### Changes from the v1 proposal

None needs a firmware change.

- The generator's sound is fixed: a 4 ms beep at 1.5 kHz (the proposal only
  said "a short sound"). Click and beep are told apart by frequency and order.
- Camera mode uses every paired pulse, the 3 start pulses too; it does not need
  the start pattern (pairing works on the click − flash difference). The
  firmware may keep it.
- Display mode does not need the swap times. The rule "D longer than the
  display delay" becomes: the beep must come 0–450 ms after the click, so
  sound minus picture must be between −100 ms and +350 ms. That also covers a
  TV chain behind the container generator.
- Constants still 0 until measured: buzzer delay (both modes) and, in display
  mode, the path difference board→microphone vs speaker→microphone. Camera
  mode subtracts 1.5 cm of sound travel.

## Open points

- The buzzer's delay from coil-on to sound must be measured once (lab: reference
  microphone + photodiode). It enters both modes as a constant.
- Whether every phone powers a passive USB-C sink without asking the user. Expected yes
  for iPhone 15 and later and most Android phones; to check on real phones.
- Sensor signal on dim screens and room light leaking into the clear sensor bodies from
  above: to check on the first boards.
- App: the screen calibration runs the generator with the audio session in
  play-and-record (to hear the microphone), the test signal in playback. Both use
  the built-in speaker and subtract the reported output latency; whether the
  real output delay differs between the two must be checked once on an iPhone
  (calibrate, then measure the test signal with a second calibrated phone).
- App: in camera mode a flash that saturates the camera makes the half-level
  edge a little early (up to half an LED pulse, 0.75 ms). The lowest ISO should
  avoid it; to check on the first boards.
