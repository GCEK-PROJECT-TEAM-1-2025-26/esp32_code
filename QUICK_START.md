# 🚀 QUICK START - Upload & Test (5 Min)

**Status:** ✅ Firmware Ready  
**Date:** May 26, 2026

---

## 📋 Prerequisites (Check These First)

- ✅ ESP32 connected via USB
- ✅ PlatformIO installed (`pio --version`)
- ✅ Backend deployed at: https://smart-box-admin.vercel.app
- ✅ Firestore rules deployed

---

## 🎯 Step-by-Step

### **Step 1: Upload (2 min)**

```powershell
cd m:\smart_box_app_esp
pio run --target upload --verbose
```

**Expected output:**

```
Uploading .pio\build\esp32doit-devkit-v1\firmware.bin
...
Hash of data verified.
Hard resetting via RTS pin...
```

---

### **Step 2: Monitor Serial (1 min)**

```powershell
pio device monitor --baud 115200
```

**Expected output (First 10 seconds):**

```
Connecting to WiFi: SREEHARI
............
WiFi connected
IP address: 192.168.x.x
Smart box ESP32 started (backend mode with EV & 3-pin relay control)
```

---

### **Step 3: Verify Backend Communication (2 min)**

**Watch Serial Monitor for:**

```
--- Cycle Start ---
Lock state before cmd: LOCKED
GET https://smart-box-admin.vercel.app/api/esp/next-command?...
HTTP GET code: 200        ← ✅ This is what we want!
Command payload: {"none":true}

Lock state after cmd: LOCKED
EV relay state: OFF
3-pin relay state: OFF

POST https://smart-box-admin.vercel.app/api/esp/ack
POST body: {...}
HTTP POST code: 200       ← ✅ This is what we want!
Response: {"ok":true,...}

--- Cycle End ---
```

---

## ✅ Success Indicators

- ✅ **HTTP GET code: 200** - Backend responding
- ✅ **HTTP POST code: 200** - Backend accepting status
- ✅ **No SSL errors** - `setInsecure()` is working
- ✅ **Repeats every 5 seconds** - Cycle running properly
- ✅ **WiFi connected** - Network communication OK

---

## ❌ If Something Goes Wrong

### **WiFi Connection Failed**

```
Connecting to WiFi: SREEHARI
.....................
WiFi connection failed
```

- [ ] Check WiFi SSID/password in code
- [ ] Verify router is accessible
- [ ] Move ESP32 closer to router

### **SSL/HTTP Errors**

```
HTTP GET failed: connection refused
start_ssl_client: -4396
```

- [ ] Verify backend is deployed: https://smart-box-admin.vercel.app
- [ ] Check internet connectivity
- [ ] Read: CERTIFICATE_FIX.md

### **Getting 401 Unauthorized**

```
HTTP GET code: 401
```

- [ ] Verify headers are lowercase: `x-device-id`, `x-device-secret`
- [ ] Check `DEVICE_SECRET` matches backend `ESP_DEVICE_SECRET` env var
- [ ] Verify `DEVICE_ID` is `box_001`

---

## 🔍 Open Firestore Console (Parallel)

While monitoring, open another browser tab:

**URL:** https://console.firebase.google.com/project/smart-box-admin/firestore

**Navigate to:** `boxes > box_001`

**Watch for:**

- ✅ `lastUpdated` timestamp updates every 5 seconds
- ✅ Fields populate correctly

---

## 📊 Current Configuration

| Setting            | Value                              |
| ------------------ | ---------------------------------- |
| **WiFi SSID**      | SREEHARI                           |
| **WiFi Password**  | 447643899                          |
| **Device ID**      | box_001                            |
| **Device Secret**  | super-secret-token                 |
| **Backend URL**    | https://smart-box-admin.vercel.app |
| **Cycle Interval** | 5 seconds                          |

---

## 🎓 What's Happening

```
Every 5 seconds:
1. Read lock feedback sensor
2. Ask backend: "Any commands for me?"
3. Backend checks Firestore for pending commands
4. If command exists: Execute it (lock/relay/etc)
5. Report back: "Executed command, current state is..."
6. Backend updates Firestore
7. Flutter app sees real-time updates
```

---

## 🚨 Troubleshooting Flowchart

```
WiFi Connected?
├─ NO → Check WiFi credentials in code
└─ YES → Continue

HTTP GET code 200?
├─ NO (SSL error) → Check CERTIFICATE_FIX.md
├─ NO (401) → Check device credentials
├─ NO (other) → Backend might be down
└─ YES → Continue

HTTP POST code 200?
├─ NO → Same as above
└─ YES → ✅ SUCCESS!

Firestore updating?
├─ NO → Check security rules deployed
└─ YES → ✅ FULL SUCCESS!
```

---

## 📞 Need Help?

| Issue                  | Document                    |
| ---------------------- | --------------------------- |
| SSL error              | CERTIFICATE_FIX.md          |
| Backend not responding | UPLOAD_AND_TEST_GUIDE.md    |
| Firestore not updating | TESTING_AND_VERIFICATION.md |
| Project overview       | MASTER_CHECKLIST.md         |
| All docs               | DOCUMENTATION_INDEX.md      |

---

## ✅ You're Done!

After seeing HTTP 200 responses, the ESP32 is successfully:

- ✅ Connected to WiFi
- ✅ Communicating with backend
- ✅ Sending status to Firestore
- ✅ Ready for command testing

**Next:** Read `TESTING_AND_VERIFICATION.md` for full test suite

---

**Ready? Type this:**

```powershell
cd m:\smart_box_app_esp; pio run --target upload
```

**Good luck! 🚀**
