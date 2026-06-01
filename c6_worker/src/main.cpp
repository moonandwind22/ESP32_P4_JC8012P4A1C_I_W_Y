#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>

#include "brief_protocol.h"
#include "brief_transport.h"
#include "c6_worker_config.h"

namespace {

constexpr uint32_t kStatusPublishMs = 2000;
constexpr uint32_t kWifiRetryMs = 15000;
constexpr uint32_t kWifiWarmupMs = 8000;
constexpr uint32_t kWeatherRefreshMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kWeatherRetryMs = 60UL * 1000UL;
constexpr uint32_t kTflRefreshMs = 2UL * 60UL * 1000UL;
constexpr uint32_t kTflRetryMs = 45UL * 1000UL;
constexpr uint32_t kNewsRefreshMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kNewsRetryMs = 60UL * 1000UL;
constexpr uint32_t kDomainSpacingMs = 15000;
constexpr uint32_t kInitialWeatherDelayMs = 5000;
constexpr uint32_t kInitialTflDelayMs = 35000;
constexpr uint32_t kInitialNewsDelayMs = 65000;
constexpr uint32_t kHttpTimeoutMs = 8000;
constexpr size_t kWeatherMaxPayloadBytes = 64UL * 1024UL;
constexpr size_t kTflMaxPayloadBytes = 48UL * 1024UL;
constexpr size_t kNewsMaxPayloadBytes = 64UL * 1024UL;
constexpr size_t kMaxFetchedTflLines = 24;
constexpr size_t kRawHeadlineLimit = 3;
constexpr uint32_t kDebugSerialBaud = 115200;
constexpr uint8_t kWorkerTransportNone = 0;
constexpr uint8_t kWorkerTransportUart = 1;
constexpr uint8_t kWorkerTransportSdio = 2;
constexpr uint8_t kWorkerTransport = C6_WORKER_TRANSPORT;
constexpr bool kUseWorkerSdioTransport = kWorkerTransport == kWorkerTransportSdio;
constexpr bool kUseWorkerUartTransport = kWorkerTransport == kWorkerTransportUart;
constexpr bool kTransportPinsConfigured = C6_WORKER_SERIAL_RX_PIN >= 0 && C6_WORKER_SERIAL_TX_PIN >= 0;
constexpr bool kUsbBinaryFallbackEnabled = C6_WORKER_ALLOW_USB_BINARY_FALLBACK != 0;
constexpr bool kWorkerDebugSerial = C6_WORKER_DEBUG_SERIAL != 0;
constexpr uint32_t kWorkerDebugLogMs = 5000;

enum WorkerDomain : uint8_t {
    kWorkerDomainWeather = 0,
    kWorkerDomainTfl = 1,
    kWorkerDomainNews = 2,
    kWorkerDomainCount = 3,
};

struct DomainSchedule {
    uint32_t next_due_ms;
    uint32_t last_success_ms;
    uint8_t failures;
};

struct RawLineStatus {
    char name[32];
    char status[48];
    uint8_t severity;
    bool disrupted;
};

enum ControlParseState : uint8_t {
    kControlSync,
    kControlHeader,
    kControlPayload,
};

uint32_t last_status_publish_ms = 0;
uint32_t last_wifi_attempt_ms = 0;
uint32_t wifi_connected_since_ms = 0;
uint32_t snapshot_sequence = 0;
uint32_t frame_sequence = 0;
uint32_t last_domain_fetch_ms = 0;
uint32_t last_worker_debug_log_ms = 0;
bool wifi_started = false;
bool warmup_scheduled = false;
wl_status_t last_worker_wifi_status = WL_IDLE_STATUS;
DomainSchedule domain_schedules[kWorkerDomainCount] = {};
ControlParseState control_state = kControlSync;
uint32_t control_sync_window = 0;
uint8_t control_header_bytes[sizeof(brief_transport::FrameHeader)] = {};
uint16_t control_header_index = 0;
brief_transport::FrameHeader control_header = {};
uint8_t control_payload[sizeof(brief::RefreshRequest)] = {};
uint32_t control_payload_index = 0;
HardwareSerial worker_transport_serial(C6_WORKER_SERIAL_PORT);
Stream *transport_stream = nullptr;
bool transport_warning_logged = false;

brief::DashboardSnapshot snapshot = {};

void reset_control_parser();

bool wifi_is_configured()
{
    return strlen(C6_WORKER_WIFI_SSID) > 0 &&
           strcmp(C6_WORKER_WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}

bool wifi_is_ready()
{
    return wifi_connected_since_ms != 0 && millis() - wifi_connected_since_ms >= kWifiWarmupMs;
}

const char *wifi_status_to_text(wl_status_t status)
{
    switch (status) {
        case WL_NO_SHIELD:
            return "NO_SHIELD";
        case WL_IDLE_STATUS:
            return "IDLE";
        case WL_NO_SSID_AVAIL:
            return "NO_SSID";
        case WL_SCAN_COMPLETED:
            return "SCAN_DONE";
        case WL_CONNECTED:
            return "CONNECTED";
        case WL_CONNECT_FAILED:
            return "CONNECT_FAILED";
        case WL_CONNECTION_LOST:
            return "CONNECTION_LOST";
        case WL_DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

const char *transport_to_text()
{
    if (kUseWorkerSdioTransport) {
        return "sdio";
    }
    if (kUseWorkerUartTransport) {
        return "uart";
    }
    return "none";
}

const char *worker_domain_to_text(WorkerDomain domain)
{
    switch (domain) {
        case kWorkerDomainWeather:
            return "weather";
        case kWorkerDomainTfl:
            return "tfl";
        case kWorkerDomainNews:
            return "news";
        default:
            return "unknown";
    }
}

void log_worker_debug(const char *stage, bool force = false)
{
    if (!kWorkerDebugSerial) {
        return;
    }

    const uint32_t now = millis();
    if (!force && last_worker_debug_log_ms != 0 &&
        static_cast<uint32_t>(now - last_worker_debug_log_ms) < kWorkerDebugLogMs) {
        return;
    }
    last_worker_debug_log_ms = now;

    const wl_status_t status = WiFi.status();
    Serial.printf(
        "[c6-debug] t=%lu stage=%s transport=%s stream=%u wifi_started=%u wifi=%d/%s ready=%u configured=%u ssid=%s ip=%s rssi=%d heap_kb=%u seq=%lu weather=%u tfl=%u news=%u failures=%u/%u/%u\n",
        static_cast<unsigned long>(now),
        stage != nullptr ? stage : "unspecified",
        transport_to_text(),
        transport_stream != nullptr ? 1U : 0U,
        wifi_started ? 1U : 0U,
        static_cast<int>(status),
        wifi_status_to_text(status),
        wifi_is_ready() ? 1U : 0U,
        wifi_is_configured() ? 1U : 0U,
        WiFi.SSID().c_str(),
        status == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-",
        status == WL_CONNECTED ? WiFi.RSSI() : 0,
        static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
        static_cast<unsigned long>(snapshot_sequence),
        static_cast<unsigned>(snapshot.weather.now.state),
        static_cast<unsigned>(snapshot.tfl.state),
        static_cast<unsigned>(snapshot.news.state),
        static_cast<unsigned>(domain_schedules[kWorkerDomainWeather].failures),
        static_cast<unsigned>(domain_schedules[kWorkerDomainTfl].failures),
        static_cast<unsigned>(domain_schedules[kWorkerDomainNews].failures)
    );
}

bool network_mode_allows_domain(brief::Domain domain)
{
    switch (static_cast<brief::NetworkMode>(C6_WORKER_NETWORK_MODE)) {
        case brief::kNetworkModeAutomatic:
        case brief::kNetworkModeManualRefresh:
            return true;
        case brief::kNetworkModeWeatherOnly:
            return domain == brief::kDomainWeather;
        case brief::kNetworkModeTflOnly:
            return domain == brief::kDomainTfl;
        case brief::kNetworkModeNewsOnly:
            return domain == brief::kDomainNews;
        case brief::kNetworkModeOffline:
            return false;
        default:
            return true;
    }
}

void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    snprintf(dest, dest_size, "%s", src);
}

bool configure_tls_client(WiFiClientSecure &client)
{
    if (C6_WORKER_TLS_ROOT_CA[0] != '\0') {
        client.setCACert(C6_WORKER_TLS_ROOT_CA);
        return true;
    }

    if (C6_WORKER_ALLOW_INSECURE_TLS != 0) {
        client.setInsecure();
        return true;
    }

    return false;
}

class BoundedStreamReader {
public:
    BoundedStreamReader(Stream &stream, size_t limit)
        : stream_(stream), limit_(limit), read_(0) {}

    int read()
    {
        if (read_ >= limit_) {
            return -1;
        }
        const int value = stream_.read();
        if (value >= 0) {
            ++read_;
        }
        return value;
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

private:
    Stream &stream_;
    size_t limit_;
    size_t read_;
};

void set_updated_now(char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    time_t now = time(nullptr);
    if (now < 100000) {
        copy_text(dest, dest_size, "--:--");
        return;
    }

    struct tm local_time = {};
    localtime_r(&now, &local_time);
    strftime(dest, dest_size, "%H:%M", &local_time);
}

void set_header(brief::PacketHeader *header, brief::MessageType type, uint32_t payload_size)
{
    if (header == nullptr) {
        return;
    }

    header->version = brief::kProtocolVersion;
    header->type = static_cast<uint8_t>(type);
    header->reserved0 = 0;
    header->payload_size = payload_size;
    header->sequence = snapshot_sequence++;
    header->uptime_ms = millis();
}

const char *weather_code_to_text(int code, bool is_day)
{
    switch (code) {
        case 0:
            return is_day ? "Clear" : "Clear night";
        case 1:
            return is_day ? "Mostly clear" : "Mostly clear night";
        case 2:
            return "Partly cloudy";
        case 3:
            return "Overcast";
        case 45:
        case 48:
            return "Fog";
        case 51:
        case 53:
        case 55:
            return "Drizzle";
        case 56:
        case 57:
            return "Freezing drizzle";
        case 61:
        case 63:
        case 65:
            return "Rain";
        case 66:
        case 67:
            return "Freezing rain";
        case 71:
        case 73:
        case 75:
            return "Snow";
        case 77:
            return "Snow grains";
        case 80:
        case 81:
        case 82:
            return "Rain showers";
        case 85:
        case 86:
            return "Snow showers";
        case 95:
            return "Thunderstorm";
        case 96:
        case 99:
            return "Storm and hail";
        default:
            return "Conditions unavailable";
    }
}

void format_hour_label(const char *iso_time, char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }
    if (iso_time == nullptr || strlen(iso_time) < 16) {
        copy_text(dest, dest_size, "--:--");
        return;
    }
    snprintf(dest, dest_size, "%c%c:%c%c", iso_time[11], iso_time[12], iso_time[14], iso_time[15]);
}

bool build_local_hour_key(char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return false;
    }

    time_t now = time(nullptr);
    if (now < 100000) {
        dest[0] = '\0';
        return false;
    }

    struct tm local_time = {};
    localtime_r(&now, &local_time);
    strftime(dest, dest_size, "%Y-%m-%dT%H:00", &local_time);
    return true;
}

size_t find_hourly_start_index(JsonArrayConst hourly_times, const char *target_key)
{
    if (target_key == nullptr || target_key[0] == '\0') {
        return 0;
    }

    size_t index = 0;
    for (JsonVariantConst entry : hourly_times) {
        const char *hour_key = entry.as<const char *>();
        if (hour_key != nullptr && strcmp(hour_key, target_key) >= 0) {
            return index;
        }
        ++index;
    }

    return 0;
}

void append_change_text(char *dest, size_t dest_size, const char *time_label, const char *condition)
{
    if (dest == nullptr || dest_size == 0 || time_label == nullptr || condition == nullptr) {
        return;
    }

    if (dest[0] != '\0') {
        strncat(dest, "  |  ", dest_size - strlen(dest) - 1);
    }

    char part[64];
    snprintf(part, sizeof(part), "%s %s", time_label, condition);
    strncat(dest, part, dest_size - strlen(dest) - 1);
}

void format_daily_label(const char *iso_date, uint8_t offset, char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    if (offset == 0) {
        copy_text(dest, dest_size, "Today");
        return;
    }
    if (offset == 1) {
        copy_text(dest, dest_size, "Tomorrow");
        return;
    }
    if (iso_date == nullptr || strlen(iso_date) < 10) {
        copy_text(dest, dest_size, "Later");
        return;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(iso_date, "%d-%d-%d", &year, &month, &day) != 3) {
        copy_text(dest, dest_size, "Later");
        return;
    }

    struct tm day_time = {};
    day_time.tm_year = year - 1900;
    day_time.tm_mon = month - 1;
    day_time.tm_mday = day;
    day_time.tm_isdst = -1;
    mktime(&day_time);
    strftime(dest, dest_size, "%a", &day_time);
}

bool text_equals_ignore_case(const char *lhs, const char *rhs)
{
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }

    while (*lhs != '\0' && *rhs != '\0') {
        const char left = static_cast<char>(tolower(static_cast<unsigned char>(*lhs)));
        const char right = static_cast<char>(tolower(static_cast<unsigned char>(*rhs)));
        if (left != right) {
            return false;
        }
        ++lhs;
        ++rhs;
    }

    return *lhs == '\0' && *rhs == '\0';
}

int compare_text_ignore_case(const char *lhs, const char *rhs)
{
    if (lhs == nullptr && rhs == nullptr) {
        return 0;
    }
    if (lhs == nullptr) {
        return -1;
    }
    if (rhs == nullptr) {
        return 1;
    }

    while (*lhs != '\0' && *rhs != '\0') {
        const char left = static_cast<char>(tolower(static_cast<unsigned char>(*lhs)));
        const char right = static_cast<char>(tolower(static_cast<unsigned char>(*rhs)));
        if (left != right) {
            return static_cast<int>(left) - static_cast<int>(right);
        }
        ++lhs;
        ++rhs;
    }

    return static_cast<int>(static_cast<unsigned char>(*lhs)) - static_cast<int>(static_cast<unsigned char>(*rhs));
}

String collapse_whitespace(const String &input)
{
    String output;
    output.reserve(input.length());

    bool previous_was_space = false;
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        const bool is_space = (c == ' ' || c == '\n' || c == '\r' || c == '\t');
        if (is_space) {
            if (!previous_was_space) {
                output += ' ';
            }
            previous_was_space = true;
        } else {
            output += c;
            previous_was_space = false;
        }
    }

