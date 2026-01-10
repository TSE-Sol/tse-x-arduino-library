# Changelog

All notable changes to TSE-X402 Arduino Library will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-01-10

### Added
- Initial release
- Support for TSE (Solana) payments
- Support for USDC (Base) payments
- Device types: Coffee Machine, Bike Lock, Door Lock, Power Switch
- WiFi connectivity for MKR WiFi 1010 and Arduino Giga R1
- BLE support for Bike Lock (via mobile app bridge)
- Memory-optimized buffers to prevent heap fragmentation
- Automatic session management with timeout
- Backend polling with configurable intervals
- Utility functions for parsing JSON responses
- Example sketches for all device types

### Device Examples
- `CoffeeMachine` - Arduino Giga R1 WiFi with touchscreen
- `BikeLock` - Arduino Nano 33 BLE
- `DoorLock` - Arduino MKR WiFi 1010
- `PowerSwitch` - Arduino MKR WiFi 1010 with time-based pricing

---

## Version History

| Version | Date | Description |
|---------|------|-------------|
| 1.0.0 | 2026-01-10 | Initial public release |

---

## Upcoming Features

- [ ] EV Charger support
- [ ] ESP32 support
- [ ] Local caching for offline operation
- [ ] OTA updates
- [ ] Multi-relay support for Power Switch
