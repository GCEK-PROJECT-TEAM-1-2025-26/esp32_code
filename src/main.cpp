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
#include <PCF8575.h>     // 16-bit I2C I/O expander (PCF8575TS)

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

/********** CONFIG: FACTORY RESET BUTTON **********/
// GPIO 0 = onboard BOOT button on most ESP32 devkits (active LOW, no extra hardware needed)
// To use a dedicated external button: wire it between your chosen GPIO and GND
#define FACTORY_RESET_PIN    0
#define FACTORY_RESET_HOLD_MS 5000   // Hold 5 seconds to trigger reset

/********** CONFIG: PCF8575 KEYPAD **********/
// PCF8575TS I2C address — set by A0/A1/A2 pins:
//   A0=A1=A2=GND → 0x20  (default — most common)
//   Change here if you've tied any address pins HIGH
#define PCF8575_I2C_ADDR    0x20

// Keypad wiring on PCF8575 (lower byte only; upper byte P10-P17 is free)
//   Rows    → P0, P1, P2, P3   (bits 0-3,  driven LOW one at a time)
//   Columns → P4, P5, P6, P7   (bits 4-7,  read back; PCF pull-ups keep them HIGH)
//
// Physical 4x4 matrix layout:
//   COL:      P4    P5    P6    P7
//   ROW P0: [ 1 ]  [ 2 ]  [ 3 ]  [ A ]
//   ROW P1: [ 4 ]  [ 5 ]  [ 6 ]  [ B ]
//   ROW P2: [ 7 ]  [ 8 ]  [ 9 ]  [ C ]
//   ROW P3: [ * ]  [ 0 ]  [ # ]  [ D ]

const char KEYPAD_MAP[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// Debounce — minimum ms a key must be held before it counts
#define KEYPAD_DEBOUNCE_MS  50

bool isAPMode = false;
String apSSID = "SmartBox-Setup";
WebServer webServer(80);
Preferences preferences;

/********** PCF8575 KEYPAD DRIVER **********/
PCF8575 pcf8575(PCF8575_I2C_ADDR);
bool keypadReady = false;      // set true in setup() if PCF8575 found on I2C bus
char lastKeyPressed = 0;       // 0 = none
unsigned long keyDebounceTime = 0;

// Scan the 4x4 matrix and return the pressed key char, or 0 if none.
// Uses the quasi-bidirectional model of PCF8575:
//   - Set all row+col pins HIGH (idle)
//   - Pull one row LOW at a time
//   - Read back cols: a LOW col means that key is pressed
char scanKeypad()
{
    if (!keypadReady) return 0;

    for (uint8_t row = 0; row < 4; row++)
    {
        // Build 16-bit output word:
        //   Lower byte bits 0-3 = rows:  drive current row LOW, rest HIGH
        //   Lower byte bits 4-7 = cols:  all HIGH (inputs via quasi-bidirectional)
        //   Upper byte (P10-P17):        all HIGH (unused, keep as inputs)
        uint16_t outWord = 0xFFFF;           // start: all HIGH
        outWord &= ~(1 << row);              // pull selected row LOW (bit 0-3)

        pcf8575.write16(outWord);
        delayMicroseconds(10);               // let the line settle

        uint16_t readBack = pcf8575.read16();

        // Check each column (bits 4-7 of lower byte)
        for (uint8_t col = 0; col < 4; col++)
        {
            bool colLow = !(readBack & (1 << (col + 4)));
            if (colLow)
            {
                // Debounce: wait and confirm still pressed
                delay(KEYPAD_DEBOUNCE_MS);
                readBack = pcf8575.read16();
                if (!(readBack & (1 << (col + 4))))
                {
                    // Restore idle state before returning
                    pcf8575.write16(0xFFFF);
                    return KEYPAD_MAP[row][col];
                }
            }
        }
    }

    // Restore idle state
    pcf8575.write16(0xFFFF);
    return 0;  // no key pressed
}

// ─── PIN Code System ────────────────────────────────────────────────────────

// Default PIN — stored and read from Preferences so it can be changed later.
// Change this to your preferred default; it is written once on first boot.
#define DEFAULT_PIN  "1234"
#define MAX_PIN_LEN  8

String keypadPIN = DEFAULT_PIN;  // loaded from Preferences in setup()
String pinBuffer = "";           // digits typed so far
bool pinEntryActive = false;     // true while user is mid-entry
unsigned long pinTimeoutStart = 0;
const unsigned long PIN_TIMEOUT_MS = 15000; // 15 s idle → auto-clear buffer

// Show the current PIN buffer on OLED as asterisks
void showPinEntry()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("=== SMART BOX ===");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    display.setCursor(0, 16);
    display.println("Enter PIN then [#]");

    // Show masked input  e.g.  [ * * _ _ ]
    display.setCursor(20, 32);
    display.setTextSize(2);
    String masked = "";
    for (int i = 0; i < (int)pinBuffer.length(); i++) masked += "*";
    for (int i = pinBuffer.length(); i < 4; i++) masked += "_";
    display.print(masked);

    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("[*] Clear  [#] OK");
    display.display();
}

