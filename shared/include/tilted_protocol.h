#pragma once

// Shared on-the-wire protocol between sensor (ESP8266) and gateway (ESP32).
// Keep this header Arduino-friendly and avoid heavy includes.

#include <stdint.h>

// IMPORTANT:
// - Keep this struct packed and stable (field order + sizes).
// - Prefer fixed-width types for cross-platform consistency.
// - If you need to change it, bump a protocol version and handle both.

struct __attribute__((packed)) TiltedSensorData
{
    float tilt;
    float temp;
    int32_t volt;      // millivolts
    int32_t interval;  // seconds
};

static_assert(sizeof(TiltedSensorData) == (sizeof(float) * 2 + sizeof(int32_t) * 2),
              "Unexpected TiltedSensorData size (packing/alignment issue)");
