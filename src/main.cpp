#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

/********** CONFIG: WIFI **********/
const char *WIFI_SSID = "SREEHARI";      // TODO: set
const char *WIFI_PASSWORD = "447643899"; // TODO: set

/********** CONFIG: BACKEND **********/
// Base URL of your backend (Next.js, etc.). No trailing slash.
const char *BACKEND_BASE_URL = "https://smart-box-admin.vercel.app"; // TODO: set
// Endpoint paths on your backend.
const char *BACKEND_NEXT_COMMAND = "/api/esp/next-command"; // GET
const char *BACKEND_ACK_ENDPOINT = "/api/esp/ack";          // POST
// Device identity (must match how backend identifies this box).
const char *DEVICE_ID = "box_001";                // TODO: set (must match Firestore doc)
const char *DEVICE_SECRET = "super-secret-token"; // TODO: set (must match backend env var)

/********** CONFIG: LOCK PINS **********/
#define LOCK_CONTROL_PIN 5     // Output to relay / driver (change as needed)
#define LOCK_FEEDBACK_PIN 18   // Input from microswitch/hall sensor (change as needed)
#define LOCK_ACTIVE_LEVEL HIGH // Level that activates driver (change if low-active)

/********** CONFIG: EV & 3-PIN RELAYS **********/
#define EV_RELAY_PIN 19      // Output to EV relay (change as needed)
#define EV_ACTIVE_LEVEL HIGH // change if relay is active LOW
#define P3_RELAY_PIN 21      // Output to 3-pin relay (change as needed)
#define P3_ACTIVE_LEVEL HIGH // change if relay is active LOW

/********** TIMING **********/
// How often to run a full cycle: read inputs, poll backend, send status.
unsigned long lastCycleMillis = 0;
const unsigned long CYCLE_INTERVAL_MS = 5000; // 5 seconds

/********** STATE **********/
String lastCommandId = ""; // Last processed command, so backend doesn't resend
bool currentEvOn = false;  // Track current EV relay state
bool currentP3On = false;  // Track current 3-pin relay state

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

/********** RELAY CONTROL: EV & 3-PIN **********/
void setEvRelay(bool on)
{
    int level = on ? EV_ACTIVE_LEVEL : !EV_ACTIVE_LEVEL;
    digitalWrite(EV_RELAY_PIN, level);
    currentEvOn = on;
    Serial.print("EV relay set to: ");
    Serial.println(on ? "ON" : "OFF");
}

void setP3Relay(bool on)
{
    int level = on ? P3_ACTIVE_LEVEL : !P3_ACTIVE_LEVEL;
    digitalWrite(P3_RELAY_PIN, level);
    currentP3On = on;
    Serial.print("3-pin relay set to: ");
    Serial.println(on ? "ON" : "OFF");
}

bool getEvStatus()
{
    return currentEvOn;
}

bool getP3Status()
{
    return currentP3On;
}

/********** BACKEND: GET NEXT COMMAND **********/
// Expects backend to return JSON like:
// { "none": true }
// or { "commandId": "abc123", "actions": { "lock": "LOCK", "ev": true, "p3": false } }
bool fetchNextCommand(String &commandId, String &lockAction, bool &evSet, bool &evOn, bool &p3Set, bool &p3On)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED)
            return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // OK for now

    HTTPClient http;

    String url = String(BACKEND_BASE_URL) + BACKEND_NEXT_COMMAND +
                 "?lastCommandId=" + lastCommandId;

    Serial.println("\n--- FETCH COMMAND ---");
    Serial.println(url);

    http.begin(client, url);
    http.setTimeout(10000);

    // ✅ FIXED HEADERS (LOWERCASE)
    http.addHeader("x-device-id", DEVICE_ID);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    int httpCode = http.GET();

    Serial.print("HTTP CODE: ");
    Serial.println(httpCode);

    String payload = http.getString();
    Serial.print("RESPONSE: ");
    Serial.println(payload);

    if (httpCode != 200)
    {
        Serial.println("GET FAILED");
        http.end();
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (err)
    {
        Serial.print("JSON ERROR: ");
        Serial.println(err.c_str());
        http.end();
        return false;
    }

    if (doc["none"] == true)
    {
        http.end();
        return false;
    }

    commandId = doc["commandId"].as<String>();

    lockAction = "";
    evSet = false;
    p3Set = false;

    if (doc.containsKey("actions"))
    {
        JsonObject actions = doc["actions"];

        if (actions.containsKey("lock"))
            lockAction = actions["lock"].as<String>();

        if (actions.containsKey("ev"))
        {
            evSet = true;
            evOn = actions["ev"];
        }

        if (actions.containsKey("p3"))
        {
            p3Set = true;
            p3On = actions["p3"];
        }
    }

    http.end();
    return true;
}

