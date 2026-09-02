#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdint.h>

#include "calendar_service.h"
#include "news_service.h"
#include "tfl_service.h"
#include "weather_service.h"

struct DisplaySettings {
    bool dark_mode;
    uint8_t brightness_percent;
};

struct WifiCredentials {
    char ssid[33];
    char password[65];
};

class AppSettingsStore {
public:
    AppSettingsStore();

    void begin();
    DisplaySettings load_display_settings();
    bool load_wifi_credentials(WifiCredentials *out) const;
    void save_brightness(uint8_t percent);
    void save_dark_mode(bool dark_mode);
    void save_wifi_credentials(const WifiCredentials &credentials);
    void clear_wifi_credentials();
    bool load_cached_weather(WeatherData *out) const;
    bool load_cached_tfl(TflData *out) const;
    bool load_cached_news(NewsData *out) const;
    bool load_cached_calendar(CalendarData *out) const;
    void save_cached_weather(const WeatherData &data);
    void save_cached_tfl(const TflData &data);
    void save_cached_news(const NewsData &data);
    void save_cached_calendar(const CalendarData &data);

private:
    bool ready_;
};

#endif
