# Calibration rig

A phone's camera and microphone are not assumed to be in sync. The rig is the reference: one microcontroller pin drives an LED and a piezo at the same instant (well under 0.1 ms apart), pulsing at the event rate (default 2 Hz). The app measures that pulse, stores the capture offset per device model and OS version, and subtracts it from later measurements.

A measurement with no record, a record older than 90 days, or a record from another OS version is shown uncalibrated, in orange, with no grade colour.

## Parts

| Part | Example | Role |
| --- | --- | --- |
| Microcontroller | ESP32-S3 DevKit or Arduino Nano | one GPIO, 2 Hz pulse |
| White LED | 5 mm, with 220 Ω series resistor | the video marker the camera sees |
| Piezo disc | 3.3–12 V passive buzzer, no oscillator | the click the microphone hears |
| Supply | USB 5 V | |

The LED and the piezo share the GPIO through the resistor (LED) and a 100 Ω series resistor (piezo). Do not use an active buzzer: its oscillator delays the click.

## Checking it

An oscilloscope on the GPIO and a photodiode on the LED should agree within 0.1 ms. This VM cannot run that check.

## Firmware

`firmware/rig.ino` pulses GPIO 4 for 1 ms every 500 ms. Flash it from the Arduino IDE or `arduino-cli` on a machine with a board attached.
