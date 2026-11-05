// MACROPAD - ESP32-S3

// HARDWARE:
//   Joystick X/Y : IO1 / IO2        Joystick BTN: IO48
//   Mode Button  : IO13
//   Key Matrix   : Rows IO9-IO12    Cols IO15,IO7,IO6,IO5
//   Poti Vol     : IO4              Poti Helligkeit: IO14
//   TFT Display  : SCK=IO16 MOSI=IO17 RST=IO18 DC=IO8 CS=IO3


#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <USBHIDConsumerControl.h>

// Fehlende Consumer-Konstanten (je nach arduino-esp32 Version)
#ifndef CONSUMER_CONTROL_SLEEP
#define CONSUMER_CONTROL_SLEEP                0x0082
#endif
#ifndef CONSUMER_CONTROL_VOLUME_INCREMENT
#define CONSUMER_CONTROL_VOLUME_INCREMENT     0x00E9
#define CONSUMER_CONTROL_VOLUME_DECREMENT     0x00EA
#define CONSUMER_CONTROL_MUTE                 0x00E2
#endif
#ifndef CONSUMER_CONTROL_BRIGHTNESS_INCREMENT
#define CONSUMER_CONTROL_BRIGHTNESS_INCREMENT 0x006F
#define CONSUMER_CONTROL_BRIGHTNESS_DECREMENT 0x0070
#endif
#ifndef CONSUMER_CONTROL_BROWSER_HOME
#define CONSUMER_CONTROL_BROWSER_HOME    0x0223
#endif
#ifndef CONSUMER_CONTROL_BROWSER_BACK
#define CONSUMER_CONTROL_BROWSER_BACK    0x0224
#endif
#ifndef CONSUMER_CONTROL_BROWSER_FORWARD
#define CONSUMER_CONTROL_BROWSER_FORWARD 0x0225
#endif
#ifndef CONSUMER_CONTROL_BROWSER_REFRESH
#define CONSUMER_CONTROL_BROWSER_REFRESH 0x0227
#endif

// PINS
#define TFT_CS    3
#define TFT_DC    8
#define TFT_MOSI  17
#define TFT_SCLK  16
#define TFT_RST   18

#define PIN_JOY_X     1
#define PIN_JOY_Y     2
#define PIN_JOY_BTN  48
#define PIN_MODE_BTN 13
#define PIN_VOL_POTI  4
#define PIN_BRI_POTI 14

const int ROW_PINS[4] = {9, 10, 11, 12};
const int COL_PINS[4] = {15, 7, 6, 5};


// EINSTELLUNGEN
#define DEBOUNCE_MS       50

// --- Potentiometer (10k Ohm an 3.3V) ---
#define POT_ADC_MIN      150    
#define POT_ADC_MAX     3900    
#define POT_SAMPLES       16    
#define POT_HYSTERESIS     2    
#define VOL_MAX_STEPS     50    
#define BRI_MAX_STEPS    100    
#define POT_SEND_PER_TICK  3    
#define POT_POLL_MS       40    

// --- Joystick (10k Ohm, analog) ---
#define JOY_SAMPLES        8    
#define JOY_CALIB_N       64    
#define JOY_DEADZONE     180    
#define JOY_MAX_SPEED     10    
#define JOY_UPDATE_MS     12    
#define JOY_BTN_DEBOUNCE  50    
#define JOY_INVERT_X       1
#define JOY_INVERT_Y       1
#define JOY_SWAP_XY        0    


// FARBEN (RGB565)
#define COL_NAVY      0x000F
#define COL_DARKGRAY  0x4208
#define COL_MIDGRAY   0x8410
#define COL_ORANGE    0xFC00


// MODI (3-Buchstaben-Abkürzungen)
enum Mode { GENERAL = 0, EASYEDA = 1, CHILL = 2, GAMING = 3 };
const char*    MODE_NAMES[]  = {"GEN", "EDA", "CHL", "GAM"};
const uint16_t MODE_COLORS[] = {ST77XX_CYAN, ST77XX_GREEN, ST77XX_MAGENTA, COL_ORANGE};


