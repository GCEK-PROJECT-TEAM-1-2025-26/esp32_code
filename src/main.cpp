#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>

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

/********** CONFIG: WIFI & BACKEND (LOADED DYNAMICALLY) **********/
String wifiSsid = "";
String wifiPassword = "";
String deviceId = "";
String deviceSecret = "";
String boxLocation = "";

const char *BACKEND_BASE_URL = "https://smart-box-admin.vercel.app"; // TODO: set
const char *BACKEND_NEXT_COMMAND = "/api/esp/next-command"; // GET
const char *BACKEND_ACK_ENDPOINT = "/api/esp/ack";          // POST

bool isAPMode = false;
String apSSID = "SmartBox-Setup";
WebServer webServer(80);
Preferences preferences;

void saveConfiguration(String ssid, String pass, String devId, String devSec, String loc)
{
    preferences.begin("smartbox", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", pass);
    preferences.putString("deviceId", devId);
    preferences.putString("deviceSecret", devSec);
    preferences.putString("location", loc);
    preferences.end();
    Serial.println("Configuration saved!");
}

void loadConfiguration()
{
    preferences.begin("smartbox", true);
    wifiSsid = preferences.getString("ssid", "");
    wifiPassword = preferences.getString("password", "");
    deviceId = preferences.getString("deviceId", "");
    deviceSecret = preferences.getString("deviceSecret", "");
    boxLocation = preferences.getString("location", "");
    preferences.end();
    Serial.println("Loaded Configuration:");
    Serial.print("  SSID: "); Serial.println(wifiSsid);
    Serial.print("  Device ID: "); Serial.println(deviceId);
    Serial.print("  Location: "); Serial.println(boxLocation);
}

void handleScan()
{
    Serial.println("Scanning networks...");
    int n = WiFi.scanNetworks();
    Serial.print("Scan complete. Found: ");
    Serial.println(n);
    
    JsonDocument doc;
    for (int i = 0; i < n; ++i)
    {
        doc["networks"][i]["ssid"] = WiFi.SSID(i);
        doc["networks"][i]["rssi"] = WiFi.RSSI(i);
        doc["networks"][i]["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
    
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

void handleConfigure()
{
    if (!webServer.hasArg("plain"))
    {
        webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Body missing\"}");
        return;
    }
    
    String body = webServer.arg("plain");
    Serial.print("Configure payload received: ");
    Serial.println(body);
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err)
    {
        webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parse error\"}");
        return;
    }
    
    if (!doc["ssid"].is<String>() || !doc["deviceId"].is<String>())
    {
        webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing ssid or deviceId\"}");
        return;
    }
    
    String ssid = doc["ssid"].as<String>();
    String password = doc["password"] | "";
    String devId = doc["deviceId"].as<String>();
    String devSec = doc["deviceSecret"] | "super-secret-token";
    String loc = doc["location"] | "";
    
    saveConfiguration(ssid, password, devId, devSec, loc);
    
    webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configuration saved successfully. Rebooting...\"}");
    delay(2000);
    ESP.restart();
}

void startAPMode()
{
    isAPMode = true;
    WiFi.mode(WIFI_AP);
    
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    apSSID = "SmartBox-Setup-" + mac.substring(mac.length() - 4);
    
    WiFi.softAP(apSSID.c_str());
    
    Serial.println("\n--- Setup Mode Activated ---");
    Serial.print("SSID: ");
    Serial.println(apSSID);
    Serial.print("Local IP: ");
    Serial.println(WiFi.softAPIP());
    
    webServer.on("/scan", HTTP_GET, handleScan);
    webServer.on("/configure", HTTP_POST, handleConfigure);
    webServer.begin();
    
    wifiConnected = false;
    errorCode = 4;
    lastError = "Setup Mode Active";
}

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

#define PZEM_P3_RX_PIN 26
#define PZEM_P3_TX_PIN 27
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
    Serial.println(wifiSsid);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Box ESP32");
    display.println();
    display.print("Connecting to WiFi:\n");
    display.println(wifiSsid);
    display.println();
    display.print("Please wait");
    display.display();

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000)
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
                 "?deviceId=" + deviceId +
                 "&lastCommandId=" + lastCommandId;
    Serial.print("GET ");
    Serial.println(url);
    WiFiClientSecure client;
    client.setInsecure(); // TODO: Fix with proper CA certificate later

    http.begin(client, url);
    http.addHeader("x-device-id", deviceId);
    http.addHeader("x-device-secret", deviceSecret);

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

    JsonDocument doc;
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
    http.addHeader("x-device-id", deviceId);
    http.addHeader("x-device-secret", deviceSecret);

    JsonDocument doc;
    doc["deviceId"] = deviceId;
    doc["commandId"] = commandId;
    doc["success"] = commandSuccess;
    doc["timestamp"] = (long long)millis();

    doc["state"]["lock"] = locked ? "LOCKED" : "UNLOCKED";
    doc["state"]["ev"] = evOn;
    doc["state"]["p3"] = p3On;
    doc["state"]["rfid"] = rfidCardDetected;

    doc["energy"]["ok"] = (evEnergyReading.ok && p3EnergyReading.ok);

    doc["energy"]["evmeter"]["voltage"] = evEnergyReading.voltage;
    doc["energy"]["evmeter"]["current"] = evEnergyReading.current;
    doc["energy"]["evmeter"]["power"] = evEnergyReading.power;
    doc["energy"]["evmeter"]["energy"] = evEnergyReading.energy;
    doc["energy"]["evmeter"]["ok"] = evEnergyReading.ok;

    doc["energy"]["p3meter"]["voltage"] = p3EnergyReading.voltage;
    doc["energy"]["p3meter"]["current"] = p3EnergyReading.current;
    doc["energy"]["p3meter"]["power"] = p3EnergyReading.power;
    doc["energy"]["p3meter"]["energy"] = p3EnergyReading.energy;
    doc["energy"]["p3meter"]["ok"] = p3EnergyReading.ok;

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

    loadConfiguration();

    if (wifiSsid.isEmpty() || deviceId.isEmpty())
    {
        Serial.println("No configuration found. Entering Setup Mode.");
        startAPMode();
    }
    else
    {
        connectWiFi();
        wifiConnected = (WiFi.status() == WL_CONNECTED);
        if (!wifiConnected)
        {
            Serial.println("WiFi connection failed during startup. Falling back to Setup Mode.");
            startAPMode();
        }
        else
        {
            lastWiFiReconnectAttempt = millis();
        }
    }

    Serial.println("Smart box ESP32 started");
}

void loop()
{
    if (isAPMode)
    {
        webServer.handleClient();
        updateDisplay();
        delay(50);
        return;
    }

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