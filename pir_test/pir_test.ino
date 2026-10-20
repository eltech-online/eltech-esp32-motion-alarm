// ElTech-Online ESP32 Motion Alarm — PIR sensor test
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// The smallest possible sketch for the HC-SR501 motion sensor: the board's own
// blue LED lights while the sensor sees movement, and Serial Monitor prints
// each change. No buzzer, no relay, no WiFi. Use it to check the sensor is
// wired correctly and to set its two orange adjusters.
//
// Wiring (3 wires):
//   sensor VCC -> ESP32-C3 5V   (the sensor needs 5 V; its OUT pin is a safe 3.3 V)
//   sensor OUT -> ESP32-C3 GPIO 4
//   sensor GND -> ESP32-C3 GND
//
// Good to know:
//   - The sensor needs about a minute after power-on before it is reliable.
//   - After each detection, OUT stays HIGH for a while (the "Tx" adjuster sets
//     how long, from about 3 seconds to 5 minutes). Turn it fully
//     anticlockwise for the shortest time while you are testing.
//   - The "Sx" adjuster sets how far the sensor sees (about 3 to 7 metres).

#define PIR_PIN 4   // the pin the sensor's OUT wire is on
#define LED_PIN 8   // the blue LED already on the ESP32-C3 board

// setup() runs once, when the board is powered on or reset.
void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);    // INPUT = the board listens on this pin
  pinMode(LED_PIN, OUTPUT);   // OUTPUT = the board drives this pin
  Serial.println("PIR sensor test - wait a minute, then wave a hand");
}

// loop() runs over and over, forever.
void loop() {
  // digitalRead() gives HIGH (3.3 V on the pin) or LOW (0 V).
  bool motion = digitalRead(PIR_PIN) == HIGH;

  // The on-board LED is "active low": it lights when its pin is LOW.
  digitalWrite(LED_PIN, motion ? LOW : HIGH);

  // Print only when something changes, not thousands of times a second.
  // ("static" makes lastMotion keep its value between runs of loop().)
  static bool lastMotion = false;
  if (motion != lastMotion) {
    lastMotion = motion;
    Serial.println(motion ? "Motion!" : "Quiet again");
  }
}