// TASTENBESCHRIFTUNGEN (Landscape 160x128, max 5 Zeichen)
const char* KEY_LABELS[4][4][4] = { 
  // ---- GENERAL ----
  {{"FFOX", "EXPLR", "SCRSH", "LOCK "},
  {"UNDO",  "REDO",  "COPY",  "PASTE"},
  {"SAVE",  "SEL A", "FIND",  "CL TB"},
  {"NW TB", "RP TB", "AF4",   "DESK "}},
  // ---- EASYEDA ----
  {{"WIRE",  "PLACE", "ROT",   "FLIP "},
  {"UNDO",  "REDO",  "COPY",  "PASTE"},
  {"SAVE",  "DEL",   "SEL A", "ESC  "},
  {"GND",   "PWR",   "NET",   "BUS  "}},
  // ---- CHILL ----
  {{"PLAY",  "NEXT",  "PREV",  "STOP "},
  {"MUTE",  "VOL+",  "VOL-",  "SCRSH"},
  {"LOCK",  "CALC",  "HOME",  "BACK "},
  {"FWD",   "RFRSH", "B.HM",  "SLEEP"}},
  // ---- GAMING ----
  {{"SL 1",  "SL 2",  "SL 3",  "SL 4 "},
  {"Q",     "E",     "R",     "F    "},
  {"TAB",   "MAP",   "ESC",   "SCRSH"},
  {"PTT",   "CROUC", "SPRNT", "PRONE"}}
};

// HID + DISPLAY OBJEKTE
USBHIDKeyboard        Keyboard;
USBHIDMouse           Mouse;
USBHIDConsumerControl Consumer;
Adafruit_ST7735       tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);


// ZUSTAND
int          currentMode        = GENERAL;
bool         keyState[4][4]     = {};
unsigned long keyDebounce[4][4] = {};
String       lastAction         = "Ready";

int  joyCenter_X  = 2048, joyCenter_Y = 2048;
bool joyBtnLast   = false;
unsigned long joyBtnTime = 0;

int sentVol = -1;   
int sentBri = -1;

// HID HILFSFUNKTIONEN
void sendConsumerStep(uint16_t code) {
  Consumer.press(code);
  Consumer.release();
  delay(10);
}

int readPoti(int pin) {
  analogRead(pin);  
  long sum = 0;
  for (int i = 0; i < POT_SAMPLES; i++) sum += analogRead(pin);
  return (int)(sum / POT_SAMPLES);  
}

int potiToSteps(int pin, int maxSteps) {
  return constrain(map(readPoti(pin), POT_ADC_MIN, POT_ADC_MAX, 0, maxSteps), 0, maxSteps);
}

int potiToPercent(int pin) {
  return constrain(map(readPoti(pin), POT_ADC_MIN, POT_ADC_MAX, 0, 100), 0, 100);
}

int readJoyAxis(int pin) {
  analogRead(pin);  
  long sum = 0;
  for (int i = 0; i < JOY_SAMPLES; i++) sum += analogRead(pin);
  return (int)(sum / JOY_SAMPLES);
}

int8_t axisToSpeed(int raw, int center) {
  int d = raw - center;
  if (d > -JOY_DEADZONE && d < JOY_DEADZONE) return 0;   
  int adj  = abs(d) - JOY_DEADZONE;                        
  int range = 2048 - JOY_DEADZONE;
  float t   = constrain((float)adj / (float)range, 0.0f, 1.0f);
  int   spd = constrain((int)(t * t * JOY_MAX_SPEED + 0.5f), 1, JOY_MAX_SPEED);
  return (int8_t)(d > 0 ? spd : -spd);
}

