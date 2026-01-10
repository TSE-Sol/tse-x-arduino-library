/*
 * TSE-X Power Switch Example
 * Arduino MKR WiFi 1010
 * 
 * Time-based power control with TSE/USDC payments.
 * User selects duration in app, pays, and relay activates.
 * 
 * Pricing:
 * - 30 min: $0.05
 * - 1 hour: $0.10
 * - 12 hours: $0.50
 * - Custom: $0.05 per 30 min
 * 
 * Hardware:
 * - Arduino MKR WiFi 1010
 * - Relay module on pin 2
 */

#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <TSE_X402.h>

// ============ CONFIGURATION ============
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* BACKEND_HOST = TSE_DEFAULT_BACKEND_HOST;
const int BACKEND_PORT = TSE_DEFAULT_BACKEND_PORT;

const char* DEVICE_ID = "POWER-SWITCH-001";
const char* DEVICE_SECRET = "";  // Optional

// ============ HARDWARE ============
const int RELAY_PIN = 2;
const int STATUS_LED = LED_BUILTIN;

// ============ STATE ============
WiFiSSLClient wifiClient;
HttpClient* httpClient = nullptr;

bool wifiConnected = false;
bool powerOn = false;
unsigned long sessionEndTime = 0;
unsigned long lastPollMs = 0;

char responseBuffer[1024];
char pathBuffer[128];
char timeBuffer[16];

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n========================================");
  Serial.println("   TSE-X Power Switch");
  Serial.println("   " TSE_X402_VERSION);
  Serial.println("========================================");
  Serial.print("Device: ");
  Serial.println(DEVICE_ID);
  Serial.println("Accepts: TSE (Solana) & USDC (Base)");
  Serial.println();
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  connectWiFi();
}

// ============ LOOP ============
void loop() {
  unsigned long now = millis();
  
  // Check session expiry
  if (powerOn && sessionEndTime > 0 && now >= sessionEndTime) {
    endSession();
  }
  
  // Poll backend
  unsigned long interval = powerOn ? TSE_DEFAULT_POLL_ACTIVE : TSE_DEFAULT_POLL_IDLE;
  if (wifiConnected && (now - lastPollMs >= interval)) {
    lastPollMs = now;
    pollBackend();
  }
  
  // Status LED
  digitalWrite(STATUS_LED, powerOn ? HIGH : ((millis() / 1000) % 2));
  
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
    
    httpClient = new HttpClient(wifiClient, BACKEND_HOST, BACKEND_PORT);
    httpClient->setTimeout(10000);
  } else {
    Serial.println(" Failed!");
  }
}

// ============ POLL BACKEND ============
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
  Serial.println(status);
  
  if (status == TSE_HTTP_OK) {
    bool hasAccess = TSE_ParseAccessGranted(responseBuffer);
    int remaining = TSE_ParseRemainingSeconds(responseBuffer);
    
    if (hasAccess && remaining > 0 && !powerOn) {
      TSE_Currency currency = TSE_ParseCurrency(responseBuffer);
      Serial.print("✅ Payment received! ");
      Serial.println(TSE_GetCurrencyString(currency));
      startSession(remaining);
    } else if (!hasAccess && powerOn) {
      endSession();
    }
  } else if (status == TSE_HTTP_PAYMENT_REQUIRED && powerOn) {
    endSession();
  }
}

// ============ SESSION ============
void startSession(int seconds) {
  sessionEndTime = millis() + (seconds * 1000UL);
  powerOn = true;
  digitalWrite(RELAY_PIN, HIGH);
  
  Serial.println("⚡ POWER ON");
  Serial.print("Duration: ");
  Serial.println(TSE_FormatTime(seconds, timeBuffer));
  
  notifyBackend("/x402/" + String(DEVICE_ID) + "/brew", "{\"brewType\":\"power_on\"}");
}

void endSession() {
  powerOn = false;
  sessionEndTime = 0;
  digitalWrite(RELAY_PIN, LOW);
  
  Serial.println("🔌 POWER OFF");
  
  notifyBackend("/x402/" + String(DEVICE_ID) + "/complete", "{}");
}

void notifyBackend(String path, String body) {
  if (!httpClient) return;
  httpClient->post(path.c_str(), "application/json", body.c_str());
  httpClient->responseStatusCode();
  httpClient->skipResponseHeaders();
  while (httpClient->available()) httpClient->read();
  httpClient->stop();
}
