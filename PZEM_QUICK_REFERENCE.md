# ⚡ PZEM Energy Meters - Quick Reference

**Two PZEM004Tv30 Meters Ready to Integrate** ✅

---

## 🔌 Pin Configuration

```
ESP32 DevKit
├── PZEM #1 (EV Charger)
│   ├── RX: GPIO 16 (Serial2)
│   ├── TX: GPIO 17 (Serial2)
│   ├── GND: ESP32 GND
│   └── 5V: Power Supply
│
└── PZEM #2 (3-Pin Socket)
    ├── RX: GPIO 9 (Serial1)
    ├── TX: GPIO 10 (Serial1)
    ├── GND: ESP32 GND
    └── 5V: Power Supply
```

---

## 📋 Summary Table

| Parameter     | PZEM #1 (EV)      | PZEM #2 (3-Pin)     |
| ------------- | ----------------- | ------------------- |
| **Purpose**   | EV Charger Energy | 3-Pin Socket Energy |
| **RX Pin**    | GPIO 16           | GPIO 9              |
| **TX Pin**    | GPIO 17           | GPIO 10             |
| **UART**      | Serial2           | Serial1             |
| **Baud Rate** | 9600              | 9600                |
| **Address**   | 0xF8              | 0xF8                |

---

## ✅ Configuration Status

- ✅ Pins defined in firmware
- ✅ Code compiles without errors
- ✅ Ready for PZEM library integration

---

## 🚀 Next Phase

1. Install PZEM library
2. Add reading functions
3. Integrate into JSON payload
4. Test with actual meters

---

**File:** `m:\smart_box_app_esp\src\main.cpp`  
**Status:** Configuration added ✅