    output.trim();
    return output;
}

String decode_html_entities(String text)
{
    text.replace("&amp;", "&");
    text.replace("&quot;", "\"");
    text.replace("&#39;", "'");
    text.replace("&apos;", "'");
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&#x27;", "'");
    return text;
}

String strip_markup(const String &input)
{
    String output;
    output.reserve(input.length());

    bool inside_tag = false;
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        if (c == '<') {
            inside_tag = true;
            continue;
        }
        if (c == '>') {
            inside_tag = false;
            output += ' ';
            continue;
        }
        if (!inside_tag) {
            output += c;
        }
    }

    return collapse_whitespace(decode_html_entities(output));
}

String extract_tag_value(const String &block, const String &lower_block, const char *tag)
{
    const String open_tag = String("<") + tag;
    const String close_tag = String("</") + tag + ">";
    const int open_start = lower_block.indexOf(open_tag);
    if (open_start < 0) {
        return "";
    }

    const int content_start = lower_block.indexOf('>', open_start);
    if (content_start < 0) {
        return "";
    }

    const int close_start = lower_block.indexOf(close_tag, content_start + 1);
    if (close_start < 0) {
        return "";
    }

    String value = block.substring(content_start + 1, close_start);
    value.replace("<![CDATA[", "");
    value.replace("]]>", "");
    return strip_markup(value);
}

