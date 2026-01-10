/*
 * TSE-X Bike Lock Example
 * Arduino Nano 33 BLE / Arduino Nano ESP32
 * 
 * BLE-based bike lock controlled via mobile app bridge.
 * User pays in app, app sends unlock command via BLE.
 * 
 * Pricing: $0.50 for 30 minutes
 * 
 * Hardware:
 * - Arduino Nano 33 BLE or Nano ESP32
 * - Servo or solenoid lock on pin 9
 * - LED indicator on pin LED_BUILTIN
 * 
 * BLE Services:
 * - Lock Service UUID: b3c8f420-0000-4020-8000-000000000000
 * - Control Characteristic: b3c8f420-0002-4020-8000-000000000000
 * - Status Characteristic: b3c8f420-0003-4020-8000-000000000000
 * 
 * Flow:
 * 1. Arduino advertises as BLE device
 * 2. App connects after payment verified
 * 3. App sends unlock command via BLE
 * 4. Arduino unlocks and starts 30-min timer
 * 5. Timer expires or user ends session → lock engages
 */

#include <ArduinoBLE.h>
#include <TSE_X402.h>

// ============ CONFIGURATION ============
const char* DEVICE_ID = "BIKE-LOCK-001";

// ============ HARDWARE PINS ============
const int LOCK_PIN = 9;         // Servo/solenoid control
const int STATUS_LED = LED_BUILTIN;

// ============ BLE UUIDs ============
#define LOCK_SERVICE_UUID        "b3c8f420-0000-4020-8000-000000000000"
#define LOCK_CONTROL_UUID        "b3c8f420-0002-4020-8000-000000000000"
#define LOCK_STATUS_UUID         "b3c8f420-0003-4020-8000-000000000000"

// ============ TIMING ============
const unsigned long SESSION_DURATION_MS = 30 * 60 * 1000;  // 30 minutes
const unsigned long HEARTBEAT_INTERVAL = 10000;  // 10 sec heartbeat

// ============ STATE ============
bool isLocked = true;
bool sessionActive = false;
unsigned long sessionEndTime = 0;
unsigned long lastHeartbeat = 0;

String sessionWallet = "";
String sessionCurrency = "";
String sessionTxHash = "";

// BLE
BLEService lockService(LOCK_SERVICE_UUID);
BLECharacteristic controlChar(LOCK_CONTROL_UUID, BLEWrite | BLEWriteWithoutResponse, 256);
BLECharacteristic statusChar(LOCK_STATUS_UUID, BLERead | BLENotify, 256);

char timeBuffer[16];

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n========================================");
  Serial.println("   TSE-X Bike Lock (BLE)");
  Serial.println("   " TSE_X402_VERSION);
  Serial.println("========================================");
  Serial.print("Device: ");
  Serial.println(DEVICE_ID);
  Serial.println("Price: $0.50 for 30 minutes");
  Serial.println("Accepts: TSE (Solana) & USDC (Base)");
  Serial.println();
  
  // Initialize pins
  pinMode(LOCK_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  
  // Start locked
  engageLock();
  
  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("❌ BLE init failed!");
    while (1) {
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      delay(100);
    }
  }
  
  // Setup BLE service
  BLE.setLocalName(DEVICE_ID);
  BLE.setDeviceName(DEVICE_ID);
  BLE.setAdvertisedService(lockService);
  
  lockService.addCharacteristic(controlChar);
  lockService.addCharacteristic(statusChar);
  BLE.addService(lockService);
  
  // Set initial status
  updateBLEStatus();
  
  // Start advertising
  BLE.advertise();
  
  Serial.println("🔒 Lock engaged");
  Serial.println("📡 BLE advertising...");
  Serial.println("   Waiting for app connection");
}

// ============ MAIN LOOP ============
void loop() {
  unsigned long now = millis();
  
  // Poll BLE
  BLE.poll();
  
  // Check for BLE commands
  if (controlChar.written()) {
    handleBLECommand();
  }
  
  // Check session timeout
  if (sessionActive && sessionEndTime > 0 && now >= sessionEndTime) {
    Serial.println("\n⏰ Session expired!");
    endSession();
  }
  
  // Send heartbeat status
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = now;
    updateBLEStatus();
    
    if (sessionActive) {
      unsigned long remaining = (sessionEndTime > now) ? (sessionEndTime - now) / 1000 : 0;
      Serial.print("🔓 Unlocked - ");
      Serial.print(TSE_FormatTime(remaining, timeBuffer));
      Serial.println(" remaining");
    }
  }
  
  // Status LED
  updateStatusLED(now);
  
  delay(10);
}

