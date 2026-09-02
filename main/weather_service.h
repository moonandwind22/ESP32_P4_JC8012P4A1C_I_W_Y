#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

constexpr uint8_t kWeatherHourlyPoints = 12;
constexpr uint8_t kWeatherDailyPoints = 5;

struct WeatherData {
    bool valid;
    bool loading;
    bool stale;
    bool is_day;
    uint8_t current_weather_code;
    char temperature[16];
    char condition[40];
    char detail[128];
    char updated[24];
    char status[32];
    char error[96];
    char next_changes[160];
    uint8_t hourly_count;
    uint8_t humidity_percent;
    uint8_t precipitation_peak;
    uint8_t wind_peak_kmh;
    int16_t hourly_temperatures[kWeatherHourlyPoints];
    uint8_t hourly_humidity[kWeatherHourlyPoints];
    uint8_t hourly_wind_kmh[kWeatherHourlyPoints];
    uint8_t hourly_precipitation[kWeatherHourlyPoints];
    uint8_t hourly_weather_codes[kWeatherHourlyPoints];
    char hourly_labels[kWeatherHourlyPoints][6];
    int16_t hourly_min_c;
    int16_t hourly_max_c;
    uint8_t daily_count;
    char daily_labels[kWeatherDailyPoints][12];
    char daily_conditions[kWeatherDailyPoints][32];
    int16_t daily_temp_min_c[kWeatherDailyPoints];
    int16_t daily_temp_max_c[kWeatherDailyPoints];
    uint8_t daily_precipitation_peak[kWeatherDailyPoints];
    uint8_t daily_wind_peak_kmh[kWeatherDailyPoints];
    uint8_t daily_weather_codes[kWeatherDailyPoints];
};

class WeatherService {
public:
    WeatherService();

    void init();
    void update();
    void request_refresh();
    void restore_cached(const WeatherData &cached);
    void snapshot(WeatherData *out) const;
    uint32_t revision() const;
    bool due(uint32_t now_ms) const;

private:
    bool fetch_(WeatherData *out);
    void set_loading_state_();
    void apply_success_(const WeatherData &fresh_data);
    void apply_failure_(const char *message);

    mutable SemaphoreHandle_t mutex_;
    volatile bool refresh_requested_;
    uint32_t next_update_ms_;
    uint32_t revision_;
    WeatherData data_;
};

#endif
