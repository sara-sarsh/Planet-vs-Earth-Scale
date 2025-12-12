/*
 * Interplanetary Weight Scale
 * ---------------------------
 * Created by: Sara
 * Date: 2025
 * 
 * Description: Calculates and displays weight on different planets.
 */

#include <EEPROM.h>
#include "LedControl.h"
#include "HX711.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---------------- Pins ----------------
const int LOADCELL_DOUT_PIN = 4;
const int LOADCELL_SCK_PIN  = 5;

const int BTN_TARE_PIN   = 6;
const int BTN_PLANET_PIN = 7;

// ---------------- Devices ----------------
LedControl lc(9, 10, 11, 2);   // DIN=9, CLK=10, CS=11, 2 devices
// LedControl lc = LedControl(13, 11, 12, 2); // for moon
HX711 scale;

// ---------------- EEPROM ----------------
#define EEPROM_FLAG_ADDR     0
#define EEPROM_SCALE_ADDR    4
#define EEPROM_OFFSET_ADDR   8
#define EEPROM_PLANET_ADDR  12
#define EEPROM_TRIM_ADDR    16
#define EEPROM_MAGIC_FLAG  0xA5

// ---------------- Planets ----------------
enum Planet { EARTH, MOON, MARS, JUPITER, SATURN, SUN, PLANET_COUNT };
float planetScale[PLANET_COUNT] = { 1.00f, 0.166f, 0.38f, 2.53f, 1.06f, 27.90f };
const char* planetName[PLANET_COUNT] = { "EAR", "MON", "MAR", "JUP", "SAT", "SUN" };
Planet currentPlanet = SATURN;

// ---------------- Mode ----------------
enum Mode { MODE_RUN, MODE_CAL, MODE_TRIM };
Mode mode = MODE_RUN;

// -------- Serial command buffer ----------
char cmdLine[32];
uint8_t cmdPos = 0;

float pendingKg = NAN;       // used by CAL/TRIM via "KG x"
bool pendingKgReady = false;
bool okEvent = false;
bool tareCmdEvent = false;   // set by serial "TARE"

// ---------------- Scale params ----------------
float scaleFactor = 22800.0f;
long  tareOffset  = 0;

const float ZERO_THRESHOLD_KG = 5.0f;   // RUN only
float boardTrim = 1.0000f;

const float TRIM_TARGET_KG = 50.5f;     // default target for TRIM
const float DEADBAND_KG = 0.15f;

// ---------------- Lock params ----------------
const float LOCK_MIN_KG = 10.0f;
const float LOCK_BAND_KG = 0.25f;
const unsigned long LOCK_TIME_MS = 1500;

const float UNLOCK_DELTA_KG = 1.5f;
const unsigned long UNLOCK_TIME_MS = 700;

bool locked = false;
float lockedKg = 0.0f;
unsigned long lockStartMs = 0;
unsigned long unlockStartMs = 0;

// ---------------- Smoothing ----------------
float earthWeightDisplay = 0.0f;
float objectWeightDisplay = 0.0f;

const float ALPHA_UP   = 0.55f;   // for object display smoothing
const float ALPHA_DOWN = 0.75f;

// ---------------- Button debounce ----------------
const unsigned long DEBOUNCE_MS = 10;
bool lastTareRead = HIGH, lastPlanetRead = HIGH;
bool tareStable = HIGH, planetStable = HIGH;
unsigned long tareChangedMs = 0, planetChangedMs = 0;

bool tarePressedEvent = false;
bool planetPressedEvent = false;

unsigned long tareDownStart = 0;
unsigned long planetDownStart = 0;
const unsigned long LONGPRESS_MS = 1200;

// ---------------- Serial prompts ----------------
bool runPromptPrinted = false;
bool calPromptPrinted = false;