// Show a brief result message (correct/wrong) then return to normal display
void showPinResult(bool correct)
{
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(correct ? 4 : 8, 20);
    display.println(correct ? "GRANTED!" : "WRONG PIN");
    display.setTextSize(1);
    display.setCursor(0, 50);
    display.println(correct ? "Door opening..." : "Try again");
    display.display();
    delay(1800);
}

// Show a brief action confirmation on OLED
void showKeyAction(const char* line1, const char* line2 = "")
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 10);
    display.println(line1);
    if (strlen(line2) > 0)
    {
        display.setCursor(0, 26);
        display.println(line2);
    }
    display.display();
    delay(1200);
}

// Load PIN from Preferences (called in setup after loadConfiguration)
void loadKeypadPIN()
{
    preferences.begin("smartbox", true);
    keypadPIN = preferences.getString("kpad_pin", DEFAULT_PIN);
    preferences.end();
    Serial.print("[KEYPAD] PIN loaded (len=");
    Serial.print(keypadPIN.length());
    Serial.println(")");
}

// Save a new PIN to Preferences
void saveKeypadPIN(const String& newPin)
{
    preferences.begin("smartbox", false);
    preferences.putString("kpad_pin", newPin);
    preferences.end();
    keypadPIN = newPin;
    Serial.println("[KEYPAD] New PIN saved.");
}

// Main keypad handler — call once per loop() iteration.
// Key mapping:
//   0-9  → Append digit to PIN buffer
//   *    → Clear / cancel buffer
//   #    → Submit buffer as PIN (unlock if correct)
//   A    → Toggle EV Charger      (only when box is unlocked)
//   B    → Toggle 3-Pin Socket    (only when box is unlocked)
//   C    → Lock the box
//   D    → Show live status on OLED
// Forward declarations for hardware control
void unlockBox();
void lockBox();
void setEvRelay(bool state);
void setP3Relay(bool state);

void handleKeypadInput()
{
    if (!keypadReady) return;

    char key = scanKeypad();

    // Auto-clear buffer after PIN_TIMEOUT_MS of idle
    if (pinEntryActive && (millis() - pinTimeoutStart) > PIN_TIMEOUT_MS)
    {
        pinBuffer = "";
        pinEntryActive = false;
        Serial.println("[KEYPAD] PIN entry timed out — buffer cleared");
        return;
    }

    if (key == 0 || key == lastKeyPressed)
    {
        if (key == 0) lastKeyPressed = 0;
        return;
    }
    lastKeyPressed = key;

    Serial.print("[KEYPAD] Key: ");
    Serial.println(key);

    // ── Digit keys: build PIN buffer ──────────────────────────────────────────
    if (key >= '0' && key <= '9')
    {
        if (pinBuffer.length() < MAX_PIN_LEN)
        {
            pinBuffer += key;
            pinEntryActive = true;
            pinTimeoutStart = millis();
        }
        showPinEntry();
        return;
    }

    // ── * : Clear buffer ──────────────────────────────────────────────────────
    if (key == '*')
    {
        pinBuffer = "";
        pinEntryActive = false;
        Serial.println("[KEYPAD] Buffer cleared");
        showKeyAction("Cancelled", "Buffer cleared");
        return;
    }

    // ── # : Submit PIN ────────────────────────────────────────────────────────
    if (key == '#')
    {
        if (pinBuffer.length() == 0)
        {
            showKeyAction("Enter digits", "then press #");
            return;
        }

        bool correct = (pinBuffer == keypadPIN);
        Serial.print("[KEYPAD] PIN attempt: ");
        Serial.println(correct ? "CORRECT" : "WRONG");

        pinBuffer = "";
        pinEntryActive = false;

        showPinResult(correct);

        if (correct)
        {
            // Pulse the solenoid to open the door
            unlockBox();
            isLocked_state = false;
            Serial.println("[KEYPAD] Door unlocked via PIN");
        }
        return;
    }

    // ── A : Toggle EV Charger ─────────────────────────────────────────────────
    if (key == 'A')
    {
        if (isLocked_state)
        {
            showKeyAction("Box is LOCKED", "Enter PIN first");
            return;
        }
        bool newState = !currentEvOn;
        setEvRelay(newState);
        Serial.print("[KEYPAD] EV Charger ");
        Serial.println(newState ? "ON" : "OFF");
        showKeyAction("EV Charger:", newState ? "TURNED ON" : "TURNED OFF");
        return;
    }

    // ── B : Toggle 3-Pin Socket ───────────────────────────────────────────────
    if (key == 'B')
    {
        if (isLocked_state)
        {
            showKeyAction("Box is LOCKED", "Enter PIN first");
            return;
        }
        bool newState = !currentP3On;
        setP3Relay(newState);
        Serial.print("[KEYPAD] 3-Pin Socket ");
        Serial.println(newState ? "ON" : "OFF");
        showKeyAction("3-Pin Socket:", newState ? "TURNED ON" : "TURNED OFF");
        return;
    }

    // ── C : Lock the box ──────────────────────────────────────────────────────
    if (key == 'C')
    {
        // Turn off chargers before locking
        setEvRelay(false);
        setP3Relay(false);
        lockBox();
        isLocked_state = true;
        Serial.println("[KEYPAD] Box locked via keypad");
        showKeyAction("Box LOCKED", "All off");
        return;
    }

    // ── D : Show live status ──────────────────────────────────────────────────
    if (key == 'D')
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("=== STATUS ===");
        display.print("Lock : "); display.println(isLocked_state ? "LOCKED" : "OPEN");
        display.print("EV   : "); display.println(currentEvOn   ? "ON" : "OFF");
        display.print("3-Pin: "); display.println(currentP3On   ? "ON" : "OFF");
        display.print("WiFi : "); display.println(wifiConnected  ? "OK" : "NO");
        display.print("Box  : "); display.println(deviceId);
        display.display();
        delay(3000);
        return;
    }
}

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

