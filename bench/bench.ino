// 4x4 keypad on an Arduino Nano, with the last pressed key shown on an
// SSD1306 128x64 I2C OLED (0x3C).
//
// Keypad rows -> D2, D3, D4, D5; columns -> D6, D7, D8, D9.
// OLED SDA -> A4; SCL -> A5. Needs the U8g2 library.
//
// Wire the keypad rows in this order:
//   1 2 3 A
//   4 5 6 B
//   7 8 9 C
//   * 0 # D

#include <U8g2lib.h>
#include <Wire.h>

U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

const uint8_t ROW_PINS[4] = {2, 3, 4, 5};
const uint8_t COL_PINS[4] = {6, 7, 8, 9};
const char KEYS[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
const unsigned long DEBOUNCE_MS = 25;

char candidateKey = '\0';
char stableKey = '\0';
char lastPressedKey = '\0';
unsigned long candidateSince = 0;

char scanKeypad() {
  for (uint8_t row = 0; row < 4; row++) {
    digitalWrite(ROW_PINS[row], LOW);
    for (uint8_t col = 0; col < 4; col++) {
      if (digitalRead(COL_PINS[col]) == LOW) {
        digitalWrite(ROW_PINS[row], HIGH);
        return KEYS[row][col];
      }
    }
    digitalWrite(ROW_PINS[row], HIGH);
  }
  return '\0';
}

void drawScreen() {
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_6x12_tr);
    oled.drawStr(0, 12, "LAST PRESSED KEY");

    if (lastPressedKey == '\0') {
      const char *prompt = "Press a keypad button";
      oled.setFont(u8g2_font_6x12_tr);
      int width = oled.getStrWidth(prompt);
      oled.drawStr((128 - width) / 2, 43, prompt);
    } else {
      char keyText[] = {lastPressedKey, '\0'};
      oled.setFont(u8g2_font_fub30_tr);
      int width = oled.getStrWidth(keyText);
      oled.drawStr((128 - width) / 2, 55, keyText);
    }
  } while (oled.nextPage());
}

void setup() {
  for (uint8_t row = 0; row < 4; row++) {
    pinMode(ROW_PINS[row], OUTPUT);
    digitalWrite(ROW_PINS[row], HIGH);
  }
  for (uint8_t col = 0; col < 4; col++) {
    pinMode(COL_PINS[col], INPUT_PULLUP);
  }

  oled.begin();
  drawScreen();
}

void loop() {
  char reading = scanKeypad();
  unsigned long now = millis();

  if (reading != candidateKey) {
    candidateKey = reading;
    candidateSince = now;
  }

  if (now - candidateSince >= DEBOUNCE_MS && candidateKey != stableKey) {
    stableKey = candidateKey;
    if (stableKey != '\0') {
      lastPressedKey = stableKey;
      drawScreen();
    }
  }
}