// ---------------- Display helpers ----------------
void myDisplayplanet(float number, int row) {
  if (number < 0) number = -number;
  lc.clearDisplay(row);

  if (number < 1000.0f) {
    int num = (int)(number * 10.0f);
    int d0 =  num        % 10;
    int d1 = (num / 10)  % 10;
    int d2 = (num / 100) % 10;
    int d3 = (num / 1000)% 10;

    lc.setDigit(row, 0, d0, false);
    lc.setDigit(row, 1, d1, true);
    if (d2 != 0) lc.setDigit(row, 2, d2, false); else lc.setRow(row, 2, 0);
    if (d3 != 0) lc.setDigit(row, 3, d3, false); else lc.setRow(row, 3, 0);
    return;
  }

  if (number < 10000.0f) {
    int num = (int)(number + 0.5f);
    lc.setDigit(row, 0,  num        % 10, false);
    lc.setDigit(row, 1, (num / 10)  % 10, false);
    lc.setDigit(row, 2, (num / 100) % 10, false);
    lc.setDigit(row, 3, (num / 1000)% 10, false);
    return;
  }

  lc.setChar(row, 0, 'H', false);
  lc.setChar(row, 1, 'I', false);
  lc.setRow(row, 2, 0);
  lc.setRow(row, 3, 0);
}

void myDisplayearth(float number, int row) {
  if (number < 0) number = -number;
  int num = (int)(number * 10);

  lc.setRow(row, 0, 0);
  lc.setRow(row, 1, 0);
  lc.setRow(row, 2, 0);
  lc.setRow(row, 3, 0);

  lc.setDigit(row, 1,  num % 10, false);
  lc.setDigit(row, 2, (num / 10) % 10, true);

  int d2 = (num / 100) % 10;
  if (d2 != 0) lc.setDigit(row, 3, d2, false);
}

// void showPlanetNameOnce() {
//   lc.clearDisplay(0);
//   lc.setChar(0, 0, planetName[currentPlanet][0], false);
//   lc.setChar(0, 1, planetName[currentPlanet][1], false);
//   lc.setChar(0, 2, planetName[currentPlanet][2], false);
//   lc.setRow(0, 3, 0);
//   delay(600);
// }
// ---- Non-blocking planet name overlay ----
bool showPlanetName = false;
unsigned long planetNameUntilMs = 0;

void startPlanetNameOverlay(unsigned long ms = 250) {
  showPlanetName = true;
  planetNameUntilMs = millis() + ms;
}

void updatePlanetNameOverlay() {
  if (!showPlanetName) return;

  if ((long)(millis() - planetNameUntilMs) >= 0) {
    showPlanetName = false;
    return;
  }

  // draw 3 letters on row 0
  lc.clearDisplay(0);
  lc.setChar(0, 0, planetName[currentPlanet][0], false);
  lc.setChar(0, 1, planetName[currentPlanet][1], false);
  lc.setChar(0, 2, planetName[currentPlanet][2], false);
  lc.setRow(0, 3, 0);
}
// ---------------- EEPROM ----------------
void saveSettings() {
  EEPROM.write(EEPROM_FLAG_ADDR, EEPROM_MAGIC_FLAG);
  EEPROM.put(EEPROM_SCALE_ADDR,  scaleFactor);
  EEPROM.put(EEPROM_OFFSET_ADDR, tareOffset);
  EEPROM.put(EEPROM_PLANET_ADDR, currentPlanet);
  EEPROM.put(EEPROM_TRIM_ADDR,   boardTrim);
}

void loadSettings() {
  if (EEPROM.read(EEPROM_FLAG_ADDR) != EEPROM_MAGIC_FLAG) return;
  EEPROM.get(EEPROM_SCALE_ADDR,  scaleFactor);
  EEPROM.get(EEPROM_OFFSET_ADDR, tareOffset);
  EEPROM.get(EEPROM_PLANET_ADDR, currentPlanet);
  EEPROM.get(EEPROM_TRIM_ADDR,   boardTrim);

  if (!isfinite(boardTrim) || boardTrim < 0.80f || boardTrim > 1.20f) boardTrim = 1.0f;
  if ((int)currentPlanet < 0 || (int)currentPlanet >= PLANET_COUNT) currentPlanet = SATURN;
}

