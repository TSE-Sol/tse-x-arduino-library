#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include <WiFi.h>
#include <ArduinoBLE.h>
#include <ArduinoHttpClient.h>
#include <mbed.h>
#include <rtos.h>
#include <cstring>  // For strstr() - memory efficient string search

using namespace mbed;
using namespace rtos;

GigaDisplay_GFX display;
Arduino_GigaDisplayTouch touchDetector;

// ============ CONFIGURATION ============
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* BACKEND_HOST = "tse-x-backend.onrender.com";
const int BACKEND_PORT = 443;
const char* DEVICE_ID = "YOUR_DEVICE_ID";

// Device Secret - Get this from the app's Device Creator
// Leave as empty string "" to run as unclaimed device
const char* DEVICE_SECRET = "";  // Empty = unclaimed, or paste your secret here

// ============ MEMORY-OPTIMIZED BUFFERS ============
// Pre-allocated to avoid heap fragmentation from String objects
char httpResponseBuffer[1024];  // Main thread HTTP responses (increased for full JSON)
char bgResponseBuffer[512];     // Background thread HTTP responses
char pathBuffer[128];           // URL path buffer

// ============ RELAY PINS ============
const int HOT_BREW_PIN = 2;   // D2 - Hot Brew relay
const int OVER_ICE_PIN = 3;   // D3 - Over Ice relay
const unsigned long RELAY_PULSE_MS = 150;  // Button press duration

// Timing
const unsigned long POLL_INTERVAL_MS = 1500;  // Faster polling for quicker response
const unsigned long POLL_INTERVAL_BREWING_MS = 3000;  // Poll every 3 seconds during brewing
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;  // Retry WiFi every 10 seconds

// ============ THREAD-SAFE SESSION STATE ============
volatile bool sessionEndedByApp = false;  // Set by background thread
volatile bool shouldPollInBackground = false;  // Signals background thread to poll
Mutex sessionMutex;  // Protects shared state

// Background polling thread - larger stack for SSL operations
Thread pollThread(osPriorityNormal, 16384);
bool pollThreadStarted = false;

// -------- Colors (16-bit RGB565) --------
#define COLOR_BG           0x0841  // dark bluish
#define COLOR_APPBAR       0x0011  // very dark navy
#define COLOR_TEXT         0xFFFF  // white
#define COLOR_SUBTEXT      0xC618  // light gray
#define COLOR_CARD         0x18C3  // dark gray card
#define COLOR_CARD_OUT     0x39E7  // lighter outline
#define COLOR_HOT_ACCENT   0xFBA0  // warm orange
#define COLOR_ICE_ACCENT   0x4E7F  // teal-ish
#define COLOR_SHADOW       0x0000  // black

// Confirm button colors
#define COLOR_CONFIRM_DISABLED_BG   COLOR_CARD
#define COLOR_CONFIRM_DISABLED_OUT  COLOR_CARD_OUT
#define COLOR_CONFIRM_ENABLED_BG    0x03A0  // darker green
#define COLOR_CONFIRM_ENABLED_OUT   0x03E0
#define COLOR_CONFIRM_PRESSED_BG    0x07E0  // bright green
#define COLOR_CONFIRM_PRESSED_OUT   0x03E0

// WiFi indicator colors
#define COLOR_WIFI_GREEN   0x07E0
#define COLOR_WIFI_YELLOW  0xFFE0
#define COLOR_WIFI_RED     0xF800

// Layout globals
int16_t screenW, screenH;

// Button rects
struct ButtonRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

ButtonRect hotBtn;
ButtonRect iceBtn;
ButtonRect confirmBtn;
ButtonRect stopBtn;  // Stop button for brewing screen

unsigned long lastTouchMs = 0;
const unsigned long TOUCH_DEBOUNCE_MS = 200;

// ---------- UI State ----------
enum UiState {
  UI_WAITING_PAYMENT,
  UI_SELECT_BREW,
  UI_CONFIRM_BREW,
  UI_LOCKED_BREWING,
  UI_THANK_YOU       // NEW: Show after brew completes
};

UiState uiState = UI_WAITING_PAYMENT;

enum BrewSelection {
  SEL_NONE,
  SEL_HOT,
  SEL_ICE
};

BrewSelection selectedBrew = SEL_NONE;

// ---------- WiFi State ----------
WiFiSSLClient wifiClient;
HttpClient* httpClient = nullptr;

bool wifiConnected = false;
bool wifiConnecting = false;
unsigned long lastWifiRetryMs = 0;
unsigned long wifiConnectStartMs = 0;

// ---------- Payment State ----------
bool accessGranted = false;
unsigned long lastPollMs = 0;

// ---------- BLE ----------
BLEService deviceInfoService("b3c8f420-0000-4020-8000-000000000000");
BLECharacteristic deviceInfoChar("b3c8f420-0001-4020-8000-000000000000", BLERead, 512);

// BLE device info will be built dynamically in initBLE() using DEVICE_ID

// ---------- Brewing Timer / Animation ----------
const unsigned long BREW_HOT_MS = 130000;   // Hot Brew: 130 seconds
const unsigned long BREW_ICE_MS = 150000;   // Over Ice: 150 seconds
unsigned long currentBrewTotalMs = BREW_HOT_MS;  // Will be set based on selection
unsigned long brewStartMs = 0;
bool brewingActive = false;

int cupCx = 0;
int cupCy = 0;

int steamPhase = 0;
unsigned long lastSteamUpdateMs = 0;
const unsigned long STEAM_INTERVAL_MS = 200;

// Thank you screen timer
unsigned long thankYouStartMs = 0;
const unsigned long THANK_YOU_DURATION_MS = 3000; // Show for 3 seconds

// Track last displayed time to avoid unnecessary redraws
uint16_t lastDisplayedSec = 0xFFFF;  // Invalid value to force first draw

// ---------- Utility: Rounded Rect (fake) ----------
void fillRoundRectFake(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  display.fillRect(x + r, y, w - 2 * r, h, color);
  display.fillRect(x, y + r, r, h - 2 * r, color);
  display.fillRect(x + w - r, y + r, r, h - 2 * r, color);

  display.fillCircle(x + r,         y + r,         r, color);
  display.fillCircle(x + w - r - 1, y + r,         r, color);
  display.fillCircle(x + r,         y + h - r - 1, r, color);
  display.fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}