bool extract_item_block(const String &payload, const String &lower_payload, int *search_from, String *block_out)
{
    if (search_from == nullptr || block_out == nullptr) {
        return false;
    }

    const int item_start = lower_payload.indexOf("<item", *search_from);
    const int entry_start = lower_payload.indexOf("<entry", *search_from);

    int start = -1;
    String close_tag;
    if (item_start >= 0 && (entry_start < 0 || item_start < entry_start)) {
        start = item_start;
        close_tag = "</item>";
    } else if (entry_start >= 0) {
        start = entry_start;
        close_tag = "</entry>";
    }

    if (start < 0) {
        return false;
    }

    const int end = lower_payload.indexOf(close_tag, start);
    if (end < 0) {
        return false;
    }

    *block_out = payload.substring(start, end);
    *search_from = end + close_tag.length();
    return true;
}

void log_http_failure(const char *service_tag, int http_code, const String &body)
{
#if !C6_WORKER_ENABLE_BINARY_SNAPSHOT_STREAM
    Serial.printf("[c6][%s] http=%d body=%s\n", service_tag, http_code, body.substring(0, 120).c_str());
#else
    (void)service_tag;
    (void)http_code;
    (void)body;
#endif
}

void log_parse_failure(const char *service_tag, const char *message)
{
#if !C6_WORKER_ENABLE_BINARY_SNAPSHOT_STREAM
    Serial.printf("[c6][%s] parse=%s\n", service_tag, message == nullptr ? "unknown" : message);
#else
    (void)service_tag;
    (void)message;
#endif
}

void reset_domain_state_for_disconnect()
{
    snapshot.weather.now.state = brief::kDataStateWaiting;
    copy_text(snapshot.weather.summary_text, sizeof(snapshot.weather.summary_text), "Waiting for Wi-Fi");
    copy_text(snapshot.weather.next_change_text, sizeof(snapshot.weather.next_change_text), "Hourly weather ribbon pending");

    snapshot.tfl.state = brief::kDataStateWaiting;
    copy_text(snapshot.tfl.summary_text, sizeof(snapshot.tfl.summary_text), "Waiting for Wi-Fi");

    snapshot.news.state = brief::kDataStateWaiting;
    snapshot.news.headline_count = 0;
    copy_text(snapshot.news.updated_hhmm, sizeof(snapshot.news.updated_hhmm), "--:--");
}