// ---------------- Buttons ----------------
void updateButtons() {
  tarePressedEvent = false;
  planetPressedEvent = false;

  bool tareRead = digitalRead(BTN_TARE_PIN);
  bool planetRead = digitalRead(BTN_PLANET_PIN);

  if (tareRead != lastTareRead) { tareChangedMs = millis(); lastTareRead = tareRead; }
  if (millis() - tareChangedMs > DEBOUNCE_MS) {
    if (tareRead != tareStable) {
      tareStable = tareRead;
      if (tareStable == LOW) tarePressedEvent = true;
    }
  }

  if (planetRead != lastPlanetRead) { planetChangedMs = millis(); lastPlanetRead = planetRead; }
  if (millis() - planetChangedMs > DEBOUNCE_MS) {
    if (planetRead != planetStable) {
      planetStable = planetRead;
      if (planetStable == LOW) planetPressedEvent = true;
    }
  }
}

// ---------------- Prompts ----------------
void printRunPromptOnce() {
  if (runPromptPrinted) return;
  runPromptPrinted = true;
  calPromptPrinted = false;

  Serial.println();
  Serial.println(F("=== RUN MODE ==="));
  Serial.println(F("PLANET short press: next planet"));
  Serial.println(F("TARE short press  : ZERO"));
  Serial.println(F("TARE long press   : CALIBRATE (hold 1.2s)"));
  Serial.println(F("Serial: HELP"));
  Serial.println();
}

void printCalPromptOnce() {
  if (calPromptPrinted) return;
  calPromptPrinted = true;
  runPromptPrinted = false;

  Serial.println();
  Serial.println(F("=== CALIBRATION MODE ==="));
  Serial.println(F("1) EMPTY scale"));
  Serial.println(F("2) Press TARE button OR type TARE in Serial"));
  Serial.println(F("3) Put known weight"));
  Serial.println(F("4) In Serial type: KG 1.000  then Enter"));
  Serial.println(F("After saving -> auto returns to RUN"));
  Serial.println();
}

void printTrimManual() {
  Serial.println();
  Serial.println(F("=== TRIM MANUAL ==="));
  Serial.println(F("1) Enter TRIM mode (long press PLANET)"));
  Serial.println(F("2) Stand still on the scale"));
  Serial.println(F("3) In Serial Monitor type ONE of these:"));
  Serial.println(F("   OK"));
  Serial.println(F("     -> uses default target (TRIM_TARGET_KG)"));
  Serial.println(F("   KG 50.5   then   OK"));
  Serial.println(F("     -> uses your value and saves"));
  Serial.println(F("Other commands: RUN / CAL / TRIM / SAVE / LOAD / SF x / OFF x / BT x"));
  Serial.println();
}

// ---------------- Serial command reader ----------------
static bool ieq(const char* a, const char* b) { return strcasecmp(a,b) == 0; }
static bool istarts(const char* a, const char* pfx) { return strncasecmp(a,pfx,strlen(pfx)) == 0; }

