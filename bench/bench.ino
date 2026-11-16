#include <Wire.h>
#include <Keypad.h>
#include <U8g2lib.h>
#include <stdio.h>
#include <string.h>

U8G2_SSD1306_128X64_NONAME_1_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_1_SW_I2C petDisplay(
  U8G2_R0, 10, 11, U8X8_PIN_NONE
);

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'0', '*', 'D', '#'},
  {'8', '7', 'C', '9'},
  {'2', '1', 'A', '3'},
  {'5', '4', 'B', '6'}
};

byte rowPins[ROWS] = {5, 4, 3, 2};
byte colPins[COLS] = {7, 6, 9, 8};

Keypad keypad = Keypad(
  makeKeymap(keys), rowPins, colPins, ROWS, COLS
);

const byte TRIG_PIN = A1;
const byte ECHO_PIN = A0;
const unsigned long ECHO_TIMEOUT_US = 2000UL;
const unsigned long SENSOR_REFRESH_MS = 100UL;
const unsigned long STATUS_DISPLAY_REFRESH_MS = 1000UL;
const unsigned long SENSOR_DEBUG_REFRESH_MS = 1000UL;
const float MAX_DETECTION_DISTANCE_CM = 12.0;
const byte NO_DETECTION_READINGS_TO_FLAG = 3;

unsigned long keyPressCount = 0;
const byte MAX_TRACKED_PRESS_EVENTS = 12;
const unsigned long VISITOR_WINDOW_MS = 4000UL;
const unsigned long OMNIPRESENT_WINDOW_MS = 5000UL;
const unsigned long ACTIVITY_REFRESH_MS = 500UL;

unsigned long pressTimes[MAX_TRACKED_PRESS_EVENTS];
byte pressHead = 0;
byte pressCount = 0;
unsigned long startupTime = 0;
unsigned long lastActivityUpdate = 0;
unsigned long lastSensorRead = 0;
unsigned long lastStatusDisplay = 0;
unsigned long lastSensorDebug = 0;
unsigned long lastKeyTime = 0;
char lastKey = '-';
bool hasPressedKey = false;
bool petWasReacting = false;
const char *lastDisplayedState = "";
unsigned long lastDisplayedCount = 0;
const char *machineStatus = "CHECKING";
byte consecutiveNoDetectionReadings = 0;
unsigned long latestEchoDurationUs = 0;
float latestDistanceCm = 0;
bool latestObjectDetected = false;

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
  lastKeyTime = now;
  hasPressedKey = true;
}

bool captureKeyPress(bool sensorResponsive) {
  char key = keypad.getKey();
  if (key == NO_KEY || !sensorResponsive) {
    return false;
  }

  unsigned long now = millis();
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

void drawMachineStatus() {
  char detail[20];
  if (latestEchoDurationUs > 0) {
    if (latestObjectDetected) {
      snprintf(detail, sizeof(detail), "Distance: %d cm",
               (int)(latestDistanceCm + 0.5));
    } else {
      snprintf(detail, sizeof(detail), "Too far: %d cm",
               (int)(latestDistanceCm + 0.5));
    }
  } else if (strcmp(machineStatus, "CHECKING") == 0) {
    snprintf(detail, sizeof(detail), "Waiting for echo");
  } else {
    snprintf(detail, sizeof(detail), "No echo detected");
  }

  petDisplay.firstPage();
  do {
    petDisplay.setFont(u8g2_font_6x12_tr);
    petDisplay.drawStr(32, 13, "MACHINE STATUS");
    petDisplay.setFont(u8g2_font_9x18B_tr);
    int statusWidth = petDisplay.getStrWidth(machineStatus);
    petDisplay.drawStr((128 - statusWidth) / 2, 40, machineStatus);
    petDisplay.setFont(u8g2_font_5x8_tr);
    int detailWidth = petDisplay.getStrWidth(detail);
    petDisplay.drawStr((128 - detailWidth) / 2, 58, detail);
  } while (petDisplay.nextPage());
}

void clearActivityScreen() {
  display.firstPage();
  do {
    display.setDrawColor(0);
    display.drawBox(0, 0, 128, 64);
    display.setDrawColor(1);
  } while (display.nextPage());
}

void updateMachineStatus(unsigned long now) {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  latestEchoDurationUs = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  bool echoReceived = latestEchoDurationUs > 0;
  latestObjectDetected = false;
  const char *nextStatus = machineStatus;
  if (echoReceived) {
    latestDistanceCm = latestEchoDurationUs / 58.0;
    latestObjectDetected =
      latestDistanceCm <= MAX_DETECTION_DISTANCE_CM;
  }
  if (latestObjectDetected) {
    consecutiveNoDetectionReadings = 0;
    nextStatus = "RESPONSIVE";
  } else if (consecutiveNoDetectionReadings <
             NO_DETECTION_READINGS_TO_FLAG) {
    consecutiveNoDetectionReadings++;
    if (consecutiveNoDetectionReadings >= NO_DETECTION_READINGS_TO_FLAG) {
      nextStatus = "UNRESPONSIVE";
    }
  }

  bool statusChanged = strcmp(nextStatus, machineStatus) != 0;
  if (statusChanged) {
    machineStatus = nextStatus;
    if (strcmp(machineStatus, "RESPONSIVE") != 0) {
      clearActivityScreen();
    }
    Serial.print("Machine: ");
    Serial.println(machineStatus);
  }
  if (statusChanged || now - lastStatusDisplay >= STATUS_DISPLAY_REFRESH_MS) {
    drawMachineStatus();
    lastStatusDisplay = now;
  }
  if (now - lastSensorDebug >= SENSOR_DEBUG_REFRESH_MS) {
    Serial.print("Ultrasonic echo: ");
    Serial.print(latestEchoDurationUs);
    Serial.print(" us");
    if (echoReceived) {
      Serial.print(" | ");
      Serial.print(latestDistanceCm, 1);
      Serial.print(" cm | ");
      Serial.println(latestObjectDetected ? "target detected" : "too far");
    } else {
      Serial.println(" | no echo");
    }
    lastSensorDebug = now;
  }
  lastSensorRead = now;
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

  return state;
}

void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  display.begin();
  petDisplay.begin();
  clearActivityScreen();
  startupTime = millis();
  lastActivityUpdate = startupTime;
  lastStatusDisplay = startupTime;
  lastSensorDebug = startupTime;
  lastSensorRead = startupTime - SENSOR_REFRESH_MS;
  drawMachineStatus();
  Serial.println("Keypad ready!");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSensorRead >= SENSOR_REFRESH_MS) {
    updateMachineStatus(now);
  }

  bool sensorResponsive = latestObjectDetected;
  bool keyPressed = captureKeyPress(sensorResponsive);
  if (!sensorResponsive) {
    return;
  }

  now = millis();
  const char *state = updateActivityState(now);
  bool stateChanged = strcmp(state, lastDisplayedState) != 0;
  bool countChanged = keyPressCount != lastDisplayedCount;
  bool reacting = hasPressedKey && now - lastKeyTime < 700UL;
  bool reactionEnded = petWasReacting && !reacting;

  if (keyPressed || countChanged || stateChanged || reactionEnded ||
      now - lastActivityUpdate >= ACTIVITY_REFRESH_MS) {
    if (stateChanged) {
      Serial.print("State: ");
      Serial.println(state);
    }
    drawActivity(state);
    lastDisplayedState = state;
    lastDisplayedCount = keyPressCount;
    lastActivityUpdate = now;
  }
  petWasReacting = reacting;
}
