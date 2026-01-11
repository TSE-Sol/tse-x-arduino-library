#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ESP32Servo.h>
#include <LiquidCrystal.h>
#include <ArduinoJson.h>

// ==================== DEVICE CONFIG ====================
#define DEVICE_ID        "YOUR_DEVICE_ID"
#define DEVICE_NAME      "YOUR_DEVICE_NAME"
#define DEVICE_TYPE      "Bike Lock"
#define DEVICE_MODEL     "BL-100"
#define FIRMWARE_VERSION "1.1.0"  // Updated for restore feature

// Wallet addresses for payments (must match backend server.js)
#define SOLANA_WALLET    "YOUR_SOLANA_WALLET_ADDRESS"
#define BASE_WALLET      "YOUR_BASE_WALLET_ADDRESS"

// ==================== BLE UUIDs ====================
#define SERVICE_UUID           "b3c8f420-0000-4020-8000-000000000000"
#define DEVICE_INFO_UUID       "b3c8f420-0001-4020-8000-000000000000"
#define LOCK_CONTROL_UUID      "b3c8f420-0002-4020-8000-000000000000"
#define SESSION_STATUS_UUID    "b3c8f420-0003-4020-8000-000000000000"

// ==================== HARDWARE PINS ====================
const int servoPin    = D13;
const int buzzerPin   = D12;
const int redLedPin   = D2;
const int greenLedPin = D4;

// ==================== LCD (Parallel) ====================
// LiquidCrystal(RS, E, D4, D5, D6, D7)
LiquidCrystal lcd(D5, D6, D7, D8, D9, D10);

// ==================== OBJECTS ====================
Servo lockServo;

BLEServer* pServer = nullptr;
BLECharacteristic* pDeviceInfoChar = nullptr;
BLECharacteristic* pLockControlChar = nullptr;
BLECharacteristic* pSessionStatusChar = nullptr;

// ==================== STATE ====================
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool isUnlocked = false;

// Session timing
unsigned long sessionDurationMs = 0;
unsigned long sessionStartMillis = 0;
long lastDisplaySeconds = -1;
String currentTxHash = "";
String payerWallet = "";
String paymentCurrency = "";

// ==================== BUZZER FUNCTIONS ====================
void playBeep(int freq, int durationMs) {
  int periodMicros = 1000000 / freq;
  int cycles = (freq * durationMs) / 1000;
  for (int i = 0; i < cycles; i++) {
    digitalWrite(buzzerPin, HIGH);
    delayMicroseconds(periodMicros / 2);
    digitalWrite(buzzerPin, LOW);
    delayMicroseconds(periodMicros / 2);
  }
}

void playUnlockSound() {
  playBeep(1000, 100);
  delay(50);
  playBeep(1200, 100);
  delay(50);
  playBeep(1400, 150);
}

void playLockSound() {
  playBeep(800, 150);
  delay(100);
  playBeep(600, 200);
}

void playWarningSound() {
  for (int i = 0; i < 3; i++) {
    playBeep(1000, 100);
    delay(100);
  }
}

void playErrorSound() {
  playBeep(400, 300);
  delay(100);
  playBeep(300, 300);
}

void playSuccessSound() {
  playBeep(1000, 100);
  delay(50);
  playBeep(1500, 150);
}

// ============ NEW: Restore sound (distinct from unlock) ============
void playRestoreSound() {
  playBeep(800, 80);
  delay(40);
  playBeep(1000, 80);
  delay(40);
  playBeep(1200, 80);
  delay(40);
  playBeep(1400, 120);
}

// ==================== LOCK CONTROL ====================
void setLocked(bool locked) {
  if (locked) {
    Serial.println("🔒 LOCKING...");
    
    digitalWrite(redLedPin, HIGH);
    digitalWrite(greenLedPin, LOW);
    
    playLockSound();
    
    lockServo.write(0);
    isUnlocked = false;
    
    sessionDurationMs = 0;
    sessionStartMillis = 0;
    currentTxHash = "";
    payerWallet = "";
    paymentCurrency = "";
    lastDisplaySeconds = -1;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LOCKED");
    lcd.setCursor(0, 1);
    lcd.print("Scan to unlock");
    
    updateSessionStatus();
    
  } else {
    Serial.println("🔓 UNLOCKING...");
    
    digitalWrite(redLedPin, LOW);
    digitalWrite(greenLedPin, HIGH);
    
    playUnlockSound();
    
    lockServo.write(90);
    isUnlocked = true;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("UNLOCKED");
    lcd.setCursor(0, 1);
    lcd.print("Time: --:--");
  }
}

