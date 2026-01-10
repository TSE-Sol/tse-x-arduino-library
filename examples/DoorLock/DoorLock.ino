/*
 * TSE-X Door Lock Example
 * Arduino MKR WiFi 1010
 * 
 * WiFi-based door lock controlled via X.402 payments.
 * User pays in app, backend grants access, Arduino unlocks.
 * 
 * Pricing: $0.50 for 30 minutes access
 * 
 * Hardware:
 * - Arduino MKR WiFi 1010
 * - Electric door strike or solenoid on pin 2
 * - LED indicator on LED_BUILTIN
 * - Optional: Buzzer on pin 3 for feedback
 * 
 * Flow:
 * 1. Arduino polls backend every 1.5 seconds
 * 2. User scans QR code and pays in app
 * 3. Backend returns accessGranted: true
 * 4. Arduino unlocks door for 30 minutes
 * 5. Timer expires → door locks
 */

#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <TSE_X402.h>

// ============ CONFIGURATION ============
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* BACKEND_HOST = TSE_DEFAULT_BACKEND_HOST;
const int BACKEND_PORT = TSE_DEFAULT_BACKEND_PORT;

const char* DEVICE_ID = "DOOR-LOCK-001";
const char* DEVICE_SECRET = "";  // Optional

// ============ HARDWARE PINS ============
const int LOCK_PIN = 2;         // Electric strike relay
const int BUZZER_PIN = 3;       // Optional buzzer
const int STATUS_LED = LED_BUILTIN;

// ============ TIMING ============
const unsigned long SESSION_DURATION_MS = 30 * 60 * 1000;  // 30 minutes
const unsigned long POLL_INTERVAL_IDLE = 1500;
const unsigned long POLL_INTERVAL_ACTIVE = 3000;

// ============ STATE ============
bool wifiConnected = false;
bool isLocked = true;
bool sessionActive = false;
unsigned long sessionEndTime = 0;
unsigned long lastPollMs = 0;

String sessionCurrency = "";

// WiFi and HTTP
WiFiSSLClient wifiClient;
HttpClient* httpClient = nullptr;

char responseBuffer[1024];
char pathBuffer[128];
char timeBuffer[16];

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n========================================");
  Serial.println("   TSE-X Door Lock");
  Serial.println("   Arduino MKR WiFi 1010");
  Serial.println("   " TSE_X402_VERSION);
  Serial.println("========================================");
  Serial.print("Device: ");
  Serial.println(DEVICE_ID);
  Serial.println("Price: $0.50 for 30 minutes");
  Serial.println("Accepts: TSE (Solana) & USDC (Base)");
  Serial.println();
  
  // Initialize pins
  pinMode(LOCK_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  
  // Start locked
  engageLock();
  
  connectWiFi();
  
  Serial.println("🔒 Door locked - waiting for payment");
}

// ============ MAIN LOOP ============
void loop() {
  unsigned long now = millis();
  
  // Check session timeout
  if (sessionActive && sessionEndTime > 0 && now >= sessionEndTime) {
    Serial.println("\n⏰ Session expired!");
    endSession();
  }
  
  // Poll backend
  unsigned long pollInterval = sessionActive ? POLL_INTERVAL_ACTIVE : POLL_INTERVAL_IDLE;
  if (wifiConnected && (now - lastPollMs >= pollInterval)) {
    lastPollMs = now;
    pollBackend();
  }
  
  // Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    connectWiFi();
  }
  
  // Status LED
  updateStatusLED(now);
  
  delay(100);
}

// ============ WIFI ============
void connectWiFi() {
  Serial.print("Connecting to WiFi...");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println(" Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    
    if (httpClient) delete httpClient;
    httpClient = new HttpClient(wifiClient, BACKEND_HOST, BACKEND_PORT);
    httpClient->setTimeout(10000);
  } else {
    Serial.println(" Failed!");
  }
}

// ============ BACKEND POLLING ============
void pollBackend() {
  if (!httpClient) return;
  
  // Build URL
  if (strlen(DEVICE_SECRET) > 0) {
    snprintf(pathBuffer, sizeof(pathBuffer), 
             "/x402/%s/status?deviceSecret=%s", DEVICE_ID, DEVICE_SECRET);
  } else {
    snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/status", DEVICE_ID);
  }
  
  httpClient->get(pathBuffer);
  int status = httpClient->responseStatusCode();
  
  httpClient->skipResponseHeaders();
  int len = 0;
  while (httpClient->available() && len < sizeof(responseBuffer) - 1) {
    responseBuffer[len++] = httpClient->read();
  }
  responseBuffer[len] = '\0';
  httpClient->stop();
  
  Serial.print("Poll: ");
  Serial.print(status);
  
  if (status == TSE_HTTP_OK) {
    bool hasAccess = TSE_ParseAccessGranted(responseBuffer);
    int remaining = TSE_ParseRemainingSeconds(responseBuffer);
    
    Serial.print(" access=");
    Serial.println(hasAccess ? "yes" : "no");
    
    if (hasAccess && remaining > 0) {
      if (!sessionActive) {
        // New session - payment received!
        sessionCurrency = TSE_GetCurrencyString(TSE_ParseCurrency(responseBuffer));
        Serial.println("\n✅ PAYMENT RECEIVED!");
        Serial.print("   Paid with: ");
        Serial.println(sessionCurrency);
        startSession(remaining);
      } else {
        // Update remaining time from backend
        sessionEndTime = millis() + (remaining * 1000UL);
      }
    } else if (!hasAccess && sessionActive) {
      Serial.println("⏳ Access revoked");
      endSession();
    }
    
  } else if (status == TSE_HTTP_PAYMENT_REQUIRED) {
    Serial.println(" - waiting for payment");
    if (sessionActive) {
      endSession();
    }
  } else {
    Serial.println();
  }
}

// ============ SESSION MANAGEMENT ============
void startSession(int durationSeconds) {
  sessionActive = true;
  sessionEndTime = millis() + (durationSeconds * 1000UL);
  
  releaseLock();
  playTone(2);  // Success beep
  
  Serial.println("\n🔓 DOOR UNLOCKED!");
  Serial.print("   Access for: ");
  Serial.println(TSE_FormatTime(durationSeconds, timeBuffer));
}

void endSession() {
  sessionActive = false;
  sessionEndTime = 0;
  sessionCurrency = "";
  
  engageLock();
  playTone(1);  // Lock beep
  
  Serial.println("🔒 Door locked - session ended");
  Serial.println("   Ready for next customer");
}

// ============ LOCK CONTROL ============
void engageLock() {
  digitalWrite(LOCK_PIN, LOW);  // De-energize strike (locked)
  isLocked = true;
}

void releaseLock() {
  digitalWrite(LOCK_PIN, HIGH);  // Energize strike (unlocked)
  isLocked = false;
}

// ============ AUDIO FEEDBACK ============
void playTone(int beeps) {
  for (int i = 0; i < beeps; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < beeps - 1) delay(100);
  }
}

// ============ STATUS LED ============
void updateStatusLED(unsigned long now) {
  static unsigned long lastBlink = 0;
  
  if (sessionActive) {
    // Solid green when unlocked
    digitalWrite(STATUS_LED, HIGH);
  } else {
    // Slow blink when locked
    if (now - lastBlink >= 1000) {
      lastBlink = now;
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    }
  }
}