void populate_system_status()
{
    set_header(&snapshot.header, brief::kMsgDashboardSnapshot, sizeof(brief::DashboardSnapshot) - sizeof(brief::PacketHeader));
    set_header(&snapshot.system.header, brief::kMsgSystemStatus, sizeof(brief::SystemStatus) - sizeof(brief::PacketHeader));
    snapshot.system.network_mode = static_cast<uint8_t>(C6_WORKER_NETWORK_MODE);
    snapshot.system.wifi_connected = WiFi.status() == WL_CONNECTED ? 1 : 0;
    snapshot.system.wifi_ready = wifi_is_ready() ? 1 : 0;
    snapshot.system.link_healthy = transport_stream != nullptr ? 1 : 0;
    snapshot.system.wifi_rssi_dbm = snapshot.system.wifi_connected ? static_cast<int8_t>(WiFi.RSSI()) : 0;
    snapshot.system.fault_count =
        domain_schedules[kWorkerDomainWeather].failures +
        domain_schedules[kWorkerDomainTfl].failures +
        domain_schedules[kWorkerDomainNews].failures;
    snapshot.system.cooldown_active = 0;
    snapshot.system.cooldown_remaining_ms = 0;
    snapshot.system.last_refresh_ms = millis();

    if (!wifi_is_configured()) {
        copy_text(snapshot.system.status_text, sizeof(snapshot.system.status_text), "C6 Wi-Fi not configured");
    } else if (snapshot.system.wifi_connected) {
        snprintf(snapshot.system.status_text, sizeof(snapshot.system.status_text), "C6 connected to %s", WiFi.SSID().c_str());
    } else if (wifi_started) {
        copy_text(snapshot.system.status_text, sizeof(snapshot.system.status_text), "C6 reconnecting");
    } else {
        copy_text(snapshot.system.status_text, sizeof(snapshot.system.status_text), "C6 startup pending");
    }
}

void publish_snapshot()
{
#if C6_WORKER_ENABLE_BINARY_SNAPSHOT_STREAM
    if (transport_stream == nullptr) {
        if (!transport_warning_logged) {
            transport_warning_logged = true;
            Serial.println(kUseWorkerSdioTransport ?
                           "[c6] SDIO snapshot transport pending: driver not enabled yet" :
                           "[c6] transport disabled: set C6_WORKER_SERIAL_RX_PIN/TX_PIN for P4 snapshots");
        }
        return;
    }

    const auto header = brief_transport::make_header(
        static_cast<uint8_t>(brief::kMsgDashboardSnapshot),
        frame_sequence++,
        millis(),
        reinterpret_cast<const uint8_t *>(&snapshot),
        sizeof(snapshot)
    );
    transport_stream->write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
    transport_stream->write(reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot));
    transport_stream->flush();
#else
    Serial.printf(
        "[c6] wifi=%u ready=%u weather=%u tfl=%u news=%u rssi=%d\n",
        snapshot.system.wifi_connected,
        snapshot.system.wifi_ready,
        snapshot.weather.now.state,
        snapshot.tfl.state,
        snapshot.news.state,
        snapshot.system.wifi_rssi_dbm
    );
#endif
}

void begin_transport()
{
#if C6_WORKER_ENABLE_BINARY_SNAPSHOT_STREAM
    if (kUseWorkerSdioTransport) {
        transport_stream = nullptr;
        Serial.println("[c6] transport sdio snapshot selected; driver pending");
    } else if (kUseWorkerUartTransport && kTransportPinsConfigured) {
        worker_transport_serial.begin(
            C6_WORKER_SERIAL_BAUD,
            SERIAL_8N1,
            C6_WORKER_SERIAL_RX_PIN,
            C6_WORKER_SERIAL_TX_PIN
        );
        transport_stream = &worker_transport_serial;
        Serial.printf(
            "[c6] transport uart%d rx=%d tx=%d baud=%lu\n",
            C6_WORKER_SERIAL_PORT,
            C6_WORKER_SERIAL_RX_PIN,
            C6_WORKER_SERIAL_TX_PIN,
            static_cast<unsigned long>(C6_WORKER_SERIAL_BAUD)
        );
    } else if (kUsbBinaryFallbackEnabled) {
        transport_stream = &Serial;
        esp_log_level_set("*", ESP_LOG_NONE);
    } else {
        transport_stream = nullptr;
        Serial.println("[c6] transport not started: C6_WORKER_SERIAL_RX_PIN/TX_PIN are not configured");
    }
#else
    transport_stream = nullptr;
#endif
    reset_control_parser();
    log_worker_debug("transport configured", true);
}

void begin_wifi_if_needed()
{
    if (!wifi_is_configured()) {
        log_worker_debug("wifi skipped: credentials missing");
        return;
    }
    if (static_cast<brief::NetworkMode>(C6_WORKER_NETWORK_MODE) == brief::kNetworkModeOffline) {
        log_worker_debug("wifi skipped: offline mode");
        return;
    }
    if (wifi_started && WiFi.status() == WL_CONNECTED) {
        return;
    }
    if (millis() - last_wifi_attempt_ms < kWifiRetryMs) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.begin(C6_WORKER_WIFI_SSID, C6_WORKER_WIFI_PASSWORD);
    wifi_started = true;
    last_wifi_attempt_ms = millis();
    log_worker_debug("WiFi.begin issued", true);
}

void schedule_initial_domains()
{
    const uint32_t now = millis();
    domain_schedules[kWorkerDomainWeather].next_due_ms = now + kInitialWeatherDelayMs;
    domain_schedules[kWorkerDomainTfl].next_due_ms = now + kInitialTflDelayMs;
    domain_schedules[kWorkerDomainNews].next_due_ms = now + kInitialNewsDelayMs;
    warmup_scheduled = true;
    log_worker_debug("domain warmup scheduled", true);
}

void update_wifi_state()
{
    const wl_status_t status = WiFi.status();
    if (status != last_worker_wifi_status) {
        last_worker_wifi_status = status;
        log_worker_debug("wifi status transition", true);
    }

    if (status == WL_CONNECTED) {
        if (wifi_connected_since_ms == 0) {
            wifi_connected_since_ms = millis();
            warmup_scheduled = false;
            log_worker_debug("wifi connected", true);
        }
        if (wifi_is_ready() && !warmup_scheduled) {
            schedule_initial_domains();
        }
        return;
    }

    wifi_connected_since_ms = 0;
    warmup_scheduled = false;
    reset_domain_state_for_disconnect();
    begin_wifi_if_needed();
}

int map_weather_trend(int16_t first_tenths, int16_t later_tenths)
{
    const int16_t delta = later_tenths - first_tenths;
    if (delta >= 10) {
        return brief::kWeatherTrendRising;
    }
    if (delta <= -10) {
        return brief::kWeatherTrendFalling;
    }
    return brief::kWeatherTrendSteady;
}