void serialPoll() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      cmdLine[cmdPos] = 0;
      cmdPos = 0;

      // trim leading spaces
      char *p = cmdLine;
      while (*p == ' ') p++;
      if (*p == 0) return;

      okEvent = false;

      if (ieq(p, "OK")) {
        okEvent = true;
        Serial.println(F("[SER] OK"));
        return;
      }
      if (ieq(p, "HELP") || ieq(p, "TRIM?") || ieq(p, "TRIMHELP")) {
        printTrimManual();
        return;
      }
      if (ieq(p, "TARE")) {
        tareCmdEvent = true;
        Serial.println(F("[SER] TARE"));
        return;
      }
      if (ieq(p, "RUN"))  { mode = MODE_RUN;  Serial.println(F("[SER] mode=RUN"));  return; }
      if (ieq(p, "CAL"))  { mode = MODE_CAL;  Serial.println(F("[SER] mode=CAL"));  return; }
      if (ieq(p, "TRIM")) { mode = MODE_TRIM; Serial.println(F("[SER] mode=TRIM")); return; }

      if (istarts(p, "KG "))  { pendingKg = atof(p + 3); pendingKgReady = true; Serial.print(F("[SER] KG=")); Serial.println(pendingKg, 3); return; }
      if (istarts(p, "SF "))  { scaleFactor = atof(p + 3); scale.set_scale(scaleFactor); Serial.print(F("[SER] scaleFactor set=")); Serial.println(scaleFactor, 4); return; }
      if (istarts(p, "OFF ")) { tareOffset = atol(p + 4); scale.set_offset(tareOffset); Serial.print(F("[SER] tareOffset set=")); Serial.println(tareOffset); return; }
      if (istarts(p, "BT "))  {
        boardTrim = atof(p + 3);
        if (!isfinite(boardTrim) || boardTrim < 0.80f || boardTrim > 1.20f) boardTrim = 1.0f;
        Serial.print(F("[SER] boardTrim set=")); Serial.println(boardTrim, 4);
        return;
      }

      if (ieq(p, "SAVE")) { saveSettings(); Serial.println(F("[SER] saved EEPROM")); return; }
      if (ieq(p, "LOAD")) {
        loadSettings();
        scale.set_scale(scaleFactor);
        scale.set_offset(tareOffset);
        Serial.print(F("[SER] loaded: SF=")); Serial.print(scaleFactor, 4);
        Serial.print(F(" OFF=")); Serial.print(tareOffset);
        Serial.print(F(" BT=")); Serial.println(boardTrim, 4);
        return;
      }

      Serial.println(F("[SER] Unknown command. Try: HELP / RUN / CAL / TRIM / KG 50.5 / OK"));
      return;
    }

    if (cmdPos < sizeof(cmdLine) - 1) cmdLine[cmdPos++] = c;
  }
}

// ---------------- Better smoothing (fix slow last <1kg + slow step-down) ----------------
void updateEarthDisplayToward(float targetKg) {
  float diff = targetKg - earthWeightDisplay;
  float ad = fabs(diff);

  // force fast zero when stepping off
  if (targetKg == 0.0f && earthWeightDisplay < 1.0f) {
    earthWeightDisplay = 0.0f;
    return;
  }

  float k;
  if (ad > 5.0f)      k = 0.65f;
  else if (ad > 1.0f) k = 0.35f;
  else                k = 0.25f;   // <-- faster near the end (was 0.15)

  // speed up going DOWN (step off)
  if (diff < 0) {
    k *= 1.8f;              // faster decay
    if (k > 0.90f) k = 0.90f;
  }

  earthWeightDisplay += diff * k;

  // snap when extremely close (fix last digits crawling)
  if (fabs(targetKg - earthWeightDisplay) < 0.05f) {
    earthWeightDisplay = targetKg;
  }
}

// ---------------- Calibration ----------------
void calibrationLoop() {
  printCalPromptOnce();

  lc.setChar(0, 0, 'C', false);
  lc.setChar(0, 1, 'A', false);
  lc.setChar(0, 2, 'L', false);
  lc.setRow(0, 3, 0);

  static bool waitingKg = false;

  if (!waitingKg) {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 700) {
      lastPrint = millis();
      long r = scale.read_average(20);
      Serial.print(F("[CAL] RAW_COUNTS="));
      Serial.println(r);
      myDisplayearth((float)(r % 10000) / 10.0f, 1);
    }

    if (tarePressedEvent || tareCmdEvent) {
      tareCmdEvent = false;   // consume serial event
      Serial.println(F("[CAL] Capturing EMPTY offset..."));
      tareOffset = scale.read_average(25);
      Serial.print(F("[CAL] tareOffset="));
      Serial.println(tareOffset);
      Serial.println(F("[CAL] Now type: KG 1.000  then Enter"));
      waitingKg = true;
    }
    return;
  }

  // Use your "KG x" command instead of Serial.parseFloat()
  if (pendingKgReady) {
    float realKg = pendingKg;
    pendingKgReady = false;

    Serial.print(F("[CAL] realKg=")); Serial.println(realKg, 3);
    if (realKg <= 0.0f) {
      Serial.println(F("[CAL][ERROR] realKg <= 0. Type again."));
      return;
    }

    long withW = scale.read_average(25);
    long net = withW - tareOffset;

    Serial.print(F("[CAL] withWeight=")); Serial.println(withW);
    Serial.print(F("[CAL] netCounts="));  Serial.println(net);

    if (labs(net) < 1000) {
      Serial.println(F("[CAL][ERROR] netCounts too small. Try again."));
      return;
    }

    scaleFactor = (float)net / realKg;
    Serial.print(F("[CAL] scaleFactor=")); Serial.println(scaleFactor, 4);

    scale.set_scale(scaleFactor);
    scale.set_offset(tareOffset);
    saveSettings();

    Serial.println(F("[CAL] Saved. Switching to RUN."));
    waitingKg = false;
    mode = MODE_RUN;
    while (digitalRead(BTN_TARE_PIN) == LOW) { delay(10); }
  }
}

