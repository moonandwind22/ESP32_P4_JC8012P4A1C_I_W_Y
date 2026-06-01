#include "tfl_service.h"

#include <algorithm>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "service_support.h"

namespace {

constexpr uint32_t kTflRefreshMs = 2UL * 60UL * 1000UL;
constexpr uint32_t kTflRetryMs = 45UL * 1000UL;
constexpr size_t kMaxFetchedLines = 24;
constexpr size_t kTflMaxPayloadBytes = 48UL * 1024UL;
constexpr char kServiceTag[] = "tfl";

struct RawLineStatus {
    char name[32];
    char status[48];
    uint8_t severity;
    bool disrupted;
};

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

void clear_line(TflLineStatus *line)
{
    if (line == nullptr) {
        return;
    }

    memset(line, 0, sizeof(TflLineStatus));
}

void set_line(TflLineStatus *dest, const RawLineStatus &src)
{
    if (dest == nullptr) {
        return;
    }

    dest->available = true;
    dest->disrupted = src.disrupted;
    dest->severity = src.severity;
    service_copy_text(dest->name, sizeof(dest->name), src.name);
    service_copy_text(dest->status, sizeof(dest->status), src.status);
}

bool line_exists(const TflData &data, const char *name)
{
    for (uint8_t i = 0; i < data.line_count; ++i) {
        if (text_equals_ignore_case(data.lines[i].name, name)) {
            return true;
        }
    }
    return false;
}

void push_line(TflData &data, const RawLineStatus &line)
{
    if (data.line_count >= kTflVisibleLines || line_exists(data, line.name)) {
        return;
    }

    set_line(&data.lines[data.line_count], line);
    data.line_count++;
}

}  // namespace

TflService::TflService()
    : mutex_(nullptr),
      refresh_requested_(false),
      next_update_ms_(0),
      revision_(0),
      data_{} {}

void TflService::init()
{
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }

    TflData initial = {};
    initial.valid = false;
    initial.loading = false;
    initial.stale = false;
    initial.line_count = 0;
    service_copy_text(initial.summary, sizeof(initial.summary), "TfL line status");
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

void TflService::request_refresh()
{
    refresh_requested_ = true;
    set_loading_state_();
}

void TflService::restore_cached(const TflData &cached)
{
    if (!cached.valid) {
        return;
    }

    TflData restored = cached;
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

void TflService::snapshot(TflData *out) const
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

uint32_t TflService::revision() const
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        const uint32_t value = revision_;
        xSemaphoreGive(mutex_);
        return value;
    }

    return revision_;
}

bool TflService::due(uint32_t now_ms) const
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        const bool value = refresh_requested_ || service_time_reached(now_ms, next_update_ms_);
        xSemaphoreGive(mutex_);
        return value;
    }

    return refresh_requested_ || service_time_reached(now_ms, next_update_ms_);
}

void TflService::update()
{
    const uint32_t now = millis();
    if (!refresh_requested_ && !service_time_reached(now, next_update_ms_)) {
        return;
    }

    refresh_requested_ = false;
    set_loading_state_();

    if (!service_network_ready()) {
        apply_failure_("Waiting for Wi-Fi");
        next_update_ms_ = now + kTflRetryMs;
        return;
    }

    TflData fresh = {};
    if (fetch_(&fresh)) {
        apply_success_(fresh);
        next_update_ms_ = now + kTflRefreshMs;
    } else {
        next_update_ms_ = now + kTflRetryMs;
    }
}

