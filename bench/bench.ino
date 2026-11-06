#include <Wire.h>
#include <Keypad.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display{SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET};

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'7', '*', '1', '4'},
  {'8', '0', '2', '5'},
  {'9', '#', '3', '6'},
  {'C', 'D', 'A', 'B'}
};

byte rowPins[ROWS] = {5, 4, 3, 2};
byte colPins[COLS] = {7, 6, 9, 8};

Keypad keypad = Keypad(
  makeKeymap(keys), rowPins, colPins, ROWS, COLS
);

unsigned long keyPressCount = 0;
const byte MAX_TRACKED_PRESS_EVENTS = 3;
const unsigned long VISITOR_WINDOW_MS = 4000UL;
const unsigned long OMNIPRESENT_WINDOW_MS = 5000UL;
const unsigned long DISPLAY_REFRESH_MS = 250UL;

unsigned long pressTimes[MAX_TRACKED_PRESS_EVENTS];
byte pressHead = 0;
byte pressCount = 0;
unsigned long startupTime = 0;
unsigned long lastDisplayTime = 0;
char lastKey = '-';
bool nobodyActive = false;

void recordKeyPress(char key, unsigned long now) {
  if (pressCount == MAX_TRACKED_PRESS_EVENTS) {
    pressTimes[pressHead] = now;
    pressHead = (pressHead + 1) % MAX_TRACKED_PRESS_EVENTS;
  } else {
    byte index = (pressHead + pressCount) % MAX_TRACKED_PRESS_EVENTS;
    pressTimes[index] = now;
    pressCount++;
  }

  keyPressCount++;
  lastKey = key;
}

void pruneOldPresses(unsigned long now) {
  while (pressCount > 0 &&
         now - pressTimes[pressHead] >= OMNIPRESENT_WINDOW_MS) {
    pressHead = (pressHead + 1) % MAX_TRACKED_PRESS_EVENTS;
    pressCount--;
  }
}

void drawActivity(unsigned long now) {
  pruneOldPresses(now);

  byte pressesInFourSeconds = 0;
  byte pressesInFiveSeconds = 0;
  for (byte i = 0; i < pressCount; i++) {
    byte index = (pressHead + i) % MAX_TRACKED_PRESS_EVENTS;
    unsigned long age = now - pressTimes[index];
    if (age < VISITOR_WINDOW_MS) {
      pressesInFourSeconds++;
    }
    pressesInFiveSeconds++;
  }

  const char *state;
  bool isNobody = false;
  if (pressesInFiveSeconds > 2) {
    state = "Omnipresent";
  } else if (pressesInFourSeconds > 0) {
    state = "Visitor";
  } else if (now - startupTime >= VISITOR_WINDOW_MS) {
    state = "Nobody";
    isNobody = true;
  } else {
    state = "Waiting";
  }

  if (isNobody && !nobodyActive) {
    keyPressCount = 0;
  }
  nobodyActive = isNobody;

  display.clearDisplay();
  display.drawLine(38, 0, 38, SCREEN_HEIGHT - 1, SSD1306_WHITE);
  display.setTextSize(3);
  display.setCursor(8, 21);
  display.write(lastKey);

  display.setTextSize(1);
  display.setCursor(45, 0);
  display.print("Count:");
  display.setCursor(45, 10);
  display.print(keyPressCount);
  display.setCursor(45, 24);
  display.print("State:");
  display.setCursor(45, 36);
  display.print(state);
  display.display();
}

void setup() {
  Serial.begin(9600);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not detected");
    while (true) {}
  }

  display.setTextColor(SSD1306_WHITE);
  startupTime = millis();
  lastDisplayTime = startupTime;
  drawActivity(startupTime);
  Serial.println("Keypad ready!");
}

void loop() {
  char key = keypad.getKey();
  unsigned long now = millis();

  if (key) {
    recordKeyPress(key, now);
    Serial.print("Key pressed: ");
    Serial.print(key);
    Serial.print(" | Keys since boot: ");
    Serial.println(keyPressCount);
    drawActivity(now);
    lastDisplayTime = now;
  } else if (now - lastDisplayTime >= DISPLAY_REFRESH_MS) {
    drawActivity(now);
    lastDisplayTime = now;
  }
}
