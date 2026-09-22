/*
 * Megachess - hardware bring-up
 *
 * Answers three questions about the DIYables 3.5" 480x320 touch shield on a
 * Mega 2560 before we trust the game code on it:
 *
 *   1. WHICH DRIVER IC?  DIYables shipped three revisions of this shield and
 *      the class has to match. The sketch cycles through the two that library
 *      v2.x supports, painting a full-screen card for each. Tap the screen
 *      while a card is up to lock that driver in.
 *
 *        - Text crisp, TL marker top-left, RED/GREEN/BLUE labels over
 *          matching swatches  ->  that is your driver, tap it.
 *        - Blank, garbled, mirrored, or red and blue swapped on BOTH cards
 *          ->  you have the older ILI9488 board. Nothing to tap; tell me and
 *              I will downgrade the library to v1.0.0, which has that class.
 *
 *   2. DOES TOUCH LAND WHERE YOU PRESS?  After you tap, it switches to a touch
 *      test: a crosshair follows your finger and raw + mapped coordinates go
 *      to Serial. Four corner targets show how far off calibration is.
 *
 *   3. IS THE SD CARD REACHABLE?  Checked once at boot, reported on Serial.
 *      NOTE: on a Mega this shield's microSD slot is NOT wired to the Mega's
 *      SPI pins - DIYables say so themselves. Without three jumpers it will
 *      fail here, and that is expected rather than a fault. See README.
 *
 * Serial monitor: 115200 baud.
 */

#include <DIYables_TFT_Touch_Shield.h>
#include <SD.h>

#define SD_CS 10

// RGB565
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_BLUE   0x001F
#define C_YELLOW 0xFFE0
#define C_GREY   0x8410
#define C_DARK   0x18E3

DIYables_TFT_RM68140_Shield rm68140;
DIYables_TFT_HX8357D_Shield hx8357d;

typedef DIYables_TFT_Touch_Shield_Base Panel;

Panel*      panel   = NULL;   // NULL until the user taps a card
const char* drvName = NULL;

// ---------------------------------------------------------------------------

void sdCheck() {
  Serial.println(F("--- SD card ---"));
  pinMode(SD_CS, OUTPUT);
  if (SD.begin(SD_CS)) {
    Serial.println(F("SD.begin() OK. Root listing:"));
    File root = SD.open("/");
    while (true) {
      File e = root.openNextFile();
      if (!e) break;
      Serial.print(F("    "));
      Serial.print(e.name());
      if (e.isDirectory()) Serial.print(F("   <DIR>"));
      else { Serial.print(F("   ")); Serial.print(e.size()); Serial.print(F(" bytes")); }
      Serial.println();
      e.close();
    }
    root.close();
  } else {
    Serial.println(F("SD.begin() FAILED."));
    Serial.println(F("On a Mega this is expected until you jumper the shield's"));
    Serial.println(F("SD lines to the Mega's hardware SPI pins:"));
    Serial.println(F("    Mega 50 (MISO) -> shield D12"));
    Serial.println(F("    Mega 51 (MOSI) -> shield D11"));
    Serial.println(F("    Mega 52 (SCK)  -> shield D13"));
    Serial.println(F("    CS stays on D10 - no wire needed."));
    Serial.println(F("The game runs fine without a card; SD features just switch off."));
  }
  Serial.println();
}

// A full-screen identification card for one candidate driver.
void drawCard(Panel& p, const char* name) {
  p.begin();
  p.setRotation(1);                 // 480x320 landscape, same as the game
  p.fillScreen(C_DARK);

  // Orientation marker. If this block is not in the TOP-LEFT corner the
  // rotation is mirrored or flipped for this driver.
  p.fillRect(0, 0, 44, 30, C_YELLOW);
  p.setTextColor(C_BLACK);
  p.setTextSize(2);
  p.setCursor(6, 8);
  p.print(F("TL"));

  p.setTextColor(C_WHITE);
  p.setTextSize(2);
  p.setCursor(70, 10);
  p.print(F("driver test"));

  p.setTextSize(4);
  p.setCursor(70, 52);
  p.print(name);

  // Colour check. Wrong driver or a BGR/RGB mix-up swaps RED and BLUE.
  const uint16_t sw[3]  = { C_RED, C_GREEN, C_BLUE };
  const char*    lbl[3] = { "RED", "GREEN", "BLUE" };
  for (uint8_t i = 0; i < 3; i++) {
    int16_t x = 40 + i * 140;
    p.fillRect(x, 120, 120, 90, sw[i]);
    p.drawRect(x, 120, 120, 90, C_WHITE);
    p.setTextColor(C_WHITE);
    p.setTextSize(2);
    p.setCursor(x + 12, 222);
    p.print(lbl[i]);
  }

  p.setTextColor(C_YELLOW);
  p.setTextSize(2);
  p.setCursor(40, 262);
  p.print(F("Readable + correct?"));
  p.setTextColor(C_WHITE);
  p.setCursor(40, 288);
  p.print(F("TAP to lock it in"));
}