// ---------------- TRIM ----------------
void trimLoop() {
  static bool promptPrinted = false;
  if (!promptPrinted) {
    promptPrinted = true;
    Serial.println();
    Serial.println(F("=== TRIM MODE ==="));
    printTrimManual();
    Serial.println(F("Tip: send KG <value> then OK (no need to press buttons)."));
  }

  lc.clearDisplay(0);
  lc.setChar(0, 0, 'T', false);
  lc.setChar(0, 1, 'R', false);
  lc.setChar(0, 2, 'I', false);
  lc.setRow(0, 3, 0);

  float w = fabs(scale.get_units(10));
  myDisplayearth(w, 1);

  if (planetPressedEvent) {
    Serial.println(F("[TRIM] Cancel -> RUN"));
    promptPrinted = false;
    mode = MODE_RUN;
    while (digitalRead(BTN_PLANET_PIN) == LOW) { delay(10); }
    return;
  }

  // Confirm with OK (target is either default or last "KG x")
  if (okEvent) {
    okEvent = false;

    float target = TRIM_TARGET_KG;
    if (pendingKgReady) { target = pendingKg; pendingKgReady = false; }

    if (w < 5.0f) {
      Serial.println(F("[TRIM][ERROR] Reading too small. Stand still and try again."));
      return;
    }

    boardTrim = target / w;
    if (boardTrim < 0.80f) boardTrim = 0.80f;
    if (boardTrim > 1.20f) boardTrim = 1.20f;

    Serial.print(F("[TRIM] measured=")); Serial.print(w, 2);
    Serial.print(F(" target="));        Serial.print(target, 2);
    Serial.print(F(" -> boardTrim="));  Serial.println(boardTrim, 4);

    saveSettings();
    Serial.println(F("[TRIM] Saved. Back to RUN."));
    promptPrinted = false;
    mode = MODE_RUN;
  }

  delay(80);
}