// ============ NEW: setUnlockedQuiet - for restore (no sound, just state) ============
void setUnlockedQuiet() {
  Serial.println("🔓 UNLOCKING (quiet - restore)...");
  
  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, HIGH);
  
  lockServo.write(90);
  isUnlocked = true;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SESSION RESTORED");
  lcd.setCursor(0, 1);
  lcd.print("Time: --:--");
}

void startSession(unsigned long durationMs, String txHash, String wallet, String currency) {
  Serial.println("▶️ ============ STARTING SESSION ============");
  Serial.print("   Duration: ");
  Serial.print(durationMs / 1000);
  Serial.println(" seconds");
  Serial.print("   TxHash: ");
  Serial.println(txHash);
  Serial.print("   Wallet: ");
  Serial.println(wallet);
  Serial.print("   Currency: ");
  Serial.println(currency);
  
  sessionDurationMs = durationMs;
  sessionStartMillis = millis();
  currentTxHash = txHash;
  payerWallet = wallet;
  paymentCurrency = currency;
  lastDisplaySeconds = -1;
  
  setLocked(false);
  Serial.println("▶️ Session started successfully!");
}

// ============ NEW: Restore session from backend (after power loss) ============
void restoreSession(unsigned long remainingMs, String wallet, String currency, String txHash) {
  Serial.println("🔄 ============ RESTORING SESSION ============");
  Serial.print("   Remaining: ");
  Serial.print(remainingMs / 1000);
  Serial.println(" seconds");
  Serial.print("   Wallet: ");
  Serial.println(wallet);
  Serial.print("   Currency: ");
  Serial.println(currency);
  Serial.print("   TxHash: ");
  Serial.println(txHash);
  
  // Set session state
  sessionDurationMs = remainingMs;  // Only the remaining time
  sessionStartMillis = millis();     // Start counting from now
  currentTxHash = txHash.length() > 0 ? txHash : "restored";
  payerWallet = wallet;
  paymentCurrency = currency;
  lastDisplaySeconds = -1;
  
  // Unlock quietly (no fanfare since this is a restore)
  setUnlockedQuiet();
  
  // Play restore sound (different from unlock)
  playRestoreSound();
  
  // Update display after short delay
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print("UNLOCKED        ");
  
  updateSessionStatus();
  Serial.println("🔄 Session restored successfully!");
}

void endSession() {
  Serial.println("⏹️ ============ ENDING SESSION ============");
  Serial.print("   Was unlocked: ");
  Serial.println(isUnlocked ? "YES" : "NO");
  setLocked(true);
  Serial.println("⏹️ Session ended!");
}

void extendSession(unsigned long extraMs, String txHash) {
  if (!isUnlocked) return;
  
  sessionDurationMs += extraMs;
  currentTxHash = txHash;
  
  lcd.setCursor(0, 0);
  lcd.print("TIME EXTENDED!  ");
  playSuccessSound();
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print("UNLOCKED        ");
  
  updateSessionStatus();
}

// ==================== SESSION STATUS ====================
void updateSessionStatus() {
  if (pSessionStatusChar == nullptr) return;
  
  StaticJsonDocument<512> doc;
  
  doc["deviceId"] = DEVICE_ID;
  doc["state"] = isUnlocked ? "unlocked" : "locked";
  
  if (isUnlocked && sessionDurationMs > 0) {
    unsigned long elapsed = millis() - sessionStartMillis;
    unsigned long remaining = (elapsed >= sessionDurationMs) ? 0 : (sessionDurationMs - elapsed);
    
    doc["sessionActive"] = true;
    doc["remainingMs"] = remaining;
    doc["totalDurationMs"] = sessionDurationMs;
    doc["elapsedMs"] = elapsed;
    doc["txHash"] = currentTxHash;
    doc["payerWallet"] = payerWallet;
    doc["currency"] = paymentCurrency;
  } else {
    doc["sessionActive"] = false;
    doc["remainingMs"] = 0;
  }
  
  String output;
  serializeJson(doc, output);
  
  pSessionStatusChar->setValue(output.c_str());
  
  // DISABLED: Don't send notifications - app manages state locally
  // Notifications were causing race conditions with app state
  // if (deviceConnected) {
  //   pSessionStatusChar->notify();
  // }
}

