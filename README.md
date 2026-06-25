# Smart Box ESP32 Firmware ⚡

This repository contains the C++ firmware for the ESP32 microcontroller that acts as the hardware interface for the Smart Box system. It handles physical locking, power relays, energy monitoring (PZEM-004T), matrix keypad input (via PCF8575), and OLED displays.

## 📌 Hardware Pin Mapping

### Lock & Relays
- **Lock Control**: GPIO 5 (Active LOW)
- **Lock Feedback**: GPIO 18
- **EV Charger Relay**: GPIO 19 (Active HIGH)
- **3-Pin Socket Relay**: GPIO 23 (Active HIGH)

### Energy Monitoring (PZEM-004T v3.0)
- **EV PZEM (Serial2)**: RX = GPIO 16, TX = GPIO 17 (Baud: 9600, Addr: 0xF8)
- **Socket PZEM (Serial1)**: RX = GPIO 26, TX = GPIO 27 (Baud: 9600, Addr: 0xF8)

### I2C Peripherals (OLED & Keypad Expander)
- **I2C Bus**: SDA = GPIO 21, SCL = GPIO 22 (Default ESP32 hardware I2C)
- **OLED Display**: SSD1306 128x64 (I2C Address: 0x3C)
- **PCF8575 Keypad Expander**: (I2C Address: 0x20)
  - Rows: P0, P1, P2, P3
  - Columns: P4, P5, P6, P7

### Other Pins
- **Factory Reset Button**: GPIO 0 (Onboard BOOT button, Hold for 5s)
- **RFID Status**: GPIO 32

## ⚙️ Setup & Flashing

1. Install **PlatformIO** (VS Code extension).
2. Open this `esp32_code` folder in PlatformIO.
3. Plug in the ESP32 via USB.
4. Click the `Upload and Monitor` button (the right arrow `→` at the bottom toolbar).

## 🛜 WiFi Manager (Access Point)

When powered on for the first time (or after a factory reset), the ESP32 creates a WiFi Hotspot:
- **SSID**: `SmartBox_Setup`
- **Password**: `12345678`

Connect to it, open `192.168.4.1` in your browser, and configure:
- WiFi SSID & Password
- Box ID (e.g., `box_001`)
- Box Device Secret (provided by admin)

## 📡 Cloud Communication

The ESP32 communicates with the cloud backend (`smart-box-admin.vercel.app`) using HTTP polling. It polls the backend every few seconds to ask for the next command (e.g., `{"command": "unlock"}`).

When the backend receives the command from the Flutter app (via Firestore), it queues it up. Once the ESP32 fetches the command, it executes it, then sends an `ACK` to clear the queue and updates the Firestore status directly.

---
**Status**: ✅ Production Ready | **Framework**: Arduino / PlatformIO
