# ☕ Coffee Maker Parts List

Smart coffee maker with touchscreen display, WiFi connectivity, and relay-controlled brewing.

## Required Components

| Part | Details | ~Price |
|------|---------|--------|
| Arduino GIGA R1 WiFi | Main board with WiFi + BLE built-in | $70-80 |
| GIGA Display Shield | 800x480 capacitive touchscreen | $50-60 |
| 2-Channel Relay Module | 5V relay for controlling brew buttons | $5-8 |
| Mr. Coffee Machine | Model X402 or similar with physical buttons | $40-80 |
| Jumper Wires | Male-to-female for relay connections | $4-6 |
| USB-C Cable | For programming and power | $5-8 |

**Estimated Total: $175-240**

## Pin Connections

| Component | Arduino Pin |
|-----------|-------------|
| Hot Brew Relay | D2 |
| Over Ice Relay | D3 |

## Wiring Diagram

```
Arduino GIGA R1          Components
───────────────          ──────────
D2  ─────────────────►  Relay CH1 (IN) ──► Hot Brew Button
D3  ─────────────────►  Relay CH2 (IN) ──► Over Ice Button
5V  ─────────────────►  Relay VCC
GND ─────────────────►  Relay GND

Relay Module             Coffee Maker
────────────             ────────────
CH1 COM ─────────────►  Hot Brew Button (one terminal)
CH1 NO  ─────────────►  Hot Brew Button (other terminal)
CH2 COM ─────────────►  Over Ice Button (one terminal)
CH2 NO  ─────────────►  Over Ice Button (other terminal)
```

## How It Works

1. The relay module connects in parallel with the coffee maker's physical brew buttons
2. When a user pays via the TSE-X app, the Arduino receives the command
3. The Arduino pulses the appropriate relay for 150ms
4. This simulates pressing the physical button, starting the brew cycle
5. The touchscreen displays brewing progress and remaining time

## Notes

- Use **NO (Normally Open)** relay contacts
- The relay "presses" the button by momentarily closing the circuit
- No modification to the coffee maker's internal electronics required
- Works with any coffee maker that has physical push buttons

---

*TSE-X — Pay with crypto for real-world IoT devices*