bool TflService::fetch_(TflData *out)
{
    if (out == nullptr) {
        apply_failure_("TfL output missing");
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

    String url = "https://api.tfl.gov.uk/Line/Mode/tube,dlr,overground,elizabeth-line,tram/Status?detail=false";
    if (strlen(LONDONBRIEF_TFL_APP_KEY) > 0) {
        url += "&app_key=";
        url += LONDONBRIEF_TFL_APP_KEY;
    }

    service_log_fetch_start(kServiceTag, strlen(LONDONBRIEF_TFL_APP_KEY) > 0 ? "api.tfl.gov.uk line status | key=present" : "api.tfl.gov.uk line status | key=empty");
    if (!http.begin(client, url)) {
        service_report_network_fault(kServiceTag, -1001);
        service_log_parse_failure(kServiceTag, "HTTP begin failed");
        apply_failure_("TfL connection failed");
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
        apply_failure_("TfL request failed");
        return false;
    }

    if (payload_size > 0 && static_cast<size_t>(payload_size) > kTflMaxPayloadBytes) {
        http.end();
        apply_failure_("TfL payload too large");
        return false;
    }

    const String payload = http.getString();
    http.end();
    service_log_payload_result(kServiceTag, payload.length());
    if (payload.length() == 0) {
        apply_failure_("TfL payload empty");
        return false;
    }
    if (payload.length() > kTflMaxPayloadBytes) {
        apply_failure_("TfL payload too large");
        return false;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(
        doc,
        payload,
        DeserializationOption::NestingLimit(16)
    );
    if (error || !doc.is<JsonArray>()) {
        service_log_parse_failure(kServiceTag, error ? error.c_str() : "TfL payload is not an array");
        apply_failure_("TfL parse failed");
        return false;
    }

    RawLineStatus fetched[kMaxFetchedLines] = {};
    size_t fetched_count = 0;
    uint8_t disruptions = 0;

    for (JsonObject line : doc.as<JsonArray>()) {
        if (fetched_count >= kMaxFetchedLines) {
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

        service_copy_text(fetched[fetched_count].name, sizeof(fetched[fetched_count].name), line_name);
        service_copy_text(fetched[fetched_count].status, sizeof(fetched[fetched_count].status), description);
        fetched[fetched_count].severity = static_cast<uint8_t>(severity);
        fetched[fetched_count].disrupted = disrupted;
        fetched_count++;

        if (disrupted) {
            disruptions++;
        }
    }

    TflData fresh = {};
    fresh.valid = true;
    fresh.loading = false;
    fresh.stale = false;
    service_set_updated_now(fresh.updated, sizeof(fresh.updated));
    service_copy_text(fresh.status, sizeof(fresh.status), "Live");
    fresh.error[0] = '\0';

    std::sort(fetched, fetched + fetched_count, [](const RawLineStatus &lhs, const RawLineStatus &rhs) {
        if (lhs.disrupted != rhs.disrupted) {
            return lhs.disrupted > rhs.disrupted;
        }

        return compare_text_ignore_case(lhs.name, rhs.name) < 0;
    });

    for (size_t i = 0; i < fetched_count; ++i) {
        push_line(fresh, fetched[i]);
    }

    if (disruptions == 0) {
        snprintf(fresh.summary, sizeof(fresh.summary), "TfL: %u lines shown | Good service", static_cast<unsigned>(fresh.line_count));
    } else {
        snprintf(
            fresh.summary,
            sizeof(fresh.summary),
            "TfL: %u disrupted | %u lines shown",
            static_cast<unsigned>(disruptions),
            static_cast<unsigned>(fresh.line_count)
        );
    }

    *out = fresh;
    char success_text[96];
    snprintf(
        success_text,
        sizeof(success_text),
        "lines=%u disruptions=%u",
        static_cast<unsigned>(fresh.line_count),
        static_cast<unsigned>(disruptions)
    );
    service_log_success(kServiceTag, success_text);
    return true;
}

void TflService::set_loading_state_()
{
    if (mutex_ == nullptr) {
        data_.loading = true;
        service_copy_text(data_.status, sizeof(data_.status), data_.valid ? "Refreshing" : "Loading");
        ++revision_;
        return;
    }

    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return;
    }

    data_.loading = true;
    service_copy_text(data_.status, sizeof(data_.status), data_.valid ? "Refreshing" : "Loading");
    ++revision_;
    xSemaphoreGive(mutex_);
}

void TflService::apply_success_(const TflData &fresh_data)
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_ = fresh_data;
        ++revision_;
        xSemaphoreGive(mutex_);
        return;
    }

    data_ = fresh_data;
    ++revision_;
}

void TflService::apply_failure_(const char *message)
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_.loading = false;
        service_copy_text(data_.error, sizeof(data_.error), message);

        if (data_.valid) {
            data_.stale = true;
            service_copy_text(data_.status, sizeof(data_.status), "Stale");
        } else {
            data_.stale = false;
            service_copy_text(data_.summary, sizeof(data_.summary), "TfL line status");
            service_copy_text(data_.updated, sizeof(data_.updated), "--:--");
            service_copy_text(data_.status, sizeof(data_.status), "Offline");
            data_.line_count = 0;
        }

        ++revision_;
        xSemaphoreGive(mutex_);
        return;
    }

    service_copy_text(data_.error, sizeof(data_.error), message);
    data_.loading = false;
    ++revision_;
}
