#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SimpleDHT.h>
#include <RTClib.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// =====================================================
// LCD
// =====================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

// =====================================================
// SH1106 OLED
// 0.96 inch 128x64
//
// SDA -> A4
// SCL -> A5
// VCC -> 5V
// GND -> GND
// =====================================================

#define OLED_ADDRESS 0x3C

// SH1106 normally has a 2-column offset
#define OLED_COLUMN_OFFSET 2

// Arduino Wire buffer is limited, so send 16 bytes
// at a time instead of sending the entire page.
#define OLED_CHUNK_SIZE 16

// Only ONE page (128 bytes) is stored.
// This keeps RAM usage low.
byte oledBuffer[128];
unsigned long lastOLEDUpdate = 0;
byte faceFrame = 0;
const unsigned long FACE_FRAME_TIME = 250;

// =====================================================
// LCD / OTHER PINS
// =====================================================

const int trigPin = 9;
const int echoPin = 10;
const int touchPin = 2;
const int pirPin = 4;
const int ledPin = 5;
const int resetBtn = 6;
const int servoPin = 8;
const int servoPin2 = 7;       // second servo, mirrors the first
const int lockServoPin = 13;   // phone lock compartment servo
const int micPin = A0;
const int ldrPin = A1;

// =====================================================
// DHT11
// =====================================================

#define DHTPIN 3
SimpleDHT11 dht11(DHTPIN);

// =====================================================
// SERVO
// =====================================================

Servo myServo;
Servo myServo2;    // second servo object
Servo lockServo;   // phone lock compartment servo

// Phone lock compartment angles
const int LOCK_FOCUS_ANGLE = 120;   // locked during focus
const int LOCK_BREAK_ANGLE = 0;     // unlocked during break

// Ear positions (both servos move together)
const int EAR_STARTUP_ANGLE = 90;    // position right after power-on
const int EAR_FOCUS_ANGLE = 70;      // straight, during focus mode
const int EAR_BREAK_ANGLE = 20;      // droopy, during break mode
const int EAR_SHAKE_ANGLE_A = 40;    // "too close" shake, position A
const int EAR_SHAKE_ANGLE_B = 100;   // "too close" shake, position B

// Tracks which shake position to use next so the ears
// alternate back and forth while the user is too close.
bool earShakeToggle = false;

// =====================================================
// RTC
// =====================================================

RTC_DS3231 rtc;

// =====================================================
// DFPLAYER
// =====================================================

SoftwareSerial mp3Serial(11, 12);
DFRobotDFPlayerMini player;
bool audioReady = false;

// =====================================================
// SYSTEM STATE
// =====================================================

bool isRunning = true;
int displayMode = 0;

// =====================================================
// POMODORO
// =====================================================

int workMode = 0;
bool pomodoroRunning = false;
bool isBreakMode = false;
unsigned long pomodoroStartTime = 0;
unsigned long pomodoroDuration = 0;
unsigned long lastPomodoroDisplay = 0;
int pomodoroSessionCount = 0;
unsigned long focusDurations[] = {0, 25UL * 60UL * 1000UL, 50UL * 60UL * 1000UL,
                                  1UL * 60UL * 1000UL};
unsigned long breakDurations[] = {0, 5UL * 60UL * 1000UL, 10UL * 60UL * 1000UL,
                                  1UL * 60UL * 1000UL};

// =====================================================
// SLEEP
// =====================================================

unsigned long pausedStartTime = 0;
bool sleepMode = false;
bool pausedDrawn = false;

// =====================================================
// HYDRATION
// =====================================================

unsigned long lastHydrationTime = 0;
const unsigned long hydrationInterval = 1UL * 60UL * 1000UL;

// =====================================================
// LIGHT
// =====================================================

bool autoMode = true;
int manualBrightness = 0;

// =====================================================
// TIMING
// =====================================================

unsigned long lastCheck = 0;
unsigned long lastDHTRead = 0;
unsigned long tempDisplayStart = 0;

// =====================================================
// DHT VALUES
// =====================================================

byte temperature = 0;
byte humidity = 0;

// =====================================================
// TOUCH
// =====================================================

bool lastTouch = LOW;
bool longPressHandled = false;
unsigned long touchStart = 0;
unsigned long lastTapTime = 0;
int tapCount = 0;

