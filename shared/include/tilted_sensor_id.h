#pragma once

// Shared helpers for generating stable sensor identifiers.
// Arduino-friendly; header-only.

#include <stdint.h>
#include <stdio.h>

// Returns a stable 32-bit chip identifier.
// - ESP8266: ESP.getChipId()
// - ESP32: lower 32 bits of Efuse MAC
static inline uint32_t tilted_get_chip_id32()
{
#if defined(ESP8266)
    return ESP.getChipId();
#elif defined(ESP32)
    // Arduino-ESP32 provides ESP.getEfuseMac() (uint64_t)
    return (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFFULL);
#else
    return 0;
#endif
}

// Builds a name like: "<type>-%08x" using the chip id.
// Returns number of bytes written (excluding null terminator), truncated if needed.
static inline uint8_t tilted_build_name_from_type(char* out, uint8_t outMax, const char* typePrefix)
{
    if (!out || outMax == 0 || !typePrefix)
        return 0;

    uint32_t id = tilted_get_chip_id32();
    int n = snprintf(out, outMax, "%s-%08x", typePrefix, (unsigned)id);
    if (n <= 0)
        return 0;
    if (n >= outMax)
        n = outMax - 1;
    return (uint8_t)n;
}