inline void key1(uint8_t k) {
  Keyboard.releaseAll(); delay(5);
  Keyboard.press(k); delay(15); Keyboard.releaseAll();
}
inline void key2(uint8_t m, uint8_t k) {
  Keyboard.releaseAll(); delay(5);
  Keyboard.press(m); Keyboard.press(k); delay(15); Keyboard.releaseAll();
}
inline void key3(uint8_t m1, uint8_t m2, uint8_t k) {
  Keyboard.releaseAll(); delay(5);
  Keyboard.press(m1); Keyboard.press(m2); Keyboard.press(k); delay(15); Keyboard.releaseAll();
}

void winRun(const char* cmd) {
  Keyboard.releaseAll();
  Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r');
  delay(200); Keyboard.releaseAll();
  delay(600);
  Keyboard.print(cmd);
  delay(150);
  Keyboard.write(KEY_RETURN);
}

void linuxRun(const char* cmd) {
  Keyboard.releaseAll();
  Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_F2);
  delay(100); Keyboard.releaseAll();
  delay(500);                    
  Keyboard.print(cmd);
  delay(300);
  Keyboard.write(KEY_RETURN);
}

// TASTENAKTIONEN
void doGeneral(int r, int c) {
  switch (r * 4 + c) {
    case  0: linuxRun("firefox"); break;                       
    case  1: key2(KEY_LEFT_GUI, 'e'); break;                   
    case  2: key3(KEY_LEFT_GUI, KEY_LEFT_SHIFT, 's'); break;   
    case  3: key2(KEY_LEFT_GUI, 'l'); break;                   
    case  4: key2(KEY_LEFT_CTRL, 'z'); break;                  
    case  5: key2(KEY_LEFT_CTRL, 'y'); break;                  
    case  6: key2(KEY_LEFT_CTRL, 'c'); break;                  
    case  7: key2(KEY_LEFT_CTRL, 'v'); break;                  
    case  8: key2(KEY_LEFT_CTRL, 's'); break;                  
    case  9: key2(KEY_LEFT_CTRL, 'a'); break;                  
    case 10: key2(KEY_LEFT_CTRL, 'f'); break;                  
    case 11: key2(KEY_LEFT_CTRL, 'w'); break;                  
    case 12: key2(KEY_LEFT_CTRL, 't'); break;                  
    case 13: key3(KEY_LEFT_CTRL, KEY_LEFT_SHIFT, 't'); break;  
    case 14: key2(KEY_LEFT_ALT, KEY_F4); break;                
    case 15: key2(KEY_LEFT_GUI, 'd'); break;                   
  }
}

void doEasyEDA(int r, int c) {
  switch (r * 4 + c) {
    case  0: key1('w'); break;                                 
    case  1: key1('p'); break;                                 
    case  2: key1('r'); break;                                 
    case  3: key1('x'); break;                                 
    case  4: key2(KEY_LEFT_CTRL, 'z'); break;                  
    case  5: key2(KEY_LEFT_CTRL, 'y'); break;                  
    case  6: key2(KEY_LEFT_CTRL, 'c'); break;                  
    case  7: key2(KEY_LEFT_CTRL, 'v'); break;                  
    case  8: key2(KEY_LEFT_CTRL, 's'); break;                  
    case  9: key1(KEY_DELETE); break;                          
    case 10: key2(KEY_LEFT_CTRL, 'a'); break;                  
    case 11: key1(KEY_ESC); break;                             
    case 12: key1('g'); break;                                 
    case 13: key3(KEY_LEFT_CTRL, KEY_LEFT_SHIFT, 'p'); break;  
    case 14: key1('n'); break;                                 
    case 15: key1('b'); break;                                 
  }
}