// =====================================================
// PIR / AWAY
// =====================================================

unsigned long lastMotionSeen = 0;
unsigned long awayPromptStart = 0;
bool awayPromptActive = false;
bool autoPausedByAway = false;
const unsigned long noMotionLimit = 30UL * 1000UL;
const unsigned long awayPromptDuration = 10UL * 1000UL;

// =====================================================
// NOISE
// =====================================================

unsigned long lastNoiseCheck = 0;
unsigned long lastNoiseDisplay = 0;
int noiseThreshold = 350;

// =====================================================
// POSTURE
// =====================================================

unsigned long lastTooCloseAudio = 0;

// =====================================================
// RTC
// =====================================================

int lastDay = -1;

// =====================================================
// OLED COMMAND
// =====================================================

void oledCommand(byte command) {
  Wire.beginTransmission(OLED_ADDRESS);
  Wire.write(0x00);
  Wire.write(command);
  Wire.endTransmission();
}

// =====================================================
// OLED INITIALIZATION
// =====================================================

void oledInit() {
  delay(100);
  oledCommand(0xAE);
  oledCommand(0xD5);
  oledCommand(0x80);
  oledCommand(0xA8);
  oledCommand(0x3F);
  oledCommand(0xD3);
  oledCommand(0x00);
  oledCommand(0x40);
  oledCommand(0x8D);
  oledCommand(0x14);
  oledCommand(0x20);
  oledCommand(0x02);
  oledCommand(0xA1);
  oledCommand(0xC8);
  oledCommand(0xDA);
  oledCommand(0x12);
  oledCommand(0x81);
  oledCommand(0xBF);
  oledCommand(0xD9);
  oledCommand(0x22);
  oledCommand(0xDB);
  oledCommand(0x40);
  oledCommand(0xA4);
  oledCommand(0xA6);
  oledCommand(0xAF);
  delay(100);
}

// =====================================================
// CLEAR OLED BUFFER
// =====================================================

void oledClearBuffer() {
  for (byte i = 0; i < 128; i++) {
    oledBuffer[i] = 0;
  }
}

// =====================================================
// SET OLED PIXEL
// =====================================================

void oledPixel(byte x, byte y, byte page) {
  if (x >= 128 || y >= 64)
    return;
  if ((y / 8) != page)
    return;
  oledBuffer[x] |= (1 << (y & 7));
}

// =====================================================
// HORIZONTAL LINE
// =====================================================

void oledHLine(byte x1, byte x2, byte y, byte page) {
  if (x1 > x2) {
    byte t = x1;
    x1 = x2;
    x2 = t;
  }
  for (byte x = x1; x <= x2; x++) {
    oledPixel(x, y, page);
  }
}

// =====================================================
// VERTICAL LINE
// =====================================================

void oledVLine(byte x, byte y1, byte y2, byte page) {
  if (y1 > y2) {
    byte t = y1;
    y1 = y2;
    y2 = t;
  }
  for (byte y = y1; y <= y2; y++) {
    oledPixel(x, y, page);
  }
}

// =====================================================
// FILLED RECTANGLE
// =====================================================

void oledFillRect(byte x, byte y, byte w, byte h, byte page) {
  for (byte yy = y; yy < y + h; yy++) {
    for (byte xx = x; xx < x + w; xx++) {
      oledPixel(xx, yy, page);
    }
  }
}

// =====================================================
// OUTLINE RECTANGLE
// =====================================================

void oledRect(byte x, byte y, byte w, byte h, byte page) {
  oledHLine(x, x + w - 1, y, page);
  oledHLine(x, x + w - 1, y + h - 1, page);
  oledVLine(x, y, y + h - 1, page);
  oledVLine(x + w - 1, y, y + h - 1, page);
}

// =====================================================
// ROUNDED RECTANGLE
// =====================================================
//
// Approximation using horizontal/vertical lines.
// It uses very little memory.
// =====================================================

void oledRoundedRect(byte x, byte y, byte w, byte h, byte page) {
  // Top
  oledHLine(x + 4, x + w - 5, y, page);
  oledHLine(x + 2, x + w - 3, y + 1, page);
  oledHLine(x + 1, x + w - 2, y + 2, page);
  oledHLine(x, x + w - 1, y + 3, page);

  // Bottom
  oledHLine(x + 4, x + w - 5, y + h - 1, page);
  oledHLine(x + 2, x + w - 3, y + h - 2, page);
  oledHLine(x + 1, x + w - 2, y + h - 3, page);
  oledHLine(x, x + w - 1, y + h - 4, page);

  // Left
  oledVLine(x, y + 4, y + h - 5, page);

  // Right
  oledVLine(x + w - 1, y + 4, y + h - 5, page);
}

