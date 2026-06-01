#ifndef C6_SNAPSHOT_RECEIVER_H
#define C6_SNAPSHOT_RECEIVER_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "brief_protocol.h"
#include "brief_transport.h"

class C6SnapshotReceiver {
public:
    C6SnapshotReceiver();

    void begin(Stream *stream);
    bool poll(brief::DashboardSnapshot *out);

    const char *status_text() const;
    bool active() const;
    uint32_t frames_received() const;
    uint32_t crc_errors() const;
    uint32_t framing_errors() const;
    uint32_t last_frame_ms() const;

private:
    enum ParseState : uint8_t {
        kSync,
        kHeader,
        kPayload,
    };

    void reset_parser_();
    void note_status_(const char *text);
    bool consume_byte_(uint8_t byte, brief::DashboardSnapshot *out);
    bool validate_header_();
    bool validate_snapshot_(const brief::DashboardSnapshot &snapshot);

    Stream *stream_;
    ParseState state_;
    uint32_t sync_window_;
    uint8_t header_bytes_[sizeof(brief_transport::FrameHeader)];
    uint16_t header_index_;
    brief_transport::FrameHeader header_;
    uint8_t payload_[sizeof(brief::DashboardSnapshot)];
    uint32_t payload_index_;
    uint32_t frames_received_;
    uint32_t crc_errors_;
    uint32_t framing_errors_;
    uint32_t last_frame_ms_;
    char status_text_[96];
};

#endif
