#include "calendar_service.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "service_support.h"

namespace {

constexpr uint32_t kCalendarRefreshMs = 12UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kCalendarRetryMs = 5UL * 60UL * 1000UL;
constexpr size_t kCalendarMaxPayloadBytes = 96UL * 1024UL;
constexpr char kServiceTag[] = "calendar";
constexpr char kCalendarRegionKey[] = "england-and-wales";
constexpr char kCalendarRegionName[] = "England & Wales";

bool build_today_key(char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return false;
    }

    time_t now = time(nullptr);
    if (!service_wall_clock_valid(now)) {
        dest[0] = '\0';
        return false;
    }

    struct tm local_time = {};
    localtime_r(&now, &local_time);
    strftime(dest, dest_size, "%Y-%m-%d", &local_time);
    return true;
}

bool parse_iso_date(const char *iso_date, struct tm *out)
{
    if (iso_date == nullptr || out == nullptr) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(iso_date, "%d-%d-%d", &year, &month, &day) != 3) {
        return false;
    }

    struct tm parsed = {};
    parsed.tm_year = year - 1900;
    parsed.tm_mon = month - 1;
    parsed.tm_mday = day;
    parsed.tm_hour = 12;
    parsed.tm_isdst = -1;
    if (mktime(&parsed) < 0) {
        return false;
    }

    *out = parsed;
    return true;
}

void format_event_date(const char *iso_date, char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    struct tm parsed = {};
    if (!parse_iso_date(iso_date, &parsed)) {
        service_copy_text(dest, dest_size, iso_date != nullptr ? iso_date : "--");
        return;
    }

    strftime(dest, dest_size, "%a %d %b", &parsed);
}

int32_t civil_day_number(int year, int month, int day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned moy = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned doy = (153U * moy + 2U) / 5U + static_cast<unsigned>(day - 1);
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void format_relative_date(const char *iso_date, char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    time_t now = time(nullptr);
    if (!service_wall_clock_valid(now)) {
        service_copy_text(dest, dest_size, "Date pending");
        return;
    }

    struct tm event_tm = {};
    if (!parse_iso_date(iso_date, &event_tm)) {
        service_copy_text(dest, dest_size, "Date pending");
        return;
    }

    struct tm now_tm = {};
    localtime_r(&now, &now_tm);
    now_tm.tm_hour = 12;
    now_tm.tm_min = 0;
    now_tm.tm_sec = 0;
    now_tm.tm_isdst = -1;

    const long delta_days = static_cast<long>(
        civil_day_number(event_tm.tm_year + 1900, event_tm.tm_mon + 1, event_tm.tm_mday) -
        civil_day_number(now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday));
    if (delta_days <= 0) {
        service_copy_text(dest, dest_size, delta_days == 0 ? "Today" : "Past");
    } else if (delta_days == 1) {
        service_copy_text(dest, dest_size, "Tomorrow");
    } else {
        snprintf(dest, dest_size, "In %ld days", delta_days);
    }
}

int find_start_index(JsonArrayConst events, const char *today_key)
{
    if (events.isNull()) {
        return -1;
    }

    if (today_key != nullptr && today_key[0] != '\0') {
        int index = 0;
        for (JsonObjectConst event : events) {
            const char *date = event["date"] | "";
            if (date[0] != '\0' && strcmp(date, today_key) >= 0) {
                return index;
            }
            ++index;
        }
    }

    return events.size() > 0 ? 0 : -1;
}

}  // namespace

CalendarService::CalendarService()
    : mutex_(nullptr),
      refresh_requested_(false),
      next_update_ms_(0),
      revision_(0),
      data_{} {}

void CalendarService::init()
{
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }

    CalendarData initial = {};
    initial.valid = false;
    initial.loading = false;
    initial.stale = false;
    initial.event_count = 0;
    service_copy_text(initial.region, sizeof(initial.region), kCalendarRegionName);
    service_copy_text(initial.summary, sizeof(initial.summary), "Upcoming bank holidays");
    service_copy_text(initial.updated, sizeof(initial.updated), "--:--");
    service_copy_text(initial.status, sizeof(initial.status), "Waiting");
    service_copy_text(initial.error, sizeof(initial.error), "Waiting for Wi-Fi");

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_ = initial;
        ++revision_;
        xSemaphoreGive(mutex_);
    } else {
        data_ = initial;
        ++revision_;
    }

    next_update_ms_ = 0;
    refresh_requested_ = true;
}