// =====================================================
// ROBOT ANTENNAE
// =====================================================

void drawAntennae(byte page) {
  // LEFT antenna
  oledHLine(35, 38, 8, page);
  oledHLine(33, 40, 7, page);
  oledHLine(34, 39, 6, page);
  oledHLine(35, 38, 5, page);

  // left diagonal
  for (byte i = 0; i < 14; i++) {
    oledPixel(39 + i, 8 + i, page);
  }

  // RIGHT antenna
  oledHLine(89, 92, 8, page);
  oledHLine(87, 94, 7, page);
  oledHLine(88, 93, 6, page);
  oledHLine(89, 92, 5, page);

  // right diagonal
  for (byte i = 0; i < 14; i++) {
    oledPixel(88 - i, 8 + i, page);
  }
}

// =====================================================
// ROBOT FACE: EYES OPEN
// =====================================================

void drawRobotEyesOpen(byte page) {
  // Left square eye
  oledFillRect(35, 22, 15, 13, page);

  // Right square eye
  oledFillRect(78, 22, 15, 13, page);
}

// =====================================================
// ROBOT FACE: EYES CLOSING
// =====================================================

void drawRobotEyesClosing(byte page) {
  // Left eye becomes thin
  oledFillRect(35, 27, 15, 3, page);

  // Right eye becomes thin
  oledFillRect(78, 27, 15, 3, page);
}

// =====================================================
// ROBOT FACE: EYES CLOSED
// =====================================================

void drawRobotEyesClosed(byte page) {
  oledHLine(35, 50, 28, page);
  oledHLine(78, 93, 28, page);
}

// =====================================================
// ROBOT FACE: EYES OPENING
// =====================================================

void drawRobotEyesOpening(byte page) {
  // Small eyes
  oledFillRect(35, 26, 15, 5, page);
  oledFillRect(78, 26, 15, 5, page);
}

// =====================================================
// ROBOT MOUTH
// =====================================================

void drawRobotMouth(byte page) {
  // Main rectangular mouth
  oledFillRect(50, 42, 28, 10, page);

  // Small lower detail
  oledFillRect(58, 53, 12, 4, page);
}

// =====================================================
// DRAW NORMAL ROBOT
// =====================================================

void oledDrawNormalFace(byte page, byte frame) {
  oledClearBuffer();

  // ---------------------------------------------------
  // Robot head outline
  // ---------------------------------------------------

  oledRoundedRect(22, 14, 84, 45, page);

  // ---------------------------------------------------
  // Antennae
  // ---------------------------------------------------

  drawAntennae(page);

  // ---------------------------------------------------
  // Eyes
  // ---------------------------------------------------

  if (frame == 0) {
    // Eyes OPEN
    drawRobotEyesOpen(page);
  } else if (frame == 1) {
    // Eyes CLOSING
    drawRobotEyesClosing(page);
  } else if (frame == 2) {
    // Eyes CLOSED
    drawRobotEyesClosed(page);
  } else {
    // Eyes OPENING
    drawRobotEyesOpening(page);
  }

  // ---------------------------------------------------
  // Mouth
  // ---------------------------------------------------

  drawRobotMouth(page);
}

// =====================================================
// SLEEPY ROBOT FACE
// =====================================================

