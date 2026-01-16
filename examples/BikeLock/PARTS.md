# 🚲 Bike Lock Parts List

Smart bike lock with BLE connectivity, LCD display, and servo-controlled locking mechanism.

## Required Components

| Part | Details | ~Price |
|------|---------|--------|
| ESP32 Development Board | ESP32-WROOM or ESP32-S3 with BLE | $8-15 |
| Servo Motor | SG90 or MG90S (for lock mechanism) | $3-5 |
| 16x2 LCD Display | HD44780 compatible (parallel, not I2C) | $5-8 |
| Passive Buzzer | 3.3V/5V compatible | $1-2 |
| Red LED | 5mm standard | $0.10 |
| Green LED | 5mm standard | $0.10 |
| 220Ω Resistors (x2) | For LED current limiting | $0.10 |
| Breadboard | 400 or 830 tie points | $3-5 |
| Jumper Wires | Male-to-male and male-to-female set | $4-6 |
| USB Cable | Micro USB or USB-C (for your ESP32) | $3-5 |

**Estimated Total: $30-50**

## Pin Connections

| Component | ESP32 Pin |
|-----------|-----------|
| Servo Signal | D13 |
| Buzzer | D12 |
| Red LED | D2 |
| Green LED | D4 |
| LCD RS | D5 |
| LCD E | D6 |
| LCD D4-D7 | D7, D8, D9, D10 |

## Wiring Diagram

```
ESP32          Components
─────          ──────────
D13  ────────► Servo Signal (orange/yellow wire)
D12  ────────► Buzzer (+)
D2   ────────► Red LED (+) ──► 220Ω ──► GND
D4   ────────► Green LED (+) ──► 220Ω ──► GND
D5   ────────► LCD RS
D6   ────────► LCD E
D7   ────────► LCD D4
D8   ────────► LCD D5
D9   ────────► LCD D6
D10  ────────► LCD D7
3.3V ────────► LCD VDD, Servo VCC
GND  ────────► LCD VSS, Servo GND, Buzzer (-), LEDs (-)
```

## Notes

- The servo controls the physical lock mechanism (0° = locked, 90° = unlocked)
- LCD shows lock status and countdown timer during active sessions
- Red LED = locked, Green LED = unlocked
- Buzzer provides audio feedback for lock/unlock events

---

*TSE-X — Pay with crypto for real-world IoT devices*
