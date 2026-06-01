#include "app_settings.h"

#include <Preferences.h>

namespace {

constexpr char kNamespace[] = "lbrief";
constexpr char kBrightnessKey[] = "brightness";
constexpr char kDarkModeKey[] = "darkmode";
constexpr char kWifiCredsKey[] = "wifi_creds";
constexpr char kWeatherCacheKey[] = "wxcache";
constexpr char kTflCacheKey[] = "tflcache";
constexpr char kNewsCacheKey[] = "newscache";
constexpr char kCalendarCacheKey[] = "calcache";
constexpr uint8_t kDefaultBrightnessPercent = 42;
constexpr uint8_t kMinBrightnessPercent = 20;

Preferences preferences;

uint8_t clamp_brightness(uint8_t percent)
{
    if (percent < kMinBrightnessPercent) {
        return kMinBrightnessPercent;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

template <typename T>
bool load_blob(const char *key, T *out)
{
    if (key == nullptr || out == nullptr) {
        return false;
    }

    if (!preferences.isKey(key)) {
        return false;
    }

    if (preferences.getBytesLength(key) != sizeof(T)) {
        return false;
    }

    T value = {};
    const size_t read = preferences.getBytes(key, &value, sizeof(T));
    if (read != sizeof(T)) {
        return false;
    }

    *out = value;
    return true;
}

template <typename T>
void save_blob(const char *key, const T &value)
{
    if (key == nullptr) {
        return;
    }

    preferences.putBytes(key, &value, sizeof(T));
}

}  // namespace

AppSettingsStore::AppSettingsStore()
    : ready_(false) {}

void AppSettingsStore::begin()
{
    if (ready_) {
        return;
    }

    ready_ = preferences.begin(kNamespace, false);
}

DisplaySettings AppSettingsStore::load_display_settings()
{
    DisplaySettings settings = {
        true,
        kDefaultBrightnessPercent,
    };

    if (!ready_) {
        return settings;
    }

    settings.brightness_percent = clamp_brightness(preferences.getUChar(kBrightnessKey, kDefaultBrightnessPercent));
    settings.dark_mode = preferences.getBool(kDarkModeKey, true);
    return settings;
}

bool AppSettingsStore::load_wifi_credentials(WifiCredentials *out) const
{
    if (!ready_) {
        return false;
    }

    WifiCredentials credentials = {};
    if (!load_blob(kWifiCredsKey, &credentials)) {
        return false;
    }

    if (credentials.ssid[0] == '\0') {
        return false;
    }

    if (out != nullptr) {
        *out = credentials;
    }
    return true;
}

void AppSettingsStore::save_brightness(uint8_t percent)
{
    if (!ready_) {
        return;
    }

    preferences.putUChar(kBrightnessKey, clamp_brightness(percent));
}

void AppSettingsStore::save_dark_mode(bool dark_mode)
{
    if (!ready_) {
        return;
    }

    preferences.putBool(kDarkModeKey, dark_mode);
}

void AppSettingsStore::save_wifi_credentials(const WifiCredentials &credentials)
{
    if (!ready_) {
        return;
    }

    if (credentials.ssid[0] == '\0') {
        preferences.remove(kWifiCredsKey);
        return;
    }

    save_blob(kWifiCredsKey, credentials);
}

void AppSettingsStore::clear_wifi_credentials()
{
    if (!ready_) {
        return;
    }

    preferences.remove(kWifiCredsKey);
}

bool AppSettingsStore::load_cached_weather(WeatherData *out) const
{
    if (!ready_) {
        return false;
    }

    return load_blob(kWeatherCacheKey, out);
}

bool AppSettingsStore::load_cached_tfl(TflData *out) const
{
    if (!ready_) {
        return false;
    }

    return load_blob(kTflCacheKey, out);
}

bool AppSettingsStore::load_cached_news(NewsData *out) const
{
    if (!ready_) {
        return false;
    }

    return load_blob(kNewsCacheKey, out);
}

bool AppSettingsStore::load_cached_calendar(CalendarData *out) const
{
    if (!ready_) {
        return false;
    }

    return load_blob(kCalendarCacheKey, out);
}

void AppSettingsStore::save_cached_weather(const WeatherData &data)
{
    if (!ready_ || !data.valid) {
        return;
    }

    save_blob(kWeatherCacheKey, data);
}

void AppSettingsStore::save_cached_tfl(const TflData &data)
{
    if (!ready_ || !data.valid) {
        return;
    }

    save_blob(kTflCacheKey, data);
}

void AppSettingsStore::save_cached_news(const NewsData &data)
{
    if (!ready_ || !data.valid) {
        return;
    }

    save_blob(kNewsCacheKey, data);
}

void AppSettingsStore::save_cached_calendar(const CalendarData &data)
{
    if (!ready_ || !data.valid) {
        return;
    }

    save_blob(kCalendarCacheKey, data);
}
