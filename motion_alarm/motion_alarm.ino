// ElTech-Online ESP32 Motion Alarm — a PIR motion sensor that sounds a buzzer
// and switches a relay, armed and disarmed from a web page on your phone.
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// Three parts, two techniques:
//   - HC-SR501 PIR sensor -> a DIGITAL INPUT: one wire that is HIGH or LOW
//   - Buzzer and relay    -> DIGITAL OUTPUTS that SWITCH A LOAD the board
//                            could never drive on its own
// And the board serves its own WEB PAGE: arm/disarm, an event log and a manual
// relay switch.
//
// SAFETY: use the relay for LOW-VOLTAGE circuits only (battery or USB powered
// things, up to 24 V). Do not connect mains electricity to this kit.
//
// Libraries needed: none to install. WiFi and WebServer are built into the
// ESP32 board package.
// Board package: esp32 by Espressif Systems
//
// How to use:
//   1. Flash this sketch. The buzzer beeps twice and the relay clicks once.
//   2. Serial Monitor shows the board's WiFi name and password. Join that
//      network with your phone and open http://192.168.4.1
//   3. Wait for the sensor to warm up (about a minute), then press Arm.
// Full source, wiring diagram and setup guide: github.com/eltech-online/eltech-esp32-motion-alarm
//
// ---------------------------------------------------------------------------
// New to Arduino code? How to read this file
// ---------------------------------------------------------------------------
// Lines starting with // are comments: notes for people, ignored by the board.
// The file is in this order, and you can read it top to bottom:
//   1. Settings       - pin numbers and times you can safely change
//   2. Outputs        - switching the buzzer, the relay and the LED
//   3. Event log      - remembering the last 20 things that happened
//   4. Alarm states   - the "state machine" that decides what the alarm does
//   5. Web server     - what the board sends to, and accepts from, your phone
//   6. setup()        - runs ONCE when the board is powered on
//   7. loop()         - then runs over and over, forever
// A good first experiment: change EXIT_DELAY_SECONDS below and upload.

#include <WebServer.h>
#include <Preferences.h>   // saves the settings in flash

// ---- WiFi Access Point settings ----
// Leave both empty ("") and every board gets its OWN network name (e.g.
// "ElTech-MA-A3F2") and its OWN random 8-character password, saved in flash.
// Or type your own: name up to 32 characters, password 8-63 characters.
const char* AP_SSID     = "";
const char* AP_PASSWORD = "";
const bool  AP_OPEN_NETWORK = false;  // true = no password at all
#define KIT_SSID_PREFIX "ElTech-MA-"
#define KIT_PREFS       "alarm"       // name of this kit's flash "notebook"
#include "eltech_wifi.h"     // starts the WiFi network (second tab in the IDE)
#include "page_template.h"   // the web page's HTML (third tab)

// ---- Pins ----
#define PIR_PIN    4   // PIR sensor OUT
#define RELAY_PIN  5   // relay module IN1
#define BUZZER_PIN 6   // buzzer module I/O
#define LED_PIN    8   // the blue LED already on the ESP32-C3 board

// ---- Which way round each part switches ----
// "Active low" means the part turns ON when its pin is LOW (0 V) and OFF when
// it is HIGH. It sounds backwards, but it is very common. Our buzzer module is
// printed "Low level trigger", and the on-board LED is wired the same way.
// If your relay clicks ON when it should be OFF, change its line to false.
const bool BUZZER_ACTIVE_LOW = true;
const bool RELAY_ACTIVE_LOW  = true;
const bool LED_ACTIVE_LOW    = true;

// ---- Times ----
const int WARMUP_SECONDS     = 60;  // the PIR sensor gives false alarms for its first minute
const int EXIT_DELAY_SECONDS = 10;  // time to leave the room after pressing Arm
const int DEFAULT_ALARM_SECONDS = 20;   // how long the alarm sounds (changeable on the web page)

WebServer server(80);

// Settings you can change from the web page. They are saved in flash.
int alarmSeconds = DEFAULT_ALARM_SECONDS;
bool buzzerEnabled = true;       // false = silent alarm (log and relay only)
bool relayOnAlarm = true;        // false = the alarm leaves the relay alone

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------
// One small function per part, so the rest of the code can say
// setBuzzer(true) without caring whether "on" is HIGH or LOW for that part.
bool relayIsOn = false;