// ============ BLE COMMAND HANDLER ============
void handleBLECommand() {
  int len = controlChar.valueLength();
  if (len == 0) return;
  
  char cmdBuffer[257];
  memcpy(cmdBuffer, controlChar.value(), len);
  cmdBuffer[len] = '\0';
  
  Serial.print("📥 BLE command: ");
  Serial.println(cmdBuffer);
  
  // Parse JSON command
  // Expected: {"action":"unlock","wallet":"...","currency":"TSE","txHash":"..."}
  
  if (strstr(cmdBuffer, "\"action\":\"unlock\"") != nullptr) {
    // Extract wallet
    const char* walletStart = strstr(cmdBuffer, "\"wallet\":\"");
    if (walletStart) {
      walletStart += 10;
      const char* walletEnd = strchr(walletStart, '"');
      if (walletEnd) {
        sessionWallet = String(walletStart).substring(0, walletEnd - walletStart);
      }
    }
    
    // Extract currency
    if (strstr(cmdBuffer, "\"currency\":\"USDC\"") != nullptr) {
      sessionCurrency = "USDC (Base)";
    } else {
      sessionCurrency = "TSE (Solana)";
    }
    
    // Extract txHash
    const char* txStart = strstr(cmdBuffer, "\"txHash\":\"");
    if (txStart) {
      txStart += 10;
      const char* txEnd = strchr(txStart, '"');
      if (txEnd) {
        sessionTxHash = String(txStart).substring(0, txEnd - txStart);
      }
    }
    
    startSession();
    
  } else if (strstr(cmdBuffer, "\"action\":\"lock\"") != nullptr) {
    Serial.println("🔒 Lock command received");
    endSession();
    
  } else if (strstr(cmdBuffer, "\"action\":\"status\"") != nullptr) {
    updateBLEStatus();
    
  } else if (strstr(cmdBuffer, "\"action\":\"restore\"") != nullptr) {
    // Restore existing session
    const char* remainingStart = strstr(cmdBuffer, "\"remainingMs\":");
    if (remainingStart) {
      remainingStart += 14;
      unsigned long remainingMs = atol(remainingStart);
      
      if (remainingMs > 0) {
        sessionEndTime = millis() + remainingMs;
        sessionActive = true;
        releaseLock();
        
        Serial.println("🔄 Session restored!");
        Serial.print("   Remaining: ");
        Serial.println(TSE_FormatTime(remainingMs / 1000, timeBuffer));
      }
    }
  }
}

// ============ SESSION MANAGEMENT ============
void startSession() {
  Serial.println("\n✅ UNLOCKING!");
  Serial.print("   Paid with: ");
  Serial.println(sessionCurrency);
  Serial.print("   Wallet: ");
  Serial.println(sessionWallet.substring(0, 8) + "...");
  
  sessionActive = true;
  sessionEndTime = millis() + SESSION_DURATION_MS;
  
  releaseLock();
  updateBLEStatus();
  
  Serial.println("🔓 Lock released - 30 minute session started");
}

void endSession() {
  sessionActive = false;
  sessionEndTime = 0;
  sessionWallet = "";
  sessionCurrency = "";
  sessionTxHash = "";
  
  engageLock();
  updateBLEStatus();
  
  Serial.println("🔒 Lock engaged - session ended");
  Serial.println("   Ready for next customer");
}

// ============ LOCK CONTROL ============
void engageLock() {
  digitalWrite(LOCK_PIN, LOW);  // Adjust based on your lock type
  isLocked = true;
}

void releaseLock() {
  digitalWrite(LOCK_PIN, HIGH);  // Adjust based on your lock type
  isLocked = false;
}

// ============ BLE STATUS ============
void updateBLEStatus() {
  char status[256];
  unsigned long remaining = 0;
  
  if (sessionActive && sessionEndTime > millis()) {
    remaining = (sessionEndTime - millis()) / 1000;
  }
  
  snprintf(status, sizeof(status),
    "{\"locked\":%s,\"sessionActive\":%s,\"remainingSeconds\":%lu,\"deviceId\":\"%s\"}",
    isLocked ? "true" : "false",
    sessionActive ? "true" : "false",
    remaining,
    DEVICE_ID
  );
  
  statusChar.writeValue(status);
}

// ============ STATUS LED ============
void updateStatusLED(unsigned long now) {
  static unsigned long lastBlink = 0;
  
  if (sessionActive) {
    // Solid on when unlocked
    digitalWrite(STATUS_LED, HIGH);
  } else {
    // Slow blink when locked
    if (now - lastBlink >= 1000) {
      lastBlink = now;
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    }
  }
}