void drawCardShadow(const ButtonRect &btn) {
  int16_t offset = 4;
  fillRoundRectFake(btn.x + offset, btn.y + offset, btn.w, btn.h, 18, COLOR_SHADOW);
}

// ---------- Text helpers ----------
void drawCenteredText(const char* txt, int16_t cx, int16_t cy, uint8_t textSize, uint16_t color, uint16_t bg) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(textSize);
  display.setTextColor(color, bg);
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  int16_t x = cx - w / 2;
  int16_t y = cy - h / 2;
  display.setCursor(x, y);
  display.print(txt);
}

// ---------- WiFi Indicator ----------
void drawWifiIndicator() {
  int16_t indicatorX = screenW - 25;
  int16_t indicatorY = 30;
  int16_t radius = 8;
  
  // Clear old indicator
  display.fillCircle(indicatorX, indicatorY, radius + 2, COLOR_APPBAR);
  
  // Draw new indicator
  uint16_t color;
  if (wifiConnecting) {
    color = COLOR_WIFI_YELLOW;
  } else if (wifiConnected) {
    color = COLOR_WIFI_GREEN;
  } else {
    color = COLOR_WIFI_RED;
  }
  
  display.fillCircle(indicatorX, indicatorY, radius, color);
}

// ---------- Icons ----------
void drawHotIcon(const ButtonRect &btn) {
  int16_t cx = btn.x + btn.w / 4;
  int16_t cy = btn.y + btn.h / 2;

  display.fillRoundRect(cx - 25, cy - 15, 50, 35, 6, COLOR_HOT_ACCENT);
  display.drawFastHLine(cx - 28, cy - 15, 56, COLOR_HOT_ACCENT);
  display.drawFastHLine(cx - 35, cy + 22, 70, COLOR_HOT_ACCENT);

  display.drawLine(cx - 10, cy - 25, cx - 5,  cy - 35, COLOR_HOT_ACCENT);
  display.drawLine(cx,      cy - 25, cx + 5,  cy - 35, COLOR_HOT_ACCENT);
  display.drawLine(cx + 10, cy - 25, cx + 15, cy - 35, COLOR_HOT_ACCENT);
}

void drawIceIcon(const ButtonRect &btn) {
  int16_t cx = btn.x + btn.w / 4;
  int16_t cy = btn.y + btn.h / 2;

  display.fillRoundRect(cx - 25, cy - 20, 50, 45, 6, COLOR_ICE_ACCENT);
  display.drawLine(cx - 12, cy - 30, cx - 2,  cy - 40, COLOR_ICE_ACCENT);
  display.drawLine(cx - 2,  cy - 40, cx + 4,  cy - 36, COLOR_ICE_ACCENT);

  display.drawRect(cx - 15, cy - 12, 10, 10, COLOR_BG);
  display.drawRect(cx,      cy - 5,  10, 10, COLOR_BG);
  display.drawRect(cx - 5,  cy + 5,  10, 10, COLOR_BG);
}

// ---------- Buttons ----------
void drawButtonCard(const ButtonRect &btn, bool isHot, bool highlighted) {
  uint16_t cardColor   = COLOR_CARD;
  uint16_t outline     = COLOR_CARD_OUT;
  uint16_t accent      = isHot ? COLOR_HOT_ACCENT : COLOR_ICE_ACCENT;
  const char* label    = isHot ? "Hot Brew" : "Over Ice";

  if (highlighted) {
    cardColor = outline;
  }

  drawCardShadow(btn);
  fillRoundRectFake(btn.x, btn.y, btn.w, btn.h, 18, cardColor);
  display.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 18, accent);

  if (isHot) drawHotIcon(btn);
  else       drawIceIcon(btn);

  int16_t labelCx = btn.x + (btn.w * 3) / 4;
  int16_t labelCy = btn.y + btn.h / 2 - 15;
  drawCenteredText(label, labelCx, labelCy, 5, COLOR_TEXT, cardColor);

  const char* hint = isHot ? "Hot coffee" : "Pour over ice";
  drawCenteredText(hint, labelCx, labelCy + 38, 3, COLOR_SUBTEXT, cardColor);
}

void drawConfirmButton(bool enabled, bool pressed) {
  uint16_t bg, outline, txtColor;

  if (!enabled) {
    bg       = COLOR_CONFIRM_DISABLED_BG;
    outline  = COLOR_CONFIRM_DISABLED_OUT;
    txtColor = COLOR_SUBTEXT;
  } else if (pressed) {
    bg       = COLOR_CONFIRM_PRESSED_BG;
    outline  = COLOR_CONFIRM_PRESSED_OUT;
    txtColor = 0x0000;
  } else {
    bg       = COLOR_CONFIRM_ENABLED_BG;
    outline  = COLOR_CONFIRM_ENABLED_OUT;
    txtColor = COLOR_TEXT;
  }

  fillRoundRectFake(confirmBtn.x, confirmBtn.y, confirmBtn.w, confirmBtn.h, 16, bg);
  display.drawRoundRect(confirmBtn.x, confirmBtn.y, confirmBtn.w, confirmBtn.h, 16, outline);

  drawCenteredText("Confirm Brew", confirmBtn.x + confirmBtn.w / 2,
                   confirmBtn.y + confirmBtn.h / 2, 4, txtColor, bg);
}

// ---------- App Bars ----------
void drawAppBarWaiting() {
  display.fillRect(0, 0, screenW, 60, COLOR_APPBAR);
  drawCenteredText("Mr. Coffee - Pay to Brew", screenW / 2, 22, 3, COLOR_TEXT, COLOR_APPBAR);
  
  // Status message based on WiFi state
  if (wifiConnecting) {
    drawCenteredText("Connecting to WiFi...", screenW / 2, 44, 2, COLOR_WIFI_YELLOW, COLOR_APPBAR);
  } else if (wifiConnected) {
    drawCenteredText("Waiting for payment...", screenW / 2, 44, 2, COLOR_WIFI_GREEN, COLOR_APPBAR);
  } else {
    drawCenteredText("WiFi disconnected", screenW / 2, 44, 2, COLOR_WIFI_RED, COLOR_APPBAR);
  }
  
  drawWifiIndicator();
}

void drawAppBarSelect() {
  display.fillRect(0, 0, screenW, 60, COLOR_APPBAR);
  drawCenteredText("Mr. Coffee - Pay to Brew", screenW / 2, 22, 3, COLOR_TEXT, COLOR_APPBAR);
  drawCenteredText("Choose your drink", screenW / 2, 44, 2, COLOR_SUBTEXT, COLOR_APPBAR);
  drawWifiIndicator();
}

