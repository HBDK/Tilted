#include "WiFi.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <CircularBuffer.h>

#include "tilted_protocol.h"
#include "config_portal.h"
#include "espnow_receiver.h"

// Preferences
Preferences preferences;

// Config variables
String deviceName = "TiltedGateway";
String wifiSSID = "";
String wifiPassword = "";
String polynomial = "";// somthing like "0.5013885598189161 + 0.019948730468857152 *tilt" use https://www.ispindel.de/tools/calibration/calibration.htm for calibration
String brewfatherURL = "";

// AP mode settings
const char* apSSID = "TiltedGateway-Setup";
const char* apPassword = "tilted123";

// Config mode flag
bool configMode = false;

#define RETRY_INTERVAL 5000

// Hold this pin LOW during boot to force config/AP mode.
// GPIO13 is currently unused by this firmware and not part of the display SPI pins (18/19/5/16/23/4).
static constexpr int CONFIG_MODE_PIN = 13;

WiFiClient wifiClient;

EspNowReceiver espNow;

ConfigPortal configPortal(preferences);

static void ensureEspNow()
{
    if (!espNow.begin())
    {
        delay(RETRY_INTERVAL);
        ESP.restart();
    }
}

void wifiConnect()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(250);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\nWiFi connected, IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed");
    }
}

void publishBrewfather()
{
    Serial.println("Sending to Brewfather...");
    String jsonBody = espNow.takePendingJson();

    Serial.println("");
    Serial.println("JSON Body:");
    Serial.println(jsonBody);
    Serial.println("");

    HTTPClient http;
    http.begin(wifiClient, brewfatherURL.c_str());
    http.addHeader("Content-Type", "application/json");
    http.POST(jsonBody);
    http.end();
}

String macToString(const uint8_t* mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

bool integrationEnabled(String integration) {
    return !integration.isEmpty();
}

// Load settings from Preferences
void loadSettings() {
    preferences.begin("tilted", false);
    
    deviceName = preferences.getString("deviceName", "TiltedGateway");
    wifiSSID = preferences.getString("wifiSSID", "");
    wifiPassword = preferences.getString("wifiPassword", "");
    polynomial = preferences.getString("polynomial", "");
    brewfatherURL = preferences.getString("brewfatherURL", "");
    
    preferences.end();
    
    Serial.println("Settings loaded:");
    Serial.println("Device Name: " + deviceName);
    Serial.println("WiFi SSID: " + wifiSSID);
    Serial.println("Polynomial: " + polynomial);

    // Make sure the ESP-NOW module has the latest polynomial so it can compute gravity in-callback.
    espNow.setPolynomial(polynomial);
}

// Start AP mode and web server (moved to ConfigPortal)
void startConfigMode() {
    WiFi.disconnect();
    configMode = true;
    espNow.clearPending();
    configPortal.setApCredentials(apSSID, apPassword);
    configPortal.start(deviceName, wifiSSID, wifiPassword, polynomial, brewfatherURL);

    // While in config mode the polynomial may change; keep receiver updated.
    espNow.setPolynomial(polynomial);
}

void setup()
{
    Serial.begin(115200);

    pinMode(CONFIG_MODE_PIN, INPUT_PULLUP);
    bool forceConfigMode = (digitalRead(CONFIG_MODE_PIN) == LOW);

    // Load settings
    loadSettings();

    if (forceConfigMode || wifiSSID.isEmpty()) {
        if (forceConfigMode)
        {
            Serial.println("Forcing config mode (GPIO13 held low)");
        }
        startConfigMode();
    } else {
        // Disconnect from AP before initializing ESP-Now.
        // This is needed because IoTWebConf for some reason sets up the AP with init().
        //WiFi.softAPdisconnect(true);
        ensureEspNow();
    }
}

void loop()
{
    if (configMode)
    {
        configPortal.handle();
    }

    if (espNow.hasPending())
    {
        wifiConnect();
        if (integrationEnabled(brewfatherURL))
        {
            publishBrewfather();
        }

        ensureEspNow();
    }
}