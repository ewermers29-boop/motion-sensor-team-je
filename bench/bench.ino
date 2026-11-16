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
const byte TRIG_PIN = A0;
const byte ECHO_PIN = A1;
const unsigned long SENSOR_INTERVAL_MS = 100UL;
const unsigned long ECHO_TIMEOUT_US = 30000UL;
const float MAX_ACTIVE_DISTANCE_CM = 100.0;
const float MOTION_THRESHOLD_CM = 8.0;
const byte MOTION_CONFIRMATION_READINGS = 2;
const float DISTANCE_BASELINE_ADAPTATION = 0.1;
const unsigned long MOTION_TIMEOUT_MS = 3000UL;
const unsigned long FAR_DISTANCE_TIMEOUT_MS = 3000UL;

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
const unsigned long ACTIVITY_REFRESH_MS = 500UL;
const unsigned long GRAPH_REFRESH_MS = 500UL;

unsigned long pressTimes[MAX_TRACKED_PRESS_EVENTS];
byte graphPresses[GRAPH_BUCKETS] = {0};
byte pressHead = 0;
byte pressCount = 0;
byte graphCurrentBucket = 0;
unsigned long startupTime = 0;
unsigned long graphBucketStart = 0;
unsigned long lastSensorRead = 0;
unsigned long lastKeyTime = 0;
unsigned long lastMotionTime = 0;
unsigned long farDistanceSince = 0;
float distanceCm = -1.0;
bool systemActive = false;
bool farDistanceTiming = false;
char lastKey = '-';
bool hasPressedKey = false;
bool petWasReacting = false;
const char *lastDisplayedState = "";
unsigned long lastDisplayedCount = 0;

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

bool captureKeyPress() {
  char key = keypad.getKey();
  if (key == NO_KEY) {
    return false;
  }

  unsigned long now = millis();
  advanceGraphBuckets(now);
  recordKeyPress(key, now);
  Serial.print("Key pressed: ");
  Serial.print(key);
  Serial.print(" | Keys since boot: ");
  Serial.println(keyPressCount);
  return true;
}

void pruneOldPresses(unsigned long now) {
  while (pressCount > 0 &&
         now - pressTimes[pressHead] >= OMNIPRESENT_WINDOW_MS) {
    pressHead = (pressHead + 1) % MAX_TRACKED_PRESS_EVENTS;
    pressCount--;
  }
}

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  return duration ? duration / 58.0 : -1.0;
}

bool detectMotion(float distance) {
  static bool hasBaseline = false;
  static float baselineCm = 0.0;
  static byte changedReadings = 0;

  if (!hasBaseline) {
    baselineCm = distance;
    hasBaseline = true;
    return false;
  }

  float change = distance > baselineCm
    ? distance - baselineCm
    : baselineCm - distance;
  if (change >= MOTION_THRESHOLD_CM) {
    if (changedReadings < MOTION_CONFIRMATION_READINGS) {
      changedReadings++;
    }
    if (changedReadings >= MOTION_CONFIRMATION_READINGS) {
      baselineCm = distance;
      changedReadings = 0;
      return true;
    }
    return false;
  }

  changedReadings = 0;
  baselineCm += (distance - baselineCm) * DISTANCE_BASELINE_ADAPTATION;
  return false;
}

