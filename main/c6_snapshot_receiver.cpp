#include "c6_snapshot_receiver.h"

#include <string.h>

namespace {

constexpr uint16_t kMaxBytesPerPoll = 768;

}  // namespace

C6SnapshotReceiver::C6SnapshotReceiver()
    : stream_(nullptr),
      state_(kSync),
      sync_window_(0),
      header_bytes_{},
      header_index_(0),
      header_{},
      payload_{},
      payload_index_(0),
      frames_received_(0),
      crc_errors_(0),
      framing_errors_(0),
      last_frame_ms_(0),
      status_text_("C6 transport idle") {}

void C6SnapshotReceiver::begin(Stream *stream)
{
    stream_ = stream;
    reset_parser_();
    note_status_(stream_ != nullptr ? "C6 transport listening" : "C6 transport unavailable");
}

bool C6SnapshotReceiver::poll(brief::DashboardSnapshot *out)
{
    if (stream_ == nullptr || out == nullptr) {
        return false;
    }

    uint16_t consumed = 0;
    while (stream_->available() > 0 && consumed < kMaxBytesPerPoll) {
        const int next = stream_->read();
        if (next < 0) {
            break;
        }
        ++consumed;
        if (consume_byte_(static_cast<uint8_t>(next), out)) {
            return true;
        }
    }

    return false;
}

const char *C6SnapshotReceiver::status_text() const
{
    return status_text_;
}

bool C6SnapshotReceiver::active() const
{
    return stream_ != nullptr;
}

uint32_t C6SnapshotReceiver::frames_received() const
{
    return frames_received_;
}

uint32_t C6SnapshotReceiver::crc_errors() const
{
    return crc_errors_;
}

uint32_t C6SnapshotReceiver::framing_errors() const
{
    return framing_errors_;
}

uint32_t C6SnapshotReceiver::last_frame_ms() const
{
    return last_frame_ms_;
}

void C6SnapshotReceiver::reset_parser_()
{
    state_ = kSync;
    sync_window_ = 0;
    header_index_ = 0;
    payload_index_ = 0;
    memset(header_bytes_, 0, sizeof(header_bytes_));
    memset(&header_, 0, sizeof(header_));
}

void C6SnapshotReceiver::note_status_(const char *text)
{
    snprintf(status_text_, sizeof(status_text_), "%s", text != nullptr ? text : "C6 transport status unknown");
}

bool C6SnapshotReceiver::consume_byte_(uint8_t byte, brief::DashboardSnapshot *out)
{
    switch (state_) {
        case kSync:
            sync_window_ = (sync_window_ >> 8U) | (static_cast<uint32_t>(byte) << 24U);
            if (sync_window_ == brief_transport::kFrameMagic) {
                memcpy(header_bytes_, &brief_transport::kFrameMagic, sizeof(brief_transport::kFrameMagic));
                header_index_ = sizeof(brief_transport::kFrameMagic);
                state_ = kHeader;
            }
            return false;

        case kHeader:
            header_bytes_[header_index_++] = byte;
            if (header_index_ < sizeof(header_bytes_)) {
                return false;
            }

            memcpy(&header_, header_bytes_, sizeof(header_));
            if (!validate_header_()) {
                reset_parser_();
                return false;
            }

            payload_index_ = 0;
            state_ = kPayload;
            return false;

        case kPayload:
            payload_[payload_index_++] = byte;
            if (payload_index_ < header_.payload_size) {
                return false;
            }

            if (brief_transport::crc32(payload_, header_.payload_size) != header_.payload_crc32) {
                ++crc_errors_;
                note_status_("C6 frame checksum failed");
                reset_parser_();
                return false;
            }

            memcpy(out, payload_, sizeof(brief::DashboardSnapshot));
            if (!validate_snapshot_(*out)) {
                ++framing_errors_;
                note_status_("C6 snapshot schema rejected");
                reset_parser_();
                return false;
            }

            ++frames_received_;
            last_frame_ms_ = millis();
            note_status_("C6 snapshot received");
            reset_parser_();
            return true;
    }

    reset_parser_();
    return false;
}

bool C6SnapshotReceiver::validate_header_()
{
    if (header_.magic != brief_transport::kFrameMagic ||
        header_.version != brief_transport::kFrameVersion ||
        header_.type != static_cast<uint8_t>(brief::kMsgDashboardSnapshot) ||
        header_.payload_size != sizeof(brief::DashboardSnapshot) ||
        header_.payload_size > sizeof(payload_)) {
        ++framing_errors_;
        note_status_("C6 frame header rejected");
        return false;
    }

    return true;
}

bool C6SnapshotReceiver::validate_snapshot_(const brief::DashboardSnapshot &snapshot)
{
    return snapshot.header.version == brief::kProtocolVersion &&
           snapshot.header.type == static_cast<uint8_t>(brief::kMsgDashboardSnapshot) &&
           snapshot.header.payload_size == sizeof(brief::DashboardSnapshot) - sizeof(brief::PacketHeader) &&
           snapshot.system.header.version == brief::kProtocolVersion &&
           snapshot.system.header.type == static_cast<uint8_t>(brief::kMsgSystemStatus);
}