void doChill(int r, int c) {
  switch (r * 4 + c) {
    case  0: sendConsumerStep(CONSUMER_CONTROL_PLAY_PAUSE); break;
    case  1: sendConsumerStep(CONSUMER_CONTROL_SCAN_NEXT); break;
    case  2: sendConsumerStep(CONSUMER_CONTROL_SCAN_PREVIOUS); break;
    case  3: sendConsumerStep(CONSUMER_CONTROL_STOP); break;
    case  4: sendConsumerStep(CONSUMER_CONTROL_MUTE); break;
    case  5: sendConsumerStep(CONSUMER_CONTROL_VOLUME_INCREMENT); break;
    case  6: sendConsumerStep(CONSUMER_CONTROL_VOLUME_DECREMENT); break;
    case  7: key3(KEY_LEFT_GUI, KEY_LEFT_SHIFT, 's'); break;   
    case  8: key2(KEY_LEFT_GUI, 'l'); break;                   
    case  9: sendConsumerStep(CONSUMER_CONTROL_CALCULATOR); break;
    case 10: sendConsumerStep(CONSUMER_CONTROL_BROWSER_HOME); break;
    case 11: sendConsumerStep(CONSUMER_CONTROL_BROWSER_BACK); break;
    case 12: sendConsumerStep(CONSUMER_CONTROL_BROWSER_FORWARD); break;
    case 13: sendConsumerStep(CONSUMER_CONTROL_BROWSER_REFRESH); break;
    case 14: sendConsumerStep(CONSUMER_CONTROL_BROWSER_HOME); break;
    case 15: sendConsumerStep(CONSUMER_CONTROL_SLEEP); break;
  }
}

void doGaming(int r, int c) {
  switch (r * 4 + c) {
    case  0: key1('1'); break;
    case  1: key1('2'); break;
    case  2: key1('3'); break;
    case  3: key1('4'); break;
    case  4: key1('q'); break;
    case  5: key1('e'); break;
    case  6: key1('r'); break;
    case  7: key1('f'); break;
    case  8: key1(KEY_TAB); break;                             
    case  9: key1('m'); break;                                 
    case 10: key1(KEY_ESC); break;                             
    case 11: key3(KEY_LEFT_GUI, KEY_LEFT_SHIFT, 's'); break;   
    case 12: key1('t'); break;                                 
    case 13: key1('c'); break;                                 
    case 14: key1(KEY_LEFT_SHIFT); break;                      
    case 15: key1('z'); break;                                 
  }
}

void executeAction(int mode, int r, int c) {
  lastAction = String(KEY_LABELS[mode][r][c]);
  switch (mode) {
    case GENERAL: doGeneral(r, c); break;
    case EASYEDA: doEasyEDA(r, c); break;
    case CHILL:   doChill(r, c);   break;
    case GAMING:  doGaming(r, c);  break;
  }
}

// Ultra-lange, kompakte Balken für maximale Ausnutzung
void drawPotiBarInline(int x, int y, const char* label, uint16_t color, int pct) {
  tft.fillRect(x, y, 122, 10, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(x, y + 1);
  tft.print(label);
  
  // Langer Balken der komplett bis zum rechten Rand ausnutzt
  int barX = x + 12;
  int barW = 84; 
  tft.fillRect(barX, y + 2, barW, 6, 0x2104);
  tft.drawRect(barX, y + 2, barW, 6, COL_DARKGRAY);
  int fillW = map(pct, 0, 100, 0, barW - 2);
  if (fillW > 0) tft.fillRect(barX + 1, y + 3, fillW, 4, color);
  
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(barX + barW + 4, y + 1);
  if (pct < 100) tft.print(" ");
  if (pct < 10)  tft.print(" ");
  tft.print(pct);
  tft.print("%");
}

void drawKeyGrid() {
  const int startY = 26; // Startet direkt unter dem schlanken Header
  const int cellW  = 40;
  const int cellH  = 25; // Riesige Tastenhöhe (25px * 4 = 100px) für maximale Lesbarkeit
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int x = c * cellW;
      int y = startY + r * cellH;
      tft.fillRect(x, y, cellW, cellH, 0x1082);
      tft.drawRect(x, y, cellW, cellH, COL_DARKGRAY);
      const char* lbl = KEY_LABELS[currentMode][r][c];
      int len = strlen(lbl);
      while (len > 0 && lbl[len - 1] == ' ') len--;
      int textX = x + (cellW - len * 6) / 2;
      int textY = y + (cellH - 7) / 2;
      tft.setTextColor(ST77XX_WHITE);
      tft.setTextSize(1);
      tft.setCursor(textX, textY);
      for (int i = 0; i < len; i++) tft.write(lbl[i]);
    }
  }
}

