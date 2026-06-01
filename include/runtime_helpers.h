#ifndef RUNTIME_HELPERS_H
#define RUNTIME_HELPERS_H

#include <string.h>

inline bool londonbrief_wifi_credentials_configured(const char *ssid, const char *password)
{
    if (ssid == nullptr || password == nullptr) {
        return false;
    }

    return ssid[0] != '\0' &&
           password[0] != '\0' &&
           strcmp(ssid, "YOUR_WIFI_SSID") != 0 &&
           strcmp(password, "YOUR_WIFI_PASSWORD") != 0;
}

inline const char *londonbrief_runtime_wifi_text(bool configured, bool connected, const char *status_text)
{
    if (status_text != nullptr && status_text[0] != '\0') {
        return status_text;
    }

    if (connected) {
        return "Connected";
    }

    if (configured) {
        return "Reconnecting";
    }

    return "Not configured";
}

#endif
