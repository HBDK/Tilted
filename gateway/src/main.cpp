#include "WiFi.h"
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <tinyexpr.h>
#include <Preferences.h>
#include <SPI.h>
#include <CircularBuffer.h>
#include <WebServer.h>

#include "tilted_protocol.h"

// Preferences
Preferences preferences;

// Config variables
String deviceName = "TiltedGateway";
String wifiSSID = "";
String wifiPassword = "";
String polynomial = "";
String brewfatherURL = "";

// AP mode settings
const char* apSSID = "TiltedGateway-Setup";
const char* apPassword = "tilted123";

// Config mode flag
bool configMode = false;

// Web server
WebServer server(80);

#define RETRY_INTERVAL 5000

WiFiClient wifiClient;

// the following three settings must match the slave settings
uint8_t mac[] = {0x3A, 0x33, 0x33, 0x33, 0x33, 0x33};
const uint8_t channel = 1;

uint8_t sensorId[6];
TiltedSensorData tiltData;
float tiltGravity = 0;

// Buffer with readings for graph display.
// Can be either tilt value or gravity.
CircularBuffer<float, 24> readingsHistory;

volatile boolean haveReading = false;

// HTML for configuration page
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <title>Tilted Gateway Configuration</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
            body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
            .form-group { margin-bottom: 15px; }
            label { display: block; margin-bottom: 5px; }
            input[type="text"], input[type="password"] { width: 100%; padding: 8px; box-sizing: border-box; }
            button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; cursor: pointer; }
            fieldset { margin-bottom: 20px; }
            .section { margin-bottom: 30px; }
        </style>
    </head>
    <body>
        <h1>Tilted Gateway Configuration</h1>
        <form action="/save" method="post">
            <div class="section">
                <fieldset>
                    <legend>Device Settings</legend>
                    <div class="form-group">
                        <label for="deviceName">Device Name:</label>
                        <input type="text" id="deviceName" name="deviceName" value="%DEVICE_NAME%">
                    </div>
                </fieldset>
            </div>
            
            <div class="section">
                <fieldset>
                    <legend>WiFi Settings</legend>
                    <div class="form-group">
                        <label for="wifiSSID">WiFi SSID:</label>
                        <input type="text" id="wifiSSID" name="wifiSSID" value="%WIFI_SSID%">
                    </div>
                    <div class="form-group">
                        <label for="wifiPassword">WiFi Password:</label>
                        <input type="password" id="wifiPassword" name="wifiPassword" value="%WIFI_PASSWORD%">
                    </div>
                </fieldset>
            </div>
            
            <div class="section">
                <fieldset>
                    <legend>Calibration</legend>
                    <div class="form-group">
                        <label for="polynomial">Polynomial:</label>
                        <input type="text" id="polynomial" name="polynomial" value="%POLYNOMIAL%">
                    </div>
                </fieldset>
            </div>
            
            <div class="section">
                <fieldset>
                    <legend>Brewfather Settings</legend>
                    <div class="form-group">
                        <label for="brewfatherURL">Brewfather URL:</label>
                        <input type="text" id="brewfatherURL" name="brewfatherURL" value="%BREWFATHER_URL%">
                    </div>
                </fieldset>
            </div>
            
            <button type="submit">Save Configuration</button>
        </form>
    </body>
    </html>
    )rawliteral";

float round3(float value)
{
    return (int)(value * 1000 + 0.5) / 1000.0;
}

float calculateGravity()
{
    double tilt = tiltData.tilt;
    double temp = tiltData.temp;
    float gravity = 0;
    int err;
    te_variable vars[] = {{"tilt", &tilt}, {"temp", &temp}};
    te_expr *expr = te_compile(polynomial.c_str(), vars, 2, &err);

    if (expr)
    {
        gravity = te_eval(expr);
        te_free(expr);

        Serial.printf("\nCalculated gravity: %.3f\n", gravity);
    }
    else
    {
        Serial.printf("Could not calculate gravity. Parse error at %d\n", err);
    }

    return round3(gravity);
}

void receiveCallBackFunction(const uint8_t *senderMac, const uint8_t *incomingData, int len)
{
    memcpy(&tiltData, incomingData, len);

    // Copy the MAC address into the data structure for identification
    memcpy(sensorId, senderMac, 6);

    Serial.printf("Transmitter MacAddr: %02x:%02x:%02x:%02x:%02x:%02x, ", senderMac[0], senderMac[1], senderMac[2], senderMac[3], senderMac[4], senderMac[5]);
    Serial.printf("\nTilt: %.2f, ", tiltData.tilt);
    Serial.printf("\nTemperature: %.2f, ", tiltData.temp);
    Serial.printf("\nVoltage: %d, ", tiltData.volt);
    Serial.printf("\nInterval: %ld, ", tiltData.interval);

    haveReading = true;
}

