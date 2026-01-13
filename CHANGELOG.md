# Changelog

All notable changes to the TSE-X Arduino Library.

## [1.3.0] - 2026-01-13

### 🔧 Memory Optimization - BikeLock
**Fixes LCD screen corruption after ~20 minutes of operation**

#### Changed
- Replaced `String` objects with fixed `char[]` buffers to prevent heap fragmentation
- `updateSessionStatus()` now uses pre-allocated `jsonOutputBuffer[512]`
- `buildDeviceInfoJson()` now uses static `deviceInfoBuffer[1024]`
- Function signatures changed from `String` to `const char*`:
  - `startSession(duration, txHash, wallet, currency)`
  - `restoreSession(remainingMs, wallet, currency, txHash)`
  - `extendSession(extraMs, txHash)`
- String comparisons changed from `==` to `strcmp()`

#### Added
- `#include <cstring>` for `strcmp`, `strlen`, `strncpy`
- Fixed buffers for session data:
  - `currentTxHash[72]`
  - `payerWallet[48]`
  - `paymentCurrency[8]`
  - `jsonOutputBuffer[512]`

#### Technical Details
The LCD corruption was caused by heap fragmentation from repeated `String` allocations in `updateSessionStatus()`, which runs every 5 seconds. After ~240 cycles (~20 minutes), fragmented memory would corrupt the LCD buffer. Fixed buffers eliminate all heap allocation during normal operation.

---

## [1.2.0] - 2026-01-12

### 🔐 Device Secret Support - BikeLock

#### Added
- `DEVICE_SECRET` configuration for claimed devices
- Device secret transmitted via BLE in `buildDeviceInfoJson()`
- Clear "USER CONFIGURATION SECTION" at top of file
- Visual box formatting for easy identification

#### Changed
- Configuration section reorganized for clarity
- Firmware version tracking

---

## [1.1.0] - 2026-01-11

### 🔄 Session Restore - BikeLock

#### Added
- `restoreSession()` function for power loss recovery
- `setUnlockedQuiet()` for silent unlock on restore
- `playRestoreSound()` distinct audio feedback
- `supportsRestore: true` capability flag

#### Changed
- Session state preserved across app disconnects
- Timer continues running when phone disconnects

---

## [1.0.0] - 2026-01-10

### 🎉 Initial Release

#### CoffeeMachine
- Arduino Giga R1 WiFi support
- Touchscreen UI with brew selection
- Hot Brew and Over Ice options
- Memory-optimized with fixed buffers
- Background polling thread (16KB stack)
- WiFi auto-reconnection

#### BikeLock
- Arduino Nano 33 BLE support
- BLE communication with app bridge
- Servo lock control
- LCD countdown display
- Warning sounds at 1 min, 30 sec, 10 sec

#### DoorLock (Coming Soon)
- Arduino MKR WiFi 1010 placeholder

#### PowerSwitch (Coming Soon)
- Arduino MKR WiFi 1010 placeholder

---

## Version History

| Version | Date | Highlights |
|---------|------|------------|
| 1.3.0 | 2026-01-13 | Memory optimization, LCD fix |
| 1.2.0 | 2026-01-12 | Device secret support |
| 1.1.0 | 2026-01-11 | Session restore |
| 1.0.0 | 2026-01-10 | Initial release |
