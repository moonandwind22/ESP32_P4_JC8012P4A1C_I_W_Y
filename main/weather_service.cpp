#include "weather_service.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "service_support.h"

namespace {

constexpr uint32_t kWeatherRefreshMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kWeatherRetryMs = 60UL * 1000UL;
constexpr size_t kWeatherMaxPayloadBytes = 64UL * 1024UL;
constexpr char kServiceTag[] = "weather";

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
        service_copy_text(dest, dest_size, "--");
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

void build_rain_outlook(const WeatherData &data, char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    dest[0] = '\0';
    if (data.hourly_count == 0) {
        service_copy_text(dest, dest_size, "Rain timing unavailable");
        return;
    }

    uint8_t peak = 0;
    int first_likely_index = -1;
    for (uint8_t i = 0; i < data.hourly_count; ++i) {
        const uint8_t probability = data.hourly_precipitation[i];
        if (probability > peak) {
            peak = probability;
        }
        if (first_likely_index < 0 && probability >= 40) {
            first_likely_index = i;
        }
    }

    if (first_likely_index == 0) {
        snprintf(dest, dest_size, "Rain risk now (%u%%)", static_cast<unsigned>(data.hourly_precipitation[0]));
        return;
    }

    if (first_likely_index > 0) {
        snprintf(
            dest,
            dest_size,
            "Rain likely %s (%u%%)",
            data.hourly_labels[first_likely_index],
            static_cast<unsigned>(data.hourly_precipitation[first_likely_index])
        );
        return;
    }

    if (peak >= 25) {
        snprintf(dest, dest_size, "Low shower risk, peak %u%%", static_cast<unsigned>(peak));
        return;
    }

    snprintf(dest, dest_size, "Dry for %u hours", static_cast<unsigned>(data.hourly_count));
}

void format_daily_label(const char *iso_date, uint8_t offset, char *dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    if (offset == 0) {
        service_copy_text(dest, dest_size, "Today");
        return;
    }
    if (offset == 1) {
        service_copy_text(dest, dest_size, "Tomorrow");
        return;
    }

    if (iso_date == nullptr || strlen(iso_date) < 10) {
        service_copy_text(dest, dest_size, "Later");
        return;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(iso_date, "%d-%d-%d", &year, &month, &day) != 3) {
        service_copy_text(dest, dest_size, "Later");
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

}  // namespace

WeatherService::WeatherService()
    : mutex_(nullptr),
      refresh_requested_(false),
      next_update_ms_(0),
      revision_(0),
      data_{} {}

void WeatherService::init()
{
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }

    WeatherData initial = {};
    initial.valid = false;
    initial.loading = false;
    initial.stale = false;
    service_copy_text(initial.temperature, sizeof(initial.temperature), "--");
    service_copy_text(initial.condition, sizeof(initial.condition), "Weather");
    service_copy_text(initial.detail, sizeof(initial.detail), "Waiting for Wi-Fi");
    service_copy_text(initial.updated, sizeof(initial.updated), "--:--");
    service_copy_text(initial.status, sizeof(initial.status), "Waiting");
    service_copy_text(initial.error, sizeof(initial.error), "Waiting for Wi-Fi");
    service_copy_text(initial.next_changes, sizeof(initial.next_changes), "Watching for the next change");
    initial.hourly_count = 0;
    initial.humidity_percent = 0;
    initial.precipitation_peak = 0;
    initial.wind_peak_kmh = 0;
    initial.hourly_min_c = 0;
    initial.hourly_max_c = 0;
    initial.daily_count = 0;

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

void WeatherService::request_refresh()
{
    refresh_requested_ = true;
    set_loading_state_();
}

void WeatherService::restore_cached(const WeatherData &cached)
{
    if (!cached.valid) {
        return;
    }

    WeatherData restored = cached;
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

void WeatherService::snapshot(WeatherData *out) const
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

uint32_t WeatherService::revision() const
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        const uint32_t value = revision_;
        xSemaphoreGive(mutex_);
        return value;
    }

    return revision_;
}

bool WeatherService::due(uint32_t now_ms) const
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        const bool value = refresh_requested_ || service_time_reached(now_ms, next_update_ms_);
        xSemaphoreGive(mutex_);
        return value;
    }

    return refresh_requested_ || service_time_reached(now_ms, next_update_ms_);
}

