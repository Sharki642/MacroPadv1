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
#include <USBCDC.h>
#include <Preferences.h>

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


// MODI (3-Buchstaben-Abkürzungen, per App änderbar)
enum Mode { GENERAL = 0, EASYEDA = 1, CHILL = 2, GAMING = 3 };
#define NUM_TABS 4
#define NUM_KEYS 16
char           tabNames[NUM_TABS][4] = {"GEN", "EDA", "CHL", "GAM"};
const uint16_t MODE_COLORS[] = {ST77XX_CYAN, ST77XX_GREEN, ST77XX_MAGENTA, COL_ORANGE};


// --- 16x16 BITMAPS FÜR DEN GENERAL TAB (PROGMEM) ---
const unsigned char PROGMEM icon_gen[16][32] = {
  // 0: FFOX (Globe/Firefox Style)
  {0x00,0x00,0x07,0xE0,0x18,0x18,0x20,0x04,0x40,0x02,0x43,0xC2,0x84,0x21,0x88,0x11,
   0x88,0x11,0x84,0x21,0x43,0xC2,0x40,0x02,0x20,0x04,0x18,0x18,0x07,0xE0,0x00,0x00},
  // 1: EXPLR (Folder)
  {0x00,0x00,0x00,0x00,0x3F,0x80,0x20,0x40,0x3F,0xFC,0x20,0x04,0x20,0x04,0x20,0x04,
   0x20,0x04,0x20,0x04,0x20,0x04,0x20,0x04,0x3F,0xFC,0x00,0x00,0x00,0x00,0x00,0x00},
  // 2: SCRSH (Camera)
  {0x00,0x00,0x00,0x00,0x0F,0xF0,0x10,0x08,0x3F,0xFC,0x21,0x04,0x25,0xA4,0x25,0xA4,
   0x21,0x04,0x3F,0xFC,0x20,0x04,0x3F,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  // 3: LOCK (Lock)
  {0x00,0x00,0x00,0x00,0x07,0xE0,0x08,0x10,0x08,0x10,0x08,0x10,0x1F,0xF8,0x1B,0xB8,
   0x1B,0xB8,0x1B,0xB8,0x1B,0xB8,0x1B,0xB8,0x1F,0xF8,0x00,0x00,0x00,0x00,0x00,0x00},
  // 4: UNDO (Back Arrow)
  {0x00,0x00,0x00,0x00,0x01,0x80,0x03,0x00,0x07,0x00,0x0F,0xFF,0x0F,0x80,0x07,0x00,
   0x03,0x00,0x01,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  // 5: REDO (Forward Arrow)
  {0x00,0x00,0x00,0x00,0x01,0x80,0x00,0xC0,0x00,0xE0,0xFF,0xF0,0x01,0xF0,0x00,0xE0,
   0x00,0xC0,0x01,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  // 6: COPY (Two Papers)
  {0x00,0x00,0x00,0x00,0x1F,0xE0,0x10,0x20,0x1F,0xE0,0x10,0x20,0xFF,0xE0,0x80,0x20,
   0x80,0x20,0x80,0x20,0x80,0x20,0x80,0x20,0xFF,0xE0,0x00,0x00,0x00,0x00,0x00,0x00},
  // 7: PASTE (Clipboard)
  {0x00,0x00,0x00,0x00,0x07,0xE0,0x1C,0x38,0x3F,0xFC,0x20,0x04,0x20,0x04,0x20,0x04,
   0x3F,0xFC,0x20,0x04,0x20,0x04,0x20,0x04,0x3F,0xFC,0x00,0x00,0x00,0x00,0x00,0x00},
  // 8: SAVE (Floppy Disk)
  {0x00,0x00,0x00,0x00,0xFF,0xFF,0x80,0x01,0x8F,0xF1,0x81,0x01,0x80,0x01,0x80,0x01,
   0x80,0x01,0x80,0x01,0x8F,0xF1,0x81,0x01,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00},
  // 9: SEL A (Checkmark/All)
  {0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x06,0x00,0x0C,0x00,0x18,0x00,0x30,0x40,0x60,
   0xE0,0xC0,0x71,0x80,0x3B,0x00,0x1E,0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  // 10: FIND (Magnifying Glass)
  {0x00,0x00,0x00,0x00,0x07,0xC0,0x08,0x20,0x10,0x10,0x10,0x10,0x10,0x10,0x08,0x20,
   0x07,0xC0,0x00,0x40,0x00,0x80,0x00,0x00,0x01,0x00,0x02,0x00,0x00,0x00,0x00,0x00},
  // 11: CL TB (Close Window 'X')
  {0x00,0x00,0x00,0x00,0x31,0x8C,0x1B,0xD8,0x0F,0xF0,0x06,0x60,0x0F,0xF0,0x1B,0xD8,
   0x31,0x8C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  // 12: NW TB (Plus Sign)
  {0x00,0x00,0x00,0x00,0x01,0x80,0x01,0x80,0x01,0x80,0x01,0x80,0xFF,0xFF,0xFF,0xFF,
   0x01,0x80,0x01,0x80,0x01,0x80,0x01,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  // 13: RP TB (Refresh Circle)
  {0x00,0x00,0x00,0x00,0x0F,0xE0,0x30,0x18,0x40,0x04,0x80,0x02,0x80,0x02,0x86,0x02,
   0x88,0x02,0x80,0x02,0x40,0x04,0x30,0x18,0x0F,0xE0,0x00,0x00,0x00,0x00,0x00,0x00},
  // 14: AF4 (Power Button)
  {0x00,0x00,0x00,0x00,0x07,0xE0,0x18,0x18,0x20,0x04,0x40,0x02,0x40,0x02,0x40,0x02,
   0x40,0x02,0x20,0x04,0x18,0x18,0x07,0xE0,0x01,0x80,0x01,0x80,0x01,0x80,0x00,0x00},
  // 15: DESK (Monitor Screen)
  {0x00,0x00,0x00,0x00,0x7F,0xFE,0x40,0x02,0x40,0x02,0x40,0x02,0x40,0x02,0x40,0x02,
   0x40,0x02,0x7F,0xFE,0x01,0x80,0x01,0x80,0x0F,0xF0,0x00,0x00,0x00,0x00,0x00,0x00}
};


// TASTEN-KONFIGURATION
// Wird im Flash (NVS) gespeichert und von der PC-App geändert.
// Tasten sind physische HID-Usages, damit das Layout (QWERTZ) keine Rolle spielt.
#define LABEL_LEN   5
#define MAX_TEXT   48

enum ActionType : uint8_t { ACT_NONE = 0, ACT_KEY = 1, ACT_TEXT = 2, ACT_RUN = 3, ACT_MEDIA = 4, ACT_MOUSE = 5 };

struct KeyConfig {
  uint8_t  type;
  uint8_t  mods;                 // Modifier-Bits (ACT_KEY)
  uint16_t code;                 // HID-Usage / Consumer-Code / Maustaste
  int8_t   icon;                 // Index in icon_gen, -1 = Text anzeigen
  uint8_t  hold;                 // 1 = gedrückt halten statt antippen
  char     label[LABEL_LEN + 1];
  uint8_t  textLen;              // Anzahl Paare in text
  uint8_t  text[MAX_TEXT * 2];   // Paare aus (mods, usage) für ACT_TEXT / ACT_RUN
};

#define M_CTRL   0x01
#define M_SHIFT  0x02
#define M_ALT    0x04
#define M_GUI    0x08

#define HID_LETTER(c) (uint16_t)((c) - 'a' + 0x04)
#define HID_DE_Z   0x1C          // Taste "Z" auf QWERTZ
#define HID_DE_Y   0x1D          // Taste "Y" auf QWERTZ
#define HID_1      0x1E
#define HID_ENTER  0x28
#define HID_ESC    0x29
#define HID_TAB    0x2B
#define HID_F2     0x3B
#define HID_F4     0x3D
#define HID_DELETE 0x4C

struct DefaultKey {
  uint8_t     type;
  uint8_t     mods;
  uint16_t    code;
  uint8_t     hold;
  int8_t      icon;
  const char* label;
  const char* run;               // Nur Kleinbuchstaben a-x
};

const DefaultKey DEFAULT_KEYS[NUM_TABS][NUM_KEYS] = {
  // ---- GENERAL ----
  {{ACT_RUN, 0,                   0,                 0, -1, "FFOX",  "firefox"},
   {ACT_KEY, M_GUI,               HID_LETTER('e'),   0,  1, "EXPLR", nullptr},
   {ACT_KEY, M_GUI | M_SHIFT,     HID_LETTER('s'),   0, -1, "SCRSH", nullptr},
   {ACT_KEY, M_GUI,               HID_LETTER('l'),   0,  3, "LOCK",  nullptr},
   {ACT_KEY, M_CTRL,              HID_DE_Z,          0,  4, "UNDO",  nullptr},
   {ACT_KEY, M_CTRL,              HID_DE_Y,          0,  5, "REDO",  nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('c'),   0, -1, "COPY",  nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('v'),   0, -1, "PASTE", nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('s'),   0, -1, "SAVE",  nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('a'),   0, -1, "SEL A", nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('f'),   0, -1, "FIND",  nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('w'),   0, -1, "CL TB", nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('t'),   0, -1, "NW TB", nullptr},
   {ACT_KEY, M_CTRL | M_SHIFT,    HID_LETTER('t'),   0, -1, "RP TB", nullptr},
   {ACT_KEY, M_ALT,               HID_F4,            0, -1, "AF4",   nullptr},
   {ACT_KEY, M_GUI,               HID_LETTER('d'),   0, 15, "DESK",  nullptr}},
  // ---- EASYEDA ----
  {{ACT_KEY, 0,                   HID_LETTER('w'),   0, -1, "WIRE",  nullptr},
   {ACT_KEY, 0,                   HID_LETTER('p'),   0, -1, "PLACE", nullptr},
   {ACT_KEY, 0,                   HID_LETTER('r'),   0, -1, "ROT",   nullptr},
   {ACT_KEY, 0,                   HID_LETTER('x'),   0, -1, "FLIP",  nullptr},
   {ACT_KEY, M_CTRL,              HID_DE_Z,          0, -1, "UNDO",  nullptr},
   {ACT_KEY, M_CTRL,              HID_DE_Y,          0, -1, "REDO",  nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('c'),   0, -1, "COPY",  nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('v'),   0, -1, "PASTE", nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('s'),   0, -1, "SAVE",  nullptr},
   {ACT_KEY, 0,                   HID_DELETE,        0, -1, "DEL",   nullptr},
   {ACT_KEY, M_CTRL,              HID_LETTER('a'),   0, -1, "SEL A", nullptr},
   {ACT_KEY, 0,                   HID_ESC,           0, -1, "ESC",   nullptr},
   {ACT_KEY, 0,                   HID_LETTER('g'),   0, -1, "GND",   nullptr},
   {ACT_KEY, M_CTRL | M_SHIFT,    HID_LETTER('p'),   0, -1, "PWR",   nullptr},
   {ACT_KEY, 0,                   HID_LETTER('n'),   0, -1, "NET",   nullptr},
   {ACT_KEY, 0,                   HID_LETTER('b'),   0, -1, "BUS",   nullptr}},
  // ---- CHILL ----
  {{ACT_MEDIA, 0, CONSUMER_CONTROL_PLAY_PAUSE,       0, -1, "PLAY",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_SCAN_NEXT,        0, -1, "NEXT",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_SCAN_PREVIOUS,    0, -1, "PREV",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_STOP,             0, -1, "STOP",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_MUTE,             0, -1, "MUTE",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_VOLUME_INCREMENT, 0, -1, "VOL+",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_VOLUME_DECREMENT, 0, -1, "VOL-",  nullptr},
   {ACT_KEY, M_GUI | M_SHIFT,     HID_LETTER('s'),   0, -1, "SCRSH", nullptr},
   {ACT_KEY, M_GUI,               HID_LETTER('l'),   0, -1, "LOCK",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_CALCULATOR,       0, -1, "CALC",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_BROWSER_HOME,     0, -1, "HOME",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_BROWSER_BACK,     0, -1, "BACK",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_BROWSER_FORWARD,  0, -1, "FWD",   nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_BROWSER_REFRESH,  0, -1, "RFRSH", nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_BROWSER_HOME,     0, -1, "B.HM",  nullptr},
   {ACT_MEDIA, 0, CONSUMER_CONTROL_SLEEP,            0, -1, "SLEEP", nullptr}},
  // ---- GAMING ----
  {{ACT_KEY, 0,                   HID_1,             1, -1, "SL 1",  nullptr},
   {ACT_KEY, 0,                   HID_1 + 1,         1, -1, "SL 2",  nullptr},
   {ACT_KEY, 0,                   HID_1 + 2,         1, -1, "SL 3",  nullptr},
   {ACT_KEY, 0,                   HID_1 + 3,         1, -1, "SL 4",  nullptr},
   {ACT_KEY, 0,                   HID_LETTER('q'),   1, -1, "Q",     nullptr},
   {ACT_KEY, 0,                   HID_LETTER('e'),   1, -1, "E",     nullptr},
   {ACT_KEY, 0,                   HID_LETTER('r'),   1, -1, "R",     nullptr},
   {ACT_KEY, 0,                   HID_LETTER('f'),   1, -1, "F",     nullptr},
   {ACT_KEY, 0,                   HID_TAB,           1, -1, "TAB",   nullptr},
   {ACT_KEY, 0,                   HID_LETTER('m'),   1, -1, "MAP",   nullptr},
   {ACT_KEY, 0,                   HID_ESC,           1, -1, "ESC",   nullptr},
   {ACT_KEY, M_GUI | M_SHIFT,     HID_LETTER('s'),   0, -1, "SCRSH", nullptr},
   {ACT_KEY, 0,                   HID_LETTER('t'),   1, -1, "PTT",   nullptr},
   {ACT_KEY, 0,                   HID_LETTER('c'),   1, -1, "CROUC", nullptr},
   {ACT_KEY, M_SHIFT,             0,                 1, -1, "SPRNT", nullptr},
   {ACT_KEY, 0,                   HID_DE_Z,          1, -1, "PRONE", nullptr}}
};

// HID + DISPLAY OBJEKTE
USBHIDKeyboard        Keyboard;
USBHIDMouse           Mouse;
USBHIDConsumerControl Consumer;
USBCDC                ConfigPort;
Preferences           prefs;
Adafruit_ST7735       tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);


// ZUSTAND
int          currentMode        = GENERAL;
bool         keyState[4][4]     = {};
unsigned long keyDebounce[4][4] = {};
String       lastAction         = "Ready";
KeyConfig    keyConfig[NUM_TABS][NUM_KEYS];

// Merkt sich gehaltene Tasten, damit das Loslassen auch nach Tabwechsel stimmt
struct HeldKey { uint8_t type; uint8_t mods; uint16_t code; };
HeldKey      heldKeys[4][4]     = {};
uint8_t      modCount[8]        = {};

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

// press()/release() senden bei Modifiern keinen Report, der PC sähe sie sonst
// ewig gedrückt. Usage 0x01 ist nie im Report, das Loslassen sendet also nur.
void sendKeyReport() {
  Keyboard.releaseRaw(0x01);
}

// Modifier zählen, damit zwei gehaltene Tasten sich nicht gegenseitig lösen
void modsDown(uint8_t mods) {
  for (int b = 0; b < 8; b++) {
    if ((mods & (1 << b)) && modCount[b]++ == 0) Keyboard.press(0x80 + b);
  }
  sendKeyReport();
}
void modsUp(uint8_t mods) {
  for (int b = 0; b < 8; b++) {
    if ((mods & (1 << b)) && modCount[b] > 0 && --modCount[b] == 0) Keyboard.release(0x80 + b);
  }
  sendKeyReport();
}

void tapKey(uint8_t mods, uint8_t code) {
  modsDown(mods);
  if (code) Keyboard.pressRaw(code);
  delay(15);
  if (code) Keyboard.releaseRaw(code);
  modsUp(mods);
  delay(5);
}

void typeText(const KeyConfig& cfg) {
  for (int i = 0; i < cfg.textLen; i++) tapKey(cfg.text[i * 2], cfg.text[i * 2 + 1]);
}

void linuxRun(const KeyConfig& cfg) {
  tapKey(M_ALT, HID_F2);
  delay(600);
  typeText(cfg);
  delay(300);
  tapKey(0, HID_ENTER);
}

// TASTENAKTIONEN
void pressAction(int r, int c) {
  const KeyConfig& cfg = keyConfig[currentMode][r * 4 + c];
  lastAction = String(cfg.label);
  switch (cfg.type) {
    case ACT_KEY:
      if (cfg.hold) {
        heldKeys[r][c] = {ACT_KEY, cfg.mods, cfg.code};
        modsDown(cfg.mods);
        if (cfg.code) Keyboard.pressRaw(cfg.code);
      } else {
        tapKey(cfg.mods, cfg.code);
      }
      break;
    case ACT_TEXT:  typeText(cfg); break;
    case ACT_RUN:   linuxRun(cfg); break;
    case ACT_MEDIA: sendConsumerStep(cfg.code); break;
    case ACT_MOUSE:
      if (cfg.hold) {
        heldKeys[r][c] = {ACT_MOUSE, 0, cfg.code};
        Mouse.press(cfg.code);
      } else {
        Mouse.click(cfg.code);
      }
      break;
  }
}

void releaseAction(int r, int c) {
  HeldKey& held = heldKeys[r][c];
  if (held.type == ACT_KEY) {
    if (held.code) Keyboard.releaseRaw(held.code);
    modsUp(held.mods);
  } else if (held.type == ACT_MOUSE) {
    Mouse.release(held.code);
  }
  held.type = ACT_NONE;
}

// Ultra-lange, kompakte Balken
void drawPotiBarInline(int x, int y, const char* label, uint16_t color, int pct) {
  tft.fillRect(x, y, 122, 10, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(x, y + 1);
  tft.print(label);
  
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

void drawKeyCell(int index) {
  const int startY = 26; 
  const int cellW  = 40;
  const int cellH  = 25; 
  int x = (index % 4) * cellW;
  int y = startY + (index / 4) * cellH;
  const KeyConfig& cfg = keyConfig[currentMode][index];

  tft.fillRect(x, y, cellW, cellH, 0x1082);
  tft.drawRect(x, y, cellW, cellH, COL_DARKGRAY);

  // Mit Icon: Bitmap zeichnen, sonst Text
  if (cfg.icon >= 0 && cfg.icon < 16) {
    // Zeichnet das 16x16 Icon zentriert in die Taste
    tft.drawBitmap(x + 12, y + 4, icon_gen[cfg.icon], 16, 16, MODE_COLORS[currentMode]);
  } else {
    int len = strlen(cfg.label);
    int textX = x + (cellW - len * 6) / 2;
    int textY = y + (cellH - 7) / 2;
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(textX, textY);
    tft.print(cfg.label);
  }
}

void drawKeyGrid() {
  for (int i = 0; i < NUM_KEYS; i++) drawKeyCell(i);
}

void drawBaseUI() {
  tft.fillScreen(ST77XX_BLACK);

  tft.fillRect(0, 0, 36, 24, COL_NAVY);
  tft.setTextColor(MODE_COLORS[currentMode]);
  tft.setTextSize(2);
  tft.setCursor(2, 5);
  tft.print(tabNames[currentMode]);

  drawPotiBarInline(38, 2,  "V", ST77XX_MAGENTA, potiToPercent(PIN_VOL_POTI));
  drawPotiBarInline(38, 13, "B", 0x07FF,         potiToPercent(PIN_BRI_POTI));

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

// KONFIGURATION IM FLASH
void loadDefaultKey(int tab, int key) {
  const DefaultKey& def = DEFAULT_KEYS[tab][key];
  KeyConfig& cfg = keyConfig[tab][key];
  memset(&cfg, 0, sizeof(cfg));
  cfg.type = def.type;
  cfg.mods = def.mods;
  cfg.code = def.code;
  cfg.hold = def.hold;
  cfg.icon = def.icon;
  strncpy(cfg.label, def.label, LABEL_LEN);
  for (const char* p = def.run; p && *p; p++) {
    cfg.text[cfg.textLen * 2 + 1] = HID_LETTER(*p);
    cfg.textLen++;
  }
}

void loadConfig() {
  prefs.begin("macropad", false);
  for (int t = 0; t < NUM_TABS; t++) {
    for (int k = 0; k < NUM_KEYS; k++) {
      loadDefaultKey(t, k);
      char name[8];
      snprintf(name, sizeof(name), "k%d", t * NUM_KEYS + k);
      KeyConfig stored;
      size_t got = prefs.getBytes(name, &stored, sizeof(stored));
      if (got >= offsetof(KeyConfig, text) && stored.textLen <= MAX_TEXT
          && got == offsetof(KeyConfig, text) + stored.textLen * 2) {
        memset(&keyConfig[t][k], 0, sizeof(KeyConfig));
        memcpy(&keyConfig[t][k], &stored, got);
      }
    }
    char name[8];
    snprintf(name, sizeof(name), "t%d", t);
    if (prefs.isKey(name)) prefs.getString(name, tabNames[t], sizeof(tabNames[t]));
  }
}

bool saveKey(int t, int k) {
  char name[8];
  snprintf(name, sizeof(name), "k%d", t * NUM_KEYS + k);
  size_t len = offsetof(KeyConfig, text) + keyConfig[t][k].textLen * 2;
  return prefs.putBytes(name, &keyConfig[t][k], len) == len;
}

// KONFIGURATIONS-PROTOKOLL (Zeilen über USB-Serial)
//   HELLO                                        -> MACROPAD 1
//   GET                                          -> TAB ... / KEY ... / END
//   SET tab key type mods code icon hold lbl txt -> OK | ERR
//   NAME tab name                                -> OK | ERR
//   RESET                                        -> OK
// lbl und txt sind Hex-Strings, "-" steht für leer.
void printHex(const uint8_t* data, int len) {
  if (len == 0) { ConfigPort.print("-"); return; }
  for (int i = 0; i < len; i++) {
    if (data[i] < 0x10) ConfigPort.print('0');
    ConfigPort.print(data[i], HEX);
  }
}

int parseHex(const char* s, uint8_t* out, int maxLen) {
  if (!s) return -1;
  if (strcmp(s, "-") == 0) return 0;
  int len = strlen(s);
  if (len % 2 != 0 || len / 2 > maxLen) return -1;
  for (int i = 0; i < len; i += 2) {
    char byteStr[3] = {s[i], s[i + 1], 0};
    char* end;
    out[i / 2] = (uint8_t)strtoul(byteStr, &end, 16);
    if (*end != 0) return -1;
  }
  return len / 2;
}

void cmdGet() {
  for (int t = 0; t < NUM_TABS; t++) {
    ConfigPort.printf("TAB %d %s\n", t, tabNames[t]);
    for (int k = 0; k < NUM_KEYS; k++) {
      const KeyConfig& cfg = keyConfig[t][k];
      ConfigPort.printf("KEY %d %d %d %d %d %d %d ", t, k, cfg.type, cfg.mods, cfg.code, cfg.icon, cfg.hold);
      printHex((const uint8_t*)cfg.label, strlen(cfg.label));
      ConfigPort.print(" ");
      printHex(cfg.text, cfg.textLen * 2);
      ConfigPort.print("\n");
    }
  }
  ConfigPort.print("END\n");
}

bool cmdSet() {
  const char* f[9];
  for (int i = 0; i < 9; i++) f[i] = strtok(nullptr, " ");
  if (!f[8]) return false;
  int t = atoi(f[0]), k = atoi(f[1]), type = atoi(f[2]), icon = atoi(f[5]);
  if (t < 0 || t >= NUM_TABS || k < 0 || k >= NUM_KEYS) return false;
  if (type < ACT_NONE || type > ACT_MOUSE || icon < -1 || icon > 15) return false;

  KeyConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.type = type;
  cfg.mods = atoi(f[3]);
  cfg.code = atoi(f[4]);
  cfg.icon = icon;
  cfg.hold = atoi(f[6]) ? 1 : 0;
  if (parseHex(f[7], (uint8_t*)cfg.label, LABEL_LEN) < 0) return false;
  int textBytes = parseHex(f[8], cfg.text, MAX_TEXT * 2);
  if (textBytes < 0 || textBytes % 2 != 0) return false;
  cfg.textLen = textBytes / 2;

  keyConfig[t][k] = cfg;
  if (!saveKey(t, k)) return false;
  if (t == currentMode) drawKeyCell(k);
  return true;
}

bool cmdName() {
  const char* tab = strtok(nullptr, " ");
  const char* name = strtok(nullptr, " ");
  if (!tab || !name || strlen(name) > 3) return false;
  int t = atoi(tab);
  if (t < 0 || t >= NUM_TABS) return false;
  strcpy(tabNames[t], name);
  char key[8];
  snprintf(key, sizeof(key), "t%d", t);
  prefs.putString(key, name);
  if (t == currentMode) drawBaseUI();
  return true;
}

void cmdReset() {
  prefs.clear();
  const char* names[NUM_TABS] = {"GEN", "EDA", "CHL", "GAM"};
  for (int t = 0; t < NUM_TABS; t++) {
    strcpy(tabNames[t], names[t]);
    for (int k = 0; k < NUM_KEYS; k++) loadDefaultKey(t, k);
  }
  drawBaseUI();
}

void handleCommand(char* line) {
  const char* cmd = strtok(line, " ");
  if (!cmd) return;
  if (strcmp(cmd, "HELLO") == 0)      ConfigPort.print("MACROPAD 1\n");
  else if (strcmp(cmd, "GET") == 0)   cmdGet();
  else if (strcmp(cmd, "SET") == 0)   ConfigPort.print(cmdSet()  ? "OK\n" : "ERR\n");
  else if (strcmp(cmd, "NAME") == 0)  ConfigPort.print(cmdName() ? "OK\n" : "ERR\n");
  else if (strcmp(cmd, "RESET") == 0) { cmdReset(); ConfigPort.print("OK\n"); }
  else ConfigPort.print("ERR\n");
}

void handleConfigPort() {
  static char buf[300];
  static int  len = 0;
  while (ConfigPort.available()) {
    char ch = ConfigPort.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      buf[len] = 0;
      len = 0;
      handleCommand(buf);
    } else if (len < (int)sizeof(buf) - 1) {
      buf[len++] = ch;
    }
  }
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
  if (btnNow != joyBtnLast && (now - joyBtnTime > JOY_BTN_DEBOUNCE)) {
    joyBtnTime = now;
    joyBtnLast = btnNow;
    if (btnNow) Mouse.click(MOUSE_LEFT);
  }
}

void handleModeButton() {
  static bool          lastPressed = false;
  static unsigned long lastTime    = 0;

  bool pressed = (digitalRead(PIN_MODE_BTN) == LOW);
  unsigned long now = millis();

  if (pressed != lastPressed && (now - lastTime > DEBOUNCE_MS)) {
    lastTime    = now;
    lastPressed = pressed;
    if (pressed) {
      currentMode = (currentMode + 1) % 4;
      lastAction  = String("-> ") + tabNames[currentMode];
      drawBaseUI();
    }
  }
}

void setup() {
  ConfigPort.begin();
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
  loadConfig();

  sentVol = potiToSteps(PIN_VOL_POTI, VOL_MAX_STEPS);
  sentBri = potiToSteps(PIN_BRI_POTI, BRI_MAX_STEPS);

  drawBaseUI();
}

void loop() {
  unsigned long now = millis();

  handleJoystick();
  handlePotis();
  handleModeButton();
  handleConfigPort();

  for (int r = 0; r < 4; r++) {
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(10);

    for (int c = 0; c < 4; c++) {
      bool pressed = (digitalRead(COL_PINS[c]) == LOW);

      if (pressed != keyState[r][c] && (now - keyDebounce[r][c] > DEBOUNCE_MS)) {
        keyDebounce[r][c] = now;
        keyState[r][c]    = pressed;
        if (pressed) pressAction(r, c);
        else         releaseAction(r, c);
      }
    }

    digitalWrite(ROW_PINS[r], HIGH);
  }
}