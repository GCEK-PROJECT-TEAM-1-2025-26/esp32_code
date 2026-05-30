# SSL Connection Fix - Summary

## 🔴 Problem

**Error:** `[WiFiClientSecure.cpp:144] connect(): start_ssl_client: -1`

This prevented ESP32 from connecting to Vercel backend over HTTPS.

### Root Causes:

1. ❌ `client.setInsecure()` + `client.setCACert(nullptr)` = Conflicting SSL settings
2. ❌ Missing proper root certificate for Vercel's SSL
3. ❌ Header keys were capitalized (X-DEVICE-ID) but backend expects lowercase

---

## ✅ Solution Implemented

### **Change 1: Added DigiCert Root Certificate**

**Before:**

```cpp
#include <ArduinoJson.h>
// [no certificate]
```

**After:**

```cpp
#include <ArduinoJson.h>

// Root certificate for Vercel (DigiCert Global Root CA)
const char *VERCEL_ROOT_CA = R"(-----BEGIN CERTIFICATE-----
[full certificate content...]
-----END CERTIFICATE-----)";
```

### **Change 2: Fixed GET Request SSL**

**Before:**

```cpp
WiFiClientSecure client;
client.setInsecure();      // ❌ Disables SSL
client.setCACert(nullptr); // ❌ Disables verification

http.addHeader("X-DEVICE-ID", DEVICE_ID);        // ❌ Wrong case
http.addHeader("X-DEVICE-SECRET", DEVICE_SECRET); // ❌ Wrong case
```

**After:**

```cpp
WiFiClientSecure client;
client.setCACert(VERCEL_ROOT_CA); // ✅ Proper SSL cert

http.addHeader("x-device-id", DEVICE_ID);        // ✅ Lowercase
http.addHeader("x-device-secret", DEVICE_SECRET); // ✅ Lowercase
```

### **Change 3: Fixed POST Request SSL**

Same changes as GET request - removed `setInsecure()` + `setCACert(nullptr)`, added proper certificate and lowercase headers.

---

## 🧪 Expected Results After Upload

### Before Fix:

```
GET https://smart-box-admin.vercel.app/api/esp/next-command?...
[245643][E][WiFiClientSecure.cpp:144] connect(): start_ssl_client: -1
HTTP GET failed: connection refused

POST https://smart-box-admin.vercel.app/api/esp/ack
[250655][E][WiFiClientSecure.cpp:144] connect(): start_ssl_client: -1
HTTP POST failed: connection refused
```

### After Fix (Expected):

```
GET https://smart-box-admin.vercel.app/api/esp/next-command?...
HTTP GET code: 200
Command payload: {"none":true}

POST https://smart-box-admin.vercel.app/api/esp/ack
HTTP POST code: 200
Response: {"ok":true,"message":"Status received and saved"}
```

---

## 📝 Files Modified

| File                                               | Change                                                   | Status  |
| -------------------------------------------------- | -------------------------------------------------------- | ------- |
| `m:\smart_box_app_esp\src\main.cpp`                | Added VERCEL_ROOT_CA, fixed SSL setup, fixed header case | ✅ Done |
| `m:\smart_box_app_esp\ESP32_INTEGRATION_STATUS.md` | Updated known issues section                             | ✅ Done |
| `m:\smart_box_app_esp\UPLOAD_AND_TEST_GUIDE.md`    | New guide with upload instructions                       | ✅ Done |

---

## 🚀 Next Action

**Upload the fixed firmware to ESP32:**

```powershell
cd m:\smart_box_app_esp
pio run --target upload
```

Then monitor output:

```powershell
pio device monitor --baud 115200
```

Should see HTTP 200 responses instead of SSL errors.

---

**Status:** Ready to test ✅