bool fetch_weather()
{
    WiFiClientSecure client;
    if (!configure_tls_client(client)) {
        snapshot.weather.now.state = brief::kDataStateError;
        copy_text(snapshot.weather.summary_text, sizeof(snapshot.weather.summary_text), "TLS root CA missing");
        return false;
    }

    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);

    const String url =
        String("https://api.open-meteo.com/v1/forecast?latitude=") + String(C6_WORKER_LATITUDE, 4) +
        "&longitude=" + String(C6_WORKER_LONGITUDE, 4) +
        "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,is_day,relative_humidity_2m"
        "&hourly=temperature_2m,weather_code,precipitation_probability,relative_humidity_2m,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max"
        "&timezone=Europe%2FLondon&forecast_days=3";

    if (!http.begin(client, url)) {
        snapshot.weather.now.state = brief::kDataStateError;
        copy_text(snapshot.weather.summary_text, sizeof(snapshot.weather.summary_text), "Weather connection failed");
        return false;
    }

    const int http_code = http.GET();
    if (http_code != HTTP_CODE_OK) {
        http.end();
        log_http_failure("weather", http_code, String());
        snapshot.weather.now.state = brief::kDataStateError;
        copy_text(snapshot.weather.summary_text, sizeof(snapshot.weather.summary_text), "Weather request failed");
        return false;
    }

    const int payload_size = http.getSize();
    if (payload_size > 0 && static_cast<size_t>(payload_size) > kWeatherMaxPayloadBytes) {
        http.end();
        snapshot.weather.now.state = brief::kDataStateError;
        copy_text(snapshot.weather.summary_text, sizeof(snapshot.weather.summary_text), "Weather payload too large");
        return false;
    }

    JsonDocument doc;
    BoundedStreamReader bounded_stream(http.getStream(), kWeatherMaxPayloadBytes);
    const DeserializationError error = deserializeJson(
        doc,
        bounded_stream,
        DeserializationOption::NestingLimit(16)
    );
    http.end();
    if (error) {
        log_parse_failure("weather", error.c_str());
        snapshot.weather.now.state = brief::kDataStateError;
        copy_text(snapshot.weather.summary_text, sizeof(snapshot.weather.summary_text), "Weather parse failed");
        return false;
    }

    JsonObject current = doc["current"];
    if (current.isNull()) {
        snapshot.weather.now.state = brief::kDataStateError;
        copy_text(snapshot.weather.summary_text, sizeof(snapshot.weather.summary_text), "Weather data missing");
        return false;
    }

    const float temperature = current["temperature_2m"] | NAN;
    const float apparent = current["apparent_temperature"] | NAN;
    const float wind_speed = current["wind_speed_10m"] | NAN;
    const int weather_code = current["weather_code"] | -1;
    const int is_day = current["is_day"] | 1;
    const int humidity = current["relative_humidity_2m"] | -1;
    const char *current_time = current["time"] | "";

    if (isnan(temperature) || isnan(apparent) || isnan(wind_speed) || weather_code < 0 || humidity < 0) {
        snapshot.weather.now.state = brief::kDataStateError;
        copy_text(snapshot.weather.summary_text, sizeof(snapshot.weather.summary_text), "Weather values missing");
        return false;
    }

    memset(&snapshot.weather, 0, sizeof(snapshot.weather));
    snapshot.weather.now.state = brief::kDataStateLive;
    snapshot.weather.now.is_day = is_day == 1 ? 1 : 0;
    snapshot.weather.now.temperature_tenths_c = static_cast<int16_t>(lroundf(temperature * 10.0f));
    snapshot.weather.now.feels_like_tenths_c = static_cast<int16_t>(lroundf(apparent * 10.0f));
    snapshot.weather.now.wind_kmh_tenths = static_cast<uint16_t>(constrain(static_cast<int>(lroundf(wind_speed * 10.0f)), 0, 65535));
    snapshot.weather.now.humidity_percent = static_cast<uint8_t>(constrain(humidity, 0, 100));
    snapshot.weather.now.weather_code = static_cast<uint8_t>(weather_code);
    copy_text(snapshot.weather.now.location, sizeof(snapshot.weather.now.location), C6_WORKER_LOCATION_NAME);
    copy_text(snapshot.weather.now.condition, sizeof(snapshot.weather.now.condition), weather_code_to_text(weather_code, is_day == 1));
    set_updated_now(snapshot.weather.now.updated_hhmm, sizeof(snapshot.weather.now.updated_hhmm));

    JsonObject hourly = doc["hourly"];
    JsonArrayConst hourly_temperatures = hourly["temperature_2m"].as<JsonArrayConst>();
    JsonArrayConst hourly_times = hourly["time"].as<JsonArrayConst>();
    JsonArrayConst hourly_codes = hourly["weather_code"].as<JsonArrayConst>();
    JsonArrayConst hourly_precip = hourly["precipitation_probability"].as<JsonArrayConst>();
    JsonArrayConst hourly_humidity = hourly["relative_humidity_2m"].as<JsonArrayConst>();
    JsonArrayConst hourly_wind = hourly["wind_speed_10m"].as<JsonArrayConst>();

    char start_key[24] = {};
    if (current_time[0] != '\0') {
        copy_text(start_key, sizeof(start_key), current_time);
    } else {
        build_local_hour_key(start_key, sizeof(start_key));
    }

    const size_t start_index = find_hourly_start_index(hourly_times, start_key);
    const size_t available = std::min(hourly_temperatures.size(), hourly_times.size());
    int previous_code = weather_code;
    uint8_t change_count = 0;
    snapshot.weather.next_change_text[0] = '\0';

    for (size_t i = start_index; i < available && snapshot.weather.hourly_count < brief::kMaxHourlyPoints; ++i) {
        const float hourly_temp = hourly_temperatures[i] | NAN;
        if (isnan(hourly_temp)) {
            continue;
        }

        brief::WeatherHourlyPoint &point = snapshot.weather.hourly[snapshot.weather.hourly_count];
        memset(&point, 0, sizeof(point));
        const char *hour_time = hourly_times[i] | "";
        format_hour_label(hour_time, point.hhmm, sizeof(point.hhmm));
        point.temperature_tenths_c = static_cast<int16_t>(lroundf(hourly_temp * 10.0f));
        point.humidity_percent = static_cast<uint8_t>(constrain(!hourly_humidity.isNull() && i < hourly_humidity.size() ? (hourly_humidity[i] | humidity) : humidity, 0, 100));
        point.rain_probability_percent = static_cast<uint8_t>(constrain(!hourly_precip.isNull() && i < hourly_precip.size() ? (hourly_precip[i] | 0) : 0, 0, 100));
        point.wind_kmh_tenths = static_cast<uint16_t>(constrain(static_cast<int>(lroundf((!hourly_wind.isNull() && i < hourly_wind.size() ? (hourly_wind[i] | wind_speed) : wind_speed) * 10.0f)), 0, 65535));
        point.weather_code = static_cast<uint8_t>(!hourly_codes.isNull() && i < hourly_codes.size() ? (hourly_codes[i] | previous_code) : previous_code);
        point.is_day = snapshot.weather.now.is_day;

        if (point.weather_code != previous_code && change_count < 3 && snapshot.weather.hourly_count > 0) {
            append_change_text(
                snapshot.weather.next_change_text,
                sizeof(snapshot.weather.next_change_text),
                point.hhmm,
                weather_code_to_text(point.weather_code, snapshot.weather.now.is_day == 1)
            );
            ++change_count;
        }

        previous_code = point.weather_code;
        ++snapshot.weather.hourly_count;
    }

    if (snapshot.weather.hourly_count >= 2) {
        snapshot.weather.now.trend = static_cast<uint8_t>(map_weather_trend(
            snapshot.weather.hourly[0].temperature_tenths_c,
            snapshot.weather.hourly[1].temperature_tenths_c
        ));
        snapshot.weather.now.rain_probability_percent = snapshot.weather.hourly[0].rain_probability_percent;
    } else {
        snapshot.weather.now.trend = brief::kWeatherTrendSteady;
        snapshot.weather.now.rain_probability_percent = 0;
    }

    if (change_count == 0) {
        copy_text(snapshot.weather.next_change_text, sizeof(snapshot.weather.next_change_text), "No major condition change in the next 12 hours");
    }

    JsonObject daily = doc["daily"];
    JsonArrayConst daily_times = daily["time"].as<JsonArrayConst>();
    JsonArrayConst daily_codes = daily["weather_code"].as<JsonArrayConst>();
    JsonArrayConst daily_temp_max = daily["temperature_2m_max"].as<JsonArrayConst>();
    JsonArrayConst daily_temp_min = daily["temperature_2m_min"].as<JsonArrayConst>();
    JsonArrayConst daily_precip = daily["precipitation_probability_max"].as<JsonArrayConst>();
    const size_t daily_available = std::min(
        std::min(daily_times.size(), daily_codes.size()),
        std::min(daily_temp_max.size(), daily_temp_min.size())
    );

    for (size_t i = 0; i < daily_available && snapshot.weather.daily_count < brief::kMaxDailyPoints; ++i) {
        brief::WeatherDailyPoint &point = snapshot.weather.daily[snapshot.weather.daily_count];
        memset(&point, 0, sizeof(point));
        format_daily_label(daily_times[i] | "", snapshot.weather.daily_count, point.label, sizeof(point.label));
        point.min_tenths_c = static_cast<int16_t>(lroundf((daily_temp_min[i] | temperature) * 10.0f));
        point.max_tenths_c = static_cast<int16_t>(lroundf((daily_temp_max[i] | temperature) * 10.0f));
        point.weather_code = static_cast<uint8_t>(daily_codes[i] | weather_code);
        point.rain_probability_percent = static_cast<uint8_t>(constrain(!daily_precip.isNull() && i < daily_precip.size() ? (daily_precip[i] | 0) : 0, 0, 100));
        ++snapshot.weather.daily_count;
    }

    snprintf(
        snapshot.weather.summary_text,
        sizeof(snapshot.weather.summary_text),
        "Feels %.0fC | Humidity %d%% | Wind %.0f km/h",
        apparent,
        humidity,
        wind_speed
    );
    return true;
}

