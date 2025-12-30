#pragma once

// Tiny helpers to build TiltedValueItem values consistently across projects.
// Kept header-only and Arduino-friendly (no heap).

#include <stdint.h>

#include "tilted_protocol.h"

// We intentionally support building items either from a scaled integer (x10)
// or from a float with scaling/rounding.
namespace TiltedValueHelper
{
    // Round a float to an integer with base-10 scaling.
    // Example: scale10 = -1 => round(value * 10)
    //          scale10 = 0  => round(value)
    static inline int32_t scaleAndRound(float value, int8_t scale10)
    {
        // We only need a small set in this project. Keep it simple.
        switch (scale10)
        {
        case -3:
            return (int32_t)lroundf(value * 1000.0f);
        case -2:
            return (int32_t)lroundf(value * 100.0f);
        case -1:
            return (int32_t)lroundf(value * 10.0f);
        case 0:
            return (int32_t)lroundf(value);
        case 1:
            return (int32_t)lroundf(value / 10.0f);
        default:
            // Fallback: best-effort (avoid powf on MCUs).
            // For unusual scales, caller should provide pre-scaled ints.
            return (int32_t)lroundf(value);
        }
    }

    static inline TiltedValueItem makeItemI32(TiltedValueType type, int32_t value, int8_t scale10 = 0)
    {
        TiltedValueItem it{};
        it.type = (uint8_t)type;
        it.scale10 = scale10;
        it.reserved = 0;
        it.value = value;
        return it;
    }

    // Common float helpers (one decimal)
    static inline TiltedValueItem tiltDeg(float tiltDeg)
    {
        return makeItemI32(TiltedValueType::Tilt, scaleAndRound(tiltDeg, -1), -1);
    }

    static inline TiltedValueItem tempC(float tempC)
    {
        return makeItemI32(TiltedValueType::Temp, scaleAndRound(tempC, -1), -1);
    }

    static inline TiltedValueItem auxTempC(float tempC)
    {
        return makeItemI32(TiltedValueType::AuxTemp, scaleAndRound(tempC, -1), -1);
    }

    // Integers
    static inline TiltedValueItem batteryMv(int32_t mv)
    {
        return makeItemI32(TiltedValueType::BatteryMv, mv, 0);
    }

    static inline TiltedValueItem intervalS(int32_t seconds)
    {
        return makeItemI32(TiltedValueType::IntervalS, seconds, 0);
    }

    static inline TiltedValueItem rssiDbm(int32_t dbm)
    {
        return makeItemI32(TiltedValueType::RssiDbm, dbm, 0);
    }
}
