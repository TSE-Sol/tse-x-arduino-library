/*
 * TSE-X402 Library
 * X.402 Payment Protocol for Arduino IoT Devices
 * 
 * Supports:
 * - TSE token payments (Solana)
 * - USDC payments (Base)
 * - WiFi devices (MKR WiFi 1010, ESP32, Arduino Giga R1)
 * - BLE devices (for mobile app bridge)
 * 
 * https://github.com/YOUR_USERNAME/tse-x-arduino-library
 */

#ifndef TSE_X402_H
#define TSE_X402_H

#include <Arduino.h>

// ============ VERSION ============
#define TSE_X402_VERSION "1.0.0"
#define TSE_X402_VERSION_MAJOR 1
#define TSE_X402_VERSION_MINOR 0
#define TSE_X402_VERSION_PATCH 0

// ============ DEVICE TYPES ============
enum TSE_DeviceType {
  DEVICE_COFFEE_MACHINE,
  DEVICE_BIKE_LOCK,
  DEVICE_DOOR_LOCK,
  DEVICE_POWER_SWITCH,
  DEVICE_EV_CHARGER,
  DEVICE_GENERIC
};

// ============ SESSION STATUS ============
enum TSE_SessionStatus {
  SESSION_NONE,           // No active session
  SESSION_PAYMENT_REQUIRED,  // Waiting for payment (402)
  SESSION_ACTIVE,         // Session active, access granted
  SESSION_EXPIRED,        // Session timed out
  SESSION_ENDED           // Session ended by user/app
};

// ============ PAYMENT CURRENCY ============
enum TSE_Currency {
  CURRENCY_UNKNOWN,
  CURRENCY_TSE,    // TSE token on Solana
  CURRENCY_USDC    // USDC on Base
};

// ============ CALLBACK TYPES ============
typedef void (*TSE_SessionCallback)(TSE_SessionStatus status, int remainingSeconds);
typedef void (*TSE_PaymentCallback)(TSE_Currency currency, float amount);

// ============ CONFIGURATION STRUCT ============
struct TSE_Config {
  const char* deviceId;
  const char* deviceSecret;
  const char* backendHost;
  int backendPort;
  TSE_DeviceType deviceType;
  unsigned long pollIntervalIdle;    // ms between polls when waiting
  unsigned long pollIntervalActive;  // ms between polls when session active
};

// ============ SESSION INFO ============
struct TSE_Session {
  bool accessGranted;
  int remainingSeconds;
  TSE_Currency currency;
  String walletAddress;
  String txHash;
  unsigned long expiresAt;  // millis() timestamp
};

// ============ DEFAULT VALUES ============
#define TSE_DEFAULT_BACKEND_HOST "tse-x-backend.onrender.com"
#define TSE_DEFAULT_BACKEND_PORT 443
#define TSE_DEFAULT_POLL_IDLE 1500
#define TSE_DEFAULT_POLL_ACTIVE 3000

// ============ HTTP STATUS CODES ============
#define TSE_HTTP_OK 200
#define TSE_HTTP_PAYMENT_REQUIRED 402
#define TSE_HTTP_FORBIDDEN 403
#define TSE_HTTP_NOT_FOUND 404

// ============ HELPER MACROS ============
#define TSE_MINUTES_TO_MS(m) ((m) * 60UL * 1000UL)
#define TSE_SECONDS_TO_MS(s) ((s) * 1000UL)

// ============ UTILITY FUNCTIONS ============

/**
 * Format seconds as human-readable time string
 * @param seconds Total seconds
 * @param buffer Output buffer (minimum 16 chars)
 * @return Pointer to buffer
 */
char* TSE_FormatTime(unsigned long seconds, char* buffer);

/**
 * Parse remaining seconds from JSON response
 * @param json JSON string from backend
 * @return Remaining seconds, or 0 if not found
 */
int TSE_ParseRemainingSeconds(const char* json);

/**
 * Parse access granted status from JSON
 * @param json JSON string from backend
 * @return true if accessGranted:true found
 */
bool TSE_ParseAccessGranted(const char* json);

/**
 * Parse currency from JSON response
 * @param json JSON string from backend
 * @return Currency enum value
 */
TSE_Currency TSE_ParseCurrency(const char* json);

/**
 * Get device type string
 * @param type Device type enum
 * @return Human-readable device type string
 */
const char* TSE_GetDeviceTypeString(TSE_DeviceType type);

/**
 * Get currency string
 * @param currency Currency enum
 * @return Human-readable currency string (e.g., "TSE (Solana)")
 */
const char* TSE_GetCurrencyString(TSE_Currency currency);

#endif // TSE_X402_H