// ---------------- RUN ----------------
void runLoop() {
  printRunPromptOnce();
  updatePlanetNameOverlay();
  // --- PLANET long press -> TRIM ---
  if (planetPressedEvent) planetDownStart = millis();

  if (planetStable == LOW && planetDownStart > 0 && (millis() - planetDownStart) >= LONGPRESS_MS) {
    Serial.println(F("[RUN] PLANET long press -> TRIM"));
    planetDownStart = 0;
    mode = MODE_TRIM;
    while (digitalRead(BTN_PLANET_PIN) == LOW) { delay(10); }
    return;
  }

  // PLANET short press cycle
  if (planetPressedEvent) {
    currentPlanet = (Planet)((currentPlanet + 1) % PLANET_COUNT);
    EEPROM.put(EEPROM_PLANET_ADDR, currentPlanet);
    Serial.print(F("[RUN] Planet -> "));
    Serial.println(planetName[currentPlanet]);
    // showPlanetNameOnce();
    startPlanetNameOverlay(250); // 250ms is enough and doesn't block
  }

  // TARE press handling
  if (tarePressedEvent) tareDownStart = millis();

  // long press -> CAL
  if (tareStable == LOW && tareDownStart > 0 && (millis() - tareDownStart) >= LONGPRESS_MS) {
    Serial.println(F("[RUN] TARE long press -> CAL"));
    tareDownStart = 0;
    mode = MODE_CAL;
    return;
  }

  // short press -> ZERO
  if (tareStable == HIGH && tareDownStart > 0) {
    unsigned long dt = millis() - tareDownStart;
    tareDownStart = 0;
    if (dt < LONGPRESS_MS) {
      Serial.println(F("[RUN] TARE short press -> ZERO"));
      scale.tare();
      tareOffset = scale.get_offset();
      EEPROM.put(EEPROM_OFFSET_ADDR, tareOffset);
    }
  }

  // read weights
  float earthWeight = scale.get_units(5) * boardTrim;

  // RUN-only threshold -> force zero + fast drop
  if (fabs(earthWeight) < ZERO_THRESHOLD_KG) {
    earthWeight = 0.0f;
    locked = false;
    lockStartMs = 0;
    unlockStartMs = 0;
  }

  updateEarthDisplayToward(earthWeight);

  float smoothKg = earthWeightDisplay;

  // lock logic
  if (smoothKg < LOCK_MIN_KG) {
    locked = false;
    lockStartMs = 0;
    unlockStartMs = 0;
  } else {
    if (!locked) {
      if (fabs(earthWeight - smoothKg) < LOCK_BAND_KG) {
        if (lockStartMs == 0) lockStartMs = millis();
        if (millis() - lockStartMs >= LOCK_TIME_MS) {
          locked = true;
          lockedKg = smoothKg;
          unlockStartMs = 0;
          Serial.print(F("[RUN] LOCK "));
          Serial.println(lockedKg, 2);
        }
      } else {
        lockStartMs = 0;
      }
    } else {
      // unlock faster if target drops near zero
      if (earthWeight == 0.0f) {
        locked = false;
        lockStartMs = 0;
        unlockStartMs = 0;
        Serial.println(F("[RUN] UNLOCK"));
      } else if (fabs(smoothKg - lockedKg) > UNLOCK_DELTA_KG) {
        if (unlockStartMs == 0) unlockStartMs = millis();
        if (millis() - unlockStartMs >= UNLOCK_TIME_MS) {
          locked = false;
          lockStartMs = 0;
          unlockStartMs = 0;
          Serial.println(F("[RUN] UNLOCK"));
        }
      } else {
        unlockStartMs = 0;
      }
    }
  }

  float showEarth = locked ? lockedKg : earthWeightDisplay;
  float showObj   = showEarth * planetScale[currentPlanet];

  // object smoothing
  float alpha = (showObj > objectWeightDisplay) ? ALPHA_UP : ALPHA_DOWN;
  objectWeightDisplay += alpha * (showObj - objectWeightDisplay);

  myDisplayearth(showEarth, 1);
  myDisplayplanet(objectWeightDisplay, 0);

  delay(20);
}

// ---------------- Setup / Loop ----------------
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(300);

  pinMode(BTN_TARE_PIN, INPUT_PULLUP);
  pinMode(BTN_PLANET_PIN, INPUT_PULLUP);

  for (int address = 0; address < 2; address++) {
    lc.shutdown(address, false);
    lc.setIntensity(address, 8);
    lc.clearDisplay(address);
  }

  // startup animation
  for (int i = 0; i < 4; i++) {
    lc.setChar(0, i, '-', false);
    lc.setChar(1, i, '-', false);
    delay(250);
  }

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  loadSettings();
  scale.set_scale(scaleFactor);
  scale.set_offset(tareOffset);

  Serial.println(F("BOOT"));
  Serial.print(F("scaleFactor=")); Serial.println(scaleFactor, 4);
  Serial.print(F("tareOffset="));  Serial.println(tareOffset);
  Serial.print(F("boardTrim="));   Serial.println(boardTrim, 4);
  Serial.print(F("planet="));      Serial.println(planetName[currentPlanet]);
  Serial.println(F("Type HELP for TRIM instructions."));

  // showPlanetNameOnce();
}

void loop() {
  serialPoll();
  updateButtons();

  if (mode == MODE_CAL) calibrationLoop();
  else if (mode == MODE_TRIM) trimLoop();
  else runLoop();
}