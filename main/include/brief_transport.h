#ifndef BRIEF_TRANSPORT_H
#define BRIEF_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

namespace brief_transport {

constexpr uint32_t kFrameMagic = 0x3146424c;  // LBF1, little-endian on ESP32.
constexpr uint16_t kFrameVersion = 1;
constexpr size_t kMaxFramePayloadSize = 8192;

struct FrameHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t type;
    uint8_t flags;
    uint32_t sequence;
    uint32_t uptime_ms;
    uint32_t payload_size;
    uint32_t payload_crc32;
};

static_assert(sizeof(FrameHeader) == 24, "FrameHeader wire layout changed");

inline uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

inline uint32_t crc32(const uint8_t *data, size_t length)
{
    return crc32_update(0, data, length);
}

inline FrameHeader make_header(uint8_t type, uint32_t sequence, uint32_t uptime_ms, const uint8_t *payload, uint32_t payload_size)
{
    FrameHeader header = {};
    header.magic = kFrameMagic;
    header.version = kFrameVersion;
    header.type = type;
    header.flags = 0;
    header.sequence = sequence;
    header.uptime_ms = uptime_ms;
    header.payload_size = payload_size;
    header.payload_crc32 = crc32(payload, payload_size);
    return header;
}

}  // namespace brief_transport

#endif