// ==================== LCD COUNTDOWN ====================
void updateCountdownDisplay() {
  if (!isUnlocked || sessionDurationMs == 0) return;
  
  unsigned long now = millis();
  unsigned long elapsed = now - sessionStartMillis;
  
  if (elapsed >= sessionDurationMs) {
    Serial.println("⏰ Session timer expired!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("TIME'S UP!");
    lcd.setCursor(0, 1);
    lcd.print("Locking...");
    delay(2000);
    endSession();
    return;
  }
  
  unsigned long remaining = sessionDurationMs - elapsed;
  unsigned long totalSeconds = remaining / 1000;
  
  if (totalSeconds == 60 && lastDisplaySeconds != 60) {
    playWarningSound();
    lcd.setCursor(0, 0);
    lcd.print("1 MIN LEFT!     ");
  }
  
  if (totalSeconds == 30 && lastDisplaySeconds != 30) {
    playWarningSound();
    lcd.setCursor(0, 0);
    lcd.print("30 SEC LEFT!    ");
  }
  
  if (totalSeconds <= 10 && totalSeconds > 0 && (long)totalSeconds != lastDisplaySeconds) {
    playBeep(1000, 50);
    lcd.setCursor(0, 0);
    lcd.print("ENDING SOON!    ");
  }
  
  if ((long)totalSeconds == lastDisplaySeconds) return;
  lastDisplaySeconds = totalSeconds;
  
  unsigned int minutes = totalSeconds / 60;
  unsigned int seconds = totalSeconds % 60;
  
  char buf[17];
  snprintf(buf, sizeof(buf), "Time: %02u:%02u", minutes, seconds);
  
  lcd.setCursor(0, 1);
  lcd.print(buf);
  lcd.print("     ");
}

// ==================== BLE CALLBACKS ====================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("📱 Phone connected via BLE");
    
    // Update session status so app can read current state
    updateSessionStatus();
    Serial.print("   Session active: ");
    Serial.println(isUnlocked ? "YES" : "NO");
    if (isUnlocked && sessionDurationMs > 0) {
      unsigned long elapsed = millis() - sessionStartMillis;
      unsigned long remaining = (elapsed >= sessionDurationMs) ? 0 : (sessionDurationMs - elapsed);
      Serial.print("   Time remaining: ");
      Serial.print(remaining / 1000);
      Serial.println(" seconds");
    }
    
    playBeep(1200, 100);
    
    if (!isUnlocked) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("App Connected");
      lcd.setCursor(0, 1);
      lcd.print("Ready to pay");
    }
  }
  
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("📱 ============ BLE DISCONNECTED ============");
    Serial.print("   Session active: ");
    Serial.println(isUnlocked ? "YES" : "NO");
    Serial.println("   NOT locking - session continues!");
    
    if (!isUnlocked) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("LOCKED");
      lcd.setCursor(0, 1);
      lcd.print("Scan to unlock");
    } else {
      Serial.println("   Timer still running...");
    }
  }
};

class LockControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();
    Serial.print("📥 Received command: ");
    Serial.println(value);
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, value);
    
    if (error) {
      Serial.print("❌ JSON parse error: ");
      Serial.println(error.c_str());
      playErrorSound();
      return;
    }
    
    String action = doc["action"] | "";
    
    if (action == "unlock") {
      String txHash = doc["txHash"] | "";
      String wallet = doc["wallet"] | "";
      String currency = doc["currency"] | "TSE";
      unsigned long duration = doc["durationMs"] | 1800000;
      
      if (txHash.length() > 0) {
        Serial.println("✅ Payment verified, unlocking...");
        startSession(duration, txHash, wallet, currency);
      } else {
        Serial.println("❌ No transaction hash");
        playErrorSound();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Payment Error");
        lcd.setCursor(0, 1);
        lcd.print("No TX hash");
        delay(2000);
        if (!isUnlocked) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("LOCKED");
          lcd.setCursor(0, 1);
          lcd.print("Try again");
        }
      }
    }
    // ============ NEW: RESTORE ACTION ============
    else if (action == "restore") {
      // Restore session from backend after power loss
      String wallet = doc["wallet"] | "";
      String currency = doc["currency"] | "TSE";
      String txHash = doc["txHash"] | "restored";
      unsigned long remainingMs = doc["remainingMs"] | 0;
      
      Serial.println("🔄 ============ RESTORE COMMAND RECEIVED ============");
      Serial.print("   Wallet: ");
      Serial.println(wallet);
      Serial.print("   Remaining: ");
      Serial.print(remainingMs / 1000);
      Serial.println(" seconds");
      
      if (remainingMs > 0 && wallet.length() > 0) {
        // Verify we're not already in a session
        if (isUnlocked) {
          Serial.println("⚠️ Already unlocked - ignoring restore");
          playErrorSound();
          return;
        }
        
        restoreSession(remainingMs, wallet, currency, txHash);
      } else {
        Serial.println("❌ Invalid restore params");
        playErrorSound();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Restore Failed");
        lcd.setCursor(0, 1);
        lcd.print("Invalid data");
        delay(2000);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("LOCKED");
        lcd.setCursor(0, 1);
        lcd.print("Scan to unlock");
      }
    }
    else if (action == "lock" || action == "end") {
      Serial.println("🔒 ============ LOCK COMMAND RECEIVED ============");
      Serial.print("   Currently unlocked: ");
      Serial.println(isUnlocked ? "YES" : "NO");
      if (isUnlocked) {
        Serial.println("   Processing lock request...");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Session Ended");
        lcd.setCursor(0, 1);
        lcd.print("Thank you!");
        delay(1500);
        endSession();
      } else {
        Serial.println("   Already locked, ignoring.");
      }
    }
    else if (action == "extend") {
      unsigned long extraMs = doc["extraMs"] | 0;
      String txHash = doc["txHash"] | "";
      
      if (isUnlocked && extraMs > 0 && txHash.length() > 0) {
        extendSession(extraMs, txHash);
      } else {
        playErrorSound();
      }
    }
    else if (action == "status") {
      updateSessionStatus();
    }
  }
};