// ─── Factory Reset ──────────────────────────────────────────────────────────
void factoryReset()
{
    Serial.println("\n!!! FACTORY RESET TRIGGERED !!!");

    // Wipe all stored configuration
    preferences.begin("smartbox", false);
    preferences.clear();
    preferences.end();
    Serial.println("All preferences cleared.");

    // Show final message on OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("=== FACTORY RESET ===");
    display.println();
    display.println("All config erased.");
    display.println("Rebooting into");
    display.println("Setup Mode...");
    display.display();

    delay(2500);
    ESP.restart();
}

// Check if the factory reset button has been held long enough.
// Call this in setup() and optionally in loop().
// Returns true if reset was triggered (device will restart, so this won't return).
void checkFactoryReset()
{
    if (digitalRead(FACTORY_RESET_PIN) == HIGH)
    {
        // Button not pressed (active LOW) — nothing to do
        return;
    }

    Serial.println("Factory reset button held — counting down...");
    unsigned long pressStart = millis();

    while (digitalRead(FACTORY_RESET_PIN) == LOW)
    {
        unsigned long held = millis() - pressStart;

        // Update OLED with countdown
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("!! FACTORY RESET !!");
        display.println();
        display.print("Hold button: ");
        display.print((held / 1000));
        display.println("s");
        display.println();
        display.print("Release to cancel");
        display.display();

        if (held >= FACTORY_RESET_HOLD_MS)
        {
            factoryReset(); // Does not return — restarts ESP32
        }

        delay(100);
    }

    // Button released before threshold — cancelled
    Serial.println("Factory reset cancelled (button released early).");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Reset cancelled.");
    display.display();
    delay(1000);
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

    // Factory reset button (active LOW — internal pull-up keeps it HIGH when not pressed)
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);

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

    // Initialize PCF8575 keypad expander
    // Wire is already started by Adafruit_SSD1306 via initializeDisplay()
    // PCF8575 shares the same I2C bus (SDA=GPIO21, SCL=GPIO22)
    pcf8575.begin();   // initialise the expander
    // Verify the chip is actually responding on the bus
    Wire.beginTransmission(PCF8575_I2C_ADDR);
    if (Wire.endTransmission() == 0)
    {
        keypadReady = true;
        pcf8575.write16(0xFFFF);  // all pins HIGH (idle / input mode)
        Serial.println("PCF8575 keypad expander found at 0x20 ✓");
    }
    else
    {
        keypadReady = false;
        Serial.println("WARNING: PCF8575 not found on I2C bus — keypad disabled");
    }

    // ── Check for factory reset BEFORE loading config ──────────────────────
    // If user holds the button at power-on, wipe config immediately
    checkFactoryReset();
    // ───────────────────────────────────────────────────────────────────────

    loadConfiguration();
    loadKeypadPIN();

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

    // ── Factory reset check in normal operation ─────────────────────────────
    // User can also trigger reset while the device is running normally
    checkFactoryReset();
    // ───────────────────────────────────────────────────────────────────────

    // ── Keypad: handle PIN entry and action keys ─────────────────────────────
    handleKeypadInput();
    // ───────────────────────────────────────────────────────────────────────

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