void drawAppBarConfirm() {
  display.fillRect(0, 0, screenW, 60, COLOR_APPBAR);
  drawCenteredText("Confirm Selection", screenW / 2, 22, 3, COLOR_TEXT, COLOR_APPBAR);
  drawCenteredText("Tap Confirm Brew to start", screenW / 2, 44, 2, COLOR_SUBTEXT, COLOR_APPBAR);
  drawWifiIndicator();
}

void drawAppBarBrewing() {
  display.fillRect(0, 0, screenW, 60, COLOR_APPBAR);
  drawCenteredText("Brewing...", screenW / 2, 32, 5, COLOR_TEXT, COLOR_APPBAR);
  drawWifiIndicator();
}

void drawBackground() {
  display.fillScreen(COLOR_BG);
}

// ---------- Layout ----------
void layoutButtons() {
  int16_t marginSide  = 40;
  int16_t marginTop   = 80;
  int16_t spacing     = 30;
  int16_t bottomArea  = 110;
  int16_t usableH     = screenH - marginTop - bottomArea - spacing;
  int16_t cardHeight  = (usableH - spacing) / 2;
  int16_t cardWidth   = screenW - 2 * marginSide;

  hotBtn.x = marginSide;
  hotBtn.y = marginTop;
  hotBtn.w = cardWidth;
  hotBtn.h = cardHeight;

  iceBtn.x = marginSide;
  iceBtn.y = marginTop + cardHeight + spacing;
  iceBtn.w = cardWidth;
  iceBtn.h = cardHeight;

  confirmBtn.w = cardWidth;
  confirmBtn.h = 60;
  confirmBtn.x = marginSide;
  confirmBtn.y = screenH - bottomArea + 25;
}

bool pointInButton(int16_t x, int16_t y, const ButtonRect &btn) {
  return (x >= btn.x && x <= btn.x + btn.w &&
          y >= btn.y && y <= btn.y + btn.h);
}

// ---------- Coffee Cup + Steam (Brewing screen) ----------
void drawCoffeeCup(int cx, int cy) {
  uint16_t cupColor = (selectedBrew == SEL_HOT) ? COLOR_HOT_ACCENT : COLOR_ICE_ACCENT;

  display.fillRoundRect(cx - 30, cy - 20, 60, 40, 8, cupColor);
  display.drawRoundRect(cx - 30, cy - 20, 60, 40, 8, COLOR_CARD_OUT);
  display.drawCircle(cx + 30, cy, 10, cupColor);
  display.drawFastHLine(cx - 40, cy + 25, 80, cupColor);
}

void drawSteamFrame(int phase) {
  int regionW = 80;
  int regionH = 50;
  int x0 = cupCx - regionW / 2;
  int y0 = cupCy - 20 - regionH;

  display.fillRect(x0, y0, regionW, regionH, COLOR_BG);

  uint16_t steamColor = COLOR_TEXT;
  int baseY = cupCy - 30;

  for (int i = 0; i < 3; i++) {
    int lx = cupCx - 15 + i * 15;
    int offset = (phase + i) % 3;
    int yTop = baseY - 20 - offset * 4;
    int yMid = baseY - 10;
    int yBot = baseY;

    display.drawLine(lx, yBot, lx, yMid, steamColor);
    display.drawLine(lx, yMid, lx, yTop, steamColor);
  }
}

// ---------- Screens ----------
void drawWaitingScreen() {
  uiState = UI_WAITING_PAYMENT;
  accessGranted = false;
  
  // Ensure background polling is stopped
  stopBackgroundPolling();
  
  drawBackground();
  drawAppBarWaiting();
  
  // Draw a coffee cup icon in the center
  int cx = screenW / 2;
  int cy = screenH / 2 - 40;
  
  // Large coffee cup
  display.fillRoundRect(cx - 60, cy - 40, 120, 80, 12, COLOR_CARD);
  display.drawRoundRect(cx - 60, cy - 40, 120, 80, 12, COLOR_HOT_ACCENT);
  display.drawCircle(cx + 60, cy, 20, COLOR_HOT_ACCENT);
  display.drawFastHLine(cx - 80, cy + 50, 160, COLOR_HOT_ACCENT);
  
  // Steam lines
  display.drawLine(cx - 20, cy - 50, cx - 15, cy - 70, COLOR_SUBTEXT);
  display.drawLine(cx, cy - 50, cx + 5, cy - 70, COLOR_SUBTEXT);
  display.drawLine(cx + 20, cy - 50, cx + 25, cy - 70, COLOR_SUBTEXT);
  
  // Instructions
  drawCenteredText("Scan QR Code or", cx, cy + 120, 3, COLOR_TEXT, COLOR_BG);
  drawCenteredText("Connect via Bluetooth", cx, cy + 160, 3, COLOR_TEXT, COLOR_BG);
  drawCenteredText("to pay and unlock", cx, cy + 200, 3, COLOR_SUBTEXT, COLOR_BG);
}

void drawSelectScreen() {
  uiState = UI_SELECT_BREW;
  selectedBrew = SEL_NONE;
  brewingActive = false;

  drawBackground();
  drawAppBarSelect();
  layoutButtons();
  drawButtonCard(hotBtn, true,  false);
  drawButtonCard(iceBtn, false, false);

  drawConfirmButton(false, false);
}

void drawConfirmScreen() {
  uiState = UI_CONFIRM_BREW;

  drawBackground();
  drawAppBarConfirm();
  layoutButtons();

  bool hotSel = (selectedBrew == SEL_HOT);
  bool iceSel = (selectedBrew == SEL_ICE);

  drawButtonCard(hotBtn, true,  hotSel);
  drawButtonCard(iceBtn, false, iceSel);

  drawConfirmButton(selectedBrew != SEL_NONE, false);
}

// ---------- Stop Button ----------
#define COLOR_STOP_BG      0xF800  // Red
#define COLOR_STOP_OUT     0xC800  // Darker red

void drawStopButton() {
  // Position at bottom of screen
  int16_t btnW = 200;
  int16_t btnH = 55;
  stopBtn.x = (screenW - btnW) / 2;
  stopBtn.y = screenH - 65;
  stopBtn.w = btnW;
  stopBtn.h = btnH;
  
  // Draw button
  fillRoundRectFake(stopBtn.x, stopBtn.y, stopBtn.w, stopBtn.h, 16, COLOR_STOP_BG);
  display.drawRoundRect(stopBtn.x, stopBtn.y, stopBtn.w, stopBtn.h, 16, COLOR_STOP_OUT);
  
  // Draw text
  drawCenteredText("STOP", stopBtn.x + stopBtn.w / 2, stopBtn.y + stopBtn.h / 2, 4, COLOR_TEXT, COLOR_STOP_BG);
}

