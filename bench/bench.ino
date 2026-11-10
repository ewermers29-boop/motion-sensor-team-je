#include <Wire.h>
#include <Keypad.h>
#include <U8g2lib.h>
#include <string.h>

U8G2_SSD1306_128X64_NONAME_1_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_1_SW_I2C petDisplay(
  U8G2_R0, 10, 11, U8X8_PIN_NONE
);

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
{'7', '*', '1', '4'},
{'8', '0', '2', '5'},
{'9', '#', '3', '6'},
{'C', 'D', 'A', 'B'}
};
};

byte rowPins[ROWS] = {5, 4, 3, 2};
byte colPins[COLS] = {7, 6, 9, 8};

Keypad keypad = Keypad(
  makeKeymap(keys), rowPins, colPins, ROWS, COLS
);

unsigned long keyPressCount = 0;
const byte MAX_TRACKED_PRESS_EVENTS = 12;
const unsigned long VISITOR_WINDOW_MS = 4000UL;
const unsigned long OMNIPRESENT_WINDOW_MS = 5000UL;

unsigned long pressTimes[MAX_TRACKED_PRESS_EVENTS];
unsigned long pressGaps[MAX_TRACKED_PRESS_EVENTS];
byte pressHead = 0;
byte pressCount = 0;
unsigned long startupTime = 0;
unsigned long lastKeyTime = 0;
char lastKey = '-';
bool nobodyActive = false;
bool hasPressedKey = false;
bool petWasReacting = false;
const char *lastDisplayedState = "";

void recordKeyPress(char key, unsigned long now) {
  unsigned long gap = hasPressedKey ? now - lastKeyTime : 0;

  if (pressCount == MAX_TRACKED_PRESS_EVENTS) {
    pressTimes[pressHead] = now;
    pressGaps[pressHead] = gap;
    pressHead = (pressHead + 1) % MAX_TRACKED_PRESS_EVENTS;
  } else {
    byte index = (pressHead + pressCount) % MAX_TRACKED_PRESS_EVENTS;
    pressTimes[index] = now;
    pressGaps[index] = gap;
    pressCount++;
  }

  keyPressCount++;
  lastKey = key;
  lastKeyTime = now;
  hasPressedKey = true;
}

void pruneOldPresses(unsigned long now) {
  while (pressCount > 0 &&
         now - pressTimes[pressHead] >= OMNIPRESENT_WINDOW_MS) {
    pressHead = (pressHead + 1) % MAX_TRACKED_PRESS_EVENTS;
    pressCount--;
  }
}

void drawCharacter(const char *state, unsigned long now) {
  bool sleepy = strcmp(state, "Nobody") == 0;
  bool excited = strcmp(state, "Omnipresent") == 0;
  bool reacting = hasPressedKey && now - lastKeyTime < 700UL;

  petDisplay.firstPage();
  do {
    petDisplay.setFont(u8g2_font_6x12_tr);
    int titleWidth = petDisplay.getStrWidth(state);
    petDisplay.drawStr((128 - titleWidth) / 2, 11, state);

    petDisplay.drawCircle(64, 39, 23);
    if (sleepy) {
      petDisplay.drawLine(51, 34, 59, 34);
      petDisplay.drawLine(69, 34, 77, 34);
      petDisplay.drawStr(92, 25, "Z");
    } else if (excited) {
      petDisplay.drawCircle(55, 33, 5);
      petDisplay.drawCircle(73, 33, 5);
      petDisplay.drawDisc(55, 33, 2);
      petDisplay.drawDisc(73, 33, 2);
    } else {
      petDisplay.drawDisc(55, 34, 3);
      petDisplay.drawDisc(73, 34, 3);
    }

    if (sleepy) {
      petDisplay.drawLine(59, 49, 69, 49);
    } else if (excited) {
      petDisplay.drawLine(54, 46, 64, 53);
      petDisplay.drawLine(64, 53, 74, 46);
    } else {
      petDisplay.drawLine(57, 47, 64, 51);
      petDisplay.drawLine(64, 51, 71, 47);
    }

    if (reacting) {
      petDisplay.drawStr(91, 39, "!");
    }
    if (hasPressedKey) {
      char keyLabel[] = "Key: X";
      keyLabel[5] = lastKey;
      petDisplay.drawStr(0, 63, keyLabel);
    }
  } while (petDisplay.nextPage());
}

void drawActivity(const char *state, unsigned long now) {
  display.firstPage();
  do {
    display.drawLine(38, 0, 38, 63);
    display.setFont(u8g2_font_9x18B_tr);
    char keyLabel[] = "X";
    keyLabel[0] = lastKey;
    display.drawStr(13, 39, keyLabel);

    display.setFont(u8g2_font_6x12_tr);
    display.drawStr(45, 11, "Count:");
    display.setCursor(45, 22);
    display.print(keyPressCount);
    display.drawStr(45, 36, "State:");
    display.drawStr(45, 51, state);
  } while (display.nextPage());

  drawCharacter(state, now);
}

const char *updateActivityState(unsigned long now) {
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
  return state;
}

void setup() {
  Serial.begin(9600);

  display.begin();
  petDisplay.begin();
  startupTime = millis();
  drawActivity("Waiting", startupTime);
  lastDisplayedState = "Waiting";
  Serial.println("Keypad ready!");
}

void loop() {
  char key = keypad.getKey();
  unsigned long now = millis();
  bool keyPressed = key != NO_KEY;

  if (keyPressed) {
    recordKeyPress(key, now);
    Serial.print("Key pressed: ");
    Serial.print(key);
    Serial.print(" | Keys since boot: ");
    Serial.println(keyPressCount);
  }

  const char *state = updateActivityState(now);
  bool stateChanged = strcmp(state, lastDisplayedState) != 0;
  bool reacting = hasPressedKey && now - lastKeyTime < 700UL;
  bool reactionEnded = petWasReacting && !reacting;

  if (keyPressed || stateChanged || reactionEnded) {
    drawActivity(state, now);
    lastDisplayedState = state;
  }
  petWasReacting = reacting;
}
