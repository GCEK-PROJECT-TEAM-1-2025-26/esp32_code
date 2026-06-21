#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Adafruit_SSD1306.h>

/********** OLED DISPLAY CONFIG **********/
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

extern Adafruit_SSD1306 display;
extern bool wifiConnected;
extern bool isLocked_state;
extern bool rfidCardDetected;
extern bool currentEvOn;
extern bool currentP3On;
extern String lastCommandId;
extern String lastError;
extern int errorCode;
extern EnergyReading evEnergyReading;
extern EnergyReading p3EnergyReading;
extern String deviceId;
extern bool isAPMode;
extern String apSSID;
extern const unsigned long CYCLE_INTERVAL_MS;

// Initialize OLED display
void initializeDisplay()
{
    Serial.println("OLED: Initializing display...");

    // Step 1: manually init I2C on correct ESP32 pins
    Wire.begin(21, 22);
    Wire.setClock(100000); // 100kHz - more reliable than 400kHz

    delay(100); // give the display time to power up

    // Step 2: use simple 2-param begin() - works on ALL Adafruit SSD1306 versions
    // The 4-param version (reset, periphBegin) is version-dependent and caused blank display
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
        Serial.println("ERROR: SSD1306 OLED display not found!");
        Serial.println("Check wiring: SDA=GPIO21, SCL=GPIO22, VCC=3.3V, ADDR=0x3C");

        // Run I2C scan to help debug
        Serial.println("Scanning I2C bus...");
        for (uint8_t addr = 1; addr < 127; addr++)
        {
            Wire.beginTransmission(addr);
            uint8_t err = Wire.endTransmission();
            if (err == 0)
            {
                Serial.print("  I2C device found at 0x");
                Serial.println(addr, HEX);
            }
        }
        Serial.println("I2C scan done.");

        errorCode = 1;
        lastError = "OLED Init Failed";
        return;
    }

    Serial.println("OLED display initialized successfully");

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Box ESP32");
    display.println("Initializing...");
    display.display();
    delay(2000);
}

// Display main status screen
void displayMainStatus()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    display.print("BOX: ");
    display.println(deviceId);
    display.print("WiFi: ");
    display.println(wifiConnected ? "ON" : "OFF");

    display.drawLine(0, 15, 128, 15, SSD1306_WHITE);
    display.setCursor(0, 18);

    display.print("Lock: ");
    display.println(isLocked_state ? "LOCKED" : "UNLOCKED");

    display.print("RFID: ");
    display.println(rfidCardDetected ? "DETECTED" : "NONE");

    display.print("EV: ");
    display.print(currentEvOn ? "ON" : "OFF");
    display.print(" | 3-Pin: ");
    display.println(currentP3On ? "ON" : "OFF");

    display.drawLine(0, 48, 128, 48, SSD1306_WHITE);
    display.setCursor(0, 50);

    if (errorCode != 0)
    {
        display.print("ERR:");
        display.println(errorCode);
        display.println(lastError);
    }
    else
    {
        display.println("Status: OK");
    }

    display.display();
}

// Display energy readings screen
void displayEnergyReadings()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    display.println("=== ENERGY ===");

    display.println("EV:");
    if (evEnergyReading.ok)
    {
        display.print("V:");
        display.print(evEnergyReading.voltage, 0);
        display.print(" I:");
        display.print(evEnergyReading.current, 1);
        display.println("A");
        display.print("P:");
        display.print(evEnergyReading.power, 0);
        display.print("W E:");
        display.print(evEnergyReading.energy, 2);
        display.println("kWh");
    }
    else
    {
        display.println("NO RESPONSE");
    }

    display.println();

    display.println("3-PIN:");
    if (p3EnergyReading.ok)
    {
        display.print("V:");
        display.print(p3EnergyReading.voltage, 0);
        display.print(" I:");
        display.print(p3EnergyReading.current, 1);
        display.println("A");
        display.print("P:");
        display.print(p3EnergyReading.power, 0);
        display.print("W E:");
        display.print(p3EnergyReading.energy, 2);
        display.println("kWh");
    }
    else
    {
        display.println("NO RESPONSE");
    }

    display.display();
}

// Display network status screen
void displayNetworkStatus()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    display.println("=== NETWORK ===");
    display.println();

    display.print("WiFi: ");
    display.println(wifiConnected ? "CONNECTED" : "DISCONNECTED");
    display.print("Device: ");
    display.println(deviceId);
    display.print("Status: ");
    if (errorCode == 0)
        display.println("OK");
    else
    {
        display.print("ERROR ");
        display.println(errorCode);
    }

    display.println();
    display.println("Last Cmd:");
    if (lastCommandId.isEmpty())
        display.println("NONE");
    else
        display.println(lastCommandId.c_str());

    display.display();
}

// Display diagnostics screen
void displayDiagnostics()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    display.println("=== DIAGNOSTICS ===");
    display.println();

    display.print("Error: ");
    display.println(errorCode);
    display.println(lastError.c_str());
    display.println();

    display.print("Uptime: ");
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    display.print(hours);
    display.print("h ");
    display.print(minutes % 60);
    display.println("m");

    display.display();
}

// Display AP setup instructions
void displayAPSetup(String ssid, String ip)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    display.println("=== SETUP MODE ===");
    display.println();
    display.println("Connect to WiFi AP:");
    display.print("SSID: ");
    display.println(ssid);
    display.println();
    display.println("Open app to configure");
    display.print("IP: ");
    display.println(ip);

    display.display();
}

// Rotate through display screens every 5 seconds
static unsigned long lastDisplayChange = 0;
static int currentDisplayScreen = 0;

void updateDisplay()
{
    if (isAPMode)
    {
        displayAPSetup(apSSID, "192.168.4.1");
        return;
    }

    unsigned long now = millis();

    if (now - lastDisplayChange >= 5000)
    {
        lastDisplayChange = now;
        currentDisplayScreen = (currentDisplayScreen + 1) % 4;
        Serial.print("OLED: Changing to screen ");
        Serial.println(currentDisplayScreen);
    }

    switch (currentDisplayScreen)
    {
    case 0:
        displayMainStatus();
        break;
    case 1:
        displayEnergyReadings();
        break;
    case 2:
        displayNetworkStatus();
        break;
    case 3:
        displayDiagnostics();
        break;
    default:
        displayMainStatus();
        break;
    }
}

#endif // OLED_DISPLAY_H