void oledDrawSleepyFace(byte page, byte frame) {
  oledClearBuffer();

  // ---------------------------------------------------
  // Same robot head
  // ---------------------------------------------------

  oledRoundedRect(22, 14, 84, 45, page);

  // ---------------------------------------------------
  // Antennae
  // ---------------------------------------------------

  drawAntennae(page);

  // ---------------------------------------------------
  // DROOPY LEFT EYE
  // ---------------------------------------------------

  if (frame == 0) {
    oledHLine(34, 51, 25, page);
    oledHLine(37, 48, 28, page);
  } else {
    oledHLine(34, 51, 28, page);
  }

  // ---------------------------------------------------
  // DROOPY RIGHT EYE
  // ---------------------------------------------------

  if (frame == 0) {
    oledHLine(77, 94, 25, page);
    oledHLine(80, 91, 28, page);
  } else {
    oledHLine(77, 94, 28, page);
  }

  // ---------------------------------------------------
  // SLEEPY MOUTH
  // ---------------------------------------------------

  oledFillRect(51, 43, 26, 6, page);

  // ---------------------------------------------------
  // ZZZ
  // ---------------------------------------------------

  if (frame == 1) {
    oledHLine(98, 105, 22, page);
    oledHLine(100, 103, 24, page);
    oledHLine(98, 105, 26, page);
  }
}

// =====================================================
// SEND OLED PAGE
// =====================================================

void oledSendPage(byte page) {
  oledCommand(0xB0 + page);
  oledCommand(0x00 + OLED_COLUMN_OFFSET);
  oledCommand(0x10 + (OLED_COLUMN_OFFSET >> 4));

  // Send 128 bytes in 16-byte chunks
  for (byte start = 0; start < 128; start += OLED_CHUNK_SIZE) {
    Wire.beginTransmission(OLED_ADDRESS);
    Wire.write(0x40);
    for (byte i = 0; i < OLED_CHUNK_SIZE; i++) {
      Wire.write(oledBuffer[start + i]);
    }
    Wire.endTransmission();
  }
}

// =====================================================
// SHOW NORMAL FACE
// =====================================================

void oledShowNormalFace(byte frame) {
  for (byte page = 0; page < 8; page++) {
    oledDrawNormalFace(page, frame);
    oledSendPage(page);
  }
}

// =====================================================
// SHOW SLEEP FACE
// =====================================================

void oledShowSleepFace(byte frame) {
  for (byte page = 0; page < 8; page++) {
    oledDrawSleepyFace(page, frame);
    oledSendPage(page);
  }
}

// =====================================================
// OLED ANIMATION
// =====================================================
//
// Normal:
// 0 = eyes open
// 1 = eyes closing
// 2 = eyes closed
// 3 = eyes opening
//
// Sleep:
// 0/1 = droopy animation
// =====================================================

void updateOLED() {
  if (millis() - lastOLEDUpdate < FACE_FRAME_TIME) {
    return;
  }
  lastOLEDUpdate = millis();
  if (sleepMode) {
    // Sleep animation
    faceFrame++;
    if (faceFrame >= 2) {
      faceFrame = 0;
    }
    oledShowSleepFace(faceFrame);
    return;
  }

  // ---------------------------------------------------
  // Normal robot blinking
  // ---------------------------------------------------

  faceFrame++;
  if (faceFrame >= 4) {
    faceFrame = 0;
  }
  oledShowNormalFace(faceFrame);
}

// =====================================================
// SERVO HELPER
// =====================================================
//
// Writes the same angle to BOTH servos so the second
// servo (D7) always mirrors the first servo (D8)
// without having to duplicate every call site.
// =====================================================

