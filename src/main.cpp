#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Wire.h>

// Store energy readings
struct EnergyReading
{
    float voltage;
    float current;
    float power;
    float energy;
    bool ok;
};

#include "oled_display.h"

/********** CONFIG: WIFI **********/
const char *WIFI_SSID = "SREEHARI";      // TODO: set
const char *WIFI_PASSWORD = "447643899"; // TODO: set

/********** CONFIG: BACKEND **********/
const char *BACKEND_BASE_URL = "https://smart-box-admin.vercel.app"; // TODO: set
const char *BACKEND_NEXT_COMMAND = "/api/esp/next-command"; // GET
const char *BACKEND_ACK_ENDPOINT = "/api/esp/ack";          // POST
const char *DEVICE_ID = "box_001";                // TODO: set
const char *DEVICE_SECRET = "super-secret-token"; // TODO: set

/********** CONFIG: LOCK PINS **********/
#define LOCK_CONTROL_PIN 5
#define LOCK_FEEDBACK_PIN 18
#define LOCK_ACTIVE_LEVEL LOW

/********** CONFIG: EV & 3-PIN RELAYS **********/
#define EV_RELAY_PIN 19
#define EV_ACTIVE_LEVEL HIGH
#define P3_RELAY_PIN 23
#define P3_ACTIVE_LEVEL HIGH

/********** CONFIG: PZEM ENERGY METERS **********/
#define PZEM_EV_RX_PIN 16
#define PZEM_EV_TX_PIN 17
#define PZEM_EV_BAUD 9600

#define PZEM_P3_RX_PIN 13
#define PZEM_P3_TX_PIN 12
#define PZEM_P3_BAUD 9600

#define PZEM_EV_ADDR 0xF8
#define PZEM_P3_ADDR 0xF8

/********** CONFIG: RFID READER **********/
#define RFID_STATUS_PIN 32

/********** TIMING **********/
unsigned long lastCycleMillis = 0;
const unsigned long CYCLE_INTERVAL_MS = 5000;
unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;

/********** STATE **********/
String lastCommandId = "";
bool currentEvOn = false;
bool currentP3On = false;
bool rfidCardDetected = false;
bool isLocked_state = true;
bool wifiConnected = false;
String lastError = "";
int errorCode = 0;

/********** OLED DISPLAY CONFIG **********/
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/********** PZEM INSTANCES **********/
PZEM004Tv30 pzemEv(Serial2, PZEM_EV_RX_PIN, PZEM_EV_TX_PIN, PZEM_EV_ADDR);
PZEM004Tv30 pzemP3(Serial1, PZEM_P3_RX_PIN, PZEM_P3_TX_PIN, PZEM_P3_ADDR);

EnergyReading evEnergyReading = {0.0, 0.0, 0.0, 0.0, false};
EnergyReading p3EnergyReading = {0.0, 0.0, 0.0, 0.0, false};

/********** PZEM ENERGY READING **********/
void readEnergyMeters()
{
    float evVoltage = pzemEv.voltage();
    float evCurrent = pzemEv.current();
    float evPower = pzemEv.power();
    float evEnergy = pzemEv.energy();

    if (!isnan(evVoltage) && evVoltage > 0)
    {
        evEnergyReading.voltage = evVoltage;
        evEnergyReading.current = evCurrent;
        evEnergyReading.power = evPower;
        evEnergyReading.energy = evEnergy;
        evEnergyReading.ok = true;
        Serial.print("EV Meter - V:");
        Serial.print(evVoltage);
        Serial.print("V I:");
        Serial.print(evCurrent);
        Serial.print("A P:");
        Serial.print(evPower);
        Serial.print("W E:");
        Serial.print(evEnergy);
        Serial.println("kWh");
    }
    else
    {
        evEnergyReading.ok = false;
        Serial.println("EV Meter - No response");
    }

    float p3Voltage = pzemP3.voltage();
    float p3Current = pzemP3.current();
    float p3Power = pzemP3.power();
    float p3Energy = pzemP3.energy();

    if (!isnan(p3Voltage) && p3Voltage > 0)
    {
        p3EnergyReading.voltage = p3Voltage;
        p3EnergyReading.current = p3Current;
        p3EnergyReading.power = p3Power;
        p3EnergyReading.energy = p3Energy;
        p3EnergyReading.ok = true;
        Serial.print("3-Pin Meter - V:");
        Serial.print(p3Voltage);
        Serial.print("V I:");
        Serial.print(p3Current);
        Serial.print("A P:");
        Serial.print(p3Power);
        Serial.print("W E:");
        Serial.print(p3Energy);
        Serial.println("kWh");
    }
    else
    {
        p3EnergyReading.ok = false;
        Serial.println("3-Pin Meter - No response");
    }
}

