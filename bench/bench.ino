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

byte rowPins[ROWS] = {5, 4, 3, 2};
byte colPins[COLS] = {7, 6, 9, 8};

Keypad keypad = Keypad(
  makeKeymap(keys), rowPins, colPins, ROWS, COLS
);

unsigned long keyPressCount = 0;
const byte MAX_TRACKED_PRESS_EVENTS = 12;
const unsigned long VISITOR_WINDOW_MS = 4000UL;
const unsigned long OMNIPRESENT_WINDOW_MS = 5000UL;
const byte GRAPH_BUCKETS = 10;
const byte GRAPH_MAX_PRESSES_PER_SECOND = 5;
const unsigned long GRAPH_BUCKET_MS = 1000UL;
const unsigned long GRAPH_REFRESH_MS = 500UL;

unsigned long pressTimes[MAX_TRACKED_PRESS_EVENTS];
byte graphPresses[GRAPH_BUCKETS] = {0};
byte pressHead = 0;
byte pressCount = 0;
byte graphCurrentBucket = 0;
unsigned long startupTime = 0;
unsigned long graphBucketStart = 0;
unsigned long lastGraphUpdate = 0;
unsigned long lastKeyTime = 0;
char lastKey = '-';
bool nobodyActive = false;
bool hasPressedKey = false;
bool petWasReacting = false;
const char *lastDisplayedState = "";

void recordKeyPress(char key, unsigned long now) {
  if (pressCount == MAX_TRACKED_PRESS_EVENTS) {
    pressTimes[pressHead] = now;
    pressHead = (pressHead + 1) % MAX_TRACKED_PRESS_EVENTS;
  } else {
    byte index = (pressHead + pressCount) % MAX_TRACKED_PRESS_EVENTS;
    pressTimes[index] = now;
    pressCount++;
  }

  if (graphPresses[graphCurrentBucket] < 255) {
    graphPresses[graphCurrentBucket]++;
  }
  keyPressCount++;
  lastKey = key;
  lastKeyTime = now;
  hasPressedKey = true;
}

void advanceGraphBuckets(unsigned long now) {
  unsigned long elapsedBuckets = (now - graphBucketStart) / GRAPH_BUCKET_MS;
  if (elapsedBuckets == 0) {
    return;
  }

  if (elapsedBuckets >= GRAPH_BUCKETS) {
    for (byte i = 0; i < GRAPH_BUCKETS; i++) {
      graphPresses[i] = 0;
    }
    graphCurrentBucket = (graphCurrentBucket + elapsedBuckets) % GRAPH_BUCKETS;
  } else {
    for (unsigned long i = 0; i < elapsedBuckets; i++) {
      graphCurrentBucket = (graphCurrentBucket + 1) % GRAPH_BUCKETS;
      graphPresses[graphCurrentBucket] = 0;
    }
  }
  graphBucketStart += elapsedBuckets * GRAPH_BUCKET_MS;
}

void pruneOldPresses(unsigned long now) {
  while (pressCount > 0 &&
         now - pressTimes[pressHead] >= OMNIPRESENT_WINDOW_MS) {
    pressHead = (pressHead + 1) % MAX_TRACKED_PRESS_EVENTS;
    pressCount--;
  }
}

void drawCharacter() {
  petDisplay.firstPage();
  do {
    const int left = 18;
    const int right = 126;
    const int top = 10;
    const int bottom = 50;
    const int plotWidth = right - left;
    const int plotHeight = bottom - top;

    petDisplay.drawLine(left, top, left, bottom);
    petDisplay.drawLine(left, bottom, right, bottom);
    petDisplay.drawLine(left, top + plotHeight / 2,
                        left + 3, top + plotHeight / 2);
    petDisplay.drawLine(left + plotWidth / 2, bottom - 2,
                        left + plotWidth / 2, bottom + 2);
    petDisplay.drawLine(right, bottom - 2, right, bottom + 2);

    petDisplay.setFont(u8g2_font_5x8_tr);
    petDisplay.drawStr(5, top + 3, "5");
    petDisplay.drawStr(5, bottom + 3, "0");
    petDisplay.drawStr(right - 31, 8, "keys/s");
    petDisplay.drawStr(left - 2, 63, "-10s");
    petDisplay.drawStr(left + plotWidth / 2 - 7, 63, "-5s");
    petDisplay.drawStr(right - 13, 63, "now");

    int previousX = left;
    int previousY = bottom;
    for (byte i = 0; i < GRAPH_BUCKETS; i++) {
      byte index = (graphCurrentBucket + 1 + i) % GRAPH_BUCKETS;
      byte count = graphPresses[index];
      if (count > GRAPH_MAX_PRESSES_PER_SECOND) {
        count = GRAPH_MAX_PRESSES_PER_SECOND;
      }

      int x = left + (int)i * plotWidth / (GRAPH_BUCKETS - 1);
      int y = bottom - (int)count * plotHeight /
              GRAPH_MAX_PRESSES_PER_SECOND;
      if (i > 0) {
        petDisplay.drawLine(previousX, previousY, x, previousY);
        petDisplay.drawLine(x, previousY, x, y);
      }
      petDisplay.drawDisc(x, y, 1);
      previousX = x;
      previousY = y;
    }
  } while (petDisplay.nextPage());
}

void drawActivity(const char *state) {
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
  graphBucketStart = startupTime;
  lastGraphUpdate = startupTime;
  drawActivity("Waiting");
  drawCharacter();
  lastDisplayedState = "Waiting";
  Serial.println("Keypad ready!");
}

void loop() {
  char key = keypad.getKey();
  unsigned long now = millis();
  advanceGraphBuckets(now);
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
    if (stateChanged) {
      Serial.print("State: ");
      Serial.println(state);
    }
    drawActivity(state);
    lastDisplayedState = state;
  }
  if (now - lastGraphUpdate >= GRAPH_REFRESH_MS) {
    drawCharacter();
    lastGraphUpdate = now;
  }
  petWasReacting = reacting;
}
