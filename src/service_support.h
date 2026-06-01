#ifndef SERVICE_SUPPORT_H
#define SERVICE_SUPPORT_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <time.h>

#ifndef LONDONBRIEF_SERVICE_DEBUG
#define LONDONBRIEF_SERVICE_DEBUG 0
#endif

using service_network_ready_fn_t = bool (*)();
using service_network_fault_fn_t = void (*)(const char *tag, int code);

void service_set_network_ready_callback(service_network_ready_fn_t callback);
bool service_network_ready();
void service_set_network_fault_callback(service_network_fault_fn_t callback);
void service_report_network_fault(const char *tag, int code);

inline bool service_time_reached(uint32_t now_ms, uint32_t target_ms)
{
    return static_cast<int32_t>(now_ms - target_ms) >= 0;
}

inline void service_copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    snprintf(dest, dest_size, "%s", src ? src : "");
}

inline bool service_wall_clock_valid(time_t now = time(nullptr))
{
    constexpr time_t kEarliestValidEpoch = 1704067200;  // 2024-01-01 00:00:00 UTC
    return now >= kEarliestValidEpoch;
}

inline void service_set_updated_now(char *dest, size_t dest_size)
{
    time_t now = time(nullptr);
    if (!service_wall_clock_valid(now)) {
        service_copy_text(dest, dest_size, "Updating...");
        return;
    }

    struct tm local_time = {};
    localtime_r(&now, &local_time);
    strftime(dest, dest_size, "%H:%M", &local_time);
}

inline void service_log_http_failure(const char *tag, int http_code, const String &body)
{
    Serial.printf("[%s] HTTP failure: %d\r\n", tag, http_code);
    if (body.length() > 0) {
        const String preview = body.substring(0, 160);
        Serial.printf("[%s] Body: %s\r\n", tag, preview.c_str());
    }
}

inline void service_log_fetch_start(const char *tag, const char *target)
{
#if LONDONBRIEF_SERVICE_DEBUG
    Serial.printf(
        "[%s] fetch start | target=%s | heap=%u KB\r\n",
        tag ? tag : "service",
        target ? target : "unknown",
        static_cast<unsigned>(ESP.getFreeHeap() / 1024U)
    );
#else
    (void)tag;
    (void)target;
#endif
}

inline void service_log_http_result(const char *tag, int http_code, int payload_size)
{
#if LONDONBRIEF_SERVICE_DEBUG
    Serial.printf(
        "[%s] http=%d | content_length=%d | heap=%u KB\r\n",
        tag ? tag : "service",
        http_code,
        payload_size,
        static_cast<unsigned>(ESP.getFreeHeap() / 1024U)
    );
#else
    (void)tag;
    (void)http_code;
    (void)payload_size;
#endif
}

inline void service_log_payload_result(const char *tag, size_t payload_length)
{
#if LONDONBRIEF_SERVICE_DEBUG
    Serial.printf(
        "[%s] payload=%u bytes | heap=%u KB\r\n",
        tag ? tag : "service",
        static_cast<unsigned>(payload_length),
        static_cast<unsigned>(ESP.getFreeHeap() / 1024U)
    );
#else
    (void)tag;
    (void)payload_length;
#endif
}

inline void service_log_success(const char *tag, const char *summary)
{
#if LONDONBRIEF_SERVICE_DEBUG
    Serial.printf(
        "[%s] success | %s | heap=%u KB\r\n",
        tag ? tag : "service",
        summary ? summary : "ok",
        static_cast<unsigned>(ESP.getFreeHeap() / 1024U)
    );
#else
    (void)tag;
    (void)summary;
#endif
}

inline void service_log_parse_failure(const char *tag, const char *message)
{
    Serial.printf("[%s] %s\r\n", tag, message ? message : "Parse failure");
}

inline bool service_configure_tls_client(WiFiClientSecure &client, const char *root_ca, bool allow_insecure)
{
    if (root_ca != nullptr && root_ca[0] != '\0') {
        client.setCACert(root_ca);
        return true;
    }

    if (allow_insecure) {
        client.setInsecure();
        return true;
    }

    return false;
}

class ServiceBoundedStream {
public:
    ServiceBoundedStream(Stream &stream, size_t limit)
        : stream_(stream), limit_(limit), read_(0) {}

    int read()
    {
        if (read_ >= limit_) {
            return -1;
        }

        char value = 0;
        const size_t count = readBytes(&value, 1);
        if (count != 1) {
            return -1;
        }

        return static_cast<uint8_t>(value);
    }

    int peek()
    {
        if (read_ >= limit_) {
            return -1;
        }

        wait_for_available_();
        return stream_.peek();
    }

    size_t readBytes(char *buffer, size_t length)
    {
        if (read_ >= limit_) {
            return 0;
        }

        const size_t allowed = length < (limit_ - read_) ? length : (limit_ - read_);
        const size_t count = stream_.readBytes(buffer, allowed);
        read_ += count;
        return count;
    }

    bool limit_reached() const
    {
        return read_ >= limit_;
    }

    size_t bytes_read() const
    {
        return read_;
    }

private:
    static constexpr uint32_t kSingleByteReadWaitMs = 250;

    bool wait_for_available_()
    {
        const uint32_t started_ms = millis();
        while (stream_.available() <= 0) {
            if (service_time_reached(millis(), started_ms + kSingleByteReadWaitMs)) {
                return false;
            }
            delay(1);
        }

        return true;
    }

    Stream &stream_;
    size_t limit_;
    size_t read_;
};

inline bool service_read_bounded_string(
    Stream &stream,
    size_t max_bytes,
    int expected_size,
    String *out,
    size_t *bytes_read
)
{
    if (out == nullptr) {
        return false;
    }

    out->remove(0);
    const size_t reserve_size =
        expected_size > 0 && static_cast<size_t>(expected_size) <= max_bytes ?
            static_cast<size_t>(expected_size) :
            (max_bytes < 4096U ? max_bytes : 4096U);
    out->reserve(reserve_size);

    ServiceBoundedStream bounded_stream(stream, max_bytes + 1U);
    char buffer[384];
    while (!bounded_stream.limit_reached()) {
        const size_t remaining = (max_bytes + 1U) - bounded_stream.bytes_read();
        const size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t count = bounded_stream.readBytes(buffer, wanted);
        if (count == 0) {
            break;
        }

        if (out->length() + count > max_bytes) {
            if (bytes_read != nullptr) {
                *bytes_read = bounded_stream.bytes_read();
            }
            return false;
        }

        out->concat(buffer, count);
        if (expected_size > 0 && bounded_stream.bytes_read() >= static_cast<size_t>(expected_size)) {
            break;
        }
    }

    if (bytes_read != nullptr) {
        *bytes_read = bounded_stream.bytes_read();
    }

    return bounded_stream.bytes_read() <= max_bytes;
}

#endif