void drawCharacter(const char *state, unsigned long now) {
  bool sleepy = strcmp(state, "Nobody") == 0;
  bool excited = strcmp(state, "Omnipresent") == 0;
  bool reacting = hasPressedKey && now - lastKeyTime < 700UL;

  petDisplay.firstPage();
  do {
    petDisplay.setFont(u8g2_font_6x12_tr);
    int titleWidth = petDisplay.getStrWidth(state);
    petDisplay.drawStr((78 - titleWidth) / 2, 11, state);
    petDisplay.drawStr(0, 27, "US-025A");
    petDisplay.drawLine(79, 16, 79, 63);

    char distanceLabel[8];
    if (distanceCm < 0) {
      strcpy(distanceLabel, "---");
    } else {
      dtostrf(distanceCm, 0, 1, distanceLabel);
    }
    petDisplay.setFont(u8g2_font_9x18B_tr);
    petDisplay.drawStr(0, 52, distanceLabel);
    int labelWidth = petDisplay.getStrWidth(distanceLabel);
    petDisplay.setFont(u8g2_font_5x8_tr);
    petDisplay.drawStr(labelWidth + 2, 52, "cm");

    petDisplay.drawCircle(103, 40, 17);
    if (sleepy) {
      petDisplay.drawLine(95, 37, 99, 37);
      petDisplay.drawLine(107, 37, 111, 37);
      petDisplay.drawStr(119, 26, "Z");
    } else if (excited) {
      petDisplay.drawCircle(97, 35, 3);
      petDisplay.drawCircle(109, 35, 3);
      petDisplay.drawDisc(97, 35, 1);
      petDisplay.drawDisc(109, 35, 1);
    } else {
      petDisplay.drawDisc(97, 36, 2);
      petDisplay.drawDisc(109, 36, 2);
    }

    if (sleepy) {
      petDisplay.drawLine(99, 47, 107, 47);
    } else if (excited) {
      petDisplay.drawLine(96, 45, 103, 51);
      petDisplay.drawLine(103, 51, 110, 45);
    } else {
      petDisplay.drawLine(98, 46, 103, 49);
      petDisplay.drawLine(103, 49, 108, 46);
    }

    if (reacting) {
      petDisplay.drawStr(117, 41, "!");
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

  return state;
}

void setup() {
  Serial.begin(9600);

  display.begin();
  petDisplay.begin();
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  graphBucketStart = millis();
  drawActivity("Move to wake", graphBucketStart);
  lastDisplayedState = "Move to wake";
  Serial.println("Keypad ready!");
}

void loop() {
  unsigned long now = millis();
  advanceGraphBuckets(now);
  bool sensorUpdated = false;
  if (now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    distanceCm = readDistanceCm();
    lastSensorRead = now;
    sensorUpdated = true;
    if (distanceCm >= 0) {
      if (distanceCm > MAX_ACTIVE_DISTANCE_CM) {
        if (!farDistanceTiming) {
          farDistanceTiming = true;
          farDistanceSince = now;
        }
      } else {
        farDistanceTiming = false;
        if (detectMotion(distanceCm)) {
          lastMotionTime = now;
          if (!systemActive) {
            systemActive = true;
            startupTime = now;
            Serial.println("Ultrasonic motion detected; keypad enabled.");
          }
        }
      }
    }
  }
  bool noMotionTimeout = now - lastMotionTime >= MOTION_TIMEOUT_MS;
  bool farDistanceTimeout = farDistanceTiming &&
    now - farDistanceSince >= FAR_DISTANCE_TIMEOUT_MS;
  if (systemActive && (noMotionTimeout || farDistanceTimeout)) {
    systemActive = false;
    keyPressCount = 0;
    pressHead = 0;
    pressCount = 0;
    hasPressedKey = false;
    lastKey = '-';
    Serial.println(farDistanceTimeout
      ? "Distance over 100 cm; keypad disabled."
      : "No ultrasonic motion; keypad disabled.");
  }
  char key = keypad.getKey();
  bool keyPressed = systemActive && key != NO_KEY;

  if (keyPressed) {
    recordKeyPress(key, now);
    Serial.print("Key pressed: ");
    Serial.print(key);
    Serial.print(" | Keys since boot: ");
    Serial.println(keyPressCount);
  }

  now = millis();
  advanceGraphBuckets(now);
  const char *state = updateActivityState(now);
  bool stateChanged = strcmp(state, lastDisplayedState) != 0;
  bool countChanged = keyPressCount != lastDisplayedCount;
  bool reacting = hasPressedKey && now - lastKeyTime < 700UL;
  bool reactionEnded = petWasReacting && !reacting;

  if (keyPressed || stateChanged) {
    drawActivity(state, now);
    lastDisplayedState = state;
  } else if (sensorUpdated || reactionEnded) {
    drawCharacter(state, now);
  }
  petWasReacting = reacting;
}