void initEspNow()
{
    WiFi.softAPdisconnect(true);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    esp_wifi_set_mac(WIFI_IF_STA, &mac[0]);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    Serial.println();
    Serial.println("ESP-Now Receiver");
    Serial.printf("Transmitter mac: %s\n", WiFi.macAddress().c_str());
    Serial.printf("Receiver mac: %s\n", WiFi.softAPmacAddress().c_str());
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("ESP_Now init failed...");
        delay(RETRY_INTERVAL);
        ESP.restart();
    }
    Serial.println(WiFi.channel());
    esp_now_register_recv_cb(receiveCallBackFunction);
    Serial.println("Slave ready. Waiting for messages...");
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
    const size_t capacity = JSON_OBJECT_SIZE(5);
    DynamicJsonDocument doc(capacity);

    doc["name"] = deviceName;
    doc["temp"] = tiltData.temp;
    doc["temp_unit"] = "C";
    doc["gravity"] = tiltGravity;
    doc["gravity_unit"] = "G";
    doc["battery"] = tiltData.volt/1000.0; // Convert mV to V
    doc["angle"] = tiltData.tilt; // Use tilt angle

    String jsonBody;
    serializeJson(doc, jsonBody);

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
}

// Save settings to Preferences
void saveSettings() {
    preferences.begin("tilted", false);
    
    preferences.putString("deviceName", deviceName);
    preferences.putString("wifiSSID", wifiSSID);
    preferences.putString("wifiPassword", wifiPassword);
    preferences.putString("polynomial", polynomial);
    preferences.putString("brewfatherURL", brewfatherURL);
    
    preferences.end();
    
    Serial.println("Settings saved");
}

// Replace placeholders in HTML template
String processTemplate() {
    String html = CONFIG_HTML;
    html.replace("%DEVICE_NAME%", deviceName);
    html.replace("%WIFI_SSID%", wifiSSID);
    html.replace("%WIFI_PASSWORD%", wifiPassword);
    html.replace("%POLYNOMIAL%", polynomial);
    html.replace("%BREWFATHER_URL%", brewfatherURL);
    return html;
}

// Start AP mode and web server
void startConfigMode() {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID, apPassword);
    
    Serial.println("AP Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
    
    // Configure web server
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", processTemplate());
    });
    
    server.on("/save", HTTP_POST, []() {
        deviceName = server.arg("deviceName");
        wifiSSID = server.arg("wifiSSID");
        wifiPassword = server.arg("wifiPassword");
        polynomial = server.arg("polynomial");
        brewfatherURL = server.arg("brewfatherURL");
        
        saveSettings();
        
        server.send(200, "text/html", 
            "<html><head><meta http-equiv='refresh' content='5;url=/'></head>"
            "<body><h1>Configuration Saved</h1>"
            "<p>The device will restart in 5 seconds.</p></body></html>");
            
        delay(5000);
        ESP.restart();
    });
    
    server.begin();
    configMode = true;
}

// Update the battery indicator based on voltage
void updateBatteryIndicator(int voltage) {
    // Map voltage to a battery percentage (adjust these values for your battery)
    // Assuming 3.0V is empty and 4.2V is full for a LiPo battery
    int percentage = map(constrain(voltage, 2800, 3400), 2800, 3400, 0, 100);
    
    Serial.printf("Battery: %d%% (%d mV)\n", percentage, voltage);
}

void saveReading(float reading)
{
    readingsHistory.push(reading);
}

void setup()
{
    Serial.begin(115200);

    // Load settings
    loadSettings();

    if (wifiSSID.isEmpty()) {
        startConfigMode();
    } else {
        // Disconnect from AP before initializing ESP-Now.
        // This is needed because IoTWebConf for some reason sets up the AP with init().
        //WiFi.softAPdisconnect(true);
        initEspNow();
    }
}

void loop()
{
    if (configMode)
    {
        server.handleClient();
    }

    if (haveReading)
    {
        haveReading = false;
        tiltGravity = calculateGravity();

        // Update battery indicator with each new reading
        updateBatteryIndicator(tiltData.volt);
        
        saveReading(tiltGravity);
        wifiConnect();
        if (integrationEnabled(brewfatherURL))
        {
            publishBrewfather();
        }
        initEspNow();
    }
}