void WeatherService::update()
{
    const uint32_t now = millis();
    if (!refresh_requested_ && !service_time_reached(now, next_update_ms_)) {
        return;
    }

    refresh_requested_ = false;
    set_loading_state_();

    if (!service_network_ready()) {
        Serial.println("[weather] update skipped | waiting for Wi-Fi readiness");
        apply_failure_("Waiting for Wi-Fi");
        next_update_ms_ = now + kWeatherRetryMs;
        return;
    }

    WeatherData fresh = {};
    Serial.println("[weather] fetch start | open-meteo");
    if (fetch_(&fresh)) {
        apply_success_(fresh);
        next_update_ms_ = now + kWeatherRefreshMs;
    } else {
        next_update_ms_ = now + kWeatherRetryMs;
    }
}

bool WeatherService::fetch_(WeatherData *out)
{
    if (out == nullptr) {
        apply_failure_("Weather output missing");
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

    const String url =
        String("https://api.open-meteo.com/v1/forecast?latitude=") + String(LONDONBRIEF_LATITUDE, 4) +
        "&longitude=" + String(LONDONBRIEF_LONGITUDE, 4) +
        "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,is_day,relative_humidity_2m"
        "&hourly=temperature_2m,weather_code,precipitation_probability,relative_humidity_2m,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max"
        "&timezone=Europe%2FLondon&forecast_days=3";

    service_log_fetch_start(kServiceTag, "api.open-meteo.com forecast");
    if (!http.begin(client, url)) {
        service_report_network_fault(kServiceTag, -1001);
        service_log_parse_failure(kServiceTag, "HTTP begin failed");
        apply_failure_("Weather connection failed");
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
        apply_failure_("Weather request failed");
        return false;
    }

    if (payload_size > 0 && static_cast<size_t>(payload_size) > kWeatherMaxPayloadBytes) {
        http.end();
        apply_failure_("Weather payload too large");
        return false;
    }

    const String payload = http.getString();
    http.end();
    service_log_payload_result(kServiceTag, payload.length());
    if (payload.length() == 0) {
        apply_failure_("Weather payload empty");
        return false;
    }
    if (payload.length() > kWeatherMaxPayloadBytes) {
        apply_failure_("Weather payload too large");
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
        apply_failure_("Weather parse failed");
        return false;
    }

    JsonObject current = doc["current"];
    if (current.isNull()) {
        apply_failure_("Weather data missing");
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
        apply_failure_("Weather values missing");
        return false;
    }

    WeatherData fresh = {};
    fresh.valid = true;
    fresh.loading = false;
    fresh.stale = false;
    fresh.is_day = is_day == 1;
    fresh.current_weather_code = static_cast<uint8_t>(constrain(weather_code, 0, 255));
    snprintf(fresh.temperature, sizeof(fresh.temperature), "%.0f C", temperature);
    service_copy_text(fresh.condition, sizeof(fresh.condition), weather_code_to_text(weather_code, is_day == 1));
    snprintf(
        fresh.detail,
        sizeof(fresh.detail),
        "Feels %.0f C  |  Humidity %d%%\nWind %.0f km/h  |  %s",
        apparent,
        humidity,
        wind_speed,
        LONDONBRIEF_LOCATION_NAME
    );
    service_set_updated_now(fresh.updated, sizeof(fresh.updated));
    service_copy_text(fresh.status, sizeof(fresh.status), "Live");
    fresh.error[0] = '\0';
    service_copy_text(fresh.next_changes, sizeof(fresh.next_changes), "Stable for the next few hours");
    fresh.hourly_count = 0;
    fresh.humidity_percent = static_cast<uint8_t>(humidity);
    fresh.precipitation_peak = 0;
    fresh.wind_peak_kmh = static_cast<uint8_t>(lroundf(wind_speed));
    fresh.hourly_min_c = static_cast<int16_t>(lroundf(temperature));
    fresh.hourly_max_c = static_cast<int16_t>(lroundf(temperature));
    fresh.daily_count = 0;

    JsonObject hourly = doc["hourly"];
    JsonArrayConst hourly_temperatures = hourly["temperature_2m"].as<JsonArrayConst>();
    JsonArrayConst hourly_times = hourly["time"].as<JsonArrayConst>();
    JsonArrayConst hourly_codes = hourly["weather_code"].as<JsonArrayConst>();
    JsonArrayConst hourly_precip = hourly["precipitation_probability"].as<JsonArrayConst>();
    JsonArrayConst hourly_humidity = hourly["relative_humidity_2m"].as<JsonArrayConst>();
    JsonArrayConst hourly_wind = hourly["wind_speed_10m"].as<JsonArrayConst>();

    if (!hourly_temperatures.isNull() && !hourly_times.isNull()) {
        char start_key[24] = {};
        if (current_time[0] != '\0') {
            service_copy_text(start_key, sizeof(start_key), current_time);
        } else {
            build_local_hour_key(start_key, sizeof(start_key));
        }

        const size_t start_index = find_hourly_start_index(hourly_times, start_key);
        const size_t available = min(hourly_temperatures.size(), hourly_times.size());
        int previous_code = weather_code;
        uint8_t change_count = 0;
        char condition_changes[96] = {};

        for (size_t i = start_index; i < available && fresh.hourly_count < kWeatherHourlyPoints; ++i) {
            const float hourly_temp = hourly_temperatures[i] | NAN;
            const char *hour_time = hourly_times[i] | "";
            if (isnan(hourly_temp)) {
                continue;
            }

            const int16_t rounded_temp = static_cast<int16_t>(lroundf(hourly_temp));
            fresh.hourly_temperatures[fresh.hourly_count] = rounded_temp;
            format_hour_label(hour_time, fresh.hourly_labels[fresh.hourly_count], sizeof(fresh.hourly_labels[0]));

            if (fresh.hourly_count == 0) {
                fresh.hourly_min_c = rounded_temp;
                fresh.hourly_max_c = rounded_temp;
            } else {
                if (rounded_temp < fresh.hourly_min_c) {
                    fresh.hourly_min_c = rounded_temp;
                }
                if (rounded_temp > fresh.hourly_max_c) {
                    fresh.hourly_max_c = rounded_temp;
                }
            }

            const int hourly_code = !hourly_codes.isNull() && i < hourly_codes.size() ? (hourly_codes[i] | previous_code) : previous_code;
            const int hourly_rain = !hourly_precip.isNull() && i < hourly_precip.size() ? (hourly_precip[i] | 0) : 0;
            const int hourly_humidity_value = !hourly_humidity.isNull() && i < hourly_humidity.size() ? (hourly_humidity[i] | humidity) : humidity;
            const float hourly_wind_value = !hourly_wind.isNull() && i < hourly_wind.size() ? (hourly_wind[i] | wind_speed) : wind_speed;
            if (hourly_rain > fresh.precipitation_peak) {
                fresh.precipitation_peak = static_cast<uint8_t>(hourly_rain);
            }
            fresh.hourly_precipitation[fresh.hourly_count] = static_cast<uint8_t>(constrain(hourly_rain, 0, 100));
            fresh.hourly_humidity[fresh.hourly_count] = static_cast<uint8_t>(constrain(hourly_humidity_value, 0, 100));
            fresh.hourly_weather_codes[fresh.hourly_count] = static_cast<uint8_t>(constrain(hourly_code, 0, 255));
            const uint8_t rounded_wind = static_cast<uint8_t>(constrain(static_cast<int>(lroundf(hourly_wind_value)), 0, 255));
            fresh.hourly_wind_kmh[fresh.hourly_count] = rounded_wind;
            if (rounded_wind > fresh.wind_peak_kmh) {
                fresh.wind_peak_kmh = rounded_wind;
            }
            if (hourly_code != previous_code && change_count < 3 && fresh.hourly_count > 0) {
                append_change_text(
                    condition_changes,
                    sizeof(condition_changes),
                    fresh.hourly_labels[fresh.hourly_count],
                    weather_code_to_text(hourly_code, is_day == 1)
                );
                change_count++;
            }
            previous_code = hourly_code;

            fresh.hourly_count++;
        }

        char rain_outlook[72] = {};
        build_rain_outlook(fresh, rain_outlook, sizeof(rain_outlook));
        if (change_count == 0) {
            snprintf(
                fresh.next_changes,
                sizeof(fresh.next_changes),
                "%s | No major condition change",
                rain_outlook
            );
        } else {
            snprintf(
                fresh.next_changes,
                sizeof(fresh.next_changes),
                "%s | %s",
                rain_outlook,
                condition_changes
            );
        }
    }

    JsonObject daily = doc["daily"];
    JsonArrayConst daily_times = daily["time"].as<JsonArrayConst>();
    JsonArrayConst daily_codes = daily["weather_code"].as<JsonArrayConst>();
    JsonArrayConst daily_temp_max = daily["temperature_2m_max"].as<JsonArrayConst>();
    JsonArrayConst daily_temp_min = daily["temperature_2m_min"].as<JsonArrayConst>();
    JsonArrayConst daily_precip = daily["precipitation_probability_max"].as<JsonArrayConst>();
    JsonArrayConst daily_wind = daily["wind_speed_10m_max"].as<JsonArrayConst>();

    if (!daily_times.isNull() && !daily_codes.isNull() && !daily_temp_max.isNull() && !daily_temp_min.isNull()) {
        const size_t daily_available = min(
            min(daily_times.size(), daily_codes.size()),
            min(daily_temp_max.size(), daily_temp_min.size())
        );

        for (size_t i = 0; i < daily_available && fresh.daily_count < kWeatherDailyPoints; ++i) {
            const char *day_time = daily_times[i] | "";
            const int day_code = daily_codes[i] | weather_code;
            const float day_max = daily_temp_max[i] | temperature;
            const float day_min = daily_temp_min[i] | temperature;
            const int day_precip = !daily_precip.isNull() && i < daily_precip.size() ? (daily_precip[i] | 0) : 0;
            const float day_wind = !daily_wind.isNull() && i < daily_wind.size() ? (daily_wind[i] | wind_speed) : wind_speed;

            format_daily_label(day_time, fresh.daily_count, fresh.daily_labels[fresh.daily_count], sizeof(fresh.daily_labels[0]));
            service_copy_text(
                fresh.daily_conditions[fresh.daily_count],
                sizeof(fresh.daily_conditions[0]),
                weather_code_to_text(day_code, true)
            );
            fresh.daily_temp_min_c[fresh.daily_count] = static_cast<int16_t>(lroundf(day_min));
            fresh.daily_temp_max_c[fresh.daily_count] = static_cast<int16_t>(lroundf(day_max));
            fresh.daily_precipitation_peak[fresh.daily_count] = static_cast<uint8_t>(constrain(day_precip, 0, 100));
            fresh.daily_wind_peak_kmh[fresh.daily_count] = static_cast<uint8_t>(constrain(static_cast<int>(lroundf(day_wind)), 0, 255));
            fresh.daily_weather_codes[fresh.daily_count] = static_cast<uint8_t>(constrain(day_code, 0, 255));
            fresh.daily_count++;
        }
    }

    *out = fresh;
    char success_text[96];
    snprintf(
        success_text,
        sizeof(success_text),
        "%s %s | hourly=%u daily=%u",
        fresh.temperature,
        fresh.condition,
        static_cast<unsigned>(fresh.hourly_count),
        static_cast<unsigned>(fresh.daily_count)
    );
    service_log_success(kServiceTag, success_text);
    return true;
}

void WeatherService::set_loading_state_()
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

void WeatherService::apply_success_(const WeatherData &fresh_data)
{
    Serial.printf(
        "[weather] live update ok | temp=%s condition=%s updated=%s hourly=%u daily=%u\n",
        fresh_data.temperature,
        fresh_data.condition,
        fresh_data.updated,
        static_cast<unsigned>(fresh_data.hourly_count),
        static_cast<unsigned>(fresh_data.daily_count)
    );
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_ = fresh_data;
        ++revision_;
        xSemaphoreGive(mutex_);
        return;
    }

    data_ = fresh_data;
    ++revision_;
}

