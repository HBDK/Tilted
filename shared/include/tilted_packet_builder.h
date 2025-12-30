#pragma once

// Build TLV readings packets into a caller-provided buffer.
// No heap allocations; safe for MCUs.

#include <stdint.h>
#include <string.h>

#include "tilted_protocol.h"

// Encodes a readings packet into outBuf.
// Returns packet length on success, 0 on failure.
static inline uint16_t tilted_encode_readings_packet(
    uint8_t* outBuf,
    uint16_t outBufMax,
    uint32_t chipId,
    uint16_t intervalSeconds,
    const char* name,
    uint8_t nameLen,
    const TiltedValueItem* items,
    uint8_t itemCount)
{
    if (!outBuf || !name || (!items && itemCount != 0))
        return 0;
    if (nameLen > TILTED_MAX_NAME_LEN)
        return 0;

    uint16_t pktLen = tilted_readings_packet_size(nameLen, itemCount);
    if (pktLen == 0 || pktLen > outBufMax)
        return 0;

    TiltedReadingsHeader hdr;
    hdr.magic = TILTED_MAGIC;
    hdr.chipId = chipId;
    hdr.interval_s = intervalSeconds;
    hdr.nameLen = nameLen;
    hdr.itemCount = itemCount;

    uint8_t* p = outBuf;
    memcpy(p, &hdr, sizeof(hdr));
    p += sizeof(hdr);
    memcpy(p, name, nameLen);
    p += nameLen;
    if (itemCount)
    {
        memcpy(p, items, (size_t)itemCount * sizeof(TiltedValueItem));
    }

    return pktLen;
}
