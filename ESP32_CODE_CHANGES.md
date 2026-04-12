# ESP32 Code Changes Summary

## Key Changes Needed

Your current ESP32 code needs these updates to work with the backend and support lock + EV relay + 3-pin relay:

---

## 1. **Add Relay Pin Definitions**

After `#define LOCK_ACTIVE_LEVEL HIGH`, add:

```cpp
/********** CONFIG: EV & 3-PIN RELAYS **********/
#define EV_RELAY_PIN 19        // Output to EV relay (change as needed)
#define EV_ACTIVE_LEVEL HIGH   // change if relay is active LOW

#define P3_RELAY_PIN 21        // Output to 3-pin relay (change as needed)
#define P3_ACTIVE_LEVEL HIGH   // change if relay is active LOW
```

---

## 2. **Update State Variables**

Change:

```cpp
String lastCommandId = "";
```

To:

```cpp
String lastCommandId = "";
bool currentEvOn = false;
bool currentP3On = false;
```

---

## 3. **Update DEVICE_ID**

Change:

```cpp
const char *DEVICE_ID = "box-001";
```

To:

```cpp
const char *DEVICE_ID = "box_001";  // Must match Firestore document ID
```

---

## 4. **Add Relay Control Functions**

Add these after the `isLocked()` function:

```cpp
/********** RELAY CONTROL **********/
void setEvRelay(bool on)
{
    digitalWrite(EV_RELAY_PIN, on ? EV_ACTIVE_LEVEL : !EV_ACTIVE_LEVEL);
    currentEvOn = on;
}

void setP3Relay(bool on)
{
    digitalWrite(P3_RELAY_PIN, on ? P3_ACTIVE_LEVEL : !P3_ACTIVE_LEVEL);
    currentP3On = on;
}

bool getEvStatus()
{
    return currentEvOn;
}

bool getP3Status()
{
    return currentP3On;
}
```

---

## 5. **Update fetchNextCommand() Function**

Replace the entire `fetchNextCommand()` function with:

```cpp
/********** BACKEND: GET NEXT COMMAND **********/
// Backend returns JSON like:
// { "none": true }
// or
// {
//   "commandId": "cmd_123",
//   "actions": {
//     "lock": "LOCK" | "UNLOCK" | "NONE",
//     "ev": true | false | null,
//     "p3": true | false | null
//   }
// }
bool fetchNextCommand(String &commandId,
                      String &lockAction,
                      bool &evSet,
                      bool &evOn,
                      bool &p3Set,
                      bool &p3On)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED)
            return false;
    }

    HTTPClient http;
    String url = String(BACKEND_BASE_URL) + BACKEND_NEXT_COMMAND +
                 "?deviceId=" + DEVICE_ID +
                 "&lastCommandId=" + lastCommandId;

    Serial.print("GET ");
    Serial.println(url);

    http.begin(url);
    http.addHeader("X-DEVICE-ID", DEVICE_ID);
    http.addHeader("X-DEVICE-SECRET", DEVICE_SECRET);

    int httpCode = http.GET();
    if (httpCode <= 0)
    {
        Serial.print("HTTP GET failed: ");
        Serial.println(http.errorToString(httpCode));
        http.end();
        return false;
    }

    Serial.print("HTTP GET code: ");
    Serial.println(httpCode);

    if (httpCode != 200)
    {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    Serial.print("Payload: ");
    Serial.println(payload);

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Serial.print("JSON error: ");
        Serial.println(err.c_str());
        return false;
    }

    // Check if no commands
    if (doc.containsKey("none") && doc["none"].as<bool>())
    {
        return false;
    }

    commandId = doc["commandId"].as<String>();

    // Parse actions object
    JsonObject actions = doc["actions"].as<JsonObject>();

    lockAction = "NONE";
    if (actions.containsKey("lock"))
    {
        lockAction = actions["lock"].as<String>();
    }

    evSet = false;
    evOn = false;
    if (actions.containsKey("ev") && !actions["ev"].isNull())
    {
        evSet = true;
        evOn = actions["ev"].as<bool>();
    }

    p3Set = false;
    p3On = false;
    if (actions.containsKey("p3") && !actions["p3"].isNull())
    {
        p3Set = true;
        p3On = actions["p3"].as<bool>();
    }

    return true;
}
```

