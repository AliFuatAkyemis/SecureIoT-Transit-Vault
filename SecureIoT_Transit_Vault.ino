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
// Add your card UID strings here after reading from Serial Monitor.
// Example: "A1 B2 C3 D4"
const String AUTHORIZED_UIDS[] = {"33 E1 F0 21"};
const int    AUTHORIZED_COUNT  = sizeof(AUTHORIZED_UIDS) / sizeof(AUTHORIZED_UIDS[0]);

// ── Objects ────────────────────────────────────────────────────────
Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);
MFRC522          rfid(PIN_RFID_SS, PIN_RFID_RST);
MPU6050          mpu(Wire);
Servo            vaultServo;

// ── State ──────────────────────────────────────────────────────────
bool alarmActive              = false;
unsigned long lastDisturbance = 0;
const unsigned long AUTO_CLEAR_MS = 5000;
unsigned long lastOLED        = 0;
const unsigned long OLED_INTERVAL = 500;
float prevAngleX              = 0.0f;
unsigned long lastMotionTime  = 0;

// ── Forward declarations ───────────────────────────────────────────
void initOLED();
void initMPU();
void initRFID();
void initServo();
void initBuzzer();
void lockVault();
void unlockVault();
void triggerAlarm();
void readMotion();
void readRFID();
void updateOLED();
bool isAuthorized(const String &uid);

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

  lockVault();
  Serial.println("[BOOT] Ready.");
}

void loop() {
  ArduinoCloud.update();
  readMotion();
  readRFID();

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
  Serial.println("[LOCK] Vault LOCKED");
}

void unlockVault() {
  vaultServo.write(SERVO_OPEN);
  lockStatus = true;
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
    triggerAlarm();
    if (impactG)     Serial.printf("[MOTION] Dynamic G=%.2f\n", gForce);
    if (impactAngle) Serial.printf("[MOTION] GyroRate=%.1f deg/s\n", gyroRate);
  } else if (alarmActive && (millis() - lastDisturbance > AUTO_CLEAR_MS)) {
    alarmActive = false;
    digitalWrite(PIN_BUZZER, LOW);
    Serial.println("[ALARM] Auto-cleared — MPU stable 5s");
  }
}

// ── RFID reading ───────────────────────────────────────────────────
void readRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

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

  if (authorized) {
    Serial.println("[RFID] AUTHORIZED — unlocking");
    unlockVault();
  } else {
    Serial.println("[RFID] UNAUTHORIZED");
  }
}

bool isAuthorized(const String &uid) {
  for (int i = 0; i < AUTHORIZED_COUNT; i++) {
    if (AUTHORIZED_UIDS[i] == uid) return true;
  }
  return false;
}

// ── OLED display ───────────────────────────────────────────────────
void updateOLED() {
  oled.clearDisplay();

  // Top bar — Wi-Fi status + cloud time
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  oled.print(wifiOk ? "WiFi:OK" : "WiFi:--");

  auto t = ArduinoCloud.getLocalTime();
  if (t != 0) {
    struct tm *tm_info = localtime((time_t *)&t);
    char timeBuf[9];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", tm_info);
    oled.setCursor(72, 0);
    oled.print(timeBuf);
  }

  // Divider
  oled.drawLine(0, 9, SCREEN_W - 1, 9, SSD1306_WHITE);

  // Middle — status
  oled.setTextSize(2);
  oled.setCursor(4, 18);
  if (alarmActive) {
    oled.print("! ALARM !");
  } else if (lockStatus) {
    oled.print("UNLOCKED");
  } else {
    oled.print("SCAN CARD");
  }

  // Bottom — sensor values
  oled.setTextSize(1);
  oled.setCursor(0, 48);
  oled.print("Tilt:");
  oled.print(angle, 1);
  oled.print((char)247);  // degree symbol

  oled.setCursor(0, 57);
  oled.print("G:   ");
  oled.print(gForce, 2);
  oled.print("g");

  oled.display();
}

// ── Cloud callbacks ────────────────────────────────────────────────
void onRemoteUnlockChange() {
  Serial.print("[CLOUD] remoteUnlock = ");
  Serial.println(remoteUnlock);
  if (remoteUnlock) {
    unlockVault();
    remoteUnlock = false;  // auto-reset so next press triggers again
  }
}

void onAlarmResetChange() {
  Serial.print("[CLOUD] alarmReset = ");
  Serial.println(alarmReset);
  if (alarmReset) {
    alarmActive = false;
    digitalWrite(PIN_BUZZER, LOW);
    Serial.println("[ALARM] Buzzer reset by cloud");
    alarmReset = false;
  }
}