// ==================== BUILD DEVICE INFO ====================
String buildDeviceInfoJson() {
  StaticJsonDocument<1024> doc;
  
  doc["deviceId"] = DEVICE_ID;
  doc["deviceName"] = DEVICE_NAME;
  doc["deviceType"] = DEVICE_TYPE;
  doc["model"] = DEVICE_MODEL;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  
  doc["supportsLock"] = true;
  doc["supportsTimer"] = true;
  doc["supportsBLE"] = true;
  doc["supportsNFC"] = false;
  doc["supportsPayment"] = true;
  doc["supportsTSE"] = true;
  doc["supportsUSDC"] = true;
  doc["supportsRestore"] = true;  // NEW: Advertise restore capability
  
  JsonArray chains = doc.createNestedArray("chains");
  
  JsonObject solana = chains.createNestedObject();
  solana["chain"] = "solana";
  solana["wallet"] = SOLANA_WALLET;
  
  JsonObject base = chains.createNestedObject();
  base["chain"] = "base";
  base["wallet"] = BASE_WALLET;
  
  doc["state"] = isUnlocked ? "unlocked" : "locked";
  
  String output;
  serializeJson(doc, output);
  return output;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n🚲 TSE-X Bike Lock Starting...");
  Serial.println("   Firmware: " FIRMWARE_VERSION);
  Serial.println("   Supports session restore: YES");
  
  // LCD init (Parallel)
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("TSE-X Bike Lock");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  // Servo
  lockServo.attach(servoPin, 500, 2400);
  lockServo.write(0);
  
  // Buzzer
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  
  // LEDs
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  digitalWrite(redLedPin, HIGH);
  digitalWrite(greenLedPin, LOW);
  
  // BLE
  Serial.println("📶 Initializing BLE...");
  BLEDevice::init(DEVICE_NAME);
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  BLEService* pService = pServer->createService(SERVICE_UUID);
  
  pDeviceInfoChar = pService->createCharacteristic(
    DEVICE_INFO_UUID,
    BLECharacteristic::PROPERTY_READ
  );
  pDeviceInfoChar->setValue(buildDeviceInfoJson().c_str());
  
  pLockControlChar = pService->createCharacteristic(
    LOCK_CONTROL_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pLockControlChar->setCallbacks(new LockControlCallbacks());
  
  pSessionStatusChar = pService->createCharacteristic(
    SESSION_STATUS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pSessionStatusChar->addDescriptor(new BLE2902());
  
  pService->start();
  
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->start();
  
  Serial.println("✅ BLE Ready!");
  
  setLocked(true);
  
  playBeep(1000, 100);
  delay(100);
  playBeep(1200, 100);
  delay(100);
  playBeep(1400, 100);
  
  Serial.println("🚲 Bike Lock Ready!");
}

// ==================== LOOP ====================
void loop() {
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  updateCountdownDisplay();
  
  static unsigned long lastStatusUpdate = 0;
  if (deviceConnected && isUnlocked && (millis() - lastStatusUpdate > 5000)) {
    lastStatusUpdate = millis();
    updateSessionStatus();
  }
  
  delay(50);
}