void WeatherService::apply_failure_(const char *message)
{
    Serial.printf("[weather] update failed | %s\n", message != nullptr ? message : "unknown");
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_.loading = false;
        service_copy_text(data_.error, sizeof(data_.error), message);

        if (data_.valid) {
            data_.stale = true;
            service_copy_text(data_.status, sizeof(data_.status), "Stale");
        } else {
            data_.stale = false;
            service_copy_text(data_.temperature, sizeof(data_.temperature), "--");
            service_copy_text(data_.condition, sizeof(data_.condition), "Weather");
            service_copy_text(data_.detail, sizeof(data_.detail), message);
            service_copy_text(data_.updated, sizeof(data_.updated), "--:--");
            service_copy_text(data_.status, sizeof(data_.status), "Offline");
            service_copy_text(data_.next_changes, sizeof(data_.next_changes), "Weather change times unavailable");
            data_.hourly_count = 0;
            data_.humidity_percent = 0;
            data_.precipitation_peak = 0;
            data_.wind_peak_kmh = 0;
            data_.hourly_min_c = 0;
            data_.hourly_max_c = 0;
            data_.daily_count = 0;
        }

        ++revision_;
        xSemaphoreGive(mutex_);
        return;
    }

    service_copy_text(data_.error, sizeof(data_.error), message);
    data_.loading = false;
    ++revision_;
}