---

## 6. **Update sendStatus() Function**

Replace the entire `sendStatus()` function signature and body:

```cpp
/********** BACKEND: SEND ACK + STATUS **********/
// Sends: lock state, EV state, 3-pin state, and optional energy data
bool sendStatus(const String &commandId,
                const String &lockAction,
                bool commandSuccess,
                bool locked,
                bool evOn,
                bool p3On)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED)
            return false;
    }

    HTTPClient http;
    String url = String(BACKEND_BASE_URL) + BACKEND_ACK_ENDPOINT;
    Serial.print("POST ");
    Serial.println(url);

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-DEVICE-ID", DEVICE_ID);
    http.addHeader("X-DEVICE-SECRET", DEVICE_SECRET);

    StaticJsonDocument<1024> doc;
    doc["commandId"]       = commandId;
    doc["lockAction"]      = lockAction;
    doc["success"]         = commandSuccess;
    doc["lockState"]       = locked ? "LOCKED" : "UNLOCKED";
    doc["evOn"]            = evOn;
    doc["p3On"]            = p3On;
    doc["timestampMillis"] = (long long)millis();

    // Placeholder for PZEM energy data (add real values later)
    JsonObject p3meter = doc.createNestedObject("p3meter");
    p3meter["ok"] = false;

    JsonObject evmeter = doc.createNestedObject("evmeter");
    evmeter["ok"] = false;

    String body;
    serializeJson(doc, body);

    Serial.print("POST body: ");
    Serial.println(body);

    int httpCode = http.POST(body);
    if (httpCode <= 0)
    {
        Serial.print("HTTP POST failed: ");
        Serial.println(http.errorToString(httpCode));
        http.end();
        return false;
    }

    Serial.print("HTTP POST code: ");
    Serial.println(httpCode);

    String response = http.getString();
    Serial.print("Response: ");
    Serial.println(response);

    http.end();
    return (httpCode == 200 || httpCode == 201);
}
```

---

## 7. **Update setup() Function**

Replace the setup function to initialize all 3 relay pins:

```cpp
void setup()
{
    Serial.begin(115200);
    delay(500);

    // Lock control
    pinMode(LOCK_CONTROL_PIN, OUTPUT);
    digitalWrite(LOCK_CONTROL_PIN, !LOCK_ACTIVE_LEVEL);
    pinMode(LOCK_FEEDBACK_PIN, INPUT_PULLUP);

    // EV relay
    pinMode(EV_RELAY_PIN, OUTPUT);
    digitalWrite(EV_RELAY_PIN, !EV_ACTIVE_LEVEL);

    // 3-pin relay
    pinMode(P3_RELAY_PIN, OUTPUT);
    digitalWrite(P3_RELAY_PIN, !P3_ACTIVE_LEVEL);

    connectWiFi();

    Serial.println("\n=== Smart Box ESP32 Started ===");
    Serial.print("Device ID: ");
    Serial.println(DEVICE_ID);
    Serial.println("Ready to poll backend for commands");
}
```

---

## 8. **Replace loop() Function**

Replace the entire `loop()` function:

