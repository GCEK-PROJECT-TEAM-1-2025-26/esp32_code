# ESP32 Testing & Verification Plan

**Status:** Ready for Upload ✅  
**Date:** May 26, 2026

---

## 📋 Pre-Upload Checklist

- [ ] ESP32 board connected via USB
- [ ] PlatformIO CLI installed (`pio --version`)
- [ ] Firebase service account credentials verified
- [ ] Backend deployed to Vercel (https://smart-box-admin.vercel.app)
- [ ] Firestore security rules deployed
- [ ] Box document exists: `firestore/boxes/box_001`
- [ ] WiFi credentials correct: `SREEHARI` / `447643899`

---

## 🚀 Step 1: Compile & Upload

### **Command:**

```powershell
cd m:\smart_box_app_esp
pio run --target upload --verbose
```

### **Expected Output:**

```
Building .pio\build\esp32doit-devkit-v1\firmware.elf
...
Uploading .pio\build\esp32doit-devkit-v1\firmware.bin
...
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
```

---

## 🔍 Step 2: Monitor Serial Output

### **Command:**

```powershell
pio device monitor --baud 115200
```

### **Wait For:**

- Baud rate: `115200`
- Auto-connect to COM port
- Serial output to start appearing

---

## ✅ Step 3: Verify WiFi Connection

### **Expected Output (First 10 seconds):**

```
Connecting to WiFi: SREEHARI
............
WiFi connected
IP address: 192.168.x.x
Smart box ESP32 started (backend mode with EV & 3-pin relay control)
```

### **If WiFi Fails:**

- [ ] Check WiFi SSID and password in code
- [ ] Verify ESP32 antenna connection
- [ ] Try moving ESP32 closer to router
- [ ] Check if network requires WPA3 (ESP32 may not support)

---

## 🌐 Step 4: Verify Backend Connectivity (Critical Test)

### **Expected Output (5-10 seconds after WiFi):**

```
--- Cycle Start ---
Lock state before cmd: LOCKED
GET https://smart-box-admin.vercel.app/api/esp/next-command?deviceId=box_001&lastCommandId=
HTTP GET code: 200
Command payload: {"none":true}
```

### **Success Indicators:**

- ✅ `HTTP GET code: 200` - Server responding
- ✅ No `start_ssl_client: -1` errors
- ✅ `Command payload: {"none":true}` - No pending commands (expected)

### **If GET Fails:**

**Error: `HTTP GET failed: connection refused`**

- [ ] Verify backend is deployed: https://smart-box-admin.vercel.app
- [ ] Check firewall blocking HTTPS
- [ ] Verify certificate in code is correct
- [ ] Try HTTP instead of HTTPS (temporary test only)

**Error: `start_ssl_client: -1`**

- [ ] Check certificate is properly formatted
- [ ] Verify `VERCEL_ROOT_CA` string is complete
- [ ] Check for line breaks in certificate

**Error: `HTTP GET code: 401`**

- [ ] Verify headers: `x-device-id: box_001`
- [ ] Verify headers: `x-device-secret: super-secret-token`
- [ ] Check backend has `ESP_DEVICE_SECRET` env var set to `super-secret-token`

---

## 📤 Step 5: Verify Status Posting (POST Request)

### **Expected Output (Continuation):**

```
Lock state after cmd: LOCKED
EV relay state: OFF
3-pin relay state: OFF

POST https://smart-box-admin.vercel.app/api/esp/ack
POST body: {"deviceId":"box_001","commandId":"","success":true,"timestamp":5234,...}
HTTP POST code: 200
Response: {"ok":true,"message":"Status received and saved"}

--- Cycle End ---
```

### **Success Indicators:**

- ✅ `HTTP POST code: 200` - Server accepted status
- ✅ `Response: {"ok":true,...}` - Backend confirmed
- ✅ No SSL errors

### **If POST Fails:**

**Error: `HTTP POST failed`**

- Same troubleshooting as GET request above

---

## 🔄 Step 6: Verify Firestore Updates

While serial monitor is running:

1. **Open Firestore Console:**

   - Go to: https://console.firebase.google.com/project/smart-box-admin/firestore
   - Navigate to: `boxes > box_001`

2. **Watch for Updates:**

   - [ ] `lastUpdated` field updates every 5 seconds
   - [ ] Timestamp changes continuously
   - [ ] No errors in `state` field

3. **Screenshot for Reference:**
   - Capture Firestore data alongside serial output
   - Document any mismatches

---

## 🧪 Step 7: Test Lock Control (Basic Test)

### **From Firestore Console:**

1. Create command in `commands` collection:

   ```json
   {
     "boxId": "box_001",
     "commandType": "lock",
     "status": "pending",
     "createdAt": "current_timestamp"
   }
   ```

2. **Watch Serial Monitor for:**

   ```
   --- Cycle Start ---
   Command received: cmd_xyz
   Lock: LOCK
   >> Locking box...
   Lock state after cmd: LOCKED
   POST body: {"success":true,...}
   HTTP POST code: 200
   --- Cycle End ---
   ```

3. **Verify Firestore:**
   - Command status changes to: `"completed"`
   - `boxes/box_001.isLocked` updates

### **If Test Fails:**

- [ ] Check lock control pin (GPIO 5) is wired correctly
- [ ] Check lock feedback pin (GPIO 18) is getting signal
- [ ] Verify relay driver is powered
- [ ] Check command JSON format matches code expectations

---

## ⚡ Step 8: Test EV Relay Control

### **From Firestore Console:**

1. Create command:

   ```json
   {
     "boxId": "box_001",
     "commandType": "deviceControl",
     "payload": {
       "device": "evCharger",
       "action": "turnOn"
     },
     "status": "pending",
     "createdAt": "current_timestamp"
   }
   ```

2. **Watch Serial Monitor for:**

   ```
   Command received: cmd_xyz
   EV set: 1 to 1
   >> Setting EV to ON
   EV relay state: ON
   POST body: {"state":{"ev":true},...}
   ```

3. **Verify Physically:**

   - Relay on pin 19 should activate
   - Listen for relay click/buzz
   - Measure pin 19 with multimeter: Should read ~3.3V

4. **Verify Firestore:**
   - `boxes/box_001.devices.evCharger.isOn` = `true`

---

## 🔌 Step 9: Test 3-Pin Socket Control

### **From Firestore Console:**

1. Create command:

   ```json
   {
     "boxId": "box_001",
     "commandType": "deviceControl",
     "payload": {
       "device": "threePinSocket",
       "action": "turnOn"
     },
     "status": "pending",
     "createdAt": "current_timestamp"
   }
   ```

2. **Watch Serial Monitor for:**

   ```
   Command received: cmd_xyz
   P3 set: 1 to 1
   >> Setting 3-Pin to ON
   3-pin relay state: ON
   POST body: {"state":{"p3":true},...}
   ```

3. **Verify Physically:**
   - Pin 21 should go HIGH
   - Relay should activate

---

## 📊 Step 10: Stress Test (Optional)

Run for 5-10 minutes and monitor:

- [ ] No memory leaks (RAM usage stable)
- [ ] No WiFi disconnections
- [ ] Consistent 5-second cycle time
- [ ] No dropped commands
- [ ] Firestore updates continuously
- [ ] No SSL reconnection errors

### **Command to Monitor RAM:**

```powershell
pio device monitor --baud 115200 | Select-String "free"
```

---

## 🎯 Success Criteria

| Test                      | Criteria                    | Status |
| ------------------------- | --------------------------- | ------ |
| **WiFi**                  | Connects within 20 seconds  | ⏳     |
| **GET Request**           | Returns HTTP 200            | ⏳     |
| **POST Request**          | Returns HTTP 200            | ⏳     |
| **Firestore Updates**     | Updates every 5 seconds     | ⏳     |
| **Lock Control**          | Executes and reports state  | ⏳     |
| **EV Relay**              | Energizes and reports state | ⏳     |
| **3-Pin Relay**           | Energizes and reports state | ⏳     |
| **No SSL Errors**         | Zero `start_ssl_client: -1` | ⏳     |
| **Header Authentication** | No 401 errors               | ⏳     |

**Overall Status:** ⏳ Awaiting upload and testing

---

## 🔧 Debugging Commands

### **View All Serial Ports:**

```powershell
pio device list
```

### **Force Specific COM Port:**

```powershell
pio device monitor --baud 115200 --port COM3
```

### **Compile Only (No Upload):**

```powershell
pio run
```

### **Full Clean Build:**

```powershell
pio run --target clean
pio run --target upload
```

### **View Verbose Compilation Errors:**

```powershell
pio run --target upload --verbose
```

---

## 📝 Log Template

**Date:** ****\_\_\_****  
**Time Started:** ****\_\_\_****  
**Firmware Version:** main.cpp (SSL Fix)

### WiFi Connection

- Time to connect: ****\_\_\_****
- IP Address: ****\_\_\_****
- Signal Strength: ****\_\_\_****

### Backend Connectivity

- First GET response: ****\_\_\_****
- HTTP Code: ****\_\_\_****
- Certificate: ✅ / ❌

### Command Execution

- Test Command: ****\_\_\_****
- Execution Time: ****\_\_\_****
- Result: ✅ / ❌

### Issues Encountered

```
[Document any issues here]
```

### Notes

```
[Any other observations]
```

---

## 🚀 Next Phase After Testing

Once all tests pass:

1. **Phase 2: PZEM Integration**

   - Install PZEM004Tv30 library
   - Configure UART pins
   - Read voltage/current/power
   - Send real energy data

2. **Phase 3: Multi-Device Support**

   - Add support for multiple ESP32 units
   - Update Firestore structure
   - Implement device management in Flutter

3. **Phase 4: Production Hardening**
   - Implement OTA updates
   - Add error recovery
   - Implement retry logic
   - Add watchdog timer

---

**Status:** Ready to begin testing ✅