void drawBaseUI() {
  tft.fillScreen(ST77XX_BLACK);

  // Oben links: Exakter 3-Buchstaben-Hintergrund (genau 36px breit für 3 Zeichen in Size 2)
  tft.fillRect(0, 0, 36, 24, COL_NAVY);
  tft.setTextColor(MODE_COLORS[currentMode]);
  tft.setTextSize(2);
  tft.setCursor(2, 5);
  tft.print(MODE_NAMES[currentMode]);

  // Direkt daneben: Vol und Bri Balken über die volle verbleibende Breite
  drawPotiBarInline(38, 2,  "V", ST77XX_MAGENTA, potiToPercent(PIN_VOL_POTI));
  drawPotiBarInline(38, 13, "B", 0x07FF,         potiToPercent(PIN_BRI_POTI));

  // Trennlinie unter dem Header
  tft.drawFastHLine(0, 24, 160, MODE_COLORS[currentMode]);

  drawKeyGrid();
}

// POTI HANDLER
void handlePotis() {
  static unsigned long lastPoll    = 0;
  static int           lastVolDisp = -1;
  static int           lastBriDisp = -1;

  if (millis() - lastPoll < POT_POLL_MS) return;
  lastPoll = millis();

  int volTarget = potiToSteps(PIN_VOL_POTI, VOL_MAX_STEPS);
  int briTarget = potiToSteps(PIN_BRI_POTI, BRI_MAX_STEPS);

  if (sentVol < 0) sentVol = volTarget;
  if (sentBri < 0) sentBri = briTarget;

  int volDelta = volTarget - sentVol;
  if (abs(volDelta) >= POT_HYSTERESIS) {
    int steps = constrain(volDelta, -POT_SEND_PER_TICK, POT_SEND_PER_TICK);
    uint16_t code = (steps > 0) ? CONSUMER_CONTROL_VOLUME_INCREMENT
                                : CONSUMER_CONTROL_VOLUME_DECREMENT;
    for (int i = 0; i < abs(steps); i++) sendConsumerStep(code);
    sentVol += steps;
  }

  int briDelta = briTarget - sentBri;
  if (abs(briDelta) >= POT_HYSTERESIS) {
    int steps = constrain(briDelta, -POT_SEND_PER_TICK, POT_SEND_PER_TICK);
    uint16_t code = (steps > 0) ? CONSUMER_CONTROL_BRIGHTNESS_INCREMENT
                                : CONSUMER_CONTROL_BRIGHTNESS_DECREMENT;
    for (int i = 0; i < abs(steps); i++) sendConsumerStep(code);
    sentBri += steps;
  }

  int volDisp = map(sentVol, 0, VOL_MAX_STEPS, 0, 100);
  int briDisp = map(sentBri, 0, BRI_MAX_STEPS, 0, 100);
  if (volDisp != lastVolDisp) { drawPotiBarInline(38, 2,  "V", ST77XX_MAGENTA, volDisp); lastVolDisp = volDisp; }
  if (briDisp != lastBriDisp) { drawPotiBarInline(38, 13, "B", 0x07FF,         briDisp); lastBriDisp = briDisp; }
}

