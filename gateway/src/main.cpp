#include "WiFi.h"
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <tinyexpr.h>
#include <Preferences.h>
#include <SPI.h>
#include <CircularBuffer.h>

#include "tilted_protocol.h"
#include "config_portal.h"

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

#define RETRY_INTERVAL 5000

// Hold this pin LOW during boot to force config/AP mode.
// GPIO13 is currently unused by this firmware and not part of the display SPI pins (18/19/5/16/23/4).
static constexpr int CONFIG_MODE_PIN = 13;

WiFiClient wifiClient;

// the following three settings must match the slave settings
uint8_t mac[] = {TILTED_GATEWAY_MAC[0], TILTED_GATEWAY_MAC[1], TILTED_GATEWAY_MAC[2], TILTED_GATEWAY_MAC[3], TILTED_GATEWAY_MAC[4], TILTED_GATEWAY_MAC[5]};
const uint8_t channel = TILTED_ESPNOW_CHANNEL;

uint8_t sensorId[6];

volatile boolean haveReading = false;

// JSON payload built from the most recent ESP-NOW packet.
// NOTE: don't do HTTP from the ESP-NOW callback; we just stage the JSON here.
static volatile bool havePendingPublish = false;
static String pendingJsonBody;

ConfigPortal configPortal(preferences);
static inline float round3(float value)
{
    // Round to 3 decimals in a way that works for negative values too.
    return roundf(value * 1000.0f) / 1000.0f;
}

float calculateGravity(float tilt, float temp)
{
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
    // Copy the MAC address into the data structure for identification
    memcpy(sensorId, senderMac, 6);

    // Accept TLV readings packets only.
    TiltedReadingsView view{};
    if (!(incomingData && len > 0 && tilted_decode_readings_view(incomingData, (uint16_t)len, view)))
    {
        Serial.printf("Ignoring non-TLV packet len=%d\n", len);
        return;
    }

    // Reset fields; we'll fill what we find.
    float tilt = 0;
    float temp = 0;

    // Extract name to a printable buffer
    char name[TILTED_MAX_NAME_LEN + 1];
    uint8_t nlen = view.header->nameLen;
    if (nlen > TILTED_MAX_NAME_LEN)
        nlen = TILTED_MAX_NAME_LEN;
    memcpy(name, view.name, nlen);
    name[nlen] = '\0';

    // Build a JSON document containing only what we actually received.
    // Brewfather requires at least a name field; other keys are optional.
    DynamicJsonDocument doc(512);
    doc["name"] = name;

    bool haveTilt = false;
    bool haveTemp = false;

    for (uint8_t i = 0; i < view.header->itemCount; i++)
    {
        const auto& it = view.items[i];
        switch ((TiltedValueType)it.type)
        {
        case TiltedValueType::Tilt:
        {
            tilt = (it.scale10 == -1) ? ((float)it.value / 10.0f) : (float)it.value;
            doc["angle"] = tilt;
            haveTilt = true;
            Serial.printf("Tilt: %.2f\n", tilt);
            break;
        }
        case TiltedValueType::Temp:
        {
            temp = (it.scale10 == -1) ? ((float)it.value / 10.0f) : (float)it.value;
            doc["temp"] = temp;
            doc["temp_unit"] = "C";
            haveTemp = true;
            Serial.printf("Temperature: %.2f\n", temp);
            break;
        }
        case TiltedValueType::AuxTemp:
        {
            float auxTemp = (it.scale10 == -1) ? ((float)it.value / 10.0f) : (float)it.value;
            doc["aux_temp"] = auxTemp;
            doc["aux_temp_unit"] = "C";
            break;
        }
        case TiltedValueType::BatteryMv:
        {
            int32_t mv = it.value;
            // Brewfather battery is commonly volts
            doc["battery"] = (float)mv / 1000.0f;
            Serial.printf("Voltage: %ld mV\n", (long)mv);
            break;
        }
        case TiltedValueType::IntervalS:
        {
            doc["interval"] = it.value;
            Serial.printf("Interval: %ld s\n", (long)it.value);
            break;
        }
        case TiltedValueType::RssiDbm:
        {
            doc["rssi"] = it.value;
            break;
        }
        default:
            break;
        }
    }

    // If we have tilt + temp and a polynomial configured, compute gravity.
    if (haveTilt && haveTemp && !polynomial.isEmpty())
    {
        float gravity = calculateGravity(tilt, temp);
        doc["gravity"] = gravity;
        doc["gravity_unit"] = "G";
    }

    // Stage JSON for publishing in the main loop.
    pendingJsonBody = String();
    serializeJson(doc, pendingJsonBody);
    havePendingPublish = true;

    Serial.printf("\nTLV name: %s chipId: %08x\n", name, (unsigned)view.header->chipId);

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
    String jsonBody = pendingJsonBody;

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

// Start AP mode and web server (moved to ConfigPortal)
void startConfigMode() {
    WiFi.disconnect();
    configMode = true;
    haveReading = false;
    configPortal.setApCredentials(apSSID, apPassword);
    configPortal.start(deviceName, wifiSSID, wifiPassword, polynomial, brewfatherURL);
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
        initEspNow();
    }
}

void loop()
{
    if (configMode)
    {
        configPortal.handle();
    }

    if (haveReading)
    {
        haveReading = false;

        // Only publish if we have staged JSON from the callback.
        if (havePendingPublish)
        {
            havePendingPublish = false;
            wifiConnect();
            if (integrationEnabled(brewfatherURL))
            {
                publishBrewfather();
            }
        }

        initEspNow();
    }
}