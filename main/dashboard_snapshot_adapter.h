#ifndef DASHBOARD_SNAPSHOT_ADAPTER_H
#define DASHBOARD_SNAPSHOT_ADAPTER_H

#include <stdint.h>

#include "brief_protocol.h"
#include "news_service.h"
#include "tfl_service.h"
#include "weather_service.h"

struct DashboardSystemContext {
    bool wifi_configured;
    bool wifi_connected;
    bool wifi_ready;
    uint8_t wifi_fault_count;
    uint8_t network_mode;
    uint8_t brightness_percent;
    bool dark_mode;
    const char *wifi_status_text;
};

void build_dashboard_snapshot(
    const DashboardSystemContext &system,
    const WeatherData &weather,
    const TflData &tfl,
    const NewsData &news,
    brief::DashboardSnapshot *out
);

#endif
