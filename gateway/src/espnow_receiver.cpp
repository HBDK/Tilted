#include "espnow_receiver.h"

#include "WiFi.h"
#include <esp_now.h>
#include <esp_wifi.h>

#include <ArduinoJson.h>

#include "tilted_protocol.h"

EspNowReceiver* EspNowReceiver::self_ = nullptr;

EspNowReceiver::EspNowReceiver()
{
    const uint8_t defaultMac[6] = {
        TILTED_GATEWAY_MAC[0],
        TILTED_GATEWAY_MAC[1],
        TILTED_GATEWAY_MAC[2],
        TILTED_GATEWAY_MAC[3],
        TILTED_GATEWAY_MAC[4],
        TILTED_GATEWAY_MAC[5],
    };
    memcpy(staMac_, defaultMac, 6);
    channel_ = TILTED_ESPNOW_CHANNEL;
}

bool EspNowReceiver::begin()
{
    // ensure singleton callback target
    self_ = this;

    WiFi.softAPdisconnect(true);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);

    esp_wifi_set_mac(WIFI_IF_STA, &staMac_[0]);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    Serial.println();
    Serial.println("ESP-Now Receiver");
    Serial.printf("Transmitter mac: %s\n", WiFi.macAddress().c_str());
    Serial.printf("Receiver mac: %s\n", WiFi.softAPmacAddress().c_str());

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("ESP_Now init failed...");
        return false;
    }

    Serial.println(WiFi.channel());
    esp_now_register_recv_cb(&EspNowReceiver::recvCb);
    Serial.println("Slave ready. Waiting for messages...");
    return true;
}

bool EspNowReceiver::hasPending() const
{
    return havePending_;
}

void EspNowReceiver::clearPending()
{
    havePending_ = false;
}

String EspNowReceiver::takePendingJson()
{
    if (!havePending_)
        return String();

    havePending_ = false;
    String out = pendingJson_;
    pendingJson_ = String();
    return out;
}

void EspNowReceiver::recvCb(const uint8_t* senderMac, const uint8_t* incomingData, int len)
{
    if (self_)
        self_->onRecv(senderMac, incomingData, len);
}

static inline float round3(float value)
{
    return roundf(value * 1000.0f) / 1000.0f;
}

void EspNowReceiver::onRecv(const uint8_t* senderMac, const uint8_t* incomingData, int len)
{
    memcpy(lastSender_, senderMac, 6);

    TiltedReadingsView view{};
    if (!(incomingData && len > 0 && tilted_decode_readings_view(incomingData, (uint16_t)len, view)))
    {
        Serial.printf("Ignoring non-TLV packet len=%d\n", len);
        return;
    }

    // Extract name to a printable buffer
    char name[TILTED_MAX_NAME_LEN + 1];
    uint8_t nlen = view.header->nameLen;
    if (nlen > TILTED_MAX_NAME_LEN)
        nlen = TILTED_MAX_NAME_LEN;
    memcpy(name, view.name, nlen);
    name[nlen] = '\0';

    DynamicJsonDocument doc(512);
    doc["name"] = name;

    bool haveTilt = false;
    bool haveTemp = false;
    float tilt = 0;
    float temp = 0;

    for (uint8_t i = 0; i < view.header->itemCount; i++)
    {
        const auto& it = view.items[i];
        switch ((TiltedValueType)it.type)
        {
        case TiltedValueType::Tilt:
            tilt = (it.scale10 == -1) ? ((float)it.value / 10.0f) : (float)it.value;
            doc["angle"] = tilt;
            haveTilt = true;
            Serial.printf("Tilt: %.2f\n", tilt);
            break;
        case TiltedValueType::Temp:
            temp = (it.scale10 == -1) ? ((float)it.value / 10.0f) : (float)it.value;
            doc["temp"] = temp;
            doc["temp_unit"] = "C";
            haveTemp = true;
            Serial.printf("Temperature: %.2f\n", temp);
            break;
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
            doc["battery"] = (float)mv / 1000.0f;
            Serial.printf("Voltage: %ld mV\n", (long)mv);
            break;
        }
        case TiltedValueType::IntervalS:
            doc["interval"] = it.value;
            Serial.printf("Interval: %ld s\n", (long)it.value);
            break;
        case TiltedValueType::RssiDbm:
            doc["rssi"] = it.value;
            break;
        default:
            break;
        }
    }

    // NOTE: gravity calculation (polynomial) stays in main.cpp for now,
    // since it depends on the current polynomial string + tinyexpr.
    // We only include raw values here.

    pendingJson_ = String();
    serializeJson(doc, pendingJson_);
    havePending_ = true;

    Serial.printf("\nTLV name: %s chipId: %08x\n", name, (unsigned)view.header->chipId);
}
