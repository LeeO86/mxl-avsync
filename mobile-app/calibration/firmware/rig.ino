// mxl-avsync calibration rig. One pin drives the LED and the piezo together.
// Pulse width 1 ms, period 500 ms (2 events/s). The electrical edge is the reference.

const int kPin = 4;
const unsigned long kPeriodMs = 500;
const unsigned long kPulseMs = 1;

void setup() {
  pinMode(kPin, OUTPUT);
  digitalWrite(kPin, LOW);
}

void loop() {
  digitalWrite(kPin, HIGH);
  delay(kPulseMs);
  digitalWrite(kPin, LOW);
  delay(kPeriodMs - kPulseMs);
}