void drawBrewingScreen() {
  uiState = UI_LOCKED_BREWING;

  drawBackground();
  drawAppBarBrewing();

  cupCx = screenW / 2;
  cupCy = screenH / 2 + 20;  // Move up a bit to make room for stop button

  const char* msg = (selectedBrew == SEL_HOT) ? "Hot Brew started" : "Over Ice started";
  drawCenteredText(msg, screenW / 2, 100, 4, COLOR_TEXT, COLOR_BG);
  drawCenteredText("Please wait for your coffee.", screenW / 2, 135, 3, COLOR_SUBTEXT, COLOR_BG);

  drawCoffeeCup(cupCx, cupCy);

  brewStartMs        = millis();
  brewingActive      = true;
  steamPhase         = 0;
  lastSteamUpdateMs  = 0;
  lastDisplayedSec   = 0xFFFF;    // Force timer redraw
  
  // Start background HTTP polling (runs in separate thread!)
  startBackgroundPolling();

  drawSteamFrame(steamPhase);

  unsigned long remaining = currentBrewTotalMs;
  uint16_t totalSec = remaining / 1000;
  uint8_t mm = totalSec / 60;
  uint8_t ss = totalSec % 60;
  char buf[8];
  sprintf(buf, "%02u:%02u", mm, ss);

  int16_t timerY = screenH - 100;
  drawCenteredText(buf, screenW / 2, timerY, 5, COLOR_TEXT, COLOR_BG);
  
  // Draw Stop button at bottom
  drawStopButton();
}

// ---------- Thank You Screen ----------
void drawThankYouScreen() {
  uiState = UI_THANK_YOU;
  thankYouStartMs = millis();  // Start countdown to return to waiting
  
  drawBackground();
  
  // App bar
  display.fillRect(0, 0, screenW, 60, COLOR_APPBAR);
  drawCenteredText("Brew Complete!", screenW / 2, 32, 4, COLOR_TEXT, COLOR_APPBAR);
  drawWifiIndicator();
  
  // Big checkmark or coffee icon
  int cx = screenW / 2;
  int cy = screenH / 2 - 20;
  
  // Draw a happy coffee cup
  display.fillRoundRect(cx - 50, cy - 30, 100, 70, 12, COLOR_HOT_ACCENT);
  display.drawRoundRect(cx - 50, cy - 30, 100, 70, 12, COLOR_CARD_OUT);
  display.drawCircle(cx + 50, cy + 5, 18, COLOR_HOT_ACCENT);
  display.drawFastHLine(cx - 70, cy + 50, 140, COLOR_HOT_ACCENT);
  
  // Steam lines (static)
  display.drawLine(cx - 20, cy - 40, cx - 15, cy - 60, COLOR_TEXT);
  display.drawLine(cx, cy - 40, cx + 5, cy - 60, COLOR_TEXT);
  display.drawLine(cx + 20, cy - 40, cx + 25, cy - 60, COLOR_TEXT);
  
  // Enjoy message
  drawCenteredText("Enjoy your drink!", cx, cy + 120, 4, COLOR_TEXT, COLOR_BG);
  
  const char* brewMsg = (selectedBrew == SEL_HOT) ? "Hot Brew" : "Over Ice";
  drawCenteredText(brewMsg, cx, cy + 170, 3, COLOR_SUBTEXT, COLOR_BG);
}

// ---------- Brew triggers ----------
void triggerHotBrew() {
  Serial.println("CONFIRMED: HOT BREW - Activating relay D2");
  currentBrewTotalMs = BREW_HOT_MS;
  
  // Pulse the relay to simulate button press
  digitalWrite(HOT_BREW_PIN, HIGH);
  delay(RELAY_PULSE_MS);
  digitalWrite(HOT_BREW_PIN, LOW);
  
  Serial.println("Relay pulse complete");
}

void triggerIceBrew() {
  Serial.println("CONFIRMED: OVER ICE - Activating relay D3");
  currentBrewTotalMs = BREW_ICE_MS;
  
  // Pulse the relay to simulate button press
  digitalWrite(OVER_ICE_PIN, HIGH);
  delay(RELAY_PULSE_MS);
  digitalWrite(OVER_ICE_PIN, LOW);
  
  Serial.println("Relay pulse complete");
}

// ---------- Touch handling ----------
void handleTouchSelect(int16_t tx, int16_t ty) {
  bool hotPressed = pointInButton(tx, ty, hotBtn);
  bool icePressed = pointInButton(tx, ty, iceBtn);

  if (hotPressed) {
    selectedBrew = SEL_HOT;
    drawConfirmScreen();
  } else if (icePressed) {
    selectedBrew = SEL_ICE;
    drawConfirmScreen();
  }
}

void handleTouchConfirm(int16_t tx, int16_t ty) {
  if (pointInButton(tx, ty, hotBtn)) {
    selectedBrew = SEL_HOT;
    drawConfirmScreen();
    return;
  }
  if (pointInButton(tx, ty, iceBtn)) {
    selectedBrew = SEL_ICE;
    drawConfirmScreen();
    return;
  }

  if (pointInButton(tx, ty, confirmBtn)) {
    if (selectedBrew == SEL_NONE) return;

    drawConfirmButton(true, true);
    delay(200);

    // Notify backend that brewing started (so app can sync)
    notifyBrewStart(selectedBrew == SEL_HOT);

    if (selectedBrew == SEL_HOT) {
      triggerHotBrew();
    } else if (selectedBrew == SEL_ICE) {
      triggerIceBrew();
    }
    drawBrewingScreen();
  }
}