/********** WIFI **********/
void connectWiFi()
{
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Box ESP32");
    display.println();
    display.print("Connecting to WiFi:\n");
    display.println(WIFI_SSID);
    display.println();
    display.print("Please wait");
    display.display();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000)
    {
        delay(500);
        Serial.print(".");
        display.print(".");
        display.display();
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nWiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        wifiConnected = true;
        errorCode = 0;
        lastError = "";
    }
    else
    {
        Serial.println("\nWiFi connection failed");
        wifiConnected = false;
        errorCode = 3;
        lastError = "WiFi Disconnected";
    }
}

/********** LOCK CONTROL **********/
void lockBox()
{
    digitalWrite(LOCK_CONTROL_PIN, !LOCK_ACTIVE_LEVEL);
}

void unlockBox()
{
    digitalWrite(LOCK_CONTROL_PIN, LOCK_ACTIVE_LEVEL);
    delay(1000);
    digitalWrite(LOCK_CONTROL_PIN, !LOCK_ACTIVE_LEVEL);
}

bool isLocked()
{
    int val = digitalRead(LOCK_FEEDBACK_PIN);
    return (val == HIGH);
}

/********** RFID STATUS **********/
bool isRfidCardDetected()
{
    int val = digitalRead(RFID_STATUS_PIN);
    static int debugCounter = 0;
    if (debugCounter++ % 10 == 0)
    {
        Serial.print("DEBUG RFID GPIO32 raw value: ");
        Serial.println(val);
    }
    return (val == HIGH);
}

/********** RELAY CONTROL: EV & 3-PIN **********/
void setEvRelay(bool on)
{
    // if turning on ev relay then reset energy reading for ev
    if (on && !currentEvOn)
    {
        pzemEv.resetEnergy();
        delay(500);
    }
    int level = on ? EV_ACTIVE_LEVEL : !EV_ACTIVE_LEVEL;
    digitalWrite(EV_RELAY_PIN, level);
    currentEvOn = on;
    Serial.print("EV relay set to: ");
    Serial.println(on ? "ON" : "OFF");
    Serial.print("EV GPIO19 state: ");
    Serial.println(digitalRead(EV_RELAY_PIN));
}

void setP3Relay(bool on)
{
    // if turning on 3-pin relay then reset energy reading for 3-pin
    if (on && !currentP3On)
    {
        pzemP3.resetEnergy();
        delay(500);
    }
    int level = on ? P3_ACTIVE_LEVEL : !P3_ACTIVE_LEVEL;
    digitalWrite(P3_RELAY_PIN, level);
    currentP3On = on;
    Serial.print("3-pin relay set to: ");
    Serial.println(on ? "ON" : "OFF");
    Serial.print("P3 GPIO21 state: ");
    Serial.println(digitalRead(P3_RELAY_PIN));
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
bool fetchNextCommand(String &commandId, String &lockAction, bool &evSet, bool &evOn, bool &p3Set, bool &p3On)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }
    HTTPClient http;
    String url = String(BACKEND_BASE_URL) + BACKEND_NEXT_COMMAND +
                 "?deviceId=" + DEVICE_ID +
                 "&lastCommandId=" + lastCommandId;
    Serial.print("GET ");
    Serial.println(url);
    WiFiClientSecure client;
    client.setInsecure(); // TODO: Fix with proper CA certificate later

    http.begin(client, url);
    http.addHeader("x-device-id", DEVICE_ID);
    http.addHeader("x-device-secret", DEVICE_SECRET);

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

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Serial.print("JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    if (doc["none"] | false)
    {
        return false;
    }

    commandId = doc["commandId"].as<String>();

    lockAction = "";
    evSet = false;
    p3Set = false;

    if (doc["actions"].is<JsonObject>())
    {
        JsonObject actions = doc["actions"];

        if (actions["lock"].is<String>())
        {
            lockAction = actions["lock"].as<String>();
        }

        if (actions["ev"].is<bool>())
        {
            evSet = true;
            evOn = actions["ev"].as<bool>();
        }

        if (actions["p3"].is<bool>())
        {
            p3Set = true;
            p3On = actions["p3"].as<bool>();
        }
    }

    return true;
}