void CalendarService::request_refresh()
{
    refresh_requested_ = true;
    set_loading_state_();
}

void CalendarService::restore_cached(const CalendarData &cached)
{
    if (!cached.valid) {
        return;
    }

    CalendarData restored = cached;
    restored.loading = false;
    restored.stale = true;
    service_copy_text(restored.status, sizeof(restored.status), "Cached");
    if (restored.error[0] == '\0') {
        service_copy_text(restored.error, sizeof(restored.error), "Restored from cache");
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_ = restored;
        ++revision_;
        xSemaphoreGive(mutex_);
    } else {
        data_ = restored;
        ++revision_;
    }

    refresh_requested_ = true;
    next_update_ms_ = 0;
}

void CalendarService::snapshot(CalendarData *out) const
{
    if (out == nullptr) {
        return;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        *out = data_;
        xSemaphoreGive(mutex_);
        return;
    }

    *out = data_;
}

uint32_t CalendarService::revision() const
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        const uint32_t value = revision_;
        xSemaphoreGive(mutex_);
        return value;
    }

    return revision_;
}

bool CalendarService::due(uint32_t now_ms) const
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        const bool value = refresh_requested_ || service_time_reached(now_ms, next_update_ms_);
        xSemaphoreGive(mutex_);
        return value;
    }

    return refresh_requested_ || service_time_reached(now_ms, next_update_ms_);
}

void CalendarService::update()
{
    const uint32_t now = millis();
    if (!refresh_requested_ && !service_time_reached(now, next_update_ms_)) {
        return;
    }

    refresh_requested_ = false;
    set_loading_state_();

    if (!service_network_ready()) {
        apply_failure_("Waiting for Wi-Fi");
        next_update_ms_ = now + kCalendarRetryMs;
        return;
    }

    CalendarData fresh = {};
    if (fetch_(&fresh)) {
        apply_success_(fresh);
        next_update_ms_ = now + kCalendarRefreshMs;
    } else {
        next_update_ms_ = now + kCalendarRetryMs;
    }
}