// ---------- Brewing update ----------
void updateBrewingScreen() {
  if (!brewingActive) return;

  unsigned long now = millis();
  unsigned long elapsed = now - brewStartMs;

  // Timer finished!
  if (elapsed >= currentBrewTotalMs) {
    // Stop background polling
    stopBackgroundPolling();
    
    brewingActive = false;
    accessGranted = false;
    lastDisplayedSec = 0xFFFF;  // Reset for next brew
    
    // Tell backend the brew is complete - end the session
    notifyBrewComplete();
    
    // Show thank you screen
    drawThankYouScreen();
    return;
  }

  // Steam animation
  if (now - lastSteamUpdateMs >= STEAM_INTERVAL_MS) {
    steamPhase = (steamPhase + 1) % 3;
    drawSteamFrame(steamPhase);
    lastSteamUpdateMs = now;
  }

  // Timer display - only update when second changes
  unsigned long remaining = currentBrewTotalMs - elapsed;
  uint16_t totalSec = remaining / 1000;
  
  // Only redraw if second changed
  if (totalSec != lastDisplayedSec) {
    lastDisplayedSec = totalSec;
    
    uint8_t mm = totalSec / 60;
    uint8_t ss = totalSec % 60;
    char buf[8];
    sprintf(buf, "%02u:%02u", mm, ss);

    int16_t timerW = 200;
    int16_t timerH = 70;
    int16_t timerX = screenW / 2 - timerW / 2;
    int16_t timerY = screenH - 100;

    display.fillRect(timerX, timerY - timerH / 2, timerW, timerH, COLOR_BG);
    drawCenteredText(buf, screenW / 2, timerY, 5, COLOR_TEXT, COLOR_BG);
  }
}

// ---------- WiFi Functions ----------
void tryConnectWiFi() {
  if (wifiConnecting) return;
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  wifiConnecting = true;
  wifiConnectStartMs = millis();
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Update display
  if (uiState == UI_WAITING_PAYMENT) {
    drawAppBarWaiting();
  }
}

void checkWiFiConnection() {
  unsigned long now = millis();
  
  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting = false;
      wifiConnected = true;
      
      Serial.println("WiFi connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      
      // Create HTTP client with shorter timeout for BLE compatibility
      if (httpClient != nullptr) {
        delete httpClient;
      }
      httpClient = new HttpClient(wifiClient, BACKEND_HOST, BACKEND_PORT);
      httpClient->setTimeout(3000);  // 3 second timeout for responsiveness
      
      // Update display
      if (uiState == UI_WAITING_PAYMENT) {
        drawAppBarWaiting();
      } else {
        drawWifiIndicator();
      }
      return;
    }
    
    if (now - wifiConnectStartMs >= WIFI_CONNECT_TIMEOUT_MS) {
      wifiConnecting = false;
      wifiConnected = false;
      lastWifiRetryMs = now;
      
      Serial.println("WiFi connection timeout!");
      
      if (uiState == UI_WAITING_PAYMENT) {
        drawAppBarWaiting();
      } else {
        drawWifiIndicator();
      }
      return;
    }
  } else {
    bool currentlyConnected = (WiFi.status() == WL_CONNECTED);
    
    if (currentlyConnected != wifiConnected) {
      wifiConnected = currentlyConnected;
      
      if (!wifiConnected) {
        Serial.println("WiFi disconnected!");
      } else {
        Serial.println("WiFi reconnected!");
      }
      
      if (uiState == UI_WAITING_PAYMENT) {
        drawAppBarWaiting();
      } else {
        drawWifiIndicator();
      }
    }
    
    if (!wifiConnected && !wifiConnecting) {
      if (now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
        Serial.println("Retrying WiFi connection...");
        tryConnectWiFi();
      }
    }
  }
}

// ---------- X.402 Payment Polling ----------
void notifyBrewStart(bool isHot) {
  if (!wifiConnected || httpClient == nullptr) {
    Serial.println("Cannot notify backend - WiFi not connected");
    return;
  }
  
  // Use fixed buffer instead of String
  snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/brew", DEVICE_ID);
  const char* body = isHot ? "{\"brewType\":\"hot\"}" : "{\"brewType\":\"iced\"}";
  
  Serial.println("");
  Serial.println("Notifying backend: Brew started");
  Serial.print("POST ");
  Serial.println(pathBuffer);
  
  httpClient->post(pathBuffer, "application/json", body);
  
  int statusCode = httpClient->responseStatusCode();
  httpClient->skipResponseHeaders();
  // Clear any remaining response body
  while (httpClient->available()) httpClient->read();
  
  // Stop connection after request
  httpClient->stop();
  
  Serial.print("Status: ");
  Serial.println(statusCode);
  
  if (statusCode == 200) {
    Serial.println("✅ Backend notified - brew started");
  } else {
    Serial.println("⚠️ Failed to notify backend");
  }
}

void notifyBrewComplete() {
  if (!wifiConnected || httpClient == nullptr) {
    Serial.println("Cannot notify backend - WiFi not connected");
    return;
  }
  
  // Use fixed buffer instead of String
  snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/complete", DEVICE_ID);
  
  Serial.println("");
  Serial.println("Notifying backend: Brew complete");
  Serial.print("POST ");
  Serial.println(pathBuffer);
  
  httpClient->post(pathBuffer, "application/json", "{}");
  
  int statusCode = httpClient->responseStatusCode();
  httpClient->skipResponseHeaders();
  // Clear any remaining response body
  while (httpClient->available()) httpClient->read();
  
  // Stop connection after request
  httpClient->stop();
  
  Serial.print("Status: ");
  Serial.println(statusCode);
  
  if (statusCode == 200) {
    Serial.println("✅ Backend notified - session ended");
  } else {
    Serial.println("⚠️ Failed to notify backend");
  }
}

// Notify backend that brew was stopped early
void notifyBrewStop() {
  if (!wifiConnected || httpClient == nullptr) {
    Serial.println("Cannot notify backend - WiFi not connected");
    return;
  }
  
  // Use fixed buffer instead of String
  snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/stop", DEVICE_ID);
  const char* body = (selectedBrew == SEL_HOT) ? "{\"brewType\":\"hot\"}" : "{\"brewType\":\"iced\"}";
  
  Serial.println("");
  Serial.println("🛑 Notifying backend: Brew STOPPED");
  Serial.print("POST ");
  Serial.println(pathBuffer);
  
  httpClient->post(pathBuffer, "application/json", body);
  
  int statusCode = httpClient->responseStatusCode();
  httpClient->skipResponseHeaders();
  // Clear any remaining response body
  while (httpClient->available()) httpClient->read();
  
  // Stop connection after request
  httpClient->stop();
  
  Serial.print("Status: ");
  Serial.println(statusCode);
  
  if (statusCode == 200) {
    Serial.println("✅ Backend notified - brew stopped");
  } else {
    Serial.println("⚠️ Failed to notify backend");
  }
}