void setBuzzer(bool on) { digitalWrite(BUZZER_PIN, (on != BUZZER_ACTIVE_LOW) ? HIGH : LOW); }
void setLed(bool on)    { digitalWrite(LED_PIN,    (on != LED_ACTIVE_LOW)    ? HIGH : LOW); }
void setRelay(bool on) {
  relayIsOn = on;
  digitalWrite(RELAY_PIN, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// Event log
// ---------------------------------------------------------------------------
// The last 20 events, newest overwriting oldest. This shape is called a "ring
// buffer": logNext walks round and round the array.
// The board has no clock, so each event stores millis() (milliseconds since
// power-on) and the web page shows it as "3 min ago". The log is kept in RAM,
// so it is empty again after a power cut.
const int LOG_SIZE = 20;
String logText[LOG_SIZE];
unsigned long logTime[LOG_SIZE];
int logNext = 0;     // where the next event goes
int logCount = 0;    // how many events are stored (up to LOG_SIZE)

void addEvent(const String& text) {
  logText[logNext] = text;
  logTime[logNext] = millis();
  logNext = (logNext + 1) % LOG_SIZE;       // % makes 20 wrap round to 0
  if (logCount < LOG_SIZE) logCount++;
  Serial.println("Event: " + text);
}

// ---------------------------------------------------------------------------
// Alarm states
// ---------------------------------------------------------------------------
// The alarm is always in exactly ONE of these states, and each state has its
// own simple rules. Writing a program this way is called a "state machine",
// and it stops the code turning into a tangle of if-this-but-not-that.
//
//   WARMUP   --(60 s)-->  DISARMED  --(Arm pressed)-->  EXIT_DELAY
//   EXIT_DELAY --(10 s)--> ARMED --(motion)--> ALARM --(20 s)--> ARMED
//   any state --(Disarm pressed)--> DISARMED
const int WARMUP = 0, DISARMED = 1, EXIT_DELAY = 2, ARMED = 3, ALARM = 4;
const char* STATE_NAMES[] = { "WARMING UP", "DISARMED", "EXIT DELAY", "ARMED", "ALARM" };

int state = WARMUP;
unsigned long stateSince = 0;   // millis() when we entered this state
bool motionNow = false;         // what the PIR sensor says right now
int alarmCount = 0;             // alarms since power-on

// Every change of state goes through here, so the "on entering a state" work
// is all in one place.
void enterState(int newState) {
  state = newState;
  stateSince = millis();
  setBuzzer(false);
  if (newState == DISARMED)   addEvent("Disarmed");
  if (newState == EXIT_DELAY) addEvent("Arming, leave the room");
  if (newState == ARMED)      addEvent("Armed");
  if (newState == ALARM) {
    alarmCount++;
    addEvent("MOTION - alarm");
    if (relayOnAlarm) setRelay(true);
  }
}

// How many whole seconds we have been in the current state.
unsigned long secondsInState() {
  return (millis() - stateSince) / 1000;
}

// Runs the rules of the current state. Called from loop() many times a second.
void runAlarm() {
  motionNow = digitalRead(PIR_PIN) == HIGH;
  unsigned long ms = millis();

  if (state == WARMUP) {
    setLed((ms / 500) % 2 == 0);                       // slow blink
    if (secondsInState() >= (unsigned long)WARMUP_SECONDS) enterState(DISARMED);

  } else if (state == DISARMED) {
    setLed(false);

  } else if (state == EXIT_DELAY) {
    setLed((ms / 150) % 2 == 0);                       // fast blink
    if (buzzerEnabled) setBuzzer(ms % 1000 < 40);      // a short tick each second
    if (secondsInState() >= (unsigned long)EXIT_DELAY_SECONDS) enterState(ARMED);

  } else if (state == ARMED) {
    setLed(true);                                      // steady = armed
    if (motionNow) enterState(ALARM);

  } else if (state == ALARM) {
    setLed((ms / 100) % 2 == 0);
    if (buzzerEnabled) setBuzzer((ms / 250) % 2 == 0); // beep-beep-beep
    if (secondsInState() >= (unsigned long)alarmSeconds) {
      if (relayOnAlarm) setRelay(false);
      enterState(ARMED);                               // ready for the next one
    }
  }
}

// ---------------------------------------------------------------------------
// Settings in flash
// ---------------------------------------------------------------------------
void loadSettings() {
  Preferences prefs;
  prefs.begin(KIT_PREFS, true);                 // true = read only
  alarmSeconds  = constrain(prefs.getInt("alarm_s", DEFAULT_ALARM_SECONDS), 2, 600);
  buzzerEnabled = prefs.getBool("buzzer", true);
  relayOnAlarm  = prefs.getBool("relay", true);
  prefs.end();
}

void saveSettings() {
  Preferences prefs;
  prefs.begin(KIT_PREFS, false);
  prefs.putInt("alarm_s", alarmSeconds);
  prefs.putBool("buzzer", buzzerEnabled);
  prefs.putBool("relay", relayOnAlarm);
  prefs.end();
}

// ---------------------------------------------------------------------------
// Web server
// ---------------------------------------------------------------------------
// A browser asks the board for an address, and the matching function below
// sends the answer. setup() connects each address to its function.

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "text/html", PAGE_TEMPLATE);
}

void handleStyle() {
  server.send(200, "text/css", STYLE_CSS);
}