/********** BACKEND: SEND ACK + STATUS **********/
// Sends current status and the result of the last command.
// You can extend the JSON with PZEM measurements later.
bool sendStatus(const String &commandId,
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

    // ❌ Don't send empty commandId
    if (commandId == "")
    {
        Serial.println("Skipping POST (no command)");
        return true;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    String url = String(BACKEND_BASE_URL) + BACKEND_ACK_ENDPOINT;

    Serial.println("\n--- SEND STATUS ---");
    Serial.println(url);

    http.begin(client, url);
    http.setTimeout(10000);

    http.addHeader("Content-Type", "application/json");

    // ✅ FIXED HEADERS
    http.addHeader("x-device-id", DEVICE_ID);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    StaticJsonDocument<512> doc;

    doc["commandId"] = commandId;
    doc["success"] = commandSuccess;
    doc["timestamp"] = millis();

    JsonObject state = doc.createNestedObject("state");
    state["lock"] = locked ? "LOCKED" : "UNLOCKED";
    state["ev"] = evOn;
    state["p3"] = p3On;

    JsonObject energy = doc.createNestedObject("energy");
    energy["ok"] = false;

    String body;
    serializeJson(doc, body);

    Serial.print("POST BODY: ");
    Serial.println(body);

    int httpCode = http.POST(body);

    Serial.print("HTTP CODE: ");
    Serial.println(httpCode);

    String response = http.getString();
    Serial.print("RESPONSE: ");
    Serial.println(response);

    http.end();

    return (httpCode == 200 || httpCode == 201);
}

/********** ARDUINO SETUP & LOOP **********/
void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Lock pins
    pinMode(LOCK_CONTROL_PIN, OUTPUT);
    digitalWrite(LOCK_CONTROL_PIN, !LOCK_ACTIVE_LEVEL); // idle state
    pinMode(LOCK_FEEDBACK_PIN, INPUT_PULLUP);           // change if needed

    // Relay pins
    pinMode(EV_RELAY_PIN, OUTPUT);
    digitalWrite(EV_RELAY_PIN, !EV_ACTIVE_LEVEL); // idle state
    currentEvOn = false;

    pinMode(P3_RELAY_PIN, OUTPUT);
    digitalWrite(P3_RELAY_PIN, !P3_ACTIVE_LEVEL); // idle state
    currentP3On = false;

    connectWiFi();

    Serial.println("Smart box ESP32 started (backend mode with EV & 3-pin relay control)");
}

void loop()
{
    unsigned long now = millis();

    if (now - lastCycleMillis >= CYCLE_INTERVAL_MS)
    {
        lastCycleMillis = now;

        // 1) Read current lock state before any commands
        bool lockedBefore = isLocked();
        Serial.print("Lock state before cmd: ");
        Serial.println(lockedBefore ? "LOCKED" : "UNLOCKED");

        // 2) Fetch next command (if any)
        String cmdId;
        String lockAction;
        bool evSet, evOn, p3Set, p3On;
        bool hasCommand = fetchNextCommand(cmdId, lockAction, evSet, evOn, p3Set, p3On);

        bool commandSuccess = true;

        if (hasCommand)
        {
            Serial.print("Received command: ");
            Serial.println(cmdId);

            // Handle lock command
            if (!lockAction.isEmpty())
            {
                if (lockAction == "LOCK")
                {
                    lockBox();
                }
                else if (lockAction == "UNLOCK")
                {
                    unlockBox();
                }
                else
                {
                    Serial.println("Unknown lock action");
                    commandSuccess = false;
                }
            }

            // Handle EV relay command
            if (evSet)
            {
                setEvRelay(evOn);
            }

            // Handle 3-pin relay command
            if (p3Set)
            {
                setP3Relay(p3On);
            }

            lastCommandId = cmdId;
        }

        // 3) Read final states after all commands executed
        bool lockedAfter = isLocked();
        bool evStatus = getEvStatus();
        bool p3Status = getP3Status();

        Serial.print("Lock state after cmd: ");
        Serial.println(lockedAfter ? "LOCKED" : "UNLOCKED");
        Serial.print("EV relay state: ");
        Serial.println(evStatus ? "ON" : "OFF");
        Serial.print("3-pin relay state: ");
        Serial.println(p3Status ? "ON" : "OFF");

        // 4) Send status & command result back to backend
        bool ok = sendStatus(lastCommandId, commandSuccess, lockedAfter, evStatus, p3Status);
        if (!ok)
        {
            Serial.println("Failed to send status to backend");
        }
    }

    delay(50);
}