void drawTouchScreen() {
  panel->fillScreen(C_BLACK);
  panel->setTextColor(C_GREEN);
  panel->setTextSize(3);
  panel->setCursor(10, 8);
  panel->print(F("Driver: "));
  panel->print(drvName);

  panel->setTextColor(C_GREY);
  panel->setTextSize(1);
  panel->setCursor(10, 40);
  panel->print(F("Drag a finger. Coords also go to Serial."));
  panel->setCursor(10, 52);
  panel->print(F("Hit each corner target - the cross should sit on it."));

  // Corner targets to eyeball the calibration against.
  const int16_t cx[4] = { 20, 459, 20, 459 };
  const int16_t cy[4] = { 90, 90, 299, 299 };
  for (uint8_t i = 0; i < 4; i++) {
    panel->drawCircle(cx[i], cy[i], 12, C_YELLOW);
    panel->drawFastHLine(cx[i] - 16, cy[i], 32, C_YELLOW);
    panel->drawFastVLine(cx[i], cy[i] - 16, 32, C_YELLOW);
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }
  Serial.println();
  Serial.println(F("=== Megachess bring-up ==="));
  sdCheck();
  Serial.println(F("--- Display ---"));
  Serial.println(F("Cycling driver cards. TAP the screen on whichever one"));
  Serial.println(F("renders correctly. If neither does, you have the older"));
  Serial.println(F("ILI9488 shield and need library v1.0.0."));
  Serial.println();
}

void loop() {
  static uint8_t  which  = 0;
  static uint32_t shown  = 0;
  static bool     locked = false;
  int x, y;

  // ---- phase 1: identify the driver -------------------------------------
  if (!locked) {
    if (shown == 0 || millis() - shown > 5000) {
      which ^= 1;
      if (which) { drawCard(rm68140, "RM68140"); }
      else       { drawCard(hx8357d, "HX8357D"); }
      shown = millis();
      delay(250);                       // settle before sampling touch
    }

    Panel& live = which ? (Panel&) rm68140 : (Panel&) hx8357d;
    if (live.getTouch(x, y)) {
      panel   = &live;
      drvName = which ? "RM68140" : "HX8357D";
      locked  = true;
      Serial.print(F("Locked driver: "));
      Serial.println(drvName);
      Serial.println(F("Put this in Megachess.ino:"));
      Serial.print(F("    #define MEGACHESS_DRIVER_"));
      Serial.println(drvName);
      Serial.println();
      Serial.println(F("--- Touch ---"));
      delay(400);                       // let the finger lift
      drawTouchScreen();
    }
    return;
  }

  // ---- phase 2: touch test ----------------------------------------------
  static uint32_t lastPrint = 0;
  if (panel->getTouch(x, y)) {
    panel->drawFastHLine(x - 10, y, 21, C_RED);
    panel->drawFastVLine(x, y - 10, 21, C_RED);
    panel->fillCircle(x, y, 2, C_WHITE);

    if (millis() - lastPrint > 120) {
      lastPrint = millis();
      int rx, ry, rz;
      panel->readTouchRaw(rx, ry, rz);
      Serial.print(F("mapped ("));
      Serial.print(x); Serial.print(F(", ")); Serial.print(y);
      Serial.print(F(")   raw (")); Serial.print(rx);
      Serial.print(F(", ")); Serial.print(ry);
      Serial.print(F(")   pressure ")); Serial.println(rz);
    }
  }
}
