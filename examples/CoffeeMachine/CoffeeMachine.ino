/*
 * TSE-X Coffee Machine Example
 * Arduino Giga R1 WiFi
 * 
 * Pay-per-brew coffee maker with touchscreen interface.
 * Supports Hot Brew and Over Ice selections.
 * 
 * Pricing: $0.50 per brew (TSE or USDC)
 * 
 * Hardware:
 * - Arduino Giga R1 WiFi
 * - Giga Display Shield (touchscreen)
 * - Relay module on pin 2 (Hot Brew)
 * - Relay module on pin 3 (Over Ice)
 * 
 * Flow:
 * 1. Display shows "Scan to Pay" screen
 * 2. User scans QR and pays in app
 * 3. Arduino detects payment, shows brew selection
 * 4. User selects Hot or Iced (on touchscreen or app)
 * 5. Arduino activates appropriate relay
 * 6. Brewing completes, session ends
 */

#include <WiFi.h>
#include <ArduinoHttpClient.h>
#include <TSE_X402.h>

// For Giga Display (optional - remove if not using display)
// #include <Arduino_GigaDisplay_GFX.h>
// #include <Arduino_GigaDisplayTouch.h>

// ============ CONFIGURATION ============
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* BACKEND_HOST = TSE_DEFAULT_BACKEND_HOST;
const int BACKEND_PORT = TSE_DEFAULT_BACKEND_PORT;

const char* DEVICE_ID = "X402-COFFEE-001";
const char* DEVICE_SECRET = "";  // Optional: from Device Creator

// ============ HARDWARE PINS ============
const int HOT_BREW_PIN = 2;    // Relay for hot brew
const int OVER_ICE_PIN = 3;    // Relay for over ice
const int STATUS_LED = LED_BUILTIN;

// ============ BREW TIMING ============
const int HOT_BREW_SECONDS = 130;   // 2 min 10 sec
const int ICED_BREW_SECONDS = 150;  // 2 min 30 sec

// ============ STATE ============
enum UIState {
  UI_WAITING_PAYMENT,
  UI_SELECT_BREW,
  UI_BREWING,
  UI_COMPLETE
};

UIState uiState = UI_WAITING_PAYMENT;
bool accessGranted = false;
bool brewingActive = false;
bool isHotBrew = true;

unsigned long brewStartTime = 0;
unsigned long brewDuration = 0;
unsigned long lastPollMs = 0;

// WiFi and HTTP
WiFiSSLClient wifiClient;
HttpClient* httpClient = nullptr;
bool wifiConnected = false;

char responseBuffer[1024];
char pathBuffer[128];
char timeBuffer[16];

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n========================================");
  Serial.println("   TSE-X Coffee Machine");
  Serial.println("   Arduino Giga R1 WiFi");
  Serial.println("   " TSE_X402_VERSION);
  Serial.println("========================================");
  Serial.print("Device: ");
  Serial.println(DEVICE_ID);
  Serial.println("Price: $0.50 per brew");
  Serial.println("Accepts: TSE (Solana) & USDC (Base)");
  Serial.println();
  
  // Initialize pins
  pinMode(HOT_BREW_PIN, OUTPUT);
  pinMode(OVER_ICE_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  
  // Start with relays OFF
  digitalWrite(HOT_BREW_PIN, LOW);
  digitalWrite(OVER_ICE_PIN, LOW);
  
  connectWiFi();
  
  Serial.println("☕ Ready! Waiting for payment...");
}

// ============ MAIN LOOP ============
void loop() {
  unsigned long now = millis();
  
  // Handle brewing timer
  if (brewingActive && brewStartTime > 0) {
    unsigned long elapsed = now - brewStartTime;
    if (elapsed >= brewDuration) {
      completeBrew();
    }
  }
  
  // Poll backend
  unsigned long pollInterval = (uiState == UI_WAITING_PAYMENT) ? 1500 : 2000;
  if (wifiConnected && (now - lastPollMs >= pollInterval)) {
    lastPollMs = now;
    pollBackend();
  }
  
  // Status LED
  updateStatusLED(now);
  
  delay(50);
}

// ============ WIFI ============
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✅ WiFi connected!");
    Serial.print("   IP: ");
    Serial.println(WiFi.localIP());
    
    httpClient = new HttpClient(wifiClient, BACKEND_HOST, BACKEND_PORT);
    httpClient->setTimeout(10000);
  } else {
    Serial.println("\n❌ WiFi failed!");
  }
}