void setServos(int angle) {
  myServo.write(angle);
  myServo2.write(angle);
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  // ---------------------------------------------------
  // I2C
  // ---------------------------------------------------

  Wire.begin();
  Wire.setClock(400000);

  // ---------------------------------------------------
  // OLED
  // ---------------------------------------------------

  oledInit();
  oledShowNormalFace(0);

  // ---------------------------------------------------
  // LCD
  // ---------------------------------------------------

  lcd.begin(16, 2);
  lcd.backlight();

  // ---------------------------------------------------
  // PINS
  // ---------------------------------------------------

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(touchPin, INPUT);
  pinMode(pirPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(resetBtn, INPUT_PULLUP);
  pinMode(micPin, INPUT);

  // ---------------------------------------------------
  // SERVO
  // ---------------------------------------------------

  myServo.attach(servoPin);
  myServo2.attach(servoPin2);
  lockServo.attach(lockServoPin);

  // Ears move to their startup position once, at boot.
  setServos(EAR_STARTUP_ANGLE);

  // Lock compartment starts unlocked, so the reset button
  // also works as an emergency unlock.
  lockServo.write(LOCK_BREAK_ANGLE);

  // ---------------------------------------------------
  // RTC
  // ---------------------------------------------------

  rtc.begin();

  // ---------------------------------------------------
  // DFPLAYER
  // ---------------------------------------------------

  mp3Serial.begin(9600);
  if (player.begin(mp3Serial)) {
    audioReady = true;
    player.volume(25);
  }

  // ---------------------------------------------------
  // RTC INITIAL SET
  // ---------------------------------------------------
  //
  // Set the RTC to the compile time only if it has lost
  // power (otherwise every reset would roll the clock
  // back to the upload time).
  // ---------------------------------------------------

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // ---------------------------------------------------
  // STARTUP
  // ---------------------------------------------------

  lcd.print("Starting...");
  playAudio(3);
  delay(2500);
  lcd.clear();
  DateTime now = rtc.now();
  lastDay = now.day();
  lastHydrationTime = millis();
  lastMotionSeen = millis();

  // Initial face
  oledShowNormalFace(0);
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  // ---------------------------------------------------
  // OLED
  // ---------------------------------------------------

  updateOLED();

  // ---------------------------------------------------
  // RESET BUTTON
  // ---------------------------------------------------

  handleResetButton();

  // ---------------------------------------------------
  // RTC
  // ---------------------------------------------------

  handleRTC();

  // ---------------------------------------------------
  // TOUCH
  // ---------------------------------------------------

  handleTouch();

  // ---------------------------------------------------
  // MOTION
  // ---------------------------------------------------

  handleMotionMonitor();

  // ---------------------------------------------------
  // HYDRATION
  // ---------------------------------------------------

  handleHydrationReminder();

  // ---------------------------------------------------
  // POMODORO
  // ---------------------------------------------------

  if (pomodoroRunning) {
    handlePomodoro();
    handleDHT();
    handleLighting();
    handleTemporaryDisplays();
    return;
  }

  // ---------------------------------------------------
  // PAUSED / SLEEP
  // ---------------------------------------------------

  if (!isRunning) {
    handlePausedIdle();
    return;
  }

  // ---------------------------------------------------
  // NORMAL
  // ---------------------------------------------------

  handleNoise();
  handlePosture();
  handleDHT();
  handleLighting();
  handleClock();
  handleTemporaryDisplays();
}

// =====================================================
// AUDIO
// =====================================================

void playAudio(int fileNumber) {
  if (audioReady) {
    player.playMp3Folder(fileNumber);
  }
}

void playBeepOnce() {
  playAudio(1);
}

void playBeepTwice() {
  playAudio(1);
  delay(1600);
  playAudio(1);
}

// =====================================================
// RESET
// =====================================================

void handleResetButton() {
  if (digitalRead(resetBtn) == LOW) {
    lcd.clear();
    lcd.print("System");
    lcd.setCursor(0, 1);
    lcd.print("Resetting...");
    delay(2000);
    void (*resetFunc)(void) = 0;
    resetFunc();
  }
}

// =====================================================
// RTC
// =====================================================

void handleRTC() {
  DateTime now = rtc.now();
  if (now.day() != lastDay) {
    pomodoroSessionCount = 0;
    lastDay = now.day();
  }
}

// =====================================================
// MOTION MONITOR
// =====================================================

void handleMotionMonitor() {
  bool motionDetected = digitalRead(pirPin) == HIGH;
  if (motionDetected) {
    lastMotionSeen = millis();

    // Wake from automatic away mode
    if (autoPausedByAway && !pomodoroRunning) {
      autoPausedByAway = false;
      awayPromptActive = false;
      isRunning = true;
      sleepMode = false;
      pausedDrawn = false;
      displayMode = 0;
      lcd.clear();
      lcd.print("Welcome Back");
      playAudio(4);
      delay(2000);
      lcd.clear();
    }
    return;
  }
  if (!isRunning || pomodoroRunning) {
    return;
  }

  // Automatically pause after 30 seconds
  // without motion
  if (millis() - lastMotionSeen >= noMotionLimit) {
    isRunning = false;
    autoPausedByAway = true;
    awayPromptActive = true;
    awayPromptStart = millis();
    sleepMode = false;
    pausedDrawn = true;
    pausedStartTime = millis();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Are you away?");
    lcd.setCursor(0, 1);
    lcd.print("No motion");
  }
}

// =====================================================
// HYDRATION
// =====================================================

void handleHydrationReminder() {
  if (!isRunning && !pomodoroRunning) {
    return;
  }
  if (displayMode != 0) {
    return;
  }
  if (millis() - lastHydrationTime >= hydrationInterval) {
    lastHydrationTime = millis();
    displayMode = 5;
    tempDisplayStart = millis();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Drink Water");
    lcd.setCursor(0, 1);
    lcd.print("Hydration Time");
    playAudio(5);
  }
}

// =====================================================
// TOUCH
// =====================================================

void handleTouch() {
  bool current = digitalRead(touchPin);
  if (current && !lastTouch) {
    touchStart = millis();
    longPressHandled = false;
  }
  if (current && !longPressHandled && millis() - touchStart > 1200) {
    longPressHandled = true;
    tapCount = 0;
    handleLongPress();
  }
  if (!current && lastTouch) {
    unsigned long duration = millis() - touchStart;
    if (!longPressHandled && duration < 800) {
      tapCount++;
      lastTapTime = millis();
    }
  }
  if (tapCount > 0 && millis() - lastTapTime > 400) {
    handleTap(tapCount);
    tapCount = 0;
  }
  lastTouch = current;
}

// =====================================================
// LONG PRESS
// =====================================================

void handleLongPress() {
  if (pomodoroRunning)
    return;
  if (isRunning)
    return;
  workMode++;
  if (workMode > 3) {
    workMode = 0;
  }
  pomodoroSessionCount = 0;
  isBreakMode = false;
  sleepMode = false;
  pausedStartTime = millis();
  pausedDrawn = false;
}

// =====================================================
// TAP
// =====================================================

void handleTap(int count) {
  // ---------------------------------------------------
  // SINGLE TAP
  // ---------------------------------------------------

  if (count == 1) {
    if (pomodoroRunning) {
      stopPomodoro();
      return;
    }
    if (workMode == 0) {
      isRunning = !isRunning;
      lcd.clear();
      if (!isRunning) {
        autoPausedByAway = false;
        awayPromptActive = false;
        pausedStartTime = millis();
        sleepMode = false;
        pausedDrawn = false;
      } else {
        sleepMode = false;
        pausedDrawn = false;
        lcd.print("Resuming...");
        playAudio(4);
        delay(2000);
        lcd.clear();
        lastMotionSeen = millis();
      }
    } else {
      startPomodoro();
    }
  }

  // ---------------------------------------------------
  // DOUBLE TAP
  // ---------------------------------------------------

  else if (count == 2) {
    autoMode = !autoMode;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(autoMode ? "Auto Light" : "Manual Light");
    delay(800);
    lcd.clear();
    if (!isRunning)
      pausedDrawn = false;
  }

  // ---------------------------------------------------
  // TRIPLE TAP
  // ---------------------------------------------------

  else if (count == 3) {
    if (!autoMode) {
      if (manualBrightness == 0) {
        manualBrightness = 80;
      } else if (manualBrightness == 80) {
        manualBrightness = 180;
      } else {
        manualBrightness = 0;
      }
      lcd.clear();
      lcd.print("Brightness:");
      lcd.setCursor(0, 1);
      lcd.print(manualBrightness);
      delay(800);
      lcd.clear();
      if (!isRunning)
        pausedDrawn = false;
    }
  }

  // ---------------------------------------------------
  // FOUR TAP
  // ---------------------------------------------------

  else if (count == 4) {
    showHumidity();
    if (!isRunning) {
      delay(2000);
      lcd.clear();
      pausedDrawn = false;
    } else {
      displayMode = 2;
      tempDisplayStart = millis();
    }
  }
}

// =====================================================
// PAUSED / SLEEP
// =====================================================

void handlePausedIdle() {
  if (awayPromptActive) {
    if (millis() - awayPromptStart < awayPromptDuration) {
      return;
    }
    awayPromptActive = false;
    pausedDrawn = false;
  }
  if (!pausedDrawn) {
    lcd.clear();
    if (workMode == 0) {
      lcd.setCursor(0, 0);
      lcd.print("System Paused");
      lcd.setCursor(0, 1);
      if (autoPausedByAway) {
        lcd.print("Away Mode");
      } else {
        lcd.print("Normal Mode");
      }
    } else {
      lcd.setCursor(0, 0);
      lcd.print("M");
      lcd.print(workMode);
      lcd.print(" Selected");
      lcd.setCursor(0, 1);
      if (workMode == 1)
        lcd.print("25F / 5B");
      else if (workMode == 2)
        lcd.print("50F / 10B");
      else if (workMode == 3)
        lcd.print("1F / 1B");
    }
    pausedDrawn = true;
  }

  // ---------------------------------------------------
  // SLEEP AFTER 10 SECONDS
  // ---------------------------------------------------

  if (!sleepMode && millis() - pausedStartTime >= 10000) {
    sleepMode = true;
    lcd.clear();
    lcd.print("Going to sleep");
    delay(1500);
    lcd.clear();
    pausedDrawn = false;
  }
}

// =====================================================
// START POMODORO
// =====================================================

void startPomodoro() {
  pomodoroRunning = true;
  isRunning = true;
  isBreakMode = false;
  autoPausedByAway = false;
  awayPromptActive = false;
  pomodoroStartTime = millis();
  pomodoroDuration = focusDurations[workMode];
  lastPomodoroDisplay = 0;
  sleepMode = false;
  pausedDrawn = false;
  displayMode = 0;

  // Ears straight: focus mode
  setServos(EAR_FOCUS_ANGLE);

  // Lock the phone compartment: focus mode
  lockServo.write(LOCK_FOCUS_ANGLE);
  lcd.clear();
  playBeepOnce();
}

// =====================================================
// STOP POMODORO
// =====================================================

void stopPomodoro() {
  pomodoroRunning = false;
  isRunning = false;
  isBreakMode = false;
  autoPausedByAway = false;
  awayPromptActive = false;
  pausedStartTime = millis();
  sleepMode = false;
  pausedDrawn = false;

  // Pomodoro fully stopped: ears back to home position
  setServos(EAR_STARTUP_ANGLE);

  // Session stopped early: unlock the phone compartment
  lockServo.write(LOCK_BREAK_ANGLE);
  lcd.clear();
}

// =====================================================
// HANDLE POMODORO
// =====================================================

void handlePomodoro() {
  if (isBreakMode) {
    // Break mode: ears droopy
    setServos(EAR_BREAK_ANGLE);

    // Break mode: unlock the phone compartment
    lockServo.write(LOCK_BREAK_ANGLE);
  } else {
    // Focus mode: ears straight
    setServos(EAR_FOCUS_ANGLE);

    // Focus mode: lock the phone compartment
    lockServo.write(LOCK_FOCUS_ANGLE);
  }
  unsigned long elapsed = millis() - pomodoroStartTime;
  if (elapsed >= pomodoroDuration) {
    if (!isBreakMode) {
      isBreakMode = true;
      pomodoroStartTime = millis();
      pomodoroDuration = breakDurations[workMode];
      lastPomodoroDisplay = 0;
      lcd.clear();
      playBeepTwice();
    } else {
      pomodoroSessionCount++;
      pomodoroRunning = false;
      isRunning = false;
      isBreakMode = false;
      autoPausedByAway = false;
      awayPromptActive = false;
      pausedStartTime = millis();
      sleepMode = false;
      pausedDrawn = false;

      // Session complete: ears back to home position
      setServos(EAR_STARTUP_ANGLE);
      lcd.clear();
      return;
    }
  }
  if (displayMode == 5)
    return;
  if (millis() - lastPomodoroDisplay >= 1000 || lastPomodoroDisplay == 0) {
    lastPomodoroDisplay = millis();
    showPomodoroTimer();
  }
}

// =====================================================
// POMODORO TIMER
// =====================================================

void showPomodoroTimer() {
  unsigned long elapsed = millis() - pomodoroStartTime;
  unsigned long remaining;
  if (elapsed >= pomodoroDuration) {
    remaining = 0;
  } else {
    remaining = pomodoroDuration - elapsed;
  }
  int totalSeconds = remaining / 1000;
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  lcd.setCursor(0, 0);
  if (isBreakMode)
    lcd.print("Focus in: ");
  else
    lcd.print("Break in: ");
  if (minutes < 10)
    lcd.print("0");
  lcd.print(minutes);
  lcd.print(":");
  if (seconds < 10)
    lcd.print("0");
  lcd.print(seconds);
  lcd.print(" ");
  lcd.setCursor(0, 1);
  lcd.print("M");
  lcd.print(workMode);
  lcd.print(" session:");
  lcd.print(pomodoroSessionCount);
  lcd.print(" ");
}

// =====================================================
// CLOCK
// =====================================================

void handleClock() {
  if (displayMode != 0)
    return;
  if (workMode != 0)
    return;
  static int lastSec = -1;
  DateTime now = rtc.now();
  if (now.second() == lastSec) {
    return;
  }
  lastSec = now.second();
  lcd.setCursor(0, 0);
  lcd.print("Time ");
  if (now.hour() < 10)
    lcd.print("0");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10)
    lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10)
    lcd.print("0");
  lcd.print(now.second());
  lcd.print(" ");
  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(temperature);
  lcd.print("C ");
  lcd.print("H:");
  lcd.print(humidity);
  lcd.print("% ");
}

