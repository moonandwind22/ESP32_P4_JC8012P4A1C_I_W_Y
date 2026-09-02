# LondonBrief_P4 application component.

# Build feature switches (mirrors the former PlatformIO environments).
# Override on the command line, e.g.: idf.py -DLONDONBRIEF_WIFI_DEBUG=1 build
set(LONDONBRIEF_DATA_SOURCE "0" CACHE STRING "Data source: 0=local hosted Wi-Fi, 1=C6 UART worker, 2=C6 SDIO worker")
set(LONDONBRIEF_C6_TRANSPORT "0" CACHE STRING "C6 transport: 0=none, 1=UART, 2=SDIO")
set(LONDONBRIEF_WIFI_DEBUG "0" CACHE STRING "Verbose Wi-Fi/ESP-Hosted diagnostics (esp32p4-local-wifi-debug)")
set(LONDONBRIEF_SERVICE_DEBUG "0" CACHE STRING "Verbose Weather/TfL/News service logging (esp32p4-local-service-debug)")

idf_component_register(
    SRCS
        "app_settings.cpp"
        "c6_snapshot_receiver.cpp"
        "calendar_service.cpp"
        "dashboard_snapshot_adapter.cpp"
        "main.cpp"
        "news_service.cpp"
        "service_support.cpp"
        "tfl_service.cpp"
        "ui_dashboard.cpp"
        "weather_service.cpp"
    INCLUDE_DIRS
        "."
        "include"
    REQUIRES
        arduino
        jd9365_lcd
        gsl3680_touch
    PRIV_REQUIRES
        esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    LONDONBRIEF_DATA_SOURCE=${LONDONBRIEF_DATA_SOURCE}
    LONDONBRIEF_C6_TRANSPORT=${LONDONBRIEF_C6_TRANSPORT}
    LONDONBRIEF_WIFI_DEBUG=${LONDONBRIEF_WIFI_DEBUG}
    LONDONBRIEF_SERVICE_DEBUG=${LONDONBRIEF_SERVICE_DEBUG}
)

# The application uses truncating snprintf() patterns by design; the former
# PlatformIO build never enabled -Werror, so keep this warning non-fatal.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format-truncation)