uint8_t map_tfl_severity(int severity, bool disrupted)
{
    if (!disrupted) {
        return brief::kTflSeverityGood;
    }
    if (severity <= 5) {
        return brief::kTflSeveritySevere;
    }
    if (severity <= 8) {
        return brief::kTflSeverityMajor;
    }
    if (severity <= 10) {
        return brief::kTflSeverityMinor;
    }
    return brief::kTflSeverityInfo;
}

bool fetch_tfl()
{
    WiFiClientSecure client;
    if (!configure_tls_client(client)) {
        snapshot.tfl.state = brief::kDataStateError;
        copy_text(snapshot.tfl.summary_text, sizeof(snapshot.tfl.summary_text), "TLS root CA missing");
        return false;
    }

    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);

    String url = "https://api.tfl.gov.uk/Line/Mode/tube,dlr,overground,elizabeth-line,tram/Status?detail=false";
    if (strlen(C6_WORKER_TFL_APP_KEY) > 0) {
        url += "&app_key=";
        url += C6_WORKER_TFL_APP_KEY;
    }

    if (!http.begin(client, url)) {
        snapshot.tfl.state = brief::kDataStateError;
        copy_text(snapshot.tfl.summary_text, sizeof(snapshot.tfl.summary_text), "TfL connection failed");
        return false;
    }

    const int http_code = http.GET();
    if (http_code != HTTP_CODE_OK) {
        http.end();
        log_http_failure("tfl", http_code, String());
        snapshot.tfl.state = brief::kDataStateError;
        copy_text(snapshot.tfl.summary_text, sizeof(snapshot.tfl.summary_text), "TfL request failed");
        return false;
    }

    const int payload_size = http.getSize();
    if (payload_size > 0 && static_cast<size_t>(payload_size) > kTflMaxPayloadBytes) {
        http.end();
        snapshot.tfl.state = brief::kDataStateError;
        copy_text(snapshot.tfl.summary_text, sizeof(snapshot.tfl.summary_text), "TfL payload too large");
        return false;
    }

    JsonDocument doc;
    BoundedStreamReader bounded_stream(http.getStream(), kTflMaxPayloadBytes);
    const DeserializationError error = deserializeJson(
        doc,
        bounded_stream,
        DeserializationOption::NestingLimit(16)
    );
    http.end();
    if (error || !doc.is<JsonArray>()) {
        log_parse_failure("tfl", error ? error.c_str() : "payload is not an array");
        snapshot.tfl.state = brief::kDataStateError;
        copy_text(snapshot.tfl.summary_text, sizeof(snapshot.tfl.summary_text), "TfL parse failed");
        return false;
    }

    RawLineStatus fetched[kMaxFetchedTflLines] = {};
    size_t fetched_count = 0;
    uint8_t disruptions = 0;

    for (JsonObject line : doc.as<JsonArray>()) {
        if (fetched_count >= kMaxFetchedTflLines) {
            break;
        }

        JsonArray statuses = line["lineStatuses"];
        if (statuses.isNull() || statuses.size() == 0) {
            continue;
        }

        JsonObject status = statuses[0];
        const char *line_name = line["name"] | "Unknown";
        const char *description = status["statusSeverityDescription"] | "Status unavailable";
        const int severity = status["statusSeverity"] | 0;
        const bool disrupted = strcmp(description, "Good Service") != 0;

        copy_text(fetched[fetched_count].name, sizeof(fetched[fetched_count].name), line_name);
        copy_text(fetched[fetched_count].status, sizeof(fetched[fetched_count].status), description);
        fetched[fetched_count].severity = static_cast<uint8_t>(severity);
        fetched[fetched_count].disrupted = disrupted;
        if (disrupted) {
            ++disruptions;
        }
        ++fetched_count;
    }

    std::sort(fetched, fetched + fetched_count, [](const RawLineStatus &lhs, const RawLineStatus &rhs) {
        if (lhs.disrupted != rhs.disrupted) {
            return lhs.disrupted > rhs.disrupted;
        }
        return compare_text_ignore_case(lhs.name, rhs.name) < 0;
    });

    memset(&snapshot.tfl, 0, sizeof(snapshot.tfl));
    snapshot.tfl.state = brief::kDataStateLive;
    snapshot.tfl.disrupted_count = disruptions;

    for (size_t i = 0; i < fetched_count && snapshot.tfl.line_count < brief::kMaxTflLines; ++i) {
        brief::TflLineEntry &entry = snapshot.tfl.lines[snapshot.tfl.line_count];
        entry.available = 1;
        entry.disrupted = fetched[i].disrupted ? 1 : 0;
        entry.severity = map_tfl_severity(fetched[i].severity, fetched[i].disrupted);
        copy_text(entry.line_name, sizeof(entry.line_name), fetched[i].name);
        copy_text(entry.status, sizeof(entry.status), fetched[i].status);
        ++snapshot.tfl.line_count;
    }

    set_updated_now(snapshot.tfl.updated_hhmm, sizeof(snapshot.tfl.updated_hhmm));
    if (disruptions == 0) {
        snprintf(snapshot.tfl.summary_text, sizeof(snapshot.tfl.summary_text), "TfL: %u lines shown | Good service", snapshot.tfl.line_count);
    } else {
        snprintf(snapshot.tfl.summary_text, sizeof(snapshot.tfl.summary_text), "TfL: %u disrupted | %u lines shown", disruptions, snapshot.tfl.line_count);
    }
    return true;
}