// ============ BACKEND POLLING ============
void pollBackend() {
  if (!httpClient) return;
  
  // Don't poll during brewing
  if (uiState == UI_BREWING || uiState == UI_COMPLETE) return;
  
  // Build URL
  if (strlen(DEVICE_SECRET) > 0) {
    snprintf(pathBuffer, sizeof(pathBuffer), 
             "/x402/%s/status?deviceSecret=%s", DEVICE_ID, DEVICE_SECRET);
  } else {
    snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/status", DEVICE_ID);
  }
  
  Serial.print("Polling: ");
  
  httpClient->get(pathBuffer);
  int status = httpClient->responseStatusCode();
  
  httpClient->skipResponseHeaders();
  int len = 0;
  while (httpClient->available() && len < sizeof(responseBuffer) - 1) {
    responseBuffer[len++] = httpClient->read();
  }
  responseBuffer[len] = '\0';
  httpClient->stop();
  
  Serial.println(status);
  
  if (status == TSE_HTTP_OK) {
    bool hasAccess = TSE_ParseAccessGranted(responseBuffer);
    bool isBrewing = (strstr(responseBuffer, "\"brewing\":true") != nullptr);
    bool isHot = (strstr(responseBuffer, "\"brewType\":\"hot\"") != nullptr);
    
    Serial.print("   access=");
    Serial.print(hasAccess ? "yes" : "no");
    Serial.print(", brewing=");
    Serial.println(isBrewing ? "yes" : "no");
    
    if (hasAccess) {
      if (!accessGranted) {
        // Payment just received!
        accessGranted = true;
        TSE_Currency currency = TSE_ParseCurrency(responseBuffer);
        Serial.println("\n✅ PAYMENT RECEIVED!");
        Serial.print("   Paid with: ");
        Serial.println(TSE_GetCurrencyString(currency));
      }
      
      // Check if app started brewing
      if (isBrewing && uiState != UI_BREWING) {
        Serial.println("☕ App started brewing - syncing!");
        startBrew(isHot);
      } 
      // Show selection screen if not brewing yet
      else if (uiState == UI_WAITING_PAYMENT && !isBrewing) {
        uiState = UI_SELECT_BREW;
        Serial.println("🎯 Select your brew: Hot or Iced");
        Serial.println("   (Select in app or on touchscreen)");
      }
      
    } else {
      // No access
      if (accessGranted || uiState != UI_WAITING_PAYMENT) {
        accessGranted = false;
        uiState = UI_WAITING_PAYMENT;
        Serial.println("⏳ Session ended - waiting for payment");
      }
    }
    
  } else if (status == TSE_HTTP_PAYMENT_REQUIRED) {
    if (uiState != UI_WAITING_PAYMENT) {
      accessGranted = false;
      uiState = UI_WAITING_PAYMENT;
      Serial.println("💳 Payment required");
    }
  }
}

// ============ BREWING ============
void startBrew(bool hot) {
  isHotBrew = hot;
  brewingActive = true;
  brewStartTime = millis();
  brewDuration = (hot ? HOT_BREW_SECONDS : ICED_BREW_SECONDS) * 1000UL;
  uiState = UI_BREWING;
  
  // Activate relay
  if (hot) {
    digitalWrite(HOT_BREW_PIN, HIGH);
    Serial.println("\n☕ BREWING HOT COFFEE...");
  } else {
    digitalWrite(OVER_ICE_PIN, HIGH);
    Serial.println("\n🧊 BREWING OVER ICE...");
  }
  
  Serial.print("   Duration: ");
  Serial.println(TSE_FormatTime(brewDuration / 1000, timeBuffer));
  
  // Notify backend
  notifyBrewStart(hot);
}

void completeBrew() {
  // Turn off relays
  digitalWrite(HOT_BREW_PIN, LOW);
  digitalWrite(OVER_ICE_PIN, LOW);
  
  brewingActive = false;
  brewStartTime = 0;
  uiState = UI_COMPLETE;
  
  Serial.println("\n✅ BREW COMPLETE!");
  Serial.println("   Enjoy your coffee! ☕");
  
  // Notify backend
  notifyBrewComplete();
  
  // Reset after delay
  delay(3000);
  accessGranted = false;
  uiState = UI_WAITING_PAYMENT;
  Serial.println("\n⏳ Ready for next customer...");
}

// ============ BACKEND NOTIFICATIONS ============
void notifyBrewStart(bool hot) {
  if (!httpClient) return;
  
  snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/brew", DEVICE_ID);
  const char* body = hot ? "{\"brewType\":\"hot\"}" : "{\"brewType\":\"iced\"}";
  
  httpClient->post(pathBuffer, "application/json", body);
  httpClient->responseStatusCode();
  httpClient->skipResponseHeaders();
  while (httpClient->available()) httpClient->read();
  httpClient->stop();
}

void notifyBrewComplete() {
  if (!httpClient) return;
  
  snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/complete", DEVICE_ID);
  
  httpClient->post(pathBuffer, "application/json", "{}");
  httpClient->responseStatusCode();
  httpClient->skipResponseHeaders();
  while (httpClient->available()) httpClient->read();
  httpClient->stop();
}

// ============ STATUS LED ============
void updateStatusLED(unsigned long now) {
  static unsigned long lastBlink = 0;
  
  switch (uiState) {
    case UI_WAITING_PAYMENT:
      // Slow blink
      if (now - lastBlink >= 1000) {
        lastBlink = now;
        digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      }
      break;
      
    case UI_SELECT_BREW:
      // Fast blink
      if (now - lastBlink >= 250) {
        lastBlink = now;
        digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      }
      break;
      
    case UI_BREWING:
      // Solid on
      digitalWrite(STATUS_LED, HIGH);
      break;
      
    case UI_COMPLETE:
      // Double blink
      digitalWrite(STATUS_LED, (now / 150) % 4 < 2);
      break;
  }
}