```cpp
void loop()
{
    unsigned long now = millis();

    if (now - lastCycleMillis >= CYCLE_INTERVAL_MS)
    {
        lastCycleMillis = now;

        Serial.println("\n--- Cycle Start ---");

        // 1) Read current states before executing command
        bool lockedBefore = isLocked();
        bool evBefore = getEvStatus();
        bool p3Before = getP3Status();

        Serial.print("Lock: ");
        Serial.println(lockedBefore ? "LOCKED" : "UNLOCKED");
        Serial.print("EV: ");
        Serial.println(evBefore ? "ON" : "OFF");
        Serial.print("3-Pin: ");
        Serial.println(p3Before ? "ON" : "OFF");

        // 2) Fetch next command from backend
        String cmdId, lockAction;
        bool evSet = false, evCmdOn = false;
        bool p3Set = false, p3CmdOn = false;

        bool hasCommand = fetchNextCommand(cmdId, lockAction, evSet, evCmdOn, p3Set, p3CmdOn);

        bool commandSuccess = true;
        String reportCmdId = lastCommandId;
        String reportLockAction = "NONE";

        if (hasCommand)
        {
            Serial.print("Command received: ");
            Serial.println(cmdId);
            Serial.print("  Lock: ");
            Serial.println(lockAction);
            Serial.print("  EV set: ");
            Serial.print(evSet);
            Serial.print(" to ");
            Serial.println(evCmdOn);
            Serial.print("  P3 set: ");
            Serial.print(p3Set);
            Serial.print(" to ");
            Serial.println(p3CmdOn);

            // Execute lock command
            if (lockAction == "LOCK")
            {
                Serial.println(">> Locking box...");
                lockBox();
            }
            else if (lockAction == "UNLOCK")
            {
                Serial.println(">> Unlocking box...");
                unlockBox();
            }

            // Execute EV relay command
            if (evSet)
            {
                Serial.print(">> Setting EV to ");
                Serial.println(evCmdOn ? "ON" : "OFF");
                setEvRelay(evCmdOn);
            }

            // Execute 3-pin relay command
            if (p3Set)
            {
                Serial.print(">> Setting 3-Pin to ");
                Serial.println(p3CmdOn ? "ON" : "OFF");
                setP3Relay(p3CmdOn);
            }

            lastCommandId = cmdId;
            reportCmdId = cmdId;
            reportLockAction = lockAction;
        }

        // 3) Read final states after command execution
        bool lockedAfter = isLocked();
        bool evAfter = getEvStatus();
        bool p3After = getP3Status();

        Serial.println("State after command:");
        Serial.print("  Lock: ");
        Serial.println(lockedAfter ? "LOCKED" : "UNLOCKED");
        Serial.print("  EV: ");
        Serial.println(evAfter ? "ON" : "OFF");
        Serial.print("  3-Pin: ");
        Serial.println(p3After ? "ON" : "OFF");

        // 4) Send status back to backend
        bool ok = sendStatus(reportCmdId, reportLockAction, commandSuccess, lockedAfter, evAfter, p3After);
        if (!ok)
        {
            Serial.println("!! Failed to send status");
        }

        Serial.println("--- Cycle End ---\n");
    }

    delay(100);
}
```

---

## Configuration Checklist

Before uploading, verify:

- [ ] `WIFI_SSID` = your Wi‑Fi name
- [ ] `WIFI_PASSWORD` = your Wi‑Fi password
- [ ] `BACKEND_BASE_URL` = your Next.js backend URL (e.g., `https://yourdomain.com`)
- [ ] `DEVICE_ID` = `"box_001"` (must match Firestore document)
- [ ] `DEVICE_SECRET` = `"super-secret-token"` (must match backend env var)
- [ ] `LOCK_CONTROL_PIN` = your lock relay pin
- [ ] `LOCK_FEEDBACK_PIN` = your lock sensor pin
- [ ] `EV_RELAY_PIN` = your EV relay pin
- [ ] `P3_RELAY_PIN` = your 3-pin relay pin

---

## Testing

After uploading to ESP32:

1. **Check Serial Monitor** (115200 baud) - you should see:

   ```
   === Smart Box ESP32 Started ===
   Device ID: box_001
   Ready to poll backend for commands
   ```

2. **Create a test command** in Firestore `commands` collection

3. **Watch Serial Monitor** for:

   - "Cycle Start"
   - "Payload:" with the command
   - "Locking box..." / "Unlocking box..." / "Setting EV to..." / etc.
   - "POST body:" with status
   - "Cycle End"

4. **Check Firestore** - `boxes/box_001` should update with latest state