// "/state" -> everything about the alarm as JSON, a simple text format
// programs can read. The page asks for it once a second.
void handleState() {
  String json = "{";
  json += "\"state\":\"" + String(STATE_NAMES[state]) + "\",";
  json += "\"armed\":" + String(state >= EXIT_DELAY ? "true" : "false") + ",";
  json += "\"motion\":" + String(motionNow ? "true" : "false") + ",";
  json += "\"relay\":" + String(relayIsOn ? "true" : "false") + ",";
  json += "\"alarms\":" + String(alarmCount) + ",";
  json += "\"alarm_s\":" + String(alarmSeconds) + ",";
  json += "\"buzzer\":" + String(buzzerEnabled ? "true" : "false") + ",";
  json += "\"relay_on_alarm\":" + String(relayOnAlarm ? "true" : "false") + ",";

  // Seconds left in a timed state, for the countdown on the page.
  long left = 0;
  if (state == WARMUP)     left = WARMUP_SECONDS - (long)secondsInState();
  if (state == EXIT_DELAY) left = EXIT_DELAY_SECONDS - (long)secondsInState();
  if (state == ALARM)      left = alarmSeconds - (long)secondsInState();
  json += "\"left\":" + String(max(left, 0L)) + ",";

  // The log, newest first, as [seconds ago, "text"] pairs.
  json += "\"log\":[";
  for (int i = 0; i < logCount; i++) {
    int index = (logNext - 1 - i + LOG_SIZE) % LOG_SIZE;    // walk backwards round the ring
    json += "[" + String((millis() - logTime[index]) / 1000) + ",\"" + logText[index] + "\"]";
    if (i < logCount - 1) json += ",";
  }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", json);
}

// "/arm?on=1" arms, "/arm?on=0" disarms.
void handleArm() {
  bool wantArmed = server.arg("on") == "1";
  if (!wantArmed) {
    if (state == ALARM && relayOnAlarm) setRelay(false);
    if (state != WARMUP) enterState(DISARMED);
  } else if (state == DISARMED) {
    enterState(EXIT_DELAY);
  }
  handleState();
}

// "/relay?on=1" switches the relay by hand. Refused during an alarm that is
// using the relay, so the two can't fight over it.
void handleRelay() {
  if (!(state == ALARM && relayOnAlarm)) {
    setRelay(server.arg("on") == "1");
    addEvent(relayIsOn ? "Relay switched on by hand" : "Relay switched off by hand");
  }
  handleState();
}

// "/settings?alarm_s=30&buzzer=1&relay=0" changes and saves the settings.
void handleSettings() {
  if (server.hasArg("alarm_s")) alarmSeconds = constrain(server.arg("alarm_s").toInt(), 2, 600);
  if (server.hasArg("buzzer"))  buzzerEnabled = server.arg("buzzer") == "1";
  if (server.hasArg("relay"))   relayOnAlarm = server.arg("relay") == "1";
  saveSettings();
  handleState();
}

// ---------------------------------------------------------------------------
// setup() runs once at power-on, loop() then runs forever
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Set each output to its OFF level BEFORE making the pin an output, so the
  // relay and buzzer don't switch on for an instant at power-on.
  setBuzzer(false);
  setRelay(false);
  setLed(false);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  loadSettings();

  // These parts can't tell the board they are there, so the test is one you
  // check by ear: two beeps from the buzzer, one click from the relay.
  for (int i = 0; i < 2; i++) {
    setBuzzer(true);  delay(80);
    setBuzzer(false); delay(120);
  }
  setRelay(true);  delay(300);
  setRelay(false);

  wifiOK = startAccessPoint();

  Serial.println("========================================");
  Serial.println("           ElTech-Online");
  Serial.println("     ESP32 Motion Alarm (BETA)");
  Serial.println("========================================");
  Serial.println("--- Self-test ---");
  Serial.println("Buzzer:        beeped twice (check by ear)");
  Serial.println("Relay:         clicked on and off (check by ear)");
  Serial.println("PIR sensor:    warming up, wave a hand after 60 s and watch for \"Motion\"");
  Serial.print("WiFi AP:       ");
  if (wifiOK) { Serial.println("OK"); } else { Serial.print("FAILED ("); Serial.print(wifiError); Serial.println(")"); }
  Serial.print("RESULT:        "); Serial.println(wifiOK ? "PASS" : "FAIL");
  printWifiDetails();

  server.on("/", handleRoot);
  server.on("/style.css", handleStyle);
  server.on("/state", handleState);
  server.on("/arm", HTTP_POST, handleArm);
  server.on("/relay", HTTP_POST, handleRelay);
  server.on("/settings", HTTP_POST, handleSettings);
  server.begin();

  addEvent("Power on, sensor warming up");
  state = WARMUP;
  stateSince = millis();
}

void loop() {
  server.handleClient();   // answer any browser that's waiting
  runAlarm();

  // Print the sensor to Serial Monitor whenever it changes, so you can test it
  // without arming anything. ("static" makes lastMotion keep its value
  // between one run of loop() and the next.)
  static bool lastMotion = false;
  if (motionNow != lastMotion) {
    lastMotion = motionNow;
    Serial.println(motionNow ? "Motion" : "No motion");
  }
}