// JOYSTICK
void calibrateJoystick() {
  delay(100);
  for (int i = 0; i < 16; i++) { analogRead(PIN_JOY_X); analogRead(PIN_JOY_Y); }
  long sumX = 0, sumY = 0;
  for (int i = 0; i < JOY_CALIB_N; i++) {
    sumX += analogRead(PIN_JOY_X);
    sumY += analogRead(PIN_JOY_Y);
    delay(3);
  }
  joyCenter_X = (int)(sumX / JOY_CALIB_N);
  joyCenter_Y = (int)(sumY / JOY_CALIB_N);
}

void handleJoystick() {
  if (currentMode != GENERAL) return;

  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  if (now - lastUpdate < JOY_UPDATE_MS) return;
  lastUpdate = now;

  int rawX = readJoyAxis(PIN_JOY_X);
  int rawY = readJoyAxis(PIN_JOY_Y);

  int8_t dx = axisToSpeed(rawX, joyCenter_X);
  int8_t dy = axisToSpeed(rawY, joyCenter_Y);

#if JOY_SWAP_XY
  { int8_t t = dx; dx = dy; dy = t; }
#endif
#if JOY_INVERT_X
  dx = -dx;
#endif
#if JOY_INVERT_Y
  dy = -dy;
#endif

  if (dx != 0 || dy != 0) Mouse.move(dx, dy, 0);

  bool btnNow = (digitalRead(PIN_JOY_BTN) == LOW);
  if (btnNow && !joyBtnLast && (now - joyBtnTime > JOY_BTN_DEBOUNCE)) {
    joyBtnTime = now;
    Mouse.click(MOUSE_LEFT);
  }
  joyBtnLast = btnNow;
}

void handleModeButton() {
  static bool          lastPressed = false;
  static unsigned long lastTime    = 0;

  bool pressed = (digitalRead(PIN_MODE_BTN) == LOW);
  unsigned long now = millis();

  if (pressed && !lastPressed && (now - lastTime > DEBOUNCE_MS)) {
    lastTime    = now;
    currentMode = (currentMode + 1) % 4;
    lastAction  = String("-> ") + MODE_NAMES[currentMode];
    drawBaseUI();
  }
  lastPressed = pressed;
}

void setup() {
  USB.begin();
  Keyboard.begin();
  Mouse.begin();
  Consumer.begin();
  delay(2000);

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(50);
  digitalWrite(TFT_RST, LOW);  delay(50);
  digitalWrite(TFT_RST, HIGH); delay(50);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(3);  

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 45);
  tft.print("Macropad");
  tft.setTextSize(1);
  tft.setTextColor(COL_MIDGRAY);
  tft.setCursor(10, 70);
  tft.print("Starting...");

  for (int i = 0; i < 4; i++) {
    pinMode(ROW_PINS[i], OUTPUT);
    digitalWrite(ROW_PINS[i], HIGH);
    pinMode(COL_PINS[i], INPUT_PULLUP);
  }
  pinMode(PIN_JOY_BTN,  INPUT_PULLUP);
  pinMode(PIN_MODE_BTN, INPUT_PULLUP);

  analogReadResolution(12);
  calibrateJoystick();

  sentVol = potiToSteps(PIN_VOL_POTI, VOL_MAX_STEPS);
  sentBri = potiToSteps(PIN_BRI_POTI, BRI_MAX_STEPS);

  drawBaseUI();
}

void loop() {
  unsigned long now = millis();

  handleJoystick();
  handlePotis();
  handleModeButton();

  for (int r = 0; r < 4; r++) {
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(10);

    for (int c = 0; c < 4; c++) {
      bool pressed = (digitalRead(COL_PINS[c]) == LOW);

      if (pressed && !keyState[r][c] && (now - keyDebounce[r][c] > DEBOUNCE_MS)) {
        keyDebounce[r][c] = now;
        keyState[r][c]    = true;
        executeAction(currentMode, r, c);
        // (updateLastAction wurde entfernt)

      } else if (!pressed && keyState[r][c]) {
        keyState[r][c] = false;
      }
    }

    digitalWrite(ROW_PINS[r], HIGH);
  }
}