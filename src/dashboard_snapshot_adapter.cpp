#include "dashboard_snapshot_adapter.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "service_support.h"

namespace {

brief::DataState map_state(bool valid, bool loading, bool stale)
{
    if (loading) {
        return brief::kDataStateLoading;
    }
    if (!valid) {
        return brief::kDataStateError;
    }
    if (stale) {
        return brief::kDataStateStale;
    }
    return brief::kDataStateLive;
}

brief::TflSeverity map_tfl_severity(uint8_t severity, bool disrupted)
{
    if (!disrupted) {
        return brief::kTflSeverityGood;
    }
    if (severity >= 4) {
        return brief::kTflSeveritySevere;
    }
    if (severity == 3) {
        return brief::kTflSeverityMajor;
    }
    if (severity == 2) {
        return brief::kTflSeverityMinor;
    }
    return brief::kTflSeverityInfo;
}

int16_t parse_temperature_tenths(const char *text)
{
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }

    char *end_ptr = nullptr;
    const long parsed = strtol(text, &end_ptr, 10);
    if (end_ptr == text) {
        return 0;
    }
    return static_cast<int16_t>(parsed * 10L);
}

void set_packet_header(brief::PacketHeader *header, brief::MessageType type)
{
    if (header == nullptr) {
        return;
    }

    memset(header, 0, sizeof(*header));
    header->version = brief::kProtocolVersion;
    header->type = static_cast<uint8_t>(type);
}

}  // namespace

void build_dashboard_snapshot(
    const DashboardSystemContext &system,
    const WeatherData &weather,
    const TflData &tfl,
    const NewsData &news,
    brief::DashboardSnapshot *out
)
{
    if (out == nullptr) {
        return;
    }

    (void)system.brightness_percent;
    (void)system.dark_mode;

    memset(out, 0, sizeof(*out));
    set_packet_header(&out->header, brief::kMsgDashboardSnapshot);

    set_packet_header(&out->system.header, brief::kMsgSystemStatus);
    out->system.network_mode = system.network_mode;
    out->system.wifi_connected = system.wifi_connected ? 1 : 0;
    out->system.wifi_ready = system.wifi_ready ? 1 : 0;
    out->system.link_healthy = system.wifi_fault_count == 0 ? 1 : 0;
    out->system.fault_count = system.wifi_fault_count;
    snprintf(
        out->system.status_text,
        sizeof(out->system.status_text),
        "%s",
        system.wifi_status_text != nullptr ? system.wifi_status_text :
                                             (system.wifi_configured ? "Wi-Fi offline" : "Wi-Fi not configured")
    );

    out->weather.now.state = static_cast<uint8_t>(map_state(weather.valid, weather.loading, weather.stale));
    out->weather.now.temperature_tenths_c = parse_temperature_tenths(weather.temperature);
    out->weather.now.humidity_percent = weather.humidity_percent;
    out->weather.now.rain_probability_percent = weather.precipitation_peak;
    out->weather.now.wind_kmh_tenths = static_cast<uint16_t>(weather.wind_peak_kmh * 10U);
    out->weather.now.weather_code = weather.current_weather_code;
    out->weather.now.is_day = weather.is_day ? 1 : 0;
    snprintf(out->weather.now.location, sizeof(out->weather.now.location), "%s", LONDONBRIEF_LOCATION_NAME);
    snprintf(out->weather.now.condition, sizeof(out->weather.now.condition), "%s", weather.condition);
    snprintf(out->weather.now.updated_hhmm, sizeof(out->weather.now.updated_hhmm), "%s", weather.updated);
    out->weather.hourly_count = weather.hourly_count;
    out->weather.daily_count = weather.daily_count;
    snprintf(out->weather.next_change_text, sizeof(out->weather.next_change_text), "%s", weather.next_changes);
    snprintf(
        out->weather.summary_text,
        sizeof(out->weather.summary_text),
        "%s",
        weather.valid ? weather.detail : weather.error
    );

    for (uint8_t i = 0; i < weather.hourly_count && i < brief::kMaxHourlyPoints; ++i) {
        snprintf(out->weather.hourly[i].hhmm, sizeof(out->weather.hourly[i].hhmm), "%s", weather.hourly_labels[i]);
        out->weather.hourly[i].temperature_tenths_c = static_cast<int16_t>(weather.hourly_temperatures[i] * 10);
        out->weather.hourly[i].humidity_percent = weather.hourly_humidity[i];
        out->weather.hourly[i].rain_probability_percent = weather.hourly_precipitation[i];
        out->weather.hourly[i].wind_kmh_tenths = static_cast<uint16_t>(weather.hourly_wind_kmh[i] * 10U);
        out->weather.hourly[i].weather_code = weather.hourly_weather_codes[i];
        out->weather.hourly[i].is_day = weather.is_day ? 1 : 0;
    }

    for (uint8_t i = 0; i < weather.daily_count && i < brief::kMaxDailyPoints; ++i) {
        snprintf(out->weather.daily[i].label, sizeof(out->weather.daily[i].label), "%s", weather.daily_labels[i]);
        out->weather.daily[i].min_tenths_c = static_cast<int16_t>(weather.daily_temp_min_c[i] * 10);
        out->weather.daily[i].max_tenths_c = static_cast<int16_t>(weather.daily_temp_max_c[i] * 10);
        out->weather.daily[i].weather_code = weather.daily_weather_codes[i];
        out->weather.daily[i].rain_probability_percent = weather.daily_precipitation_peak[i];
    }

    out->tfl.state = static_cast<uint8_t>(map_state(tfl.valid, tfl.loading, tfl.stale));
    out->tfl.line_count = tfl.line_count;
    snprintf(out->tfl.summary_text, sizeof(out->tfl.summary_text), "%s", tfl.valid ? tfl.summary : tfl.error);
    snprintf(out->tfl.updated_hhmm, sizeof(out->tfl.updated_hhmm), "%s", tfl.updated);
    for (uint8_t i = 0; i < tfl.line_count && i < brief::kMaxTflLines; ++i) {
        out->tfl.lines[i].available = tfl.lines[i].available ? 1 : 0;
        out->tfl.lines[i].disrupted = tfl.lines[i].disrupted ? 1 : 0;
        out->tfl.lines[i].severity = static_cast<uint8_t>(map_tfl_severity(tfl.lines[i].severity, tfl.lines[i].disrupted));
        if (tfl.lines[i].disrupted) {
            out->tfl.disrupted_count = static_cast<uint8_t>(out->tfl.disrupted_count + 1U);
        }
        snprintf(out->tfl.lines[i].line_name, sizeof(out->tfl.lines[i].line_name), "%s", tfl.lines[i].name);
        snprintf(out->tfl.lines[i].status, sizeof(out->tfl.lines[i].status), "%s", tfl.lines[i].status);
    }

    out->news.state = static_cast<uint8_t>(map_state(news.valid, news.loading, news.stale));
    out->news.headline_count = news.headline_count;
    snprintf(out->news.updated_hhmm, sizeof(out->news.updated_hhmm), "%s", news.updated);
    for (uint8_t i = 0; i < news.headline_count && i < brief::kMaxHeadlines && i < 3U; ++i) {
        out->news.headlines[i].available = 1;
        out->news.headlines[i].priority = i == 0 ? 3 : (i == 1 ? 2 : 1);
        snprintf(out->news.headlines[i].title, sizeof(out->news.headlines[i].title), "%s", news.headlines[i]);
        snprintf(out->news.headlines[i].summary, sizeof(out->news.headlines[i].summary), "%s", news.summaries[i]);
    }
}