// =====================================================
// LIGHTING
// =====================================================

void handleLighting() {
  if (autoMode) {
    int light = analogRead(ldrPin);
    if (light < 150) {
      analogWrite(ledPin, 0);
      return;
    }
    int brightness = map(light, 150, 900, 0, 255);
    brightness = constrain(brightness, 0, 255);
    brightness = brightness / 3;
    analogWrite(ledPin, brightness);
  } else {
    analogWrite(ledPin, manualBrightness);
  }
}

// =====================================================
// NOISE
// =====================================================

void handleNoise() {
  if (displayMode == 2 || displayMode == 3 || displayMode == 5) {
    return;
  }
  if (millis() - lastNoiseCheck < 100) {
    return;
  }
  lastNoiseCheck = millis();
  int noiseValue = analogRead(micPin);
  if (noiseValue > noiseThreshold && millis() - lastNoiseDisplay > 2500) {
    lastNoiseDisplay = millis();
    displayMode = 4;
    tempDisplayStart = millis();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Too Much Noise!");
    lcd.setCursor(0, 1);
    lcd.print("Noise:");
    lcd.print(noiseValue);
    lcd.print(" ");
  }
}

// =====================================================
// POSTURE
// =====================================================

void handlePosture() {
  if (displayMode == 2 || displayMode == 3 || displayMode == 4 || displayMode == 5) {
    return;
  }
  if (millis() - lastCheck < 300) {
    return;
  }
  lastCheck = millis();
  int dist = getDistance();
  if (dist > 0 && dist < 30) {
    if (displayMode != 1 && millis() - lastTooCloseAudio > 5000) {
      playAudio(2);
      lastTooCloseAudio = millis();
    }
    displayMode = 1;

    // Shake the ears: flip between the two
    // shake positions on every check while
    // the user stays too close.
    earShakeToggle = !earShakeToggle;
    setServos(earShakeToggle ? EAR_SHAKE_ANGLE_A : EAR_SHAKE_ANGLE_B);
    lcd.clear();
    lcd.print("Too Close!");
    lcd.setCursor(0, 1);
    lcd.print("Dist:");
    lcd.print(dist);
    lcd.print("cm");
  } else if (displayMode == 1) {
    displayMode = 0;

    // No longer too close: ears back to home
    setServos(EAR_STARTUP_ANGLE);
    lcd.clear();
  }
}

// =====================================================
// DHT
// =====================================================

void handleDHT() {
  if (millis() - lastDHTRead < 2000) {
    return;
  }
  lastDHTRead = millis();
  byte t;
  byte h;
  if (dht11.read(&t, &h, NULL) == SimpleDHTErrSuccess) {
    temperature = t;
    humidity = h;
  }
}

// =====================================================
// HUMIDITY
// =====================================================

void showHumidity() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Humidity: ");
  lcd.print(humidity);
  lcd.print("%");
  lcd.setCursor(0, 1);
  if (humidity > 70) {
    lcd.print("Too Humid");
  } else if (humidity < 30) {
    lcd.print("Too Dry");
  } else {
    lcd.print("Normal");
  }
}

// =====================================================
// TEMPORARY DISPLAY
// =====================================================

void handleTemporaryDisplays() {
  if ((displayMode == 2 || displayMode == 4 || displayMode == 5) &&
      millis() - tempDisplayStart > 3000) {
    displayMode = 0;
    lcd.clear();
  }
}

// =====================================================
// ULTRASONIC
// =====================================================

int getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) {
    return -1;
  }
  return duration * 0.034 / 2;
}