bool CalendarService::fetch_(CalendarData *out)
{
    if (out == nullptr) {
        apply_failure_("Calendar output missing");
        return false;
    }

    WiFiClientSecure client;
    if (!service_configure_tls_client(client, LONDONBRIEF_TLS_ROOT_CA, LONDONBRIEF_ALLOW_INSECURE_TLS != 0)) {
        apply_failure_("TLS root CA missing");
        return false;
    }

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);

    const String url = "https://www.gov.uk/bank-holidays.json";
    service_log_fetch_start(kServiceTag, "gov.uk bank holidays");
    if (!http.begin(client, url)) {
        service_report_network_fault(kServiceTag, -1001);
        service_log_parse_failure(kServiceTag, "HTTP begin failed");
        apply_failure_("Calendar connection failed");
        return false;
    }
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");

    const int http_code = http.GET();
    const int payload_size = http.getSize();
    service_log_http_result(kServiceTag, http_code, payload_size);
    if (http_code != HTTP_CODE_OK) {
        if (http_code < 0) {
            service_report_network_fault(kServiceTag, http_code);
        }
        const String body = http.getString();
        http.end();
        service_log_http_failure(kServiceTag, http_code, body);
        apply_failure_("Calendar request failed");
        return false;
    }

    if (payload_size > 0 && static_cast<size_t>(payload_size) > kCalendarMaxPayloadBytes) {
        http.end();
        apply_failure_("Calendar payload too large");
        return false;
    }

    const String payload = http.getString();
    http.end();
    service_log_payload_result(kServiceTag, payload.length());
    if (payload.length() == 0) {
        apply_failure_("Calendar payload empty");
        return false;
    }
    if (payload.length() > kCalendarMaxPayloadBytes) {
        apply_failure_("Calendar payload too large");
        return false;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(
        doc,
        payload,
        DeserializationOption::NestingLimit(16)
    );
    if (error) {
        service_log_parse_failure(kServiceTag, error.c_str());
        apply_failure_("Calendar parse failed");
        return false;
    }

    JsonObject division = doc[kCalendarRegionKey];
    if (division.isNull()) {
        apply_failure_("Calendar division missing");
        return false;
    }

    JsonArrayConst events = division["events"].as<JsonArrayConst>();
    if (events.isNull() || events.size() == 0) {
        apply_failure_("Calendar events missing");
        return false;
    }

    char today_key[16] = {};
    build_today_key(today_key, sizeof(today_key));
    const int start_index = find_start_index(events, today_key);
    if (start_index < 0) {
        apply_failure_("Calendar events unavailable");
        return false;
    }

    CalendarData fresh = {};
    fresh.valid = true;
    fresh.loading = false;
    fresh.stale = false;
    fresh.event_count = 0;
    service_copy_text(fresh.region, sizeof(fresh.region), kCalendarRegionName);
    service_copy_text(fresh.status, sizeof(fresh.status), "Live");
    fresh.error[0] = '\0';
    service_set_updated_now(fresh.updated, sizeof(fresh.updated));

    for (int i = start_index; i < static_cast<int>(events.size()) && fresh.event_count < kCalendarVisibleEvents; ++i) {
        JsonObjectConst event = events[static_cast<size_t>(i)];
        const char *title = event["title"] | "";
        const char *date = event["date"] | "";
        const char *notes = event["notes"] | "";
        if (title[0] == '\0' || date[0] == '\0') {
            continue;
        }

        CalendarEntry &entry = fresh.events[fresh.event_count];
        entry.available = true;
        service_copy_text(entry.title, sizeof(entry.title), title);
        service_copy_text(entry.iso_date, sizeof(entry.iso_date), date);
        format_event_date(date, entry.date_text, sizeof(entry.date_text));
        format_relative_date(date, entry.relative_text, sizeof(entry.relative_text));
        service_copy_text(entry.notes, sizeof(entry.notes), notes[0] != '\0' ? notes : "National bank holiday");
        ++fresh.event_count;
    }

    if (fresh.event_count == 0) {
        apply_failure_("Calendar events missing");
        return false;
    }

    const CalendarEntry &next_event = fresh.events[0];
    snprintf(
        fresh.summary,
        sizeof(fresh.summary),
        "Next: %s on %s",
        next_event.title,
        next_event.date_text
    );

    *out = fresh;
    char success_text[96];
    snprintf(success_text, sizeof(success_text), "Loaded %u bank holidays", static_cast<unsigned>(fresh.event_count));
    service_log_success(kServiceTag, success_text);
    return true;
}

void CalendarService::set_loading_state_()
{
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        data_.loading = true;
        service_copy_text(data_.status, sizeof(data_.status), "Refreshing");
        return;
    }

    data_.loading = true;
    service_copy_text(data_.status, sizeof(data_.status), "Refreshing");
    ++revision_;
    xSemaphoreGive(mutex_);
}

void CalendarService::apply_success_(const CalendarData &fresh_data)
{
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        data_ = fresh_data;
        ++revision_;
        return;
    }

    data_ = fresh_data;
    ++revision_;
    xSemaphoreGive(mutex_);
}

void CalendarService::apply_failure_(const char *message)
{
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        data_.valid = false;
        data_.loading = false;
        data_.stale = false;
        service_copy_text(data_.status, sizeof(data_.status), "Offline");
        service_copy_text(data_.error, sizeof(data_.error), message);
        ++revision_;
        return;
    }

    data_.valid = false;
    data_.loading = false;
    data_.stale = false;
    service_copy_text(data_.status, sizeof(data_.status), "Offline");
    service_copy_text(data_.error, sizeof(data_.error), message);
    ++revision_;
    xSemaphoreGive(mutex_);
}
