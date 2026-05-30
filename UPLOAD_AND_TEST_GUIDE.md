# ESP32 SSL Fix & Upload Guide

## ✅ What Was Fixed

The previous error `start_ssl_client: -1` was caused by:

- ❌ `setInsecure()` + `setCACert(nullptr)` = SSL disabled but then tried to connect
- ❌ Headers were capitalized (X-DEVICE-ID instead of x-device-id) - case mismatch with backend

## ✅ Changes Made

**File:** `m:\smart_box_app_esp\src\main.cpp`

### 1. Added DigiCert Root Certificate

```cpp
const char *VERCEL_ROOT_CA = R"(-----BEGIN CERTIFICATE-----
[Full certificate...]
-----END CERTIFICATE-----)";
```

### 2. Fixed GET Request

- ✅ Now uses proper SSL certificate: `client.setCACert(VERCEL_ROOT_CA)`
- ✅ Header case changed to lowercase: `x-device-id`, `x-device-secret`

### 3. Fixed POST Request

- ✅ Now uses proper SSL certificate: `client.setCACert(VERCEL_ROOT_CA)`
- ✅ Header case changed to lowercase: `x-device-id`, `x-device-secret`

---

## 🚀 How to Upload to ESP32

### **Option 1: Using PlatformIO CLI**

```powershell
cd m:\smart_box_app_esp
pio run --target upload
```

**Monitor Serial Output:**

```powershell
pio device monitor --baud 115200
```

### **Option 2: Using VS Code PlatformIO Extension**

1. Open VS Code in `m:\smart_box_app_esp` folder
2. Click **PlatformIO: Upload** button (arrow icon on left sidebar)
3. Or press: `Ctrl+Alt+U`
4. After upload, click **PlatformIO: Monitor** (`Ctrl+Alt+I`)

---

## 📊 Expected Serial Output (After Fix)

```
Smart box ESP32 started (backend mode with EV & 3-pin relay control)

--- Cycle Start ---
Lock state before cmd: LOCKED
GET https://smart-box-admin.vercel.app/api/esp/next-command?deviceId=box_001&lastCommandId=
HTTP GET code: 200
Command payload: {"none":true}

Lock state after cmd: LOCKED
EV relay state: OFF
3-pin relay state: OFF

POST https://smart-box-admin.vercel.app/api/esp/ack
POST body: {"deviceId":"box_001","commandId":"","success":true,"timestamp":5234,"state":{"lock":"LOCKED","ev":false,"p3":false},"energy":{"ok":false,"evmeter":{"voltage":0,"current":0,"power":0,"energy":0},"p3meter":{"voltage":0,"current":0,"power":0,"energy":0}}}
HTTP POST code: 200
Response: {"ok":true,"message":"Status received and saved"}

--- Cycle End ---

[After 5 seconds, repeat...]
```

**Key Indicators of Success:**

- ✅ `HTTP GET code: 200` - Backend responding
- ✅ `HTTP POST code: 200` - Backend accepting status
- ✅ No more `start_ssl_client: -1` errors

---

## 🧪 Testing After Upload

### **Test 1: Verify WiFi Connection**

- Look for successful WiFi connection in serial output
- Should show IP address assigned

### **Test 2: Verify Backend Communication**

- Should see repeated cycles every 5 seconds
- GET should return `{"none":true}` when no commands pending
- POST should return `{"ok":true,...}`

### **Test 3: Send Test Command from Flutter App**

1. Open Flutter app
2. Create a command (e.g., lock the box)
3. Watch Serial Monitor - you should see:
   ```
   Command payload: {"commandId":"cmd_123","actions":{"lock":"LOCK",...}}
   ```
4. ESP32 should execute: `>> Locking box...`
5. Check Firestore: `boxes/box_001` should update with `isLocked: true`

### **Test 4: Test Relay Control**

1. Send command from Flutter to turn ON EV relay
2. Serial Monitor should show: `>> Setting EV to ON`
3. Check pin 19 goes HIGH (relay energizes)
4. Firestore updates: `devices.evCharger.isOn: true`

---

## ⚠️ Troubleshooting

### **Still getting SSL errors?**

1. **Check WiFi connection first:**

   ```
   Connecting to WiFi: SREEHARI
   WiFi connected
   IP address: 192.168.x.x
   ```

2. **Try simple HTTP instead (for testing):**

   - Replace `https://smart-box-admin.vercel.app` with `http://smart-box-admin.vercel.app` in code
   - Use `WiFiClient` instead of `WiFiClientSecure`
   - ⚠️ Only for testing - HTTPS required for production

3. **Check certificate string:**

   - Ensure the `VERCEL_ROOT_CA` certificate is copied exactly
   - Look for line breaks (some were replaced with spaces - that's ok)

4. **Check device headers:**
   - Ensure header keys are **lowercase**: `x-device-id`, `x-device-secret`
   - Backend expects lowercase headers

### **Getting 401 Unauthorized?**

- Check `DEVICE_SECRET` matches backend env var `ESP_DEVICE_SECRET`
- Verify headers are being sent in request

### **Getting 404 Not Found?**

- Verify backend is deployed: https://smart-box-admin.vercel.app
- Check routes exist in backend project
- Try accessing endpoint manually in browser

---

## 📋 Pre-Upload Checklist

- [ ] ESP32 connected to computer via USB
- [ ] PlatformIO recognizes ESP32 board
- [ ] Code compiles without errors
- [ ] WiFi SSID and password are correct
- [ ] Device ID is `"box_001"` (matches Firestore)
- [ ] Device Secret matches backend `.env` var
- [ ] Backend is deployed and accessible
- [ ] Firestore rules are deployed
- [ ] Box document exists: `boxes/box_001` in Firestore

---

## ✅ Success Indicators

After uploading and waiting 5-10 seconds:

1. ✅ Serial Monitor shows repeated "--- Cycle Start ---" and "--- Cycle End ---"
2. ✅ HTTP GET returns code 200
3. ✅ HTTP POST returns code 200
4. ✅ No SSL errors in output
5. ✅ Firestore box document updates with timestamps
6. ✅ Vercel logs show incoming requests (check Vercel dashboard)

---

## 🔗 Next Steps

1. **Upload firmware** using steps above
2. **Monitor serial output** to verify connectivity
3. **Create test command** in Flutter app or Firestore
4. **Watch relay control** execute on ESP32
5. **Verify Firestore updates** in real-time
6. **Integrate PZEM meters** for energy monitoring (Phase 2)

---

**Last Updated:** May 26, 2026  
**Status:** Ready to upload with SSL fix ✅
