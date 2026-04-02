#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/********** CONFIG: WIFI **********/
const char *WIFI_SSID = "YOUR_WIFI_SSID";         // TODO: set
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; // TODO: set

/********** CONFIG: BACKEND **********/
// Base URL of your backend (Next.js, etc.). No trailing slash.
const char *BACKEND_BASE_URL = "https://your-backend-domain.com"; // TODO: set
// Endpoint paths on your backend.
const char *BACKEND_NEXT_COMMAND = "/api/esp/next-command"; // GET
const char *BACKEND_ACK_ENDPOINT = "/api/esp/ack";          // POST
// Device identity (must match how backend identifies this box).
const char *DEVICE_ID = "box-001";                // TODO: set
const char *DEVICE_SECRET = "super-secret-token"; // TODO: set (shared secret header)

/********** CONFIG: LOCK PINS **********/
#define LOCK_CONTROL_PIN 5     // Output to relay / driver (change as needed)
#define LOCK_FEEDBACK_PIN 18   // Input from microswitch/hall sensor (change as needed)
#define LOCK_ACTIVE_LEVEL HIGH // Level that activates driver (change if low-active)

/********** TIMING **********/
// How often to run a full cycle: read inputs, poll backend, send status.
unsigned long lastCycleMillis = 0;
const unsigned long CYCLE_INTERVAL_MS = 3000; // 3 seconds

/********** STATE **********/
String lastCommandId = ""; // Last processed command, so backend doesn’t resend

/********** WIFI **********/
void connectWiFi()
{
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nWiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("\nWiFi connection failed");
    }
}

/********** LOCK CONTROL **********/
void lockBox()
{
    // Simple pulse; adjust timing and logic for your driver.
    digitalWrite(LOCK_CONTROL_PIN, LOCK_ACTIVE_LEVEL);
    delay(500);
    digitalWrite(LOCK_CONTROL_PIN, !LOCK_ACTIVE_LEVEL);
}

void unlockBox()
{
    // If your hardware needs a different control (other pin / direction), change here.
    digitalWrite(LOCK_CONTROL_PIN, LOCK_ACTIVE_LEVEL);
    delay(500);
    digitalWrite(LOCK_CONTROL_PIN, !LOCK_ACTIVE_LEVEL);
}

bool isLocked()
{
    int val = digitalRead(LOCK_FEEDBACK_PIN);
    // Adjust according to your sensor wiring (HIGH vs LOW means locked).
    return (val == HIGH);
}

/********** BACKEND: GET NEXT COMMAND **********/
// Expects backend to return JSON like:
// { "none": true }
// or { "commandId": "abc123", "action": "LOCK" }
bool fetchNextCommand(String &commandId, String &action)
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
    Serial.print("Command payload: ");
    Serial.println(payload);

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Serial.print("JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    if (doc.containsKey("none") && doc["none"].as<bool>() == true)
    {
        return false; // No new command
    }

    commandId = doc["commandId"].as<String>();
    action = doc["action"].as<String>();

    return true;
}

/********** BACKEND: SEND ACK + STATUS **********/
// Sends current status and the result of the last command.
// You can extend the JSON with PZEM measurements later.
bool sendStatus(const String &commandId,
                const String &action,
                bool commandSuccess,
                bool locked)
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

    StaticJsonDocument<512> doc;
    doc["deviceId"] = DEVICE_ID;
    doc["commandId"] = commandId;
    doc["action"] = action;
    doc["success"] = commandSuccess;
    doc["lockState"] = locked ? "LOCKED" : "UNLOCKED";
    doc["timestampMillis"] = (long long)millis();

    // Placeholder energy block – fill from PZEM later.
    JsonObject energy = doc.createNestedObject("energy");
    energy["hasData"] = false;

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

/********** ARDUINO SETUP & LOOP **********/
void setup()
{
    Serial.begin(115200);
    delay(1000);

    pinMode(LOCK_CONTROL_PIN, OUTPUT);
    digitalWrite(LOCK_CONTROL_PIN, !LOCK_ACTIVE_LEVEL); // idle state

    pinMode(LOCK_FEEDBACK_PIN, INPUT_PULLUP); // change if needed

    connectWiFi();

    Serial.println("Smart box ESP32 started (backend mode)");
}

void loop()
{
    unsigned long now = millis();

    if (now - lastCycleMillis >= CYCLE_INTERVAL_MS)
    {
        lastCycleMillis = now;

        // 1) Read current lock state
        bool lockedBefore = isLocked();
        Serial.print("Lock state before cmd: ");
        Serial.println(lockedBefore ? "LOCKED" : "UNLOCKED");

        // 2) Fetch next command (if any)
        String cmdId, action;
        bool hasCommand = fetchNextCommand(cmdId, action);

        bool commandSuccess = true;
        String reportCmdId = lastCommandId;
        String reportAction = "STATUS";

        if (hasCommand)
        {
            Serial.print("Received command: ");
            Serial.print(cmdId);
            Serial.print(" action=");
            Serial.println(action);

            if (action == "LOCK")
            {
                lockBox();
            }
            else if (action == "UNLOCK")
            {
                unlockBox();
            }
            else
            {
                Serial.println("Unknown action");
                commandSuccess = false;
            }

            lastCommandId = cmdId;
            reportCmdId = cmdId;
            reportAction = action;
        }

        // 3) Read lock state after command
        bool lockedAfter = isLocked();
        Serial.print("Lock state after cmd: ");
        Serial.println(lockedAfter ? "LOCKED" : "UNLOCKED");

        // 4) Send status & command result back to backend
        bool ok = sendStatus(reportCmdId, reportAction, commandSuccess, lockedAfter);
        if (!ok)
        {
            Serial.println("Failed to send status to backend");
        }
    }

    delay(50);
}