bool fetch_news()
{
    WiFiClientSecure client;
    if (!configure_tls_client(client)) {
        snapshot.news.state = brief::kDataStateError;
        snapshot.news.headline_count = 0;
        return false;
    }

    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);

    if (!http.begin(client, C6_WORKER_NEWS_RSS_URL)) {
        snapshot.news.state = brief::kDataStateError;
        snapshot.news.headline_count = 0;
        return false;
    }

    const int http_code = http.GET();
    if (http_code != HTTP_CODE_OK) {
        http.end();
        log_http_failure("news", http_code, String());
        snapshot.news.state = brief::kDataStateError;
        snapshot.news.headline_count = 0;
        return false;
    }

    const int payload_size = http.getSize();
    if (payload_size > 0 && static_cast<size_t>(payload_size) > kNewsMaxPayloadBytes) {
        http.end();
        snapshot.news.state = brief::kDataStateError;
        snapshot.news.headline_count = 0;
        return false;
    }

    const String payload = http.getString();
    http.end();
    if (payload.length() == 0) {
        snapshot.news.state = brief::kDataStateError;
        snapshot.news.headline_count = 0;
        return false;
    }

    String lower_payload = payload;
    lower_payload.toLowerCase();

    memset(&snapshot.news, 0, sizeof(snapshot.news));
    snapshot.news.state = brief::kDataStateLive;

    int search_from = 0;
    while (snapshot.news.headline_count < kRawHeadlineLimit) {
        String item_block;
        if (!extract_item_block(payload, lower_payload, &search_from, &item_block)) {
            break;
        }

        String lower_item = item_block;
        lower_item.toLowerCase();

        const String title = extract_tag_value(item_block, lower_item, "title");
        String description = extract_tag_value(item_block, lower_item, "description");
        if (description.length() == 0) {
            description = extract_tag_value(item_block, lower_item, "summary");
        }
        if (title.length() == 0) {
            continue;
        }

        const uint8_t index = snapshot.news.headline_count;
        brief::HeadlineEntry &entry = snapshot.news.headlines[index];
        entry.available = 1;
        entry.priority = static_cast<uint8_t>(brief::kMaxHeadlines - index);
        copy_text(entry.source, sizeof(entry.source), "BBC News");
        copy_text(entry.title, sizeof(entry.title), title.c_str());
        copy_text(entry.summary, sizeof(entry.summary), description.length() > 0 ? description.c_str() : "Headline available");
        copy_text(entry.published_hhmm, sizeof(entry.published_hhmm), "--:--");
        ++snapshot.news.headline_count;
    }

    if (snapshot.news.headline_count == 0) {
        snapshot.news.state = brief::kDataStateError;
        return false;
    }

    set_updated_now(snapshot.news.updated_hhmm, sizeof(snapshot.news.updated_hhmm));
    return true;
}

void schedule_after_result(WorkerDomain domain, bool ok)
{
    const uint32_t now = millis();
    DomainSchedule &schedule = domain_schedules[domain];
    if (ok) {
        schedule.failures = 0;
        schedule.last_success_ms = now;
    } else if (schedule.failures < 250) {
        ++schedule.failures;
    }

    switch (domain) {
        case kWorkerDomainWeather:
            schedule.next_due_ms = now + (ok ? kWeatherRefreshMs : kWeatherRetryMs);
            break;
        case kWorkerDomainTfl:
            schedule.next_due_ms = now + (ok ? kTflRefreshMs : kTflRetryMs);
            break;
        case kWorkerDomainNews:
            schedule.next_due_ms = now + (ok ? kNewsRefreshMs : kNewsRetryMs);
            break;
        default:
            break;
    }
}

bool fetch_domain(WorkerDomain domain)
{
    switch (domain) {
        case kWorkerDomainWeather:
            snapshot.weather.now.state = brief::kDataStateLoading;
            return fetch_weather();
        case kWorkerDomainTfl:
            snapshot.tfl.state = brief::kDataStateLoading;
            return fetch_tfl();
        case kWorkerDomainNews:
            snapshot.news.state = brief::kDataStateLoading;
            return fetch_news();
        default:
            return false;
    }
}

