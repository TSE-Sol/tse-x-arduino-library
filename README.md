# TSE-X402 Arduino Library

X.402 Payment Protocol library for Arduino IoT devices. Enable pay-per-use functionality using **TSE tokens (Solana)** and **USDC (Base)** payments.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Version](https://img.shields.io/badge/version-1.0.0-green.svg)

## Supported Devices

| Device Type | Board | Connection | Example |
|-------------|-------|------------|---------|
| ☕ Coffee Machine | Arduino Giga R1 WiFi | WiFi | [CoffeeMachine](examples/CoffeeMachine/) |
| 🚲 Bike Lock | Arduino Nano 33 BLE | BLE | [BikeLock](examples/BikeLock/) |
| 🚪 Door Lock | Arduino MKR WiFi 1010 | WiFi | [DoorLock](examples/DoorLock/) |
| ⚡ Power Switch | Arduino MKR WiFi 1010 | WiFi | [PowerSwitch](examples/PowerSwitch/) |

## Features

- ✅ **Dual Payment Support**: TSE (Solana) and USDC (Base)
- ✅ **Real-time Session Management**: Automatic timeout handling
- ✅ **Secure Device Authentication**: Optional device secret
- ✅ **Memory Optimized**: Fixed buffers to prevent heap fragmentation
- ✅ **Connection Recovery**: Automatic WiFi reconnection
- ✅ **Backend Sync**: Polls backend for session status

## Hardware Requirements

### ☕ Coffee Machine
| Component | Purpose |
|-----------|---------|
| Arduino Giga R1 WiFi | Main controller |
| Relay module (x2) | Hot brew & Over ice control |
| Giga Display Shield | Touchscreen UI (optional) |
| 5V power supply | Power for relays |

### 🚲 Bike Lock
| Component | Purpose |
|-----------|---------|
| Arduino Nano 33 BLE | BLE communication |
| Servo or solenoid | Lock mechanism |
| Battery pack | Portable power |

### 🚪 Door Lock
| Component | Purpose |
|-----------|---------|
| Arduino MKR WiFi 1010 | WiFi controller |
| Electric door strike | Lock mechanism |
| Piezo buzzer | Audio feedback (optional) |
| 12V power supply | For door strike |

### ⚡ Power Switch
| Component | Purpose |
|-----------|---------|
| Arduino MKR WiFi 1010 | WiFi controller |
| Relay module | AC power control |
| Enclosure | Safety housing |

## Quick Start

### 1. Install the Library

#### Arduino IDE
1. Download this repository as ZIP
2. Arduino IDE → Sketch → Include Library → Add .ZIP Library
3. Select the downloaded ZIP file

#### PlatformIO
```ini
lib_deps =
    https://github.com/YOUR_USERNAME/tse-x-arduino-library.git
```

### 2. Install Dependencies

Install via Arduino Library Manager based on your board:

**All WiFi Devices (Coffee Machine, Door Lock, Power Switch):**
- `ArduinoHttpClient`

**Arduino Giga R1 WiFi (Coffee Machine):**
- `WiFi` (built-in)
- `Arduino_GigaDisplay_GFX` (optional, for touchscreen)
- `Arduino_GigaDisplayTouch` (optional, for touchscreen)

**Arduino MKR WiFi 1010 (Door Lock, Power Switch):**
- `WiFiNINA`

**Arduino Nano 33 BLE (Bike Lock):**
- `ArduinoBLE`

### 3. Configure Your Device

```cpp
// Your WiFi credentials
const char* WIFI_SSID = "YourNetwork";
const char* WIFI_PASSWORD = "YourPassword";

// Your device ID (from TSE-X app Device Creator)
const char* DEVICE_ID = "YOUR-DEVICE-001";  // e.g., "COFFEE-001", "BIKE-LOCK-001"

// Optional: Device secret for claimed devices
const char* DEVICE_SECRET = "";
```

### 4. Choose Your Example

| Device | Example File | Board |
|--------|--------------|-------|
| ☕ Coffee Machine | `examples/CoffeeMachine/CoffeeMachine.ino` | Arduino Giga R1 WiFi |
| 🚲 Bike Lock | `examples/BikeLock/BikeLock.ino` | Arduino Nano 33 BLE |
| 🚪 Door Lock | `examples/DoorLock/DoorLock.ino` | Arduino MKR WiFi 1010 |
| ⚡ Power Switch | `examples/PowerSwitch/PowerSwitch.ino` | Arduino MKR WiFi 1010 |

### 5. Upload and Test

1. Open the example for your device type
2. Update WiFi credentials and Device ID
3. Upload to your board
4. Open Serial Monitor (115200 baud)
5. Scan your device QR code in the TSE-X app
6. Make a payment
7. Watch your device activate!

## How It Works

```
┌─────────────────────────────────────────────────────────────┐
│                      TSE-X Payment Flow                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  📱 User scans QR code on device                            │
│       ↓                                                     │
│  💳 User pays with TSE or USDC in app                       │
│       ↓                                                     │
│  🌐 Backend receives payment, creates session               │
│       ↓                                                     │
│  🔄 Arduino polls backend → gets accessGranted: true        │
│       ↓                                                     │
│  ⚡ Arduino activates relay/unlock for session duration     │
│       ↓                                                     │
│  ⏰ Timer expires → Arduino deactivates, notifies backend   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## Device Registration

Before your Arduino can accept payments, register it in the TSE-X app:

1. Open TSE-X app → **Device Creator**
2. Fill in device details:
   - **Device ID**: Unique identifier (see examples below)
   - **Device Name**: Friendly name for display
   - **Device Type**: Must match exactly (case-insensitive):
     - `Coffee Machine` or `Coffee Maker`
     - `Bike Lock`
     - `Door Lock`
     - `Power Switch`
   - **Supports TSE**: ✅ (for Solana payments)
   - **Supports USDC**: ✅ (for Base payments)
   - **Solana Wallet**: Your TSE receiving address
   - **Base Wallet**: Your USDC receiving address
3. Generate QR code
4. Print or display QR code near your device

### Example Device IDs

| Device Type | Example ID | Example Name |
|-------------|------------|--------------|
| Coffee Machine | `COFFEE-001` | Office Coffee Maker |
| Bike Lock | `BIKE-LOCK-001` | Bike Rack #1 |
| Door Lock | `DOOR-LOCK-001` | Conference Room A |
| Power Switch | `POWER-001` | Smart Outlet #1 |

## API Reference

### Configuration

```cpp
// Required
const char* DEVICE_ID = "YOUR-DEVICE-ID";

// Optional
const char* DEVICE_SECRET = "";  // From app if device is claimed
const char* BACKEND_HOST = "tse-x-backend.onrender.com";
const int BACKEND_PORT = 443;
```

### Utility Functions

```cpp
// Format seconds as readable time
char buffer[16];
TSE_FormatTime(3661, buffer);  // "1h 1m 1s"

// Parse backend response
bool hasAccess = TSE_ParseAccessGranted(jsonResponse);
int remaining = TSE_ParseRemainingSeconds(jsonResponse);
TSE_Currency currency = TSE_ParseCurrency(jsonResponse);

// Get human-readable strings
const char* type = TSE_GetDeviceTypeString(DEVICE_POWER_SWITCH);  // "Power Switch"
const char* curr = TSE_GetCurrencyString(CURRENCY_TSE);  // "TSE (Solana)"
```

## Pricing Models

### Power Switch (Time-Based)
| Duration | Price |
|----------|-------|
| 30 minutes | $0.05 |
| 1 hour | $0.10 |
| 12 hours | $0.50 |
| Custom | $0.05 per 30 min |

### Coffee Machine (Per-Use)
| Action | Price |
|--------|-------|
| Hot Brew | $0.50 |
| Iced Brew | $0.50 |

### Bike/Door Lock (Time-Based)
| Duration | Price |
|----------|-------|
| 30 minutes | $0.50 |

## Troubleshooting

### Status -4 (Connection Failed)
- Ensure `httpClient->stop()` is called after each request
- Check WiFi signal strength
- Verify backend URL is correct

### 402 Payment Required
- Normal status when waiting for payment
- Make sure device ID matches app registration

### 403 Forbidden
- Check DEVICE_SECRET matches the one from Device Creator
- Device may need to be re-claimed in app

### WiFi Keeps Disconnecting
- Move closer to router
- Check for interference
- Increase `WIFI_RECONNECT_INTERVAL`

## Examples

### ☕ Coffee Machine
**File:** [`examples/CoffeeMachine/CoffeeMachine.ino`](examples/CoffeeMachine/)  
**Board:** Arduino Giga R1 WiFi  
**Connection:** WiFi  
**Price:** $0.50 per brew

Features:
- Hot Brew and Over Ice options
- Touchscreen selection (optional)
- App-triggered brewing
- Automatic session completion

```cpp
const int HOT_BREW_PIN = 2;    // Relay for hot brew
const int OVER_ICE_PIN = 3;    // Relay for over ice
```

---

### 🚲 Bike Lock
**File:** [`examples/BikeLock/BikeLock.ino`](examples/BikeLock/)  
**Board:** Arduino Nano 33 BLE  
**Connection:** BLE (via mobile app bridge)  
**Price:** $0.50 for 30 minutes

Features:
- BLE communication with app
- Unlock command via encrypted BLE
- Session restore after disconnect
- Heartbeat status updates

```cpp
#define LOCK_SERVICE_UUID  "b3c8f420-0000-4020-8000-000000000000"
const int LOCK_PIN = 9;  // Servo or solenoid
```

---

### 🚪 Door Lock
**File:** [`examples/DoorLock/DoorLock.ino`](examples/DoorLock/)  
**Board:** Arduino MKR WiFi 1010  
**Connection:** WiFi  
**Price:** $0.50 for 30 minutes

Features:
- WiFi-based polling
- Electric strike control
- Audio feedback (buzzer)
- Auto-lock on timeout

```cpp
const int LOCK_PIN = 2;    // Electric strike relay
const int BUZZER_PIN = 3;  // Feedback buzzer
```

---

### ⚡ Power Switch
**File:** [`examples/PowerSwitch/PowerSwitch.ino`](examples/PowerSwitch/)  
**Board:** Arduino MKR WiFi 1010  
**Connection:** WiFi  
**Price:** $0.05 per 30 minutes (variable duration)

Features:
- Time-based pricing
- Preset durations (30min, 1hr, 12hr)
- Custom duration support
- Relay control for any AC device

```cpp
const int RELAY_PIN = 2;  // Controls AC outlet
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## License

MIT License - see [LICENSE](LICENSE) file

## Links

- [TSE-X App](https://tse-x.app) - Mobile app for payments
- [Backend API](https://tse-x-backend.onrender.com) - Payment backend
- [Documentation](docs/) - Additional documentation
