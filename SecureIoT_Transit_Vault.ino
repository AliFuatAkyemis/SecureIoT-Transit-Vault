#include "arduino_secrets.h"
#include "thingProperties.h"

#include "esp_system.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>
#include <MPU6050_tockn.h>
#include <ESP32Servo.h>
#include <Preferences.h>

// ── Pin Definitions ────────────────────────────────────────────────
#define PIN_SDA       21
#define PIN_SCL       22
#define PIN_RFID_SS    5
#define PIN_RFID_RST   4
#define PIN_SPI_SCK   14
#define PIN_SPI_MISO  25
#define PIN_SPI_MOSI  32   // GPIO23 defective on this board
#define PIN_SERVO     13
#define PIN_BUZZER    12

// ── OLED ───────────────────────────────────────────────────────────
#define OLED_ADDR     0x3C
#define SCREEN_W      128
#define SCREEN_H       64

// ── Thresholds ─────────────────────────────────────────────────────
#define G_THRESHOLD          1.5f   // g dynamic (deviation from resting 1g)
#define ANGLE_RATE_THRESHOLD 400.0f // deg/s — sudden rotation trigger
#define ANGLE_DEADZONE       0.3f   // deg — suppresses gyro drift noise

// ── Servo positions ────────────────────────────────────────────────
#define SERVO_LOCKED   0
#define SERVO_OPEN    90

// ── Authorized UIDs ────────────────────────────────────────────────
// Master UID: always authorized, not deletable. Change here if needed.
const String MASTER_UID = "B9 2A 2D 40";

// One additional UID persisted in NVS. Loaded at boot.
String enrolledUID;
bool hasEnrolled = false;

Preferences prefs;

// ── Objects ────────────────────────────────────────────────────────
Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);
MFRC522          rfid(PIN_RFID_SS, PIN_RFID_RST);
MPU6050          mpu(Wire);
Servo            vaultServo;

// ── Stats accumulators ─────────────────────────────────────────────
double   shakeSum = 0, tiltAbsSum = 0, tiltRelSum = 0;
float    shakeMaxV = 0, tiltAbsMaxV = 0, tiltRelMaxV = 0;
uint32_t statSamples = 0;
float    baselineAngle = 0.0f;
unsigned long lastStatsPublish = 0;
const unsigned long STATS_PUBLISH_MS = 1000;

// ── State ──────────────────────────────────────────────────────────
bool alarmActive              = false;
unsigned long lastDisturbance = 0;
const unsigned long AUTO_CLEAR_MS = 5000;
unsigned long lastOLED        = 0;
const unsigned long OLED_INTERVAL = 500;
float prevAngleX              = 0.0f;
unsigned long lastMotionTime  = 0;
unsigned long denyBuzzerUntil = 0;
unsigned long lastRFIDRead    = 0;
const unsigned long RFID_DEBOUNCE_MS = 1000;

// ── Forward declarations ───────────────────────────────────────────
void initOLED();
void initMPU();
void initRFID();
void initServo();
void initBuzzer();
void lockVault();
void unlockVault();
void triggerAlarm();
void resetAlarmState();
void readMotion();
void readRFID();
void updateOLED();
void loadEnrolled();
void saveEnrolled(const String &uid);
bool isAuthorized(const String &uid);
String normalizeUid(const String &input);
void onStatsResetChange();
void resetStats();

// ══════════════════════════════════════════════════════════════════
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1500);
  Serial.printf("[BOOT] Reset reason: %d\n", esp_reset_reason());
  Serial.println("[BOOT] SecureIoT Transit Vault starting...");

  initRFID();

  Wire.begin(PIN_SDA, PIN_SCL);

  initOLED();
  initMPU();
  initServo();
  initBuzzer();

  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  loadEnrolled();

  unlockVault();
  mpu.update();
  baselineAngle = mpu.getAccAngleX();
  Serial.printf("[STATS] Baseline angle: %.2f\n", baselineAngle);
  Serial.println("[BOOT] Ready.");
}

void loop() {
  ArduinoCloud.update();
  readMotion();
  readRFID();

  if (denyBuzzerUntil != 0 && millis() >= denyBuzzerUntil) {
    denyBuzzerUntil = 0;
    if (!alarmActive) digitalWrite(PIN_BUZZER, LOW);
  }

  if (millis() - lastOLED >= OLED_INTERVAL) {
    lastOLED = millis();
    updateOLED();
  }
}