// Handle stop button press during brewing (from Arduino touch screen)
void handleStopBrew() {
  Serial.println("🛑 STOP button pressed - canceling brew");
  
  // Stop background polling
  stopBackgroundPolling();
  
  brewingActive = false;
  accessGranted = false;
  
  // Pulse the same relay again to cancel the brew
  if (selectedBrew == SEL_HOT) {
    Serial.println("   Pulsing HOT relay to cancel...");
    digitalWrite(HOT_BREW_PIN, HIGH);
    delay(RELAY_PULSE_MS);
    digitalWrite(HOT_BREW_PIN, LOW);
  } else if (selectedBrew == SEL_ICE) {
    Serial.println("   Pulsing ICE relay to cancel...");
    digitalWrite(OVER_ICE_PIN, HIGH);
    delay(RELAY_PULSE_MS);
    digitalWrite(OVER_ICE_PIN, LOW);
  }
  
  // Notify backend
  notifyBrewStop();
  
  // Reset state
  selectedBrew = SEL_NONE;
  
  // Return to waiting screen
  drawWaitingScreen();
  
  Serial.println("✅ Brew stopped - returned to waiting screen");
}

// ============ Handle stop from APP (End Task button) ============
// Called when polling detects session ended by app
void handleAppStoppedBrew() {
  Serial.println("📱 APP clicked End Task - stopping brew!");
  
  // Stop background polling
  stopBackgroundPolling();
  
  brewingActive = false;
  accessGranted = false;
  
  // Pulse the same relay again to cancel the brew on the coffee machine
  if (selectedBrew == SEL_HOT) {
    Serial.println("   Pulsing HOT relay to cancel...");
    digitalWrite(HOT_BREW_PIN, HIGH);
    delay(RELAY_PULSE_MS);
    digitalWrite(HOT_BREW_PIN, LOW);
  } else if (selectedBrew == SEL_ICE) {
    Serial.println("   Pulsing ICE relay to cancel...");
    digitalWrite(OVER_ICE_PIN, HIGH);
    delay(RELAY_PULSE_MS);
    digitalWrite(OVER_ICE_PIN, LOW);
  }
  
  // DON'T notify backend - the app already ended the session!
  
  // Reset state
  selectedBrew = SEL_NONE;
  
  // Return to waiting screen
  drawWaitingScreen();
  
  Serial.println("✅ Brew stopped by app - returned to waiting screen");
}

// ============ BACKGROUND THREAD: HTTP polling ============
// This runs in a separate thread and doesn't block the main loop
void backgroundPollTask() {
  // Create separate HTTP client for this thread
  WiFiSSLClient bgClient;
  HttpClient bgHttp(bgClient, BACKEND_HOST, BACKEND_PORT);
  bgHttp.setTimeout(5000);
  
  // Stack-allocated path buffer for this thread
  char bgPathBuffer[128];
  int errorCount = 0;
  
  while (true) {
    // Check if we should be polling
    if (!shouldPollInBackground) {
      ThisThread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    
    // Check WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
      ThisThread::sleep_for(std::chrono::milliseconds(1000));
      continue;
    }
    
    // Build path using fixed buffer (avoid String heap allocation)
    if (strlen(DEVICE_SECRET) > 0) {
      snprintf(bgPathBuffer, sizeof(bgPathBuffer), "/x402/%s/status?deviceSecret=%s", DEVICE_ID, DEVICE_SECRET);
    } else {
      snprintf(bgPathBuffer, sizeof(bgPathBuffer), "/x402/%s/status", DEVICE_ID);
    }
    
    Serial.print("🔄 [BG] Polling: ");
    
    bgHttp.get(bgPathBuffer);
    
    int statusCode = bgHttp.responseStatusCode();
    
    // IMPORTANT: Skip HTTP headers before reading body
    bgHttp.skipResponseHeaders();
    
    // Read response BODY into fixed buffer
    int responseLen = 0;
    while (bgHttp.available() && responseLen < (int)sizeof(bgResponseBuffer) - 1) {
      bgResponseBuffer[responseLen++] = bgHttp.read();
    }
    bgResponseBuffer[responseLen] = '\0';
    
    // Stop connection after request
    bgHttp.stop();
    
    Serial.println(statusCode);
    
    // Check result and update shared flag
    sessionMutex.lock();
    if (statusCode == 402) {
      sessionEndedByApp = true;
      Serial.println("   [BG] ⚠️ Session ended (402)");
      errorCount = 0;
    } else if (statusCode == 200) {
      // Use strstr on char array instead of String.indexOf
      bool hasAccess = (strstr(bgResponseBuffer, "\"accessGranted\":true") != nullptr);
      if (!hasAccess) {
        sessionEndedByApp = true;
        Serial.println("   [BG] ⚠️ Session ended (no access)");
      } else {
        Serial.println("   [BG] ✅ Session active");
      }
      errorCount = 0;
    } else if (statusCode < 0) {
      Serial.println("   [BG] ⚠️ Connection error, will retry");
      errorCount++;
      // Reset connection after too many errors
      if (errorCount >= 5) {
        Serial.println("   [BG] 🔄 Waiting before retry...");
        ThisThread::sleep_for(std::chrono::milliseconds(1000));
        errorCount = 0;
      }
    }
    sessionMutex.unlock();
    
    // Wait before next poll
    ThisThread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_BREWING_MS));
  }
}

// Start background polling (called when brewing starts)
void startBackgroundPolling() {
  // Don't start if WiFi not connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("📡 Cannot start background polling - WiFi not connected");
    return;
  }
  
  sessionMutex.lock();
  sessionEndedByApp = false;
  shouldPollInBackground = true;
  sessionMutex.unlock();
  
  if (!pollThreadStarted) {
    pollThread.start(backgroundPollTask);
    pollThreadStarted = true;
    Serial.println("📡 Background polling thread started");
  } else {
    Serial.println("📡 Background polling resumed");
  }
}

// Stop background polling (called when brewing stops)
void stopBackgroundPolling() {
  if (!pollThreadStarted) return;  // Thread not started yet
  
  sessionMutex.lock();
  shouldPollInBackground = false;
  sessionMutex.unlock();
  Serial.println("📡 Background polling paused");
}

// ============ Check session (NON-BLOCKING!) ============
bool checkSessionDuringBrew() {
  // Just check the flag - no HTTP, no blocking!
  sessionMutex.lock();
  bool ended = sessionEndedByApp;
  sessionMutex.unlock();
  
  return !ended;  // Return true if session still valid
}

