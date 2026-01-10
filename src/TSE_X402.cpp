/*
 * TSE-X402 Library Implementation
 * X.402 Payment Protocol for Arduino IoT Devices
 */

#include "TSE_X402.h"
#include <cstring>

// ============ UTILITY FUNCTIONS ============

char* TSE_FormatTime(unsigned long seconds, char* buffer) {
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;
  
  if (hours > 0) {
    sprintf(buffer, "%dh %dm %ds", hours, minutes, secs);
  } else if (minutes > 0) {
    sprintf(buffer, "%dm %ds", minutes, secs);
  } else {
    sprintf(buffer, "%ds", secs);
  }
  
  return buffer;
}

int TSE_ParseRemainingSeconds(const char* json) {
  const char* key = "\"remainingSeconds\":";
  const char* pos = strstr(json, key);
  
  if (pos != nullptr) {
    pos += strlen(key);
    return atoi(pos);
  }
  
  return 0;
}

bool TSE_ParseAccessGranted(const char* json) {
  return (strstr(json, "\"accessGranted\":true") != nullptr);
}

TSE_Currency TSE_ParseCurrency(const char* json) {
  if (strstr(json, "\"currency\":\"USDC\"") != nullptr || 
      strstr(json, "\"token\":\"USDC\"") != nullptr) {
    return CURRENCY_USDC;
  }
  
  if (strstr(json, "\"currency\":\"TSE\"") != nullptr || 
      strstr(json, "\"token\":\"TSE\"") != nullptr) {
    return CURRENCY_TSE;
  }
  
  return CURRENCY_UNKNOWN;
}

const char* TSE_GetDeviceTypeString(TSE_DeviceType type) {
  switch (type) {
    case DEVICE_COFFEE_MACHINE: return "Coffee Machine";
    case DEVICE_BIKE_LOCK: return "Bike Lock";
    case DEVICE_DOOR_LOCK: return "Door Lock";
    case DEVICE_POWER_SWITCH: return "Power Switch";
    case DEVICE_EV_CHARGER: return "EV Charger";
    default: return "Generic Device";
  }
}

const char* TSE_GetCurrencyString(TSE_Currency currency) {
  switch (currency) {
    case CURRENCY_TSE: return "TSE (Solana)";
    case CURRENCY_USDC: return "USDC (Base)";
    default: return "Unknown";
  }
}
