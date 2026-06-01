#ifndef BRIEF_PROTOCOL_H
#define BRIEF_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

// Shared C6/P4 protocol.
//
// C6 owns networking/API calls and emits compact, UI-ready snapshots.
// P4 owns touch, rendering, settings, and power controls.
//
// These structs intentionally remain fixed-size so snapshots can travel over a
// small framed transport without heap allocation. Keep the static assertions in
// place: if a future edit changes the wire layout, both firmwares should fail at
// build time instead of silently decoding the wrong fields.

namespace brief {

constexpr uint16_t kProtocolVersion = 1;

constexpr uint8_t kMaxHourlyPoints = 12;
constexpr uint8_t kMaxDailyPoints = 5;
constexpr uint8_t kMaxTflLines = 24;
constexpr uint8_t kMaxHeadlines = 5;

enum MessageType : uint8_t {
    kMsgUnknown = 0,
    kMsgHello = 1,
    kMsgSystemStatus = 2,
    kMsgDashboardSnapshot = 3,
    kMsgRefreshRequest = 4,
    kMsgConfigUpdate = 5,
    kMsgAck = 6,
};

enum Domain : uint8_t {
    kDomainAll = 0,
    kDomainWeather = 1,
    kDomainTfl = 2,
    kDomainNews = 3,
    kDomainSystem = 4,
    kDomainCalendar = 5,
};

enum NetworkMode : uint8_t {
    kNetworkModeAutomatic = 0,
    kNetworkModeManualRefresh = 1,
    kNetworkModeWeatherOnly = 2,
    kNetworkModeTflOnly = 3,
    kNetworkModeNewsOnly = 4,
    kNetworkModeOffline = 5,
};

enum DataState : uint8_t {
    kDataStateWaiting = 0,
    kDataStateLoading = 1,
    kDataStateLive = 2,
    kDataStateStale = 3,
    kDataStateError = 4,
};

enum TflSeverity : uint8_t {
    kTflSeverityGood = 0,
    kTflSeverityInfo = 1,
    kTflSeverityMinor = 2,
    kTflSeverityMajor = 3,
    kTflSeveritySevere = 4,
};

enum WeatherTrend : uint8_t {
    kWeatherTrendSteady = 0,
    kWeatherTrendRising = 1,
    kWeatherTrendFalling = 2,
};

struct PacketHeader {
    uint16_t version;
    uint8_t type;
    uint8_t reserved0;
    uint32_t payload_size;
    uint32_t sequence;
    uint32_t uptime_ms;
};

struct HelloMessage {
    PacketHeader header;
    char node_name[16];
    uint8_t protocol_version_major;
    uint8_t protocol_version_minor;
    uint8_t transport_hint;
    uint8_t reserved1;
};

struct SystemStatus {
    PacketHeader header;
    uint8_t network_mode;
    uint8_t wifi_connected;
    uint8_t wifi_ready;
    uint8_t link_healthy;
    int8_t wifi_rssi_dbm;
    uint8_t fault_count;
    uint8_t cooldown_active;
    uint8_t reserved0;
    uint32_t cooldown_remaining_ms;
    uint32_t last_refresh_ms;
    char status_text[64];
};

struct WeatherNow {
    uint8_t state;
    uint8_t is_day;
    uint8_t rain_probability_percent;
    uint8_t humidity_percent;
    int16_t temperature_tenths_c;
    int16_t feels_like_tenths_c;
    uint16_t wind_kmh_tenths;
    uint8_t weather_code;
    uint8_t trend;
    char location[24];
    char condition[24];
    char updated_hhmm[8];
};

struct WeatherHourlyPoint {
    char hhmm[6];
    int16_t temperature_tenths_c;
    uint8_t humidity_percent;
    uint8_t rain_probability_percent;
    uint16_t wind_kmh_tenths;
    uint8_t weather_code;
    uint8_t is_day;
};

struct WeatherDailyPoint {
    char label[12];
    int16_t min_tenths_c;
    int16_t max_tenths_c;
    uint8_t weather_code;
    uint8_t rain_probability_percent;
};

struct WeatherPayload {
    WeatherNow now;
    uint8_t hourly_count;
    uint8_t daily_count;
    uint8_t reserved0;
    uint8_t reserved1;
    WeatherHourlyPoint hourly[kMaxHourlyPoints];
    WeatherDailyPoint daily[kMaxDailyPoints];
    char next_change_text[64];
    char summary_text[96];
};

struct TflLineEntry {
    uint8_t available;
    uint8_t disrupted;
    uint8_t severity;
    uint8_t reserved0;
    char line_name[24];
    char status[40];
};

struct TflPayload {
    uint8_t state;
    uint8_t line_count;
    uint8_t disrupted_count;
    uint8_t reserved0;
    TflLineEntry lines[kMaxTflLines];
    char summary_text[96];
    char updated_hhmm[8];
};

struct HeadlineEntry {
    uint8_t available;
    uint8_t priority;
    uint8_t reserved0;
    uint8_t reserved1;
    char source[20];
    char title[96];
    char summary[160];
    char published_hhmm[8];
};

struct NewsPayload {
    uint8_t state;
    uint8_t headline_count;
    uint8_t reserved0;
    uint8_t reserved1;
    HeadlineEntry headlines[kMaxHeadlines];
    char updated_hhmm[8];
};

struct DashboardSnapshot {
    PacketHeader header;
    SystemStatus system;
    WeatherPayload weather;
    TflPayload tfl;
    NewsPayload news;
};

struct RefreshRequest {
    PacketHeader header;
    uint8_t domain;
    uint8_t force;
    uint8_t reserved0;
    uint8_t reserved1;
};

struct ConfigUpdate {
    PacketHeader header;
    uint8_t network_mode;
    uint8_t dark_mode;
    uint8_t brightness_percent;
    uint8_t reserved0;
};

constexpr size_t kPacketHeaderWireSize = 16;
constexpr size_t kSystemStatusWireSize = 96;
constexpr size_t kWeatherPayloadWireSize = 490;
constexpr size_t kTflPayloadWireSize = 1740;
constexpr size_t kNewsPayloadWireSize = 1452;
constexpr size_t kDashboardSnapshotWireSize = 3796;
constexpr size_t kRefreshRequestWireSize = 20;

static_assert(sizeof(PacketHeader) == kPacketHeaderWireSize, "PacketHeader wire layout changed");
static_assert(sizeof(SystemStatus) == kSystemStatusWireSize, "SystemStatus wire layout changed");
static_assert(sizeof(WeatherPayload) == kWeatherPayloadWireSize, "WeatherPayload wire layout changed");
static_assert(sizeof(TflPayload) == kTflPayloadWireSize, "TflPayload wire layout changed");
static_assert(sizeof(NewsPayload) == kNewsPayloadWireSize, "NewsPayload wire layout changed");
static_assert(sizeof(DashboardSnapshot) == kDashboardSnapshotWireSize, "DashboardSnapshot wire layout changed");
static_assert(sizeof(RefreshRequest) == kRefreshRequestWireSize, "RefreshRequest wire layout changed");
static_assert(offsetof(DashboardSnapshot, system) == sizeof(PacketHeader), "DashboardSnapshot header offset changed");

}  // namespace brief

#endif