void pollPaymentStatus() {
  if (!wifiConnected || httpClient == nullptr) return;
  
  // Don't poll during brewing or thank you screen
  // (brewing now has its own polling via checkSessionDuringBrew)
  if (uiState == UI_LOCKED_BREWING || uiState == UI_THANK_YOU) return;
  
  unsigned long now = millis();
  
  // Poll interval based on screen state
  unsigned long pollInterval;
  if (uiState == UI_WAITING_PAYMENT) {
    pollInterval = POLL_INTERVAL_MS;  // 1.5 sec on waiting screen
  } else {
    pollInterval = 2000;  // 2 sec on selection screens (detect app brew start faster)
  }
  
  if (now - lastPollMs < pollInterval) return;
  
  lastPollMs = now;
  
  // Build path using fixed buffer (avoid String heap fragmentation)
  if (strlen(DEVICE_SECRET) > 0) {
    snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/status?deviceSecret=%s", DEVICE_ID, DEVICE_SECRET);
  } else {
    snprintf(pathBuffer, sizeof(pathBuffer), "/x402/%s/status", DEVICE_ID);
  }
  
  Serial.println("");
  Serial.print("Polling: GET ");
  Serial.println(pathBuffer);
  
  httpClient->get(pathBuffer);
  
  int statusCode = httpClient->responseStatusCode();
  
  // IMPORTANT: Skip HTTP headers before reading body
  httpClient->skipResponseHeaders();
  
  // Read response BODY into fixed buffer
  int responseLen = 0;
  while (httpClient->available() && responseLen < (int)sizeof(httpResponseBuffer) - 1) {
    httpResponseBuffer[responseLen++] = httpClient->read();
  }
  httpResponseBuffer[responseLen] = '\0';
  
  // Stop connection after request
  httpClient->stop();
  
  Serial.print("Status: ");
  Serial.println(statusCode);
  
  // DEBUG: Print actual response content
  Serial.print("Response: ");
  Serial.println(httpResponseBuffer);
  
  // Handle connection errors (negative status codes)
  static int connErrorCount = 0;
  if (statusCode < 0) {
    connErrorCount++;
    Serial.print("⚠️ Connection error (");
    Serial.print(connErrorCount);
    Serial.println(") - will retry...");
    
    // After 3 consecutive errors, recreate the client
    if (connErrorCount >= 3) {
      Serial.println("🔄 Resetting HTTP client...");
      delete httpClient;
      delay(500);  // Small delay before recreating
      httpClient = new HttpClient(wifiClient, BACKEND_HOST, BACKEND_PORT);
      httpClient->setTimeout(5000);  // 5 second timeout
      connErrorCount = 0;
    }
    
    return;  // Skip this poll, try again next cycle
  }
  
  // Reset error count on successful response
  connErrorCount = 0;
  
  if (statusCode == 200) {
    // DEBUG: Print response length
    Serial.print("   Response length: ");
    Serial.println(strlen(httpResponseBuffer));
    
    // Use strstr on char array instead of String.indexOf (more memory efficient)
    bool hasAccess = (strstr(httpResponseBuffer, "\"accessGranted\":true") != nullptr);
    bool isBrewing = (strstr(httpResponseBuffer, "\"brewing\":true") != nullptr);
    bool isHot = (strstr(httpResponseBuffer, "\"brewType\":\"hot\"") != nullptr);
    
    // DEBUG: Print parsed values
    Serial.print("   hasAccess=");
    Serial.print(hasAccess ? "true" : "false");
    Serial.print(", isBrewing=");
    Serial.print(isBrewing ? "true" : "false");
    Serial.print(", isHot=");
    Serial.print(isHot ? "true" : "false");
    Serial.print(", uiState=");
    Serial.println(uiState);
    
    if (hasAccess) {
      if (!accessGranted) {
        Serial.println("✅ ACCESS GRANTED!");
        accessGranted = true;
      }
      
      // Check if app started brewing and we haven't started yet
      if (isBrewing && uiState != UI_LOCKED_BREWING) {
        Serial.println("☕ App started brewing - syncing Arduino!");
        Serial.print("   Brew type: ");
        Serial.println(isHot ? "HOT" : "ICED");
        selectedBrew = isHot ? SEL_HOT : SEL_ICE;
        
        if (isHot) {
          triggerHotBrew();
        } else {
          triggerIceBrew();
        }
        drawBrewingScreen();
      }
      // If we're on waiting screen and have access but not brewing, show selection
      else if (uiState == UI_WAITING_PAYMENT && !isBrewing) {
        Serial.println("🎯 Transitioning to SELECT screen!");
        drawSelectScreen();
      }
    } else {
      // No access - if we were doing something, go back to waiting
      if (accessGranted || uiState != UI_WAITING_PAYMENT) {
        Serial.println("⏳ Session ended - returning to payment screen");
        accessGranted = false;
        brewingActive = false;
        drawWaitingScreen();
      }
    }
  } else if (statusCode == 402) {
    // Payment required - make sure we're on waiting screen
    if (accessGranted || uiState != UI_WAITING_PAYMENT) {
      Serial.println("Payment required (402) - returning to waiting");
      accessGranted = false;
      brewingActive = false;
      drawWaitingScreen();
    }
  } else {
    Serial.print("Unexpected status: ");
    Serial.println(statusCode);
  }
}

// ---------- BLE Functions ----------
void initBLE() {
  Serial.println("Starting BLE...");
  
  if (!BLE.begin()) {
    Serial.println("BLE init failed!");
    return;
  }
  
  // Use DEVICE_ID for BLE name so it matches
  BLE.setLocalName(DEVICE_ID);
  BLE.setDeviceName(DEVICE_ID);
  
  deviceInfoService.addCharacteristic(deviceInfoChar);
  BLE.addService(deviceInfoService);
  
  // Build BLE device info JSON dynamically using DEVICE_ID
  String bleDeviceInfo = String("{\"deviceId\":\"") + DEVICE_ID + "\","
    "\"deviceName\":\"Smart Coffee Maker\","
    "\"deviceType\":\"Coffee Maker\","
    "\"model\":\"X402-CF Pro\","
    "\"firmwareVersion\":\"1.0.0\","
    "\"chains\":["
      "{\"chain\":\"solana\",\"wallet\":\"YOUR_SOLANA_WALLET_ADDRESS\"},"
      "{\"chain\":\"base\",\"wallet\":\"YOUR_BASE_WALLET_ADDRESS\"}"
    "]}";
  
  deviceInfoChar.writeValue(bleDeviceInfo.c_str());
  
  BLE.setConnectable(true);
  BLE.setAdvertisedService(deviceInfoService);
  // Don't advertise yet - wait for WiFi to connect first
  
  Serial.println("BLE initialized (waiting for WiFi before advertising)");
  Serial.print("BLE Device ID: ");
  Serial.println(DEVICE_ID);
  Serial.print("BLE Address: ");
  Serial.println(BLE.address());
}