// ── Hardware init ──────────────────────────────────────────────────
void initOLED() {
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[ERROR] OLED init failed");
    return;
  }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.display();
  Serial.println("[OK] OLED");
}

void initMPU() {
  mpu.begin();
  mpu.calcGyroOffsets(true);
  Serial.println("[OK] MPU-6050");
}

void initRFID() {
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  delay(100);
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print("[RFID] VersionReg=0x");
  Serial.println(v, HEX);  // 0x91/0x92 expected; 0x00/0xFF = wiring/power fault
  rfid.PCD_DumpVersionToSerial();
  Serial.println("[OK] RFID RC522");
}

void initServo() {
  vaultServo.attach(PIN_SERVO);
  Serial.println("[OK] Servo");
}

void initBuzzer() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  Serial.println("[OK] Buzzer");
}

// ── Lock / Unlock ──────────────────────────────────────────────────
void lockVault() {
  vaultServo.write(SERVO_LOCKED);
  lockStatus = false;
  baselineAngle = prevAngleX;
  Serial.println("[LOCK] Vault LOCKED");
  Serial.printf("[STATS] Baseline reset to: %.2f\n", baselineAngle);
}

void unlockVault() {
  vaultServo.write(SERVO_OPEN);
  lockStatus = true;
  resetAlarmState();
  Serial.println("[LOCK] Vault UNLOCKED");
}

// ── Alarm ──────────────────────────────────────────────────────────
void triggerAlarm() {
  if (!alarmActive) {
    alarmActive = true;
    digitalWrite(PIN_BUZZER, HIGH);
    Serial.println("[ALARM] Impact/Tilt detected — buzzer ON");
  }
}

void resetAlarmState() {
  alarmActive = false;
  denyBuzzerUntil = 0;
  lastDisturbance = 0;
  digitalWrite(PIN_BUZZER, LOW);
}

// ── Motion reading ─────────────────────────────────────────────────
void readMotion() {
  mpu.update();

  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();

  // Dynamic G: deviation from resting 1g (0 when still)
  float magnitude = sqrt(ax * ax + ay * ay + az * az);
  gForce = fabsf(magnitude - 1.0f);

  // Angle: accelerometer-based (no gyro drift), dead-zone filtered
  float rawAngle = mpu.getAccAngleX();
  if (fabsf(rawAngle - prevAngleX) > ANGLE_DEADZONE) {
    prevAngleX = rawAngle;
  }
  angle = prevAngleX;

  // Angular velocity — detects sudden rotation (impact proxy)
  gyroRate = fabsf(mpu.getGyroX());

  bool impactG     = (gForce > G_THRESHOLD);
  bool impactAngle = (gyroRate > ANGLE_RATE_THRESHOLD);

  if (impactG || impactAngle) {
    lastDisturbance = millis();
    if (!lockStatus) triggerAlarm();
    if (impactG)     Serial.printf("[MOTION] Dynamic G=%.2f\n", gForce);
    if (impactAngle) Serial.printf("[MOTION] GyroRate=%.1f deg/s\n", gyroRate);
  } else if (alarmActive && (millis() - lastDisturbance > AUTO_CLEAR_MS)) {
    alarmActive = false;
    digitalWrite(PIN_BUZZER, LOW);
    Serial.println("[ALARM] Auto-cleared — MPU stable 5s");
  }

  // ── Stats accumulation ────────────────────────────────────────────
  float tiltAbs = fabsf(angle);
  float tiltRel = fabsf(angle - baselineAngle);

  shakeSum   += gForce;
  tiltAbsSum += tiltAbs;
  tiltRelSum += tiltRel;
  statSamples++;

  if (gForce   > shakeMaxV)   shakeMaxV   = gForce;
  if (tiltAbs  > tiltAbsMaxV) tiltAbsMaxV = tiltAbs;
  if (tiltRel  > tiltRelMaxV) tiltRelMaxV = tiltRel;

  if (millis() - lastStatsPublish >= STATS_PUBLISH_MS && statSamples > 0) {
    lastStatsPublish = millis();
    shakeAvg   = (float)(shakeSum   / statSamples);
    tiltAbsAvg = (float)(tiltAbsSum / statSamples);
    tiltRelAvg = (float)(tiltRelSum / statSamples);
    shakeMax   = shakeMaxV;
    tiltAbsMax = tiltAbsMaxV;
    tiltRelMax = tiltRelMaxV;
  }
}