/********** BACKEND: SEND ACK + STATUS **********/
bool sendStatus(const String &commandId,
                bool commandSuccess,
                bool locked,
                bool evOn,
                bool p3On)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }
    HTTPClient http;
    String url = String(BACKEND_BASE_URL) + BACKEND_ACK_ENDPOINT;
    Serial.print("POST ");
    Serial.println(url);
    WiFiClientSecure client;
    client.setInsecure(); // TODO: Fix with proper CA certificate later

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-id", DEVICE_ID);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    StaticJsonDocument<768> doc;
    doc["deviceId"] = DEVICE_ID;
    doc["commandId"] = commandId;
    doc["success"] = commandSuccess;
    doc["timestamp"] = (long long)millis();

    JsonObject state = doc.createNestedObject("state");
    state["lock"] = locked ? "LOCKED" : "UNLOCKED";
    state["ev"] = evOn;
    state["p3"] = p3On;
    state["rfid"] = rfidCardDetected;

    JsonObject energy = doc.createNestedObject("energy");
    energy["ok"] = (evEnergyReading.ok && p3EnergyReading.ok);

    JsonObject evMeter = energy.createNestedObject("evmeter");
    evMeter["voltage"] = evEnergyReading.voltage;
    evMeter["current"] = evEnergyReading.current;
    evMeter["power"] = evEnergyReading.power;
    evMeter["energy"] = evEnergyReading.energy;
    evMeter["ok"] = evEnergyReading.ok;

    JsonObject p3Meter = energy.createNestedObject("p3meter");
    p3Meter["voltage"] = p3EnergyReading.voltage;
    p3Meter["current"] = p3EnergyReading.current;
    p3Meter["power"] = p3EnergyReading.power;
    p3Meter["energy"] = p3EnergyReading.energy;
    p3Meter["ok"] = p3EnergyReading.ok;

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

    // Lock pins
    pinMode(LOCK_CONTROL_PIN, OUTPUT);
    digitalWrite(LOCK_CONTROL_PIN, !LOCK_ACTIVE_LEVEL);
    pinMode(LOCK_FEEDBACK_PIN, INPUT_PULLUP);

    // RFID status pin
    pinMode(RFID_STATUS_PIN, INPUT_PULLUP);

    // Relay pins
    pinMode(EV_RELAY_PIN, OUTPUT);
    digitalWrite(EV_RELAY_PIN, !EV_ACTIVE_LEVEL);
    currentEvOn = false;

    pinMode(P3_RELAY_PIN, OUTPUT);
    digitalWrite(P3_RELAY_PIN, !P3_ACTIVE_LEVEL);
    currentP3On = false;

    // Initialize PZEM serial ports
    Serial2.begin(PZEM_EV_BAUD, SERIAL_8N1, PZEM_EV_RX_PIN, PZEM_EV_TX_PIN);
    Serial.println("PZEM #1 (EV) initialized on Serial2");
    Serial1.begin(PZEM_P3_BAUD, SERIAL_8N1, PZEM_P3_RX_PIN, PZEM_P3_TX_PIN);
    Serial.println("PZEM #2 (3-Pin) initialized on Serial1");

    // Initialize OLED Display
    initializeDisplay();

    connectWiFi();
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    lastWiFiReconnectAttempt = millis();

    Serial.println("Smart box ESP32 started");
}

void loop()
{
    unsigned long now = millis();

    // WiFi reconnect logic
    if (WiFi.status() != WL_CONNECTED)
    {
        wifiConnected = false;
        if (now - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS)
        {
            lastWiFiReconnectAttempt = now;
            connectWiFi();
        }
    }
    else
    {
        wifiConnected = true;
    }

    if (now - lastCycleMillis >= CYCLE_INTERVAL_MS)
    {
        lastCycleMillis = now;

        // FIX: Read energy meters at the start of every cycle
        readEnergyMeters();

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

            if (!lockAction.isEmpty())
            {
                Serial.print("Lock action: ");
                Serial.println(lockAction);
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

            if (evSet)
            {
                Serial.print("EV relay command: ");
                Serial.println(evOn ? "ON" : "OFF");
                setEvRelay(evOn);
            }

            if (p3Set)
            {
                Serial.print("3-Pin relay command: ");
                Serial.println(p3On ? "ON" : "OFF");
                setP3Relay(p3On);
            }

            lastCommandId = cmdId;
        }

        // 4) Read final states after all commands executed
        bool lockedAfter = isLocked();
        bool evStatus = getEvStatus();
        bool p3Status = getP3Status();
        rfidCardDetected = isRfidCardDetected();
        isLocked_state = lockedAfter;
        wifiConnected = (WiFi.status() == WL_CONNECTED);

        Serial.print("Lock state after cmd: ");
        Serial.println(lockedAfter ? "LOCKED" : "UNLOCKED");
        Serial.print("EV relay state (variable): ");
        Serial.print(evStatus ? "ON" : "OFF");
        Serial.print(" | GPIO19 actual: ");
        Serial.println(digitalRead(EV_RELAY_PIN));
        Serial.print("3-pin relay state (variable): ");
        Serial.print(p3Status ? "ON" : "OFF");
        Serial.print(" | GPIO21 actual: ");
        Serial.println(digitalRead(P3_RELAY_PIN));
        Serial.print("RFID card detected: ");
        Serial.println(rfidCardDetected ? "YES" : "NO");

        // 5) Send status & command result back to backend
        bool ok = sendStatus(lastCommandId, commandSuccess, lockedAfter, evStatus, p3Status);
        if (!ok)
        {
            Serial.println("Failed to send status to backend");
            errorCode = 2;
            lastError = "Backend Timeout";
        }
        else
        {
            errorCode = 0;
            lastError = "";
        }
    }

    // Update OLED display continuously
    updateDisplay();

    delay(50);
}