// ---------- Setup & Loop ----------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("");
  Serial.println("================================");
  Serial.println("  Mr. Coffee - X402 Edition");
  Serial.println("================================");

  // Initialize relay pins
  pinMode(HOT_BREW_PIN, OUTPUT);
  pinMode(OVER_ICE_PIN, OUTPUT);
  digitalWrite(HOT_BREW_PIN, LOW);
  digitalWrite(OVER_ICE_PIN, LOW);
  Serial.println("Relay pins initialized (D2=Hot, D3=Ice)");

  display.begin();
  display.setRotation(1);
  screenW = display.width();
  screenH = display.height();

  if (!touchDetector.begin()) {
    Serial.println("Touch init FAILED");
  } else {
    Serial.println("Touch init OK");
  }

  // Initialize BLE
  initBLE();
  
  // Draw initial waiting screen
  drawWaitingScreen();
  
  // Start WiFi connection
  tryConnectWiFi();
}

void loop() {
  unsigned long now = millis();
  
  // BLE only needed on waiting screen for device discovery
  // AND only when WiFi is connected (they share resources)
  static bool bleActive = false;  // Start inactive, enable after WiFi
  
  if (uiState == UI_WAITING_PAYMENT) {
    // Only enable BLE after WiFi is connected and stable
    if (wifiConnected && !wifiConnecting) {
      if (!bleActive) {
        Serial.println("📱 Starting BLE for device discovery...");
        BLE.advertise();
        bleActive = true;
      }
      BLE.poll();
    } else {
      // WiFi not connected - stop BLE to avoid interference
      if (bleActive) {
        Serial.println("📱 Pausing BLE while WiFi connects...");
        BLE.stopAdvertise();
        bleActive = false;
      }
    }
    
    // Check if BLE central is connected (app is reading device info)
    BLEDevice central = BLE.central();
    bool bleConnected = central && central.connected();
    
    // Track connection timing
    static unsigned long bleConnectTime = 0;
    static bool wasConnected = false;
    
    if (bleConnected && !wasConnected) {
      bleConnectTime = now;
      wasConnected = true;
      Serial.println("📱 BLE central connected - pausing HTTP");
    } else if (!bleConnected && wasConnected) {
      wasConnected = false;
      Serial.println("📱 BLE central disconnected");
    }
    
    // Give BLE priority for 4 seconds during connection
    bool blePriority = bleConnected && (now - bleConnectTime < 4000);
    
    if (blePriority) {
      // Still check WiFi even during BLE priority
      checkWiFiConnection();
      // During BLE connection, just poll BLE rapidly, skip HTTP
      BLE.poll();
      delay(10);
      return;
    }
  } else {
    // Not on waiting screen - stop BLE to prioritize HTTP
    if (bleActive) {
      Serial.println("📱 Stopping BLE - payment unlocked");
      BLE.stopAdvertise();
      BLEDevice central = BLE.central();
      if (central && central.connected()) {
        central.disconnect();
      }
      bleActive = false;
    }
  }
  
  // Check WiFi
  checkWiFiConnection();
  
  // Handle touch FIRST (before any blocking HTTP calls)
  // On selection screens, touch responsiveness is critical
  if (uiState == UI_SELECT_BREW || uiState == UI_CONFIRM_BREW) {
    GDTpoint_t points[5];
    uint8_t contacts = touchDetector.getTouchPoints(points);

    if (contacts > 0) {
      if (now - lastTouchMs >= TOUCH_DEBOUNCE_MS) {
        lastTouchMs = now;

        int16_t tx = points[0].y;
        int16_t ty = 480 - points[0].x;

        if (uiState == UI_SELECT_BREW) {
          handleTouchSelect(tx, ty);
        } else if (uiState == UI_CONFIRM_BREW) {
          handleTouchConfirm(tx, ty);
        }
      }
      // After touch, skip HTTP this cycle for responsiveness
      delay(10);
      return;
    }
  }
  
  // Poll for payment status (only if no touch activity)
  pollPaymentStatus();
  
  // Handle brewing animation with stop button touch detection
  if (uiState == UI_LOCKED_BREWING) {
    // Check for stop button touch
    GDTpoint_t brewPoints[5];
    uint8_t brewContacts = touchDetector.getTouchPoints(brewPoints);
    
    if (brewContacts > 0) {
      if (now - lastTouchMs >= TOUCH_DEBOUNCE_MS) {
        lastTouchMs = now;
        
        int16_t tx = brewPoints[0].y;
        int16_t ty = 480 - brewPoints[0].x;
        
        // Check if stop button was pressed
        if (pointInButton(tx, ty, stopBtn)) {
          handleStopBrew();
          return;
        }
      }
    }
    
    // Update screen FIRST (keeps timer smooth)
    updateBrewingScreen();
    
    // Then poll backend (this blocks but timer already updated)
    if (!checkSessionDuringBrew()) {
      // Session ended from app - stop brewing!
      handleAppStoppedBrew();
      return;
    }
    
    delay(10);  // Minimal delay
    return;
  }
  
  // Handle thank you screen - auto return to waiting after delay
  if (uiState == UI_THANK_YOU) {
    if (now - thankYouStartMs >= THANK_YOU_DURATION_MS) {
      drawWaitingScreen();
    }
    delay(50);
    return;
  }

  // Handle touch for waiting screen (no HTTP blocking concern)
  GDTpoint_t points[5];
  uint8_t contacts = touchDetector.getTouchPoints(points);

  if (contacts > 0) {
    if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
    lastTouchMs = now;

    int16_t tx = points[0].y;
    int16_t ty = 480 - points[0].x;

    if (uiState == UI_SELECT_BREW) {
      handleTouchSelect(tx, ty);
    } else if (uiState == UI_CONFIRM_BREW) {
      handleTouchConfirm(tx, ty);
    }
  }
  
  delay(10);
}
