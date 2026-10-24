# Calibration

Stage 1 does not include the phone app. The core is dependency-free so a later native module can link it.

The phone measures only AV offset (`reference.type = none`). Its clock is not a latency reference.

The calibration rig (stage 3, M-P1) is an LED and a piezo driven from one microcontroller pin. It is not built in this repository yet. Uncalibrated phone measurements, when that app exists, are shown without a grade colour.
