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

// ESP-NOW settings (must match on sender/receiver)
inline constexpr uint8_t TILTED_ESPNOW_CHANNEL = 1;

// Receiver (gateway) MAC address used by the sensor when adding a peer.
// NOTE: This is not the sensor MAC.
inline constexpr uint8_t TILTED_GATEWAY_MAC[6] = {0x3A, 0x33, 0x33, 0x33, 0x33, 0x33};

// -----------------------------
// TLV (typed readings) protocol
// -----------------------------

// Keep this small and stable. If you change the wire format, bump TILTED_PROTOCOL_VERSION
// and keep the old decoder around in the gateway for a while.
inline constexpr uint8_t TILTED_PROTOCOL_VERSION = 1;

enum class TiltedMsgType : uint8_t
{
    Legacy = 0,     // TiltedSensorData (no explicit header)
    Readings = 1,   // TiltedReadingsHeader + name + items
};

// Reading types. Add new ones at the end.
// These should map cleanly to Brewfather custom stream concepts.
enum class TiltedValueType : uint8_t
{
    Tilt = 1,
    Temp = 2,
    AuxTemp = 3,
    BatteryMv = 4,
    IntervalS = 5,
    RssiDbm = 6,
};

// Magic chosen to help quickly reject garbage packets.
inline constexpr uint16_t TILTED_MAGIC = 0x544C; // 'T''L'

// Maximum name bytes we will encode on the wire.
// Suggested name format: "tilt-" + HEX_CHIP_ID (e.g. "tilt-1a2b3c4d").
inline constexpr uint8_t TILTED_MAX_NAME_LEN = 24;

struct __attribute__((packed)) TiltedReadingsHeader
{
    uint16_t magic;       // TILTED_MAGIC
    uint8_t version;      // TILTED_PROTOCOL_VERSION
    uint8_t msgType;      // TiltedMsgType
    uint32_t chipId;      // ESP.getChipId() on ESP8266; on ESP32 use lower 32 bits of MAC
    uint16_t interval_s;  // intended sleep interval
    uint8_t nameLen;      // bytes following header (not null-terminated)
    uint8_t itemCount;    // number of items following name
};

// Each item is fixed-size for simple parsing.
// scale10 is a base-10 exponent for value:
//   real_value = value * 10^(scale10)
// Examples:
//   23.4 C => scale10 = -1, value = 234
//   3310 mV => scale10 = 0,  value = 3310
struct __attribute__((packed)) TiltedValueItem
{
    uint8_t type;      // TiltedValueType
    int8_t scale10;    // usually -1 for one decimal, 0 for integers (mV)
    int16_t reserved;  // align to 4 bytes, set to 0 (future flags)
    int32_t value;
};

static_assert(sizeof(TiltedReadingsHeader) == 12, "Unexpected TiltedReadingsHeader size");
static_assert(sizeof(TiltedValueItem) == 8, "Unexpected TiltedValueItem size");

// Compute total packet size (header + name + items). Returns 0 if invalid/unrepresentable.
static inline uint16_t tilted_readings_packet_size(uint8_t nameLen, uint8_t itemCount)
{
    if (nameLen > TILTED_MAX_NAME_LEN)
        return 0;

    // Prevent uint16_t wrap
    uint32_t sz = sizeof(TiltedReadingsHeader) + nameLen + (uint32_t)itemCount * sizeof(TiltedValueItem);
    if (sz > 0xFFFF)
        return 0;
    return (uint16_t)sz;
}

// Light-weight decoder helper: validates sizes and returns pointers into the provided buffer.
// No allocations; caller owns the buffer.
struct TiltedReadingsView
{
    const TiltedReadingsHeader* header;
    const char* name;
    const TiltedValueItem* items;
};

static inline bool tilted_decode_readings_view(const uint8_t* buf, uint16_t len, TiltedReadingsView& out)
{
    if (!buf || len < sizeof(TiltedReadingsHeader))
        return false;

    auto hdr = reinterpret_cast<const TiltedReadingsHeader*>(buf);
    if (hdr->magic != TILTED_MAGIC)
        return false;
    if (hdr->version != TILTED_PROTOCOL_VERSION)
        return false;
    if (hdr->msgType != (uint8_t)TiltedMsgType::Readings)
        return false;
    if (hdr->nameLen > TILTED_MAX_NAME_LEN)
        return false;

    uint16_t expected = tilted_readings_packet_size(hdr->nameLen, hdr->itemCount);
    if (expected == 0 || len != expected)
        return false;

    const uint8_t* p = buf + sizeof(TiltedReadingsHeader);
    out.header = hdr;
    out.name = reinterpret_cast<const char*>(p);
    p += hdr->nameLen;
    out.items = reinterpret_cast<const TiltedValueItem*>(p);
    return true;
}
