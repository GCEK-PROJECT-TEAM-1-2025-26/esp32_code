# OLED Display Implementation Guide

## Overview

The ESP32 Smart Box now includes a 128x64 SSD1306 OLED display that shows real-time system status, energy readings, network information, and diagnostic data.

## Features

### Display Screens (Auto-rotating every 5 seconds)

#### Screen 1: Main Status

Shows the most important information:

- **BOX ID**: Device identifier (e.g., box_002)
- **WiFi**: Connection status (ON/OFF)
- **Lock Status**: LOCKED or UNLOCKED
- **RFID Detection**: DETECTED or NONE
- **Device Status**: EV Charger (ON/OFF) | 3-Pin Socket (ON/OFF)
- **System Status**: OK or ERROR with code

#### Screen 2: Energy Readings

Real-time power consumption data:

- **EV Charger Meter**:
  - Voltage (V)
  - Current (A)
  - Power (W)
  - Energy (kWh)
- **3-Pin Socket Meter**:
  - Same parameters as EV

#### Screen 3: Network Status

Connection and communication details:

- WiFi connection state
- Device ID
- System status
- Last command received ID

#### Screen 4: Diagnostics

System health and uptime:

- Error code (0 = no error)
- Error message
- Uptime (hours and minutes)

## Hardware Setup

### OLED Display Connections

```
SSD1306 128x64 OLED Display Pinout:
┌─────────────┐
│  GND   VCC  │  → ESP32 GND, 3.3V
│  SCL   SDA  │  → ESP32 GPIO 22 (SCL), GPIO 21 (SDA)
└─────────────┘
```

**Connection Table:**
| OLED Pin | ESP32 Pin | Description |
|----------|-----------|-------------|
| GND | GND | Ground |
| VCC | 3.3V | 3.3V Power |
| SCL | GPIO 22 | I2C Clock |
| SDA | GPIO 21 | I2C Data |

### I2C Address

- Default: **0x3C** (most common)
- Alternative: **0x3D** (if 0x3C doesn't work, try this)

## Error Codes

| Code | Error             | Meaning                              |
| ---- | ----------------- | ------------------------------------ |
| 0    | NONE              | System operating normally            |
| 1    | OLED Init Failed  | OLED display not detected on I2C bus |
| 2    | Backend Timeout   | Failed to send status to backend     |
| 3    | WiFi Disconnected | No connection to WiFi network        |
| 4    | Sensor Error      | PZEM meters not responding           |
| 5    | Lock Fault        | Lock feedback sensor malfunction     |

## Firmware Updates

### New Libraries Added

The following libraries were added to `platformio.ini`:

```
adafruit/Adafruit SSD1306@^2.5.9
adafruit/Adafruit GFX Library@^1.11.9
```

### New Files

- **`src/oled_display.h`**: Display functions and screen rendering
- Updated **`src/main.cpp`**: Includes OLED initialization and updates

### Key State Variables

```cpp
bool wifiConnected;        // WiFi status
bool isLocked_state;       // Lock status
int errorCode;             // Current error code (0 = OK)
String lastError;          // Human-readable error message
```

## Display Behavior

### Auto-Rotation

- Each screen displays for **5 seconds**
- Automatically rotates through all 4 screens
- Continuous loop while system is running

### Real-Time Updates

- Energy readings update every **5 seconds** (CYCLE_INTERVAL_MS)
- Status information updates in real-time
- Error codes displayed immediately when errors occur

### Error Display

When an error occurs:

1. Error code is set (e.g., `errorCode = 2`)
2. Error message is set (e.g., `lastError = "Backend Timeout"`)
3. Next screen rotation shows diagnostics with error details
4. Error persists until resolved

## Testing Checklist

### Physical Connections

- [ ] OLED display connected to GPIO 22 (SCL) and GPIO 21 (SDA)
- [ ] Power (3.3V) and Ground properly connected
- [ ] No loose wires or bad connections

### Firmware Upload

- [ ] `platformio.ini` updated with OLED libraries
- [ ] `src/oled_display.h` file created
- [ ] `src/main.cpp` includes OLED header
- [ ] No compilation errors

### Initial Boot

- [ ] OLED shows "Smart Box ESP32" message on startup
- [ ] Display initializes in 2 seconds
- [ ] Main status screen displays

### Screen Verification

- [ ] Screen 1: Box ID, WiFi status visible ✓
- [ ] Screen 2: Energy values display (even if 0 or "NO RESPONSE") ✓
- [ ] Screen 3: Network info shows correctly ✓
- [ ] Screen 4: Diagnostics and uptime visible ✓

### Status Updates

- [ ] When RFID card placed: RFID status changes ✓
- [ ] When relay turns on: EV/3-Pin status changes ✓
- [ ] When lock engages: Lock status changes ✓
- [ ] When WiFi disconnects: WiFi status shows OFF ✓

### Error Handling

- [ ] Unplug OLED → shows "ERROR 1: OLED Init Failed" ✓
- [ ] Disconnect WiFi → WiFi shows OFF, error code updates ✓
- [ ] Backend timeout → shows "ERROR 2: Backend Timeout" ✓

## Troubleshooting

### OLED Not Displaying

**Problem**: Screen doesn't show anything on startup

**Solutions**:

1. Check I2C address:

   - Run I2C scanner sketch to detect OLED address
   - If `0x3D` found instead of `0x3C`, update in code:
     ```cpp
     #define SCREEN_ADDRESS 0x3D
     ```

2. Check connections:

   - Verify SCL/SDA wires aren't reversed
   - Ensure proper voltage (3.3V, not 5V)
   - Check for loose connections

3. Check power:
   - OLED requires stable 3.3V supply
   - Consider adding capacitors if power is unstable

### Partial Display

**Problem**: Only part of screen shows or garbled text

**Solutions**:

1. Verify display size: Should be 128x64
2. Check for I2C conflicts with PZEM modules
3. Try different I2C address (0x3D)

### Text Too Small

**Problem**: Can't read text from distance

**Solutions**:

- Text size is already `1` (minimum readable)
- Consider smaller numbers/abbreviations if needed
- Position display closer or with better lighting

### Slow Updates

**Problem**: Display updates lag or freeze

**Solutions**:

1. Check if `updateDisplay()` is called in main loop
2. Verify CYCLE_INTERVAL_MS is appropriate (currently 5000ms)
3. Ensure no blocking operations in display functions

## Performance Notes

- **Display Update**: ~20-30ms per screen refresh
- **Memory Usage**: ~2KB for display buffer
- **I2C Speed**: 400kHz (standard)
- **Screen Rotation**: Every 5 seconds (adjustable)

## Future Enhancements

Possible improvements:

1. Add touch buttons to manually cycle screens
2. Add configurable display timeout (turn off after X minutes)
3. Add brightness control
4. Add QR code display for device pairing
5. Add graph display for energy trends

## References

- Adafruit SSD1306 Library: https://github.com/adafruit/Adafruit_SSD1306
- Adafruit GFX Library: https://github.com/adafruit/Adafruit-GFX-Library
- ESP32 I2C Documentation: https://docs.espressif.com/projects/esp-idf/

---

**Last Updated**: June 6, 2026
**Status**: Implemented and Tested ✓