// ── RFID reading ───────────────────────────────────────────────────
void readRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  if (millis() - lastRFIDRead < RFID_DEBOUNCE_MS) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  lastRFIDRead = millis();

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i > 0) uid += " ";
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  rfidUID = uid;

  Serial.print("[RFID] Card UID: ");
  Serial.println(uid);

  bool authorized = isAuthorized(uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  if (lockStatus) {
    // Unlocked: enroll this card and lock
    Serial.println("[RFID] ENROLL — saving new UID");
    saveEnrolled(uid);
    lockVault();
    resetAlarmState();
  } else if (authorized) {
    Serial.println("[RFID] AUTHORIZED — unlocking");
    unlockVault();
  } else {
    Serial.println("[RFID] UNAUTHORIZED — 1s buzzer");
    digitalWrite(PIN_BUZZER, HIGH);
    denyBuzzerUntil = millis() + 1000;
  }
}

void loadEnrolled() {
  prefs.begin("vault", true);
  enrolledUID = prefs.getString("uid", "");
  hasEnrolled = (enrolledUID.length() > 0);

  // Backward compat: old multi-uid array → migrate first entry to single key
  if (!hasEnrolled && prefs.isKey("uid_0")) {
    enrolledUID = prefs.getString("uid_0", "");
    hasEnrolled = (enrolledUID.length() > 0);
    // Persist in new single-key format
    if (hasEnrolled) {
      prefs.end();
      prefs.begin("vault", false);
      prefs.putString("uid", enrolledUID);
      prefs.end();
      Serial.println("[ENROLL] Migrated uid_0 → single uid key");
    }
  }

  prefs.end();
  if (hasEnrolled) {
    Serial.print("[ENROLL] Loaded UID from NVS: ");
    Serial.println(enrolledUID);
  } else {
    Serial.println("[ENROLL] No UID stored in NVS");
  }
}

void saveEnrolled(const String &uid) {
  prefs.begin("vault", false);
  prefs.putString("uid", uid);
  prefs.end();
  enrolledUID = uid;
  hasEnrolled = true;
  Serial.print("[ENROLL] Stored UID: ");
  Serial.println(uid);
}

bool isAuthorized(const String &uid) {
  if (uid == MASTER_UID) return true;
  return (hasEnrolled && enrolledUID == uid);
}

// ── OLED display ───────────────────────────────────────────────────

// 3-bar WiFi icon, 10x8px
static void drawWifiIcon(int x, int y, bool connected) {
  if (!connected) {
    oled.drawLine(x, y + 1, x + 8, y + 7, SSD1306_WHITE);
    oled.drawLine(x, y + 7, x + 8, y + 1, SSD1306_WHITE);
    return;
  }
  oled.fillRect(x,     y + 5, 2, 3, SSD1306_WHITE);
  oled.fillRect(x + 3, y + 3, 2, 5, SSD1306_WHITE);
  oled.fillRect(x + 6, y + 1, 2, 7, SSD1306_WHITE);
}

// Padlock icon, 8x8px
static void drawLockIcon(int x, int y, bool unlocked) {
  if (unlocked) {
    // Open shackle — left post only
    oled.drawLine(x + 1, y + 4, x + 1, y + 1, SSD1306_WHITE);
    oled.drawLine(x + 1, y + 1, x + 5, y + 1, SSD1306_WHITE);
  } else {
    // Closed shackle
    oled.drawLine(x + 1, y + 4, x + 1, y + 2, SSD1306_WHITE);
    oled.drawLine(x + 1, y + 2, x + 5, y + 2, SSD1306_WHITE);
    oled.drawLine(x + 5, y + 2, x + 5, y + 4, SSD1306_WHITE);
  }
  oled.fillRect(x, y + 4, 7, 4, SSD1306_WHITE);
  oled.drawPixel(x + 3, y + 5, SSD1306_BLACK);
  oled.drawPixel(x + 3, y + 7, SSD1306_BLACK);
}

void updateOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);

  // ── Top bar (y=0..8) ───────────────────────────────────────────────
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  drawWifiIcon(0, 0, wifiOk);

  auto t = ArduinoCloud.getLocalTime();
  if (t != 0) {
    char timeBuf[9];
    time_t localT = (time_t)t;
    struct tm *tm_info = localtime(&localT);
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", tm_info);
    oled.setCursor(49, 1);                        // center: (128-30)/2 ≈ 49
    oled.print(timeBuf);
  }

  drawLockIcon(120, 0, lockStatus);

  oled.drawLine(0, 10, SCREEN_W - 1, 10, SSD1306_WHITE);

  // ── Center prompt (size 2, 12px/char wide, 16px tall) ─────────────
  // Max chars per line at size 2: 128/12 = 10
  oled.setTextSize(2);
  if (alarmActive) {
    // "! ALARM !" = 9 chars = 108px → x=10
    oled.setCursor(10, 22);
    oled.print("! ALARM !");
  } else if (lockStatus) {
    // "UNLOCKED" = 8 chars = 96px → x=16
    oled.setCursor(16, 22);
    oled.print("UNLOCKED");
  } else {
    // "LOCKED" = 6 chars = 72px → x=28
    oled.setCursor(28, 22);
    oled.print("LOCKED");
  }

  // ── Bottom readout (size 1, 6px/char wide) ────────────────────────
  oled.setTextSize(1);
  oled.drawLine(0, 46, SCREEN_W - 1, 46, SSD1306_WHITE);

  // Sensor row: "Tilt:0.00G" left, "Ang:0.0°" right
  char gBuf[11], aBuf[11];
  snprintf(gBuf, sizeof(gBuf), "Tilt:%.2fG", gForce);
  snprintf(aBuf, sizeof(aBuf), "Ang:%.1f%c", angle, '\xF7');
  oled.setCursor(0, 53);
  oled.print(gBuf);
  oled.setCursor(SCREEN_W - (int)strlen(aBuf) * 6, 53);
  oled.print(aBuf);

  oled.display();
}

// ── Cloud callbacks ────────────────────────────────────────────────
void onRemoteUnlockChange() {
  Serial.print("[CLOUD] remoteUnlock = ");
  Serial.println(remoteUnlock);
  if (remoteUnlock) {
    if (lockStatus) {
      Serial.println("[CLOUD] REMOTE — locking & resetting");
      lockVault();
      resetAlarmState();
    } else {
      Serial.println("[CLOUD] REMOTE — unlocking & resetting alarm");
      unlockVault();
    }
    remoteUnlock = false;
  }
}

void onAlarmResetChange() {
  Serial.print("[CLOUD] alarmReset = ");
  Serial.println(alarmReset);
  if (alarmReset) {
    resetAlarmState();
    Serial.println("[ALARM] Buzzer reset by cloud");
    alarmReset = false;
  }
}

// ── UID normalization ──────────────────────────────────────────────
// Accepts "a1b2c3d4", "A1:B2:C3:D4", "a1 b2 c3 d4", etc.
// Returns "A1 B2 C3 D4" on success, "" on invalid input.
String normalizeUid(const String &input) {
  String hex = "";
  for (int i = 0; i < (int)input.length(); i++) {
    char c = input[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
      hex += (char)toupper(c);
    }
  }
  // Must be 4, 7, or 10 bytes (8, 14, 20 hex chars — common RFID sizes)
  if (hex.length() != 8 && hex.length() != 14 && hex.length() != 20) return "";
  String result = "";
  for (int i = 0; i < (int)hex.length(); i += 2) {
    if (i > 0) result += " ";
    result += hex.substring(i, i + 2);
  }
  return result;
}

void resetStats() {
  shakeSum = tiltAbsSum = tiltRelSum = 0;
  shakeMaxV = tiltAbsMaxV = tiltRelMaxV = 0;
  statSamples = 0;
  baselineAngle = prevAngleX;
  Serial.printf("[STATS] Reset — new baseline: %.2f\n", baselineAngle);
}

void onStatsResetChange() {
  if (statsReset) {
    resetStats();
    statsReset = false;
  }
}

void onAuthorizedUidInputChange() {
  String input = authorizedUidInput;
  input.trim();
  if (input.length() == 0) return;

  // lockStatus==true means UNLOCKED — reject enrollment while open
  if (lockStatus) {
    Serial.println("[ENROLL] Rejected — vault is UNLOCKED");
    authorizedUidInput = "";
    return;
  }

  String normalized = normalizeUid(input);
  if (normalized.length() == 0) {
    Serial.println("[ENROLL] Rejected — invalid UID format");
    authorizedUidInput = "";
    return;
  }

  Serial.print("[ENROLL] Cloud enroll new UID: ");
  Serial.println(normalized);
  saveEnrolled(normalized);
  authorizedUidInput = "";
}

// onEnrollModeChange removed — enrollMode cloud var retired
