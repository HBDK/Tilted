#pragma once

#include <Arduino.h>

// Simple ESP-NOW receiver wrapper.
//
// Contract:
// - Call begin() once to initialize ESP-NOW receive mode.
// - When a valid TLV packet arrives, it stages JSON (and some metadata).
// - In loop(), call hasPending() / takePendingJson() to consume the staged JSON.
//
class EspNowReceiver
{
public:
    // Uses shared gateway MAC + shared ESPNOW channel constants.
    EspNowReceiver();

    // Provide polynomial used for gravity calculation.
    // If empty, gravity will not be computed.
    void setPolynomial(const String& polynomial);

    // Initializes WiFi STA + ESP-NOW, sets MAC/channel, registers callback.
    // Returns true on success.
    bool begin();

    // True if a TLV reading has been decoded and JSON is ready.
    bool hasPending() const;

    void clearPending();

    // Returns and clears the staged JSON payload.
    // If no payload pending, returns empty string.
    String takePendingJson();

private:
    static void recvCb(const uint8_t* senderMac, const uint8_t* incomingData, int len);
    void onRecv(const uint8_t* senderMac, const uint8_t* incomingData, int len);

private:
    uint8_t staMac_[6]{};
    uint8_t channel_ = 1;

    String polynomial_;

    // Staged publish payload.
    volatile bool havePending_ = false;
    String pendingJson_;

    // For debug/logging only.
    uint8_t lastSender_[6]{};

    static EspNowReceiver* self_;
};