void request_domain_now(brief::Domain domain)
{
    const uint32_t now = millis();
    switch (domain) {
        case brief::kDomainWeather:
            domain_schedules[kWorkerDomainWeather].next_due_ms = now;
            break;
        case brief::kDomainTfl:
            domain_schedules[kWorkerDomainTfl].next_due_ms = now;
            break;
        case brief::kDomainNews:
            domain_schedules[kWorkerDomainNews].next_due_ms = now;
            break;
        default:
            domain_schedules[kWorkerDomainWeather].next_due_ms = now;
            domain_schedules[kWorkerDomainTfl].next_due_ms = now + kDomainSpacingMs;
            domain_schedules[kWorkerDomainNews].next_due_ms = now + (2U * kDomainSpacingMs);
            break;
    }
}

void reset_control_parser()
{
    control_state = kControlSync;
    control_sync_window = 0;
    control_header_index = 0;
    control_payload_index = 0;
    memset(control_header_bytes, 0, sizeof(control_header_bytes));
    memset(&control_header, 0, sizeof(control_header));
}

bool validate_control_header()
{
    return control_header.magic == brief_transport::kFrameMagic &&
           control_header.version == brief_transport::kFrameVersion &&
           control_header.type == static_cast<uint8_t>(brief::kMsgRefreshRequest) &&
           control_header.payload_size == sizeof(brief::RefreshRequest) &&
           control_header.payload_size <= sizeof(control_payload);
}

void handle_control_payload()
{
    if (brief_transport::crc32(control_payload, control_header.payload_size) != control_header.payload_crc32) {
        return;
    }

    brief::RefreshRequest request = {};
    memcpy(&request, control_payload, sizeof(request));
    if (request.header.version != brief::kProtocolVersion ||
        request.header.type != static_cast<uint8_t>(brief::kMsgRefreshRequest) ||
        request.header.payload_size != sizeof(brief::RefreshRequest) - sizeof(brief::PacketHeader)) {
        return;
    }

    request_domain_now(static_cast<brief::Domain>(request.domain));
    last_domain_fetch_ms = 0;
}

void consume_control_byte(uint8_t byte)
{
    switch (control_state) {
        case kControlSync:
            control_sync_window = (control_sync_window >> 8U) | (static_cast<uint32_t>(byte) << 24U);
            if (control_sync_window == brief_transport::kFrameMagic) {
                memcpy(control_header_bytes, &brief_transport::kFrameMagic, sizeof(brief_transport::kFrameMagic));
                control_header_index = sizeof(brief_transport::kFrameMagic);
                control_state = kControlHeader;
            }
            break;

        case kControlHeader:
            control_header_bytes[control_header_index++] = byte;
            if (control_header_index < sizeof(control_header_bytes)) {
                break;
            }
            memcpy(&control_header, control_header_bytes, sizeof(control_header));
            if (!validate_control_header()) {
                reset_control_parser();
                break;
            }
            control_payload_index = 0;
            control_state = kControlPayload;
            break;

        case kControlPayload:
            control_payload[control_payload_index++] = byte;
            if (control_payload_index < control_header.payload_size) {
                break;
            }
            handle_control_payload();
            reset_control_parser();
            break;
    }
}

void poll_control_frames()
{
    if (transport_stream == nullptr) {
        return;
    }

    uint16_t consumed = 0;
    while (transport_stream->available() > 0 && consumed < 256) {
        const int next = transport_stream->read();
        if (next < 0) {
            break;
        }
        consume_control_byte(static_cast<uint8_t>(next));
        ++consumed;
    }
}

bool advance_domain_scheduler()
{
    if (!wifi_is_ready()) {
        return false;
    }
    if (millis() - last_domain_fetch_ms < kDomainSpacingMs) {
        return false;
    }

    const uint32_t now = millis();
    for (uint8_t i = 0; i < kWorkerDomainCount; ++i) {
        const WorkerDomain domain = static_cast<WorkerDomain>(i);
        const brief::Domain protocol_domain =
            domain == kWorkerDomainWeather ? brief::kDomainWeather :
            domain == kWorkerDomainTfl ? brief::kDomainTfl :
            brief::kDomainNews;
        if (!network_mode_allows_domain(protocol_domain)) {
            continue;
        }
        if (now < domain_schedules[i].next_due_ms) {
            continue;
        }

        if (kWorkerDebugSerial) {
            Serial.printf("[c6-debug] t=%lu fetch start domain=%s\n",
                          static_cast<unsigned long>(millis()),
                          worker_domain_to_text(domain));
        }
        const bool ok = fetch_domain(domain);
        schedule_after_result(domain, ok);
        last_domain_fetch_ms = millis();
        if (kWorkerDebugSerial) {
            Serial.printf("[c6-debug] t=%lu fetch result domain=%s ok=%u next_due=%lu failures=%u\n",
                          static_cast<unsigned long>(last_domain_fetch_ms),
                          worker_domain_to_text(domain),
                          ok ? 1U : 0U,
                          static_cast<unsigned long>(domain_schedules[i].next_due_ms),
                          static_cast<unsigned>(domain_schedules[i].failures));
        }
        return true;
    }

    return false;
}

}  // namespace

void setup()
{
    Serial.begin(kDebugSerialBaud);
    delay(200);
    if (kWorkerDebugSerial) {
        Serial.printf(
            "[c6-debug] boot transport=%s binary_stream=%u debug=%u configured=%u network_mode=%u\n",
            transport_to_text(),
            static_cast<unsigned>(C6_WORKER_ENABLE_BINARY_SNAPSHOT_STREAM),
            kWorkerDebugSerial ? 1U : 0U,
            wifi_is_configured() ? 1U : 0U,
            static_cast<unsigned>(C6_WORKER_NETWORK_MODE)
        );
    }
    begin_transport();
    memset(&snapshot, 0, sizeof(snapshot));
    copy_text(snapshot.weather.now.location, sizeof(snapshot.weather.now.location), C6_WORKER_LOCATION_NAME);
    reset_domain_state_for_disconnect();
    populate_system_status();
    begin_wifi_if_needed();
    log_worker_debug("setup complete", true);
}

void loop()
{
    poll_control_frames();
    update_wifi_state();
    populate_system_status();
    advance_domain_scheduler();

    if (millis() - last_status_publish_ms >= kStatusPublishMs) {
        last_status_publish_ms = millis();
        publish_snapshot();
    }

    log_worker_debug("heartbeat");
    delay(50);
}
