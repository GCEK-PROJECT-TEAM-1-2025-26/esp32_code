# 🔌 PZEM Energy Meters Configuration

**Status:** Configuration Added ✅  
**Date:** May 26, 2026

---

## 📋 PZEM Pin Assignments

### **PZEM #1 - EV Charger Energy Monitor**

```cpp
RX Pin:  GPIO 16
TX Pin:  GPIO 17
Baud:    9600
Address: 0xF8 (default)
UART:    Serial2
```

**Connections:**

```
PZEM RX → ESP32 GPIO 16 (Serial2 RX)
PZEM TX → ESP32 GPIO 17 (Serial2 TX)
GND     → ESP32 GND
5V      → ESP32 5V (or 3.3V with level shifter)
```

---

### **PZEM #2 - 3-Pin Socket Energy Monitor**

```cpp
RX Pin:  GPIO 9
TX Pin:  GPIO 10
Baud:    9600
Address: 0xF8 (default)
UART:    Serial1
```

**Connections:**

```
PZEM RX → ESP32 GPIO 9 (Serial1 RX)
PZEM TX → ESP32 GPIO 10 (Serial1 TX)
GND     → ESP32 GND
5V      → ESP32 5V (or 3.3V with level shifter)
```

---

## 📝 Code Configuration (Already Added)

```cpp
/********** CONFIG: PZEM ENERGY METERS **********/
// PZEM #1 for EV Charger - using UART Serial2 (RX=16, TX=17)
#define PZEM_EV_RX_PIN 16
#define PZEM_EV_TX_PIN 17
#define PZEM_EV_BAUD 9600

// PZEM #2 for 3-Pin Socket - using UART Serial1 (RX=9, TX=10)
#define PZEM_P3_RX_PIN 9
#define PZEM_P3_TX_PIN 10
#define PZEM_P3_BAUD 9600

// PZEM device addresses (default is 0xF8, can be changed)
#define PZEM_EV_ADDR 0xF8
#define PZEM_P3_ADDR 0xF8
```

---

## 🔧 What Each PZEM Measures

### **PZEM004Tv30 Specifications**

- **Voltage Range:** 80-260V AC
- **Current Range:** 0-100A
- **Measurements:**
  - Voltage (V)
  - Current (A)
  - Power (W)
  - Energy (kWh)
  - Frequency (Hz)
  - Power Factor

---

## 📌 GPIO Pin Summary (ESP32)

| Pin    | Usage         | Type             |
| ------ | ------------- | ---------------- |
| **5**  | Lock Control  | Output           |
| **9**  | PZEM #2 RX    | Input (Serial1)  |
| **10** | PZEM #2 TX    | Output (Serial1) |
| **16** | PZEM #1 RX    | Input (Serial2)  |
| **17** | PZEM #1 TX    | Output (Serial2) |
| **18** | Lock Feedback | Input            |
| **19** | EV Relay      | Output           |
| **21** | 3-Pin Relay   | Output           |

**Total Used:** 8 pins ✅

---

## ⚠️ Important Notes

### **Voltage Levels**

- ESP32 GPIO: 3.3V logic
- PZEM: Typically 5V logic
- **Recommendation:** Use a 3.3V to 5V level shifter for TX pins
- RX can often work directly (5V tolerant)

### **Power Supply**

- PZEM needs 5V power supply
- Separate power supply recommended
- Do NOT connect to ESP32 5V if also powering from USB

### **UART Addresses**

- Default PZEM address: `0xF8`
- Can be changed via configuration (if needed)
- Must match in code for communication

---

## 🚀 Next Steps (To Integrate)

1. **Install PZEM library:**

   ```bash
   pio lib install "PZEM004Tv30"
   ```

2. **Add library include:**

   ```cpp
   #include <PZEM004Tv30.h>
   ```

3. **Initialize PZEM objects in setup():**

   ```cpp
   PZEM004Tv30 pzem_ev(Serial2, PZEM_EV_ADDR);
   PZEM004Tv30 pzem_p3(Serial1, PZEM_P3_ADDR);
   ```

4. **Read values in loop():**

   ```cpp
   float ev_voltage = pzem_ev.voltage();
   float ev_current = pzem_ev.current();
   float ev_power = pzem_ev.power();
   float ev_energy = pzem_ev.energy();
   ```

5. **Send to backend** in JSON payload

---

## 📊 Hardware Wiring Diagram

```
┌─────────────────────────────────────────────────┐
│              ESP32 DevKit                       │
├─────────────────────────────────────────────────┤
│                                                 │
│  PZEM #1 (EV)          PZEM #2 (3-Pin)       │
│  ├─ RX → GPIO16 (Serial2)                   │
│  └─ TX → GPIO17                              │
│                       ├─ RX → GPIO9 (Serial1) │
│                       └─ TX → GPIO10           │
│                                                 │
└─────────────────────────────────────────────────┘
```

---

## 🔄 Data Flow

```
PZEM #1 (EV)
    ↓
Serial2 (GPIO 16-17)
    ↓
Read voltage, current, power, energy
    ↓
Store in variables
    ↓
Send to Backend in JSON
    ↓
Backend → Firestore
    ↓
Flutter App displays energy usage
```

---

## ✅ Checklist

- [x] Pin configuration added to firmware
- [ ] PZEM library installed
- [ ] Library includes added
- [ ] PZEM objects initialized
- [ ] Reading functions implemented
- [ ] JSON payload updated with real values
- [ ] Tested with actual meters

---

## 🎯 Current Status

**Firmware:** ✅ Configuration added  
**Code:** ⏳ Awaiting library installation and reading functions  
**Hardware:** ⏳ Awaiting physical connection

---

**Next:** Install PZEM library and implement reading functions
