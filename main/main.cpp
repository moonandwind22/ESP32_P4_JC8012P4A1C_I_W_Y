#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <sdkconfig.h>
#include <math.h>
#include <WiFi.h>
#include <time.h>

#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)
#include <esp32-hal-hosted.h>
#endif

#include "app_settings.h"
#include "board_pins.h"
#include "brief_protocol.h"
#include "brief_transport.h"
#include "c6_snapshot_receiver.h"
#include "dashboard_snapshot_adapter.h"
#include "jd9365_lcd.h"
#include "runtime_helpers.h"
#include "gsl3680_touch.h"
#include "app_config.h"
#include "calendar_service.h"
#include "news_service.h"
#include "service_support.h"
#include "tfl_service.h"
#include "ui_dashboard.h"
#include "weather_service.h"

namespace
{

    constexpr const char *kAppLogTag = "app"; 
    constexpr bool kEnableDiagnostics = false;
    constexpr bool kEnableWifiDebug = LONDONBRIEF_WIFI_DEBUG != 0;
    constexpr bool kEnableStartupLogs = true;
    constexpr bool kDiagnosticLightDashboardSync = false;
    constexpr bool kDisableWifiForTouchIsolation = false;
    constexpr bool kEnableDisplayProbePattern = false;
    constexpr bool kEnableLvglProbeScreen = false;
    constexpr bool kLvglSynchronousFlushReady = false;
    constexpr uint8_t kDataSourceLocalHosted = 0;
    constexpr uint8_t kDataSourceC6UartWorker = 1;
    constexpr uint8_t kDataSourceC6SdioWorker = 2;
    constexpr uint8_t kC6TransportNone = 0;
    constexpr uint8_t kC6TransportUart = 1;
    constexpr uint8_t kC6TransportSdio = 2;
    constexpr uint8_t kDataSource = LONDONBRIEF_DATA_SOURCE;
    constexpr uint8_t kC6Transport = LONDONBRIEF_C6_TRANSPORT;
    constexpr bool kUseC6WorkerData = kDataSource == kDataSourceC6UartWorker ||
                                      kDataSource == kDataSourceC6SdioWorker;
    constexpr bool kUseC6SdioTransport = kUseC6WorkerData &&
                                         (kDataSource == kDataSourceC6SdioWorker ||
                                          kC6Transport == kC6TransportSdio);
    constexpr bool kUseC6UartTransport = kUseC6WorkerData &&
                                         !kUseC6SdioTransport &&
                                         (kDataSource == kDataSourceC6UartWorker ||
                                          kC6Transport == kC6TransportUart);
    constexpr int kC6SerialPort = LONDONBRIEF_C6_SERIAL_PORT;
    constexpr int kC6SerialRxPin = LONDONBRIEF_C6_SERIAL_RX_PIN;
    constexpr int kC6SerialTxPin = LONDONBRIEF_C6_SERIAL_TX_PIN;
    constexpr bool kC6SerialPinsConfigured = kC6SerialRxPin >= 0 && kC6SerialTxPin >= 0;
    constexpr bool kBatteryMonitorEnabled = LONDONBRIEF_ENABLE_BATTERY_MONITOR != 0;
#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)
    constexpr bool kHostedWiFiRecoveryConservative = true;
#else
    constexpr bool kHostedWiFiRecoveryConservative = false;
#endif

    constexpr uint32_t kLvglTickPeriodUs = 5000;
    constexpr uint32_t kLvglBufferLines = 40;
    constexpr uint32_t kBytesPerPixel = 2;
    constexpr uint32_t kUiSyncPeriodMs = 500;
    constexpr uint32_t kClockUpdatePeriodMs = 1000;
    constexpr uint32_t kWifiRetryMs = 15000;
    constexpr uint32_t kWifiStatusPollMs = 3000;
    constexpr uint32_t kWifiWarmupMs = 12000;
    constexpr uint32_t kWifiPostConnectIdleMs = 30000;
    constexpr uint32_t kWifiStartupDelayMs = 20000;
    constexpr uint32_t kWifiSessionStartupDeadlineMs = 45000;
    constexpr uint32_t kWifiDisconnectFaultMs = 60000;
    constexpr uint32_t kWifiFullRecoveryMs = 90000;
    constexpr uint32_t kWifiRecoveryCooldownMs = 60000;
    constexpr uint32_t kWifiSuspendCooldownMs = 120000;
    constexpr uint32_t kWifiDebugLogMs = 5000;
    constexpr uint32_t kWifiPostBeginQuietMs = 20000;
    constexpr uint32_t kDataTaskDelayMs = 3000;
    constexpr uint32_t kServiceFetchGapMs = 15000;
    constexpr uint32_t kStartupServiceStageGapMs = 15000;
    constexpr uint32_t kServiceIdleProbeMs = 5000;
    constexpr uint32_t kServiceStableDecayMs = 10UL * 60UL * 1000UL;
    constexpr uint32_t kServiceBackoffStepMs = 10000;
    constexpr uint32_t kStartupBackoffStepMs = 5000;
    constexpr uint32_t kFragileSessionExtraQuietMs = 15000;
    constexpr uint32_t kFragileDispatchExtraGapMs = 15000;
    constexpr uint32_t kServiceNetworkPauseBaseMs = 45000;
    constexpr uint32_t kServiceNetworkPauseStepMs = 15000;
    constexpr uint32_t kServiceFaultWindowMs = 120000;
    constexpr uint8_t kServiceFaultSuspendThreshold = 4;
    constexpr uint32_t kHostedIdleSessionRolloverMs = 75000;
    constexpr uint32_t kTouchPollPeriodMs = 15;
    constexpr uint32_t kTouchInitSettleDelayMs = 200;
    constexpr uint8_t kTouchRecoveryReadFailureThreshold = 8;
    constexpr uint32_t kTouchRecoveryCooldownMs = 12000;
    constexpr uint32_t kTouchStartupValidationTimeoutMs = 8000;
    constexpr uint8_t kTouchRotation = 1;
    constexpr uint32_t kBatteryStartupDelayMs = 60000;
    constexpr uint32_t kBatterySamplePeriodMs = 60000;
    constexpr uint32_t kBatteryTouchQuietMs = 5000;
    constexpr uint32_t kDisplayProbePatternMs = 1200;
    constexpr uint32_t kLvglProbeScreenMs = 1600;
    constexpr uint8_t kDefaultBrightnessPercent = 42;
    constexpr uint8_t kSafeBootBrightnessPercent = 60;
    constexpr uint8_t kMinimumReadableBrightnessPercent = 20;
    constexpr uint32_t kSettingsPersistDelayMs = 1500;
    constexpr uint32_t kTimeSyncRetryMs = 30000;
    constexpr BaseType_t kDataTaskCore = 1;
    constexpr brief::NetworkMode kNetworkMode = static_cast<brief::NetworkMode>(LONDONBRIEF_NETWORK_MODE);
    constexpr uint8_t kServiceDomainCount = 4;
    constexpr brief::Domain kStartupServiceOrder[kServiceDomainCount] = {
        brief::kDomainWeather,
        brief::kDomainTfl,
        brief::kDomainNews,
        brief::kDomainCalendar,
    };
    constexpr brief::Domain kSteadyServiceOrder[kServiceDomainCount] = {
        brief::kDomainWeather,
        brief::kDomainTfl,
        brief::kDomainNews,
        brief::kDomainCalendar,
    };

    jd9365_lcd lcd_display(LCD_RST);
    gsl3680_touch touch_input(TP_I2C_SDA, TP_I2C_SCL, TP_RST, TP_INT);
    UIDashboard dashboard;
    WeatherService weather_service;
    TflService tfl_service;
    NewsService news_service;
    CalendarService calendar_service;
    AppSettingsStore settings_store;
    C6SnapshotReceiver c6_receiver;
    HardwareSerial c6_worker_serial(kC6SerialPort);
    DisplaySettings display_settings = {
        true,
        kDefaultBrightnessPercent,
    };
    WeatherData dashboard_weather_snapshot = {};
    TflData dashboard_tfl_snapshot = {};
    NewsData dashboard_news_snapshot = {};
    CalendarData dashboard_calendar_snapshot = {};
    brief::DashboardSnapshot dashboard_snapshot = {};
    brief::DashboardSnapshot c6_dashboard_snapshot = {};
    char runtime_summary_text[256];
    char runtime_detail_text[320];
    WeatherData cached_weather_restore = {};
    TflData cached_tfl_restore = {};
    NewsData cached_news_restore = {};
    CalendarData cached_calendar_restore = {};
    WifiCredentials configured_wifi_credentials = {};
    WifiCredentials pending_wifi_credentials = {};
    char wifi_setup_status_text[96] = "Scan for nearby access points";
    char wifi_scan_options_text[1024] = "No networks scanned yet";
    bool wifi_scan_in_progress = false;
    bool wifi_scan_requested_async = false;
    bool wifi_credentials_commit_pending = false;
    uint32_t wifi_setup_revision = 0;
    uint32_t last_rendered_wifi_setup_revision = UINT32_MAX;

    struct BatteryStatus
    {
        bool enabled;
        bool valid;
        uint8_t percent;
        float voltage;
        char status_text[64];
        uint32_t last_sample_ms;
    };

    struct WifiUiState
    {
        bool configured;
        bool connected;
        bool ready;
        int status_code;
        uint8_t fault_count;
        char status_text[96];
    };

    lv_display_t *display = nullptr;
    esp_timer_handle_t lvgl_tick_timer = nullptr;
    uint8_t *draw_buffer = nullptr;
    bool last_touch_pressed = false;
    bool touch_sample_pressed = false;
    bool touch_enabled = false;
    bool touch_ready_confirmed = false;
    bool touch_read_ever_succeeded = false;
    bool touch_activity_ever_seen = false;
    bool touch_point_ever_seen = false;
    bool touch_feedback_pending = false;
    bool touch_status_pressed = false;
    bool touch_status_dirty = false;
    bool touch_status_idle = true;
    uint16_t touch_feedback_x = 0;
    uint16_t touch_feedback_y = 0;
    uint16_t last_touch_x = 0;
    uint16_t last_touch_y = 0;
    uint32_t last_touch_seen_ms = 0;
    uint32_t last_touch_press_ms = 0;
    uint32_t last_touch_poll_ms = 0;
    uint32_t last_touch_read_ok_ms = 0;
    uint32_t last_touch_debug_log_ms = 0;
    uint32_t last_touch_heartbeat_ms = 0;
    uint32_t last_touch_recovery_attempt_ms = 0;
    uint32_t touch_init_completed_ms = 0;
    uint32_t last_touch_interrupt_low_ms = 0;
    uint8_t consecutive_touch_read_failures = 0;
    bool touch_seen_once = false;
    bool time_configured = false;
    bool time_sync_requested = false;
    uint32_t last_ui_sync_ms = 0;
    uint32_t last_clock_update_ms = 0;
    uint32_t last_time_sync_request_ms = 0;
    uint32_t last_wifi_attempt_ms = 0;
    uint32_t last_wifi_status_poll_ms = 0;
    uint32_t wifi_connected_since_ms = 0;
    uint32_t wifi_disconnected_since_ms = 0;
    uint32_t last_wifi_recovery_ms = 0;
    uint32_t wifi_start_requested_ms = 0;
    uint32_t wifi_session_started_ms = 0;
    uint32_t wifi_suspended_until_ms = 0;
    uint32_t last_wifi_debug_log_ms = 0;
    uint32_t display_settings_dirty_since_ms = 0;
    TaskHandle_t data_task_handle = nullptr;
    QueueHandle_t wifi_ui_state_queue = nullptr;
    wl_status_t last_wifi_status = WL_IDLE_STATUS;
    volatile int wifi_cached_status_code = WL_IDLE_STATUS;
    bool wifi_cached_connected = false;
    bool wifi_cached_configured = false;
    bool wifi_cached_ready = false;
    bool wifi_start_requested = false;
    bool wifi_started = false;
    bool wifi_has_connected_once = false;
    uint8_t wifi_fault_count = 0;
    uint8_t wifi_manual_reconnect_count = 0;
    char wifi_cached_status_text[96] = "Wi-Fi starting";
    WifiUiState latest_wifi_ui_state = {
        false,
        false,
        false,
        WL_IDLE_STATUS,
        0,
        "Wi-Fi starting",
    };
    bool display_settings_dirty = false;
    bool c6_transport_started = false;
    bool c6_snapshot_valid = false;
    bool c6_refresh_request_pending = false;
    bool battery_adc_checked = false;
    bool fatal_boot_error = false;
    uint32_t last_c6_snapshot_ms = 0;
    uint32_t c6_snapshot_count = 0;
    uint32_t fatal_boot_error_ms = 0;
    BatteryStatus battery_status = {
        kBatteryMonitorEnabled,
        false,
        0,
        0.0f,
        "Power connected / battery monitor off",
        0,
    };
    uint8_t next_service_slot = 0;
    bool startup_service_attempted[kServiceDomainCount] = {false, false, false};
    volatile bool pending_service_refresh[kServiceDomainCount] = {false, false, false};
    uint32_t last_saved_weather_revision = 0;
    uint32_t last_saved_tfl_revision = 0;
    uint32_t last_saved_news_revision = 0;
    uint32_t last_saved_calendar_revision = 0;
    uint32_t last_rendered_weather_revision = UINT32_MAX;
    uint32_t last_rendered_tfl_revision = UINT32_MAX;
    uint32_t last_rendered_news_revision = UINT32_MAX;
    uint32_t last_rendered_calendar_revision = UINT32_MAX;
    uint8_t service_backoff_level = 0;
    bool service_fragile_mode = true;
    uint8_t service_success_mask = 0;
    char service_scheduler_status_text[128] = "Queue idle";
    char service_queue_preview_text[128] = "TfL -> News -> Weather";
    uint32_t service_dispatch_gap_ms = kServiceFetchGapMs;
    uint32_t startup_service_gap_ms = kStartupServiceStageGapMs;
    uint32_t service_network_pause_until_ms = 0;
    uint32_t service_fault_window_started_ms = 0;
    uint8_t service_fault_window_count = 0;
    uint32_t last_successful_service_ms = 0;
    bool service_suspend_requested = false;
    char service_suspend_reason[64] = "";
    char last_runtime_event[160] = "System booting";
    char startup_reset_reason[48] = "Unknown";
    char touch_debug_text[160] = "Touch debug pending";
    gsl3680_touch_debug touch_debug_snapshot = {};
    uint32_t last_loop_heartbeat_ms = 0;
    uint32_t loop_iteration_count = 0;
    volatile uint32_t lvgl_flush_count = 0;
    volatile uint32_t lvgl_touch_cb_count = 0;
    volatile uint32_t touch_poll_count = 0;
    volatile uint32_t touch_press_sample_count = 0;

    void store_runtime_event(const char *message);
    void suspend_wifi_stack(const char *reason);

    bool wifi_credentials_valid(const WifiCredentials &credentials)
    {
        return credentials.ssid[0] != '\0';
    }

    void set_wifi_setup_status(const char *text)
    {
        snprintf(wifi_setup_status_text, sizeof(wifi_setup_status_text), "%s", text != nullptr ? text : "");
        ++wifi_setup_revision;
    }

    void set_wifi_scan_options(const char *text)
    {
        snprintf(wifi_scan_options_text, sizeof(wifi_scan_options_text), "%s", text != nullptr ? text : "No networks scanned yet");
        ++wifi_setup_revision;
    }

    void load_configured_wifi_credentials()
    {
        WifiCredentials stored = {};
        if (settings_store.load_wifi_credentials(&stored) && wifi_credentials_valid(stored))
        {
            configured_wifi_credentials = stored;
            return;
        }

        configured_wifi_credentials = {};
        if (londonbrief_wifi_credentials_configured(LONDONBRIEF_WIFI_SSID, LONDONBRIEF_WIFI_PASSWORD))
        {
            snprintf(configured_wifi_credentials.ssid, sizeof(configured_wifi_credentials.ssid), "%s", LONDONBRIEF_WIFI_SSID);
            snprintf(configured_wifi_credentials.password, sizeof(configured_wifi_credentials.password), "%s", LONDONBRIEF_WIFI_PASSWORD);
        }
    }

    bool is_leap_year(int full_year)
    {
        return ((full_year % 4) == 0 && (full_year % 100) != 0) || ((full_year % 400) == 0);
    }

    bool network_mode_allows_domain(brief::Domain domain)
    {
        switch (kNetworkMode)
        {
        case brief::kNetworkModeOffline:
            return false;
        case brief::kNetworkModeManualRefresh:
            return false;
        case brief::kNetworkModeWeatherOnly:
            return domain == brief::kDomainWeather;
        case brief::kNetworkModeTflOnly:
            return domain == brief::kDomainTfl;
        case brief::kNetworkModeNewsOnly:
            return domain == brief::kDomainNews;
        default:
            return true;
        }
    }

    int domain_to_index(brief::Domain domain)
    {
        switch (domain)
        {
        case brief::kDomainWeather:
            return 0;
        case brief::kDomainTfl:
            return 1;
        case brief::kDomainNews:
            return 2;
        case brief::kDomainCalendar:
            return 3;
        default:
            return -1;
        }
    }

    const char *domain_name(brief::Domain domain)
    {
        switch (domain)
        {
        case brief::kDomainWeather:
            return "Weather";
        case brief::kDomainTfl:
            return "TfL";
        case brief::kDomainNews:
            return "News";
        case brief::kDomainCalendar:
            return "Calendar";
        default:
            return "All";
        }
    }

    uint8_t allowed_service_domain_mask()
    {
        uint8_t mask = 0;
        for (uint8_t i = 0; i < kServiceDomainCount; ++i)
        {
            const brief::Domain domain = kSteadyServiceOrder[i];
            const int index = domain_to_index(domain);
            if (index >= 0 && network_mode_allows_domain(domain))
            {
                mask |= static_cast<uint8_t>(1U << index);
            }
        }
        return mask;
    }

    void recompute_service_timing()
    {
        service_dispatch_gap_ms = kServiceFetchGapMs + (static_cast<uint32_t>(service_backoff_level) * kServiceBackoffStepMs);
        startup_service_gap_ms = kStartupServiceStageGapMs + (static_cast<uint32_t>(service_backoff_level) * kStartupBackoffStepMs);
        if (service_fragile_mode)
        {
            service_dispatch_gap_ms += kFragileDispatchExtraGapMs;
            startup_service_gap_ms += kFragileDispatchExtraGapMs;
        }
    }

    void note_service_backoff_fault()
    {
        if (service_backoff_level < 4U)
        {
            ++service_backoff_level;
        }
        recompute_service_timing();
    }

    void clear_service_fault_window()
    {
        service_network_pause_until_ms = 0;
        service_fault_window_started_ms = 0;
        service_fault_window_count = 0;
        service_suspend_requested = false;
        service_suspend_reason[0] = '\0';
    }

    void note_service_network_fault(const char *tag, int code)
    {
        const uint32_t now = millis();
        note_service_backoff_fault();

        if (service_fault_window_started_ms == 0 ||
            !service_time_reached(now, service_fault_window_started_ms + kServiceFaultWindowMs))
        {
            if (service_fault_window_started_ms == 0)
            {
                service_fault_window_started_ms = now;
                service_fault_window_count = 0;
            }
        }
        else
        {
            service_fault_window_started_ms = now;
            service_fault_window_count = 0;
        }

        if (service_fault_window_count < UINT8_MAX)
        {
            ++service_fault_window_count;
        }

        uint32_t pause_ms = kServiceNetworkPauseBaseMs +
                            (static_cast<uint32_t>(service_backoff_level) * kServiceNetworkPauseStepMs);
        if (service_fragile_mode)
        {
            pause_ms += kFragileSessionExtraQuietMs;
        }
        if (service_fault_window_count >= 3U)
        {
            pause_ms += kServiceNetworkPauseStepMs;
        }

        const uint32_t pause_until_ms = now + pause_ms;
        if (service_network_pause_until_ms == 0 ||
            service_time_reached(pause_until_ms, service_network_pause_until_ms))
        {
            service_network_pause_until_ms = pause_until_ms;
        }

        char event_text[160];
        snprintf(
            event_text,
            sizeof(event_text),
            "%s network fault %d, pausing fetches %lus",
            tag != nullptr ? tag : "service",
            code,
            static_cast<unsigned long>((pause_ms + 999U) / 1000U));
        store_runtime_event(event_text);

        if (wifi_started && service_fault_window_count >= kServiceFaultSuspendThreshold)
        {
            service_suspend_requested = true;
            snprintf(service_suspend_reason, sizeof(service_suspend_reason), "%s", "service DNS/SSL fault burst");
            store_runtime_event("Wi-Fi suspend queued after service fault burst");
            service_fault_window_started_ms = 0;
            service_fault_window_count = 0;
        }
    }

    void maybe_relax_service_backoff()
    {
        if (service_backoff_level == 0 || wifi_connected_since_ms == 0)
        {
            return;
        }

        if (service_time_reached(millis(), wifi_connected_since_ms + kServiceStableDecayMs))
        {
            --service_backoff_level;
            recompute_service_timing();
            wifi_connected_since_ms = millis();
            store_runtime_event("Service fetch cadence relaxed after stable Wi-Fi");
        }
    }

    void refresh_service_queue_preview()
    {
        service_queue_preview_text[0] = '\0';
        bool appended = false;

        for (uint8_t i = 0; i < kServiceDomainCount; ++i)
        {
            const brief::Domain domain = kStartupServiceOrder[i];
            const int index = domain_to_index(domain);
            if (index < 0 || !network_mode_allows_domain(domain) || startup_service_attempted[index])
            {
                continue;
            }
            if (appended)
            {
                strncat(service_queue_preview_text, " -> ", sizeof(service_queue_preview_text) - strlen(service_queue_preview_text) - 1);
            }
            strncat(service_queue_preview_text, domain_name(domain), sizeof(service_queue_preview_text) - strlen(service_queue_preview_text) - 1);
            appended = true;
        }

        for (uint8_t offset = 0; offset < kServiceDomainCount; ++offset)
        {
            const uint8_t slot = static_cast<uint8_t>((next_service_slot + offset) % kServiceDomainCount);
            const brief::Domain domain = kSteadyServiceOrder[slot];
            const int index = domain_to_index(domain);
            if (index < 0 || !network_mode_allows_domain(domain))
            {
                continue;
            }
            if (appended)
            {
                strncat(service_queue_preview_text, " -> ", sizeof(service_queue_preview_text) - strlen(service_queue_preview_text) - 1);
            }
            strncat(service_queue_preview_text, domain_name(domain), sizeof(service_queue_preview_text) - strlen(service_queue_preview_text) - 1);
            appended = true;
        }

        if (!appended)
        {
            snprintf(service_queue_preview_text, sizeof(service_queue_preview_text), "%s", "No active domains");
        }
    }

    bool snapshot_domain_state(brief::Domain domain, bool *valid, bool *loading, bool *stale)
    {
        if (valid == nullptr || loading == nullptr || stale == nullptr)
        {
            return false;
        }

        switch (domain)
        {
        case brief::kDomainWeather:
        {
            WeatherData snapshot = {};
            weather_service.snapshot(&snapshot);
            *valid = snapshot.valid;
            *loading = snapshot.loading;
            *stale = snapshot.stale;
            return true;
        }
        case brief::kDomainTfl:
        {
            TflData snapshot = {};
            tfl_service.snapshot(&snapshot);
            *valid = snapshot.valid;
            *loading = snapshot.loading;
            *stale = snapshot.stale;
            return true;
        }
        case brief::kDomainNews:
        {
            NewsData snapshot = {};
            news_service.snapshot(&snapshot);
            *valid = snapshot.valid;
            *loading = snapshot.loading;
            *stale = snapshot.stale;
            return true;
        }
        case brief::kDomainCalendar:
        {
            CalendarData snapshot = {};
            calendar_service.snapshot(&snapshot);
            *valid = snapshot.valid;
            *loading = snapshot.loading;
            *stale = snapshot.stale;
            return true;
        }
        default:
            break;
        }

        *valid = false;
        *loading = false;
        *stale = false;
        return false;
    }

    uint32_t domain_revision(brief::Domain domain)
    {
        switch (domain)
        {
        case brief::kDomainWeather:
            return weather_service.revision();
        case brief::kDomainTfl:
            return tfl_service.revision();
        case brief::kDomainNews:
            return news_service.revision();
        case brief::kDomainCalendar:
            return calendar_service.revision();
        default:
            return 0;
        }
    }

    void arm_domain_refresh(brief::Domain domain)
    {
        switch (domain)
        {
        case brief::kDomainWeather:
            weather_service.request_refresh();
            break;
        case brief::kDomainTfl:
            tfl_service.request_refresh();
            break;
        case brief::kDomainNews:
            news_service.request_refresh();
            break;
        case brief::kDomainCalendar:
            calendar_service.request_refresh();
            break;
        default:
            break;
        }
    }

    void reset_service_scheduler()
    {
        next_service_slot = 0;
        for (uint8_t i = 0; i < kServiceDomainCount; ++i)
        {
            startup_service_attempted[i] = false;
        }
        service_success_mask = 0;
        service_fragile_mode = true;
        recompute_service_timing();
        snprintf(service_scheduler_status_text, sizeof(service_scheduler_status_text), "%s", "Queue reset");
        refresh_service_queue_preview();
    }

    void mark_domain_refresh_pending(brief::Domain domain)
    {
        const int index = domain_to_index(domain);
        if (index < 0)
        {
            return;
        }
        pending_service_refresh[index] = true;
    }

    bool consume_domain_refresh_pending(brief::Domain domain)
    {
        const int index = domain_to_index(domain);
        if (index < 0)
        {
            return false;
        }

        const bool pending = pending_service_refresh[index];
        pending_service_refresh[index] = false;
        return pending;
    }

    void request_domain_refresh(brief::Domain domain)
    {
        switch (domain)
        {
        case brief::kDomainWeather:
            mark_domain_refresh_pending(brief::kDomainWeather);
            break;
        case brief::kDomainTfl:
            mark_domain_refresh_pending(brief::kDomainTfl);
            break;
        case brief::kDomainNews:
            mark_domain_refresh_pending(brief::kDomainNews);
            break;
        case brief::kDomainCalendar:
            mark_domain_refresh_pending(brief::kDomainCalendar);
            break;
        default:
            if (network_mode_allows_domain(brief::kDomainWeather))
            {
                mark_domain_refresh_pending(brief::kDomainWeather);
            }
            if (network_mode_allows_domain(brief::kDomainTfl))
            {
                mark_domain_refresh_pending(brief::kDomainTfl);
            }
            if (network_mode_allows_domain(brief::kDomainNews))
            {
                mark_domain_refresh_pending(brief::kDomainNews);
            }
            if (network_mode_allows_domain(brief::kDomainCalendar))
            {
                mark_domain_refresh_pending(brief::kDomainCalendar);
            }
            break;
        }
    }

    void update_domain_now(brief::Domain domain)
    {
        switch (domain)
        {
        case brief::kDomainWeather:
            weather_service.update();
            break;
        case brief::kDomainTfl:
            tfl_service.update();
            break;
        case brief::kDomainNews:
            news_service.update();
            break;
        case brief::kDomainCalendar:
            calendar_service.update();
            break;
        default:
            break;
        }
    }

    void restore_cached_service_data()
    {
        if (settings_store.load_cached_weather(&cached_weather_restore) && cached_weather_restore.valid)
        {
            weather_service.restore_cached(cached_weather_restore);
            last_saved_weather_revision = weather_service.revision();
        }

        if (settings_store.load_cached_tfl(&cached_tfl_restore) && cached_tfl_restore.valid)
        {
            tfl_service.restore_cached(cached_tfl_restore);
            last_saved_tfl_revision = tfl_service.revision();
        }

        if (settings_store.load_cached_news(&cached_news_restore) && cached_news_restore.valid)
        {
            news_service.restore_cached(cached_news_restore);
            last_saved_news_revision = news_service.revision();
        }

        if (settings_store.load_cached_calendar(&cached_calendar_restore) && cached_calendar_restore.valid)
        {
            calendar_service.restore_cached(cached_calendar_restore);
            last_saved_calendar_revision = calendar_service.revision();
        }
    }

    void persist_cached_service_data_if_changed()
    {
        const uint32_t weather_revision = weather_service.revision();
        if (weather_revision != last_saved_weather_revision)
        {
            WeatherData cached = {};
            weather_service.snapshot(&cached);
            if (cached.valid)
            {
                settings_store.save_cached_weather(cached);
                last_saved_weather_revision = weather_revision;
            }
        }

        const uint32_t tfl_revision = tfl_service.revision();
        if (tfl_revision != last_saved_tfl_revision)
        {
            TflData cached = {};
            tfl_service.snapshot(&cached);
            if (cached.valid)
            {
                settings_store.save_cached_tfl(cached);
                last_saved_tfl_revision = tfl_revision;
            }
        }

        const uint32_t news_revision = news_service.revision();
        if (news_revision != last_saved_news_revision)
        {
            NewsData cached = {};
            news_service.snapshot(&cached);
            if (cached.valid)
            {
                settings_store.save_cached_news(cached);
                last_saved_news_revision = news_revision;
            }
        }

        const uint32_t calendar_revision = calendar_service.revision();
        if (calendar_revision != last_saved_calendar_revision)
        {
            CalendarData cached = {};
            calendar_service.snapshot(&cached);
            if (cached.valid)
            {
                settings_store.save_cached_calendar(cached);
                last_saved_calendar_revision = calendar_revision;
            }
        }
    }

    enum AppPhase : uint32_t
    {
        kPhaseBoot = 1,
        kPhaseSettingsReady,
        kPhaseLvInitDone,
        kPhaseDisplayBegin,
        kPhaseDisplayReady,
        kPhaseTouchInit,
        kPhaseTouchReady,
        kPhaseTickReady,
        kPhaseDashboardInit,
        kPhaseDashboardReady,
        kPhaseServicesReady,
        kPhaseWifiBegin,
        kPhaseDataTaskStarted,
        kPhaseSetupComplete,
        kPhaseLoopRunning,
    };

    volatile AppPhase current_app_phase = kPhaseBoot;

    void configure_time_if_needed();
    void store_runtime_event(const char *message);
    void suspend_wifi_stack(const char *reason);

    const char *reset_reason_to_text(esp_reset_reason_t reason)
    {
        switch (reason)
        {
        case ESP_RST_POWERON:
            return "Power on";
        case ESP_RST_EXT:
            return "External reset";
        case ESP_RST_SW:
            return "Software reset";
        case ESP_RST_PANIC:
            return "Kernel panic";
        case ESP_RST_INT_WDT:
            return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "Task watchdog";
        case ESP_RST_WDT:
            return "Other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "Wake from sleep";
        case ESP_RST_BROWNOUT:
            return "Brownout";
        case ESP_RST_SDIO:
            return "SDIO reset";
        default:
            return "Unknown";
        }
    }

    void store_runtime_event(const char *message)
    {
        snprintf(last_runtime_event, sizeof(last_runtime_event), "%s", message ? message : "No event");
        if (kEnableDiagnostics)
        {
            Serial.printf("[runtime] %s\n", last_runtime_event);
        }
    }

    void log_app_stage(const char *stage)
    {
        if (!kEnableStartupLogs)
        {
            return;
        }

        const unsigned long now_ms = millis();
        const unsigned free_heap_kb = static_cast<unsigned>(ESP.getFreeHeap() / 1024U);
        Serial.printf("[app-stage] %lu ms | heap=%u KB | %s\n", now_ms, free_heap_kb, stage ? stage : "(null)");
        ESP_LOGW(kAppLogTag, "stage=%s heap_kb=%u uptime_ms=%lu", stage ? stage : "(null)", free_heap_kb, now_ms);
        esp_rom_printf("[app-stage-rom] %lu ms | heap=%u KB | %s\n", now_ms, free_heap_kb, stage ? stage : "(null)");
    }

    void enter_fatal_boot_error(const char *reason)
    {
        const char *safe_reason = reason != nullptr ? reason : "Fatal boot error";
        fatal_boot_error = true;
        fatal_boot_error_ms = millis();
        store_runtime_event(safe_reason);
        Serial.printf("[fatal] %s; restarting after cooldown\n", safe_reason);
        esp_rom_printf("[fatal-rom] %s; restarting after cooldown\n", safe_reason);
    }

    void service_fatal_boot_error()
    {
        if (!fatal_boot_error)
        {
            return;
        }

        if (service_time_reached(millis(), fatal_boot_error_ms + 5000U))
        {
            ESP.restart();
        }

        delay(250);
    }

    void set_app_phase(AppPhase phase, const char *label)
    {
        current_app_phase = phase;
        log_app_stage(label);
    }

    void bump_counter(volatile uint32_t *counter)
    {
        if (counter == nullptr)
        {
            return;
        }
        *counter = *counter + 1U;
    }

    void copy_wifi_globals_to_state(WifiUiState *state)
    {
        if (state == nullptr)
        {
            return;
        }

        state->configured = wifi_cached_configured;
        state->connected = wifi_cached_connected;
        state->ready = wifi_cached_ready;
        state->status_code = wifi_cached_status_code;
        state->fault_count = wifi_fault_count;
        snprintf(state->status_text, sizeof(state->status_text), "%s", wifi_cached_status_text);
    }

    void publish_wifi_ui_state()
    {
        WifiUiState state = {};
        copy_wifi_globals_to_state(&state);

        if (wifi_ui_state_queue != nullptr)
        {
            xQueueOverwrite(wifi_ui_state_queue, &state);
        }
        else
        {
            latest_wifi_ui_state = state;
        }
    }

    void drain_wifi_ui_state()
    {
        if (wifi_ui_state_queue == nullptr)
        {
            copy_wifi_globals_to_state(&latest_wifi_ui_state);
            return;
        }

        WifiUiState incoming = {};
        while (xQueueReceive(wifi_ui_state_queue, &incoming, 0) == pdTRUE)
        {
            latest_wifi_ui_state = incoming;
        }
    }

    const char *c6_transport_name()
    {
        if (!kUseC6WorkerData)
        {
            return "disabled";
        }
        if (kUseC6SdioTransport)
        {
            return "SDIO snapshot";
        }
        if (kUseC6UartTransport)
        {
            return "UART snapshot";
        }
        return "unknown";
    }

    const char *c6_transport_wait_text()
    {
        if (kUseC6SdioTransport)
        {
            return "C6 SDIO snapshot transport pending";
        }
        if (kUseC6UartTransport && !kC6SerialPinsConfigured)
        {
            return "C6 UART pins not configured";
        }
        return c6_receiver.status_text();
    }

    void refresh_touch_debug_text()
    {
        if (!touch_debug_snapshot.controller_ready)
        {
            snprintf(
                touch_debug_text,
                sizeof(touch_debug_text),
                "Touch init failed | %s",
                touch_debug_snapshot.last_error[0] != '\0' ? touch_debug_snapshot.last_error : "Unknown error");
        }
        else if (!touch_debug_snapshot.last_read_ok)
        {
            snprintf(
                touch_debug_text,
                sizeof(touch_debug_text),
                "Touch read failed | INT:%d | %s",
                static_cast<int>(touch_debug_snapshot.int_level),
                touch_debug_snapshot.last_error);
        }
        else if (touch_debug_snapshot.last_touch_found)
        {
            snprintf(
                touch_debug_text,
                sizeof(touch_debug_text),
                "Touch points:%u | INT:%d | %u,%u",
                static_cast<unsigned>(touch_debug_snapshot.point_count),
                static_cast<int>(touch_debug_snapshot.int_level),
                static_cast<unsigned>(touch_debug_snapshot.x),
                static_cast<unsigned>(touch_debug_snapshot.y));
        }
        else
        {
            snprintf(
                touch_debug_text,
                sizeof(touch_debug_text),
                "Touch idle | INT:%d | pts:%u | %s",
                static_cast<int>(touch_debug_snapshot.int_level),
                static_cast<unsigned>(touch_debug_snapshot.point_count),
                touch_debug_snapshot.last_error);
        }
    }

    void apply_touch_runtime_configuration(const char *context)
    {
        if (!touch_enabled)
        {
            return;
        }

        touch_input.set_rotation(kTouchRotation);
        if (kEnableDiagnostics)
        {
            Serial.printf(
                "[touch] rotation applied | mode=%u | context=%s\n",
                static_cast<unsigned>(kTouchRotation),
                context != nullptr ? context : "unknown");
        }
    }

    void lv_tick_task(void *arg)
    {
        (void)arg;
        lv_tick_inc(5);
    }

    bool lcd_color_trans_done_cb(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
    {
        (void)panel;
        (void)edata;
        lv_display_flush_ready(static_cast<lv_display_t *>(user_ctx));
        return false;
    }

    void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
    {
        bump_counter(&lvgl_flush_count);
        const bool ok = lcd_display.lcd_draw_bitmap(
            static_cast<uint16_t>(area->x1),
            static_cast<uint16_t>(area->y1),
            static_cast<uint16_t>(area->x2 + 1),
            static_cast<uint16_t>(area->y2 + 1),
            px_map);

        if (lvgl_flush_count <= 8)
        {
            esp_rom_printf(
                "[lvgl-rom] flush #%lu area=%d,%d-%d,%d ok=%u sync_ready=%u\n",
                static_cast<unsigned long>(lvgl_flush_count),
                static_cast<int>(area->x1),
                static_cast<int>(area->y1),
                static_cast<int>(area->x2),
                static_cast<int>(area->y2),
                ok ? 1U : 0U,
                kLvglSynchronousFlushReady ? 1U : 0U);
        }

        if (kLvglSynchronousFlushReady || !ok)
        {
            lv_display_flush_ready(disp);
        }
    }

    void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
    {
        (void)indev;
        bump_counter(&lvgl_touch_cb_count);

        if (!touch_enabled || !touch_sample_pressed)
        {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        data->point.x = last_touch_x;
        data->point.y = last_touch_y;
        data->state = LV_INDEV_STATE_PRESSED;
    }

    const char *wifi_status_to_text(wl_status_t status)
    {
        switch (status)
        {
        case WL_NO_SHIELD:
            return "NO_SHIELD";
        case WL_IDLE_STATUS:
            return "IDLE";
        case WL_NO_SSID_AVAIL:
            return "NO_SSID";
        case WL_SCAN_COMPLETED:
            return "SCAN_DONE";
        case WL_CONNECTED:
            return "CONNECTED";
        case WL_CONNECT_FAILED:
            return "CONNECT_FAILED";
        case WL_CONNECTION_LOST:
            return "CONNECTION_LOST";
        case WL_DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
        }
    }

    void log_hosted_debug(const char *stage)
    {
        if (!kEnableWifiDebug)
        {
            return;
        }

        const char *safe_stage = stage != nullptr ? stage : "unspecified";
#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)
        int8_t clk = -1;
        int8_t cmd = -1;
        int8_t d0 = -1;
        int8_t d1 = -1;
        int8_t d2 = -1;
        int8_t d3 = -1;
        int8_t rst = -1;
        uint32_t host_major = 0;
        uint32_t host_minor = 0;
        uint32_t host_patch = 0;
        uint32_t slave_major = 0;
        uint32_t slave_minor = 0;
        uint32_t slave_patch = 0;

        hostedGetPins(&clk, &cmd, &d0, &d1, &d2, &d3, &rst);
        hostedGetHostVersion(&host_major, &host_minor, &host_patch);
        hostedGetSlaveVersion(&slave_major, &slave_minor, &slave_patch);
        Serial.printf(
            "[wifi-hosted] t=%lu stage=%s init=%u wifi_active=%u pins clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d rst=%d host=%lu.%lu.%lu slave=%lu.%lu.%lu\n",
            static_cast<unsigned long>(millis()),
            safe_stage,
            hostedIsInitialized() ? 1U : 0U,
            hostedIsWiFiActive() ? 1U : 0U,
            static_cast<int>(clk),
            static_cast<int>(cmd),
            static_cast<int>(d0),
            static_cast<int>(d1),
            static_cast<int>(d2),
            static_cast<int>(d3),
            static_cast<int>(rst),
            static_cast<unsigned long>(host_major),
            static_cast<unsigned long>(host_minor),
            static_cast<unsigned long>(host_patch),
            static_cast<unsigned long>(slave_major),
            static_cast<unsigned long>(slave_minor),
            static_cast<unsigned long>(slave_patch));
#else
        Serial.printf("[wifi-hosted] t=%lu stage=%s ESP-Hosted Wi-Fi is not enabled in sdkconfig\n",
                      static_cast<unsigned long>(millis()),
                      safe_stage);
#endif
    }

    void log_wifi_debug(const char *stage, bool force = false)
    {
        if (!kEnableWifiDebug)
        {
            return;
        }

        const uint32_t now = millis();
        if (!force && last_wifi_debug_log_ms != 0 &&
            static_cast<uint32_t>(now - last_wifi_debug_log_ms) < kWifiDebugLogMs)
        {
            return;
        }
        last_wifi_debug_log_ms = now;

        const wl_status_t live_status = WiFi.status();
        int32_t suspend_remaining_ms = 0;
        if (wifi_suspended_until_ms != 0 && !service_time_reached(now, wifi_suspended_until_ms))
        {
            suspend_remaining_ms = static_cast<int32_t>(wifi_suspended_until_ms - now);
        }

        Serial.printf(
            "[wifi-debug] t=%lu stage=%s data_source=%u c6_transport=%u configured=%u requested=%u started=%u ever_connected=%u live=%d/%s cached=%d/%s ready=%u fault=%u heap_kb=%u req_ms=%lu session_ms=%lu connected_since=%lu disconnected_since=%lu suspend_left_ms=%ld\n",
            static_cast<unsigned long>(now),
            stage != nullptr ? stage : "unspecified",
            static_cast<unsigned>(kDataSource),
            static_cast<unsigned>(kC6Transport),
            wifi_cached_configured ? 1U : 0U,
            wifi_start_requested ? 1U : 0U,
            wifi_started ? 1U : 0U,
            wifi_has_connected_once ? 1U : 0U,
            static_cast<int>(live_status),
            wifi_status_to_text(live_status),
            wifi_cached_status_code,
            wifi_status_to_text(static_cast<wl_status_t>(wifi_cached_status_code)),
            wifi_cached_ready ? 1U : 0U,
            static_cast<unsigned>(wifi_fault_count),
            static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
            static_cast<unsigned long>(wifi_start_requested_ms),
            static_cast<unsigned long>(wifi_session_started_ms),
            static_cast<unsigned long>(wifi_connected_since_ms),
            static_cast<unsigned long>(wifi_disconnected_since_ms),
            static_cast<long>(suspend_remaining_ms));
    }

    bool wifi_is_configured()
    {
        return wifi_credentials_valid(configured_wifi_credentials);
    }

    void restart_wifi_stack(const char *reason)
    {
        if (!wifi_is_configured())
        {
            log_wifi_debug("restart skipped: not configured", true);
            return;
        }

        const char *safe_reason = reason != nullptr ? reason : "unspecified";
        char event_text[128];
        snprintf(event_text, sizeof(event_text), "Wi-Fi recovery: %s", safe_reason);
        store_runtime_event(event_text);
        if (kEnableDiagnostics)
        {
            Serial.printf("[wifi] recovery start | %s\n", safe_reason);
        }
        log_wifi_debug("restart start", true);
        log_hosted_debug("before WiFi.begin");

        const bool aggressive_restart =
            !kHostedWiFiRecoveryConservative && strcmp(safe_reason, "startup") != 0;
        if (aggressive_restart)
        {
            WiFi.disconnect(false, true);
            delay(50);
            WiFi.mode(WIFI_OFF);
            delay(120);
        }
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(false);
        WiFi.begin(configured_wifi_credentials.ssid, configured_wifi_credentials.password);
        log_app_stage("WiFi.begin issued");
        last_wifi_attempt_ms = millis();
        last_wifi_recovery_ms = last_wifi_attempt_ms;
        wifi_session_started_ms = last_wifi_attempt_ms;
        service_fragile_mode = true;
        service_success_mask = 0;
        recompute_service_timing();
        last_wifi_status = WL_IDLE_STATUS;
        wifi_connected_since_ms = 0;
        wifi_started = true;
        wifi_manual_reconnect_count = 0;
        if (wifi_disconnected_since_ms == 0)
        {
            wifi_disconnected_since_ms = last_wifi_attempt_ms;
        }
        if (kEnableDiagnostics)
        {
            Serial.println("[wifi] recovery requested");
        }
        log_wifi_debug("restart complete", true);
        log_hosted_debug("after WiFi.begin");
    }

    void suspend_wifi_stack(const char *reason)
    {
        const char *safe_reason = reason != nullptr ? reason : "hosted fault";
        char event_text[128];
        snprintf(event_text, sizeof(event_text), "Wi-Fi suspended: %s", safe_reason);
        store_runtime_event(event_text);

        if (wifi_credentials_commit_pending)
        {
            WifiCredentials fallback = {};
            if (settings_store.load_wifi_credentials(&fallback) && wifi_credentials_valid(fallback))
            {
                configured_wifi_credentials = fallback;
                set_wifi_setup_status("New Wi-Fi failed; reverted to saved network.");
                store_runtime_event("Wi-Fi reverted to last saved credentials");
            }
            pending_wifi_credentials = {};
            wifi_credentials_commit_pending = false;
        }

        if (!kHostedWiFiRecoveryConservative)
        {
            WiFi.disconnect(false, true);
            delay(50);
            WiFi.mode(WIFI_OFF);
            delay(120);
        }

        wifi_started = false;
        wifi_start_requested = false;
        service_fragile_mode = true;
        service_success_mask = 0;
        recompute_service_timing();
        wifi_connected_since_ms = 0;
        wifi_disconnected_since_ms = 0;
        wifi_session_started_ms = 0;
        wifi_has_connected_once = false;
        last_successful_service_ms = 0;
        wifi_cached_connected = false;
        wifi_cached_ready = false;
        wifi_cached_status_code = WL_IDLE_STATUS;
        wifi_suspended_until_ms = millis() + kWifiSuspendCooldownMs;
        wifi_fault_count = static_cast<uint8_t>(wifi_fault_count + 1U);
        wifi_manual_reconnect_count = 0;
        note_service_backoff_fault();
        snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Wi-Fi offline after hosted fault");
        publish_wifi_ui_state();
        log_wifi_debug("suspended", true);
        log_hosted_debug("suspended");
    }

    void begin_wifi()
    {
        if (kUseC6WorkerData)
        {
            wifi_cached_configured = true;
            wifi_cached_connected = false;
            wifi_cached_ready = false;
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", c6_transport_wait_text());
            store_runtime_event(kUseC6SdioTransport ? "P4 network owner set to C6 SDIO worker" : "P4 network owner set to C6 UART worker");
            publish_wifi_ui_state();
            log_wifi_debug("P4 Wi-Fi skipped: C6 worker owns networking", true);
            return;
        }

        if (kDisableWifiForTouchIsolation || kNetworkMode == brief::kNetworkModeOffline)
        {
            store_runtime_event("Wi-Fi disabled for touch isolation");
            log_wifi_debug("Wi-Fi disabled by config", true);
            return;
        }

        if (!wifi_is_configured())
        {
            wifi_cached_configured = false;
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Open Settings to add Wi-Fi");
            publish_wifi_ui_state();
            log_wifi_debug("Wi-Fi skipped: credentials missing", true);
            return;
        }

        wifi_start_requested = true;
        wifi_started = false;
        wifi_start_requested_ms = millis();
        service_fragile_mode = true;
        service_success_mask = 0;
        recompute_service_timing();
        wifi_cached_configured = true;
        snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Wi-Fi waiting for hosted link");
        store_runtime_event("Wi-Fi startup queued");
        publish_wifi_ui_state();
        log_wifi_debug("Wi-Fi startup queued", true);
        log_hosted_debug("startup queued");
    }

    bool wifi_ready_for_requests()
    {
        if (kUseC6WorkerData)
        {
            return false;
        }

        if (kDisableWifiForTouchIsolation)
        {
            return false;
        }

        if (!wifi_cached_connected)
        {
            return false;
        }

        if (wifi_connected_since_ms == 0)
        {
            return false;
        }

        return service_time_reached(millis(), wifi_connected_since_ms + kWifiWarmupMs);
    }

    void update_wifi_cache_from_live()
    {
        if (kUseC6WorkerData)
        {
            wifi_cached_configured = kUseC6SdioTransport || (kUseC6UartTransport && kC6SerialPinsConfigured);
            if (c6_snapshot_valid)
            {
                wifi_cached_connected = c6_dashboard_snapshot.system.wifi_connected != 0;
                wifi_cached_ready = c6_dashboard_snapshot.system.wifi_ready != 0;
                wifi_cached_status_code = wifi_cached_connected ? WL_CONNECTED : WL_IDLE_STATUS;
                snprintf(
                    wifi_cached_status_text,
                    sizeof(wifi_cached_status_text),
                    "%s",
                    c6_dashboard_snapshot.system.status_text[0] != '\0' ? c6_dashboard_snapshot.system.status_text : "C6 worker snapshot received");
            }
            else
            {
                wifi_cached_connected = false;
                wifi_cached_ready = false;
                wifi_cached_status_code = WL_IDLE_STATUS;
                snprintf(
                    wifi_cached_status_text,
                    sizeof(wifi_cached_status_text),
                    "%s",
                    c6_transport_wait_text());
            }
            publish_wifi_ui_state();
            return;
        }

        wifi_cached_configured = wifi_is_configured();

        if (!wifi_cached_configured)
        {
            wifi_cached_status_code = WL_NO_SSID_AVAIL;
            wifi_cached_connected = false;
            wifi_cached_ready = false;
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Open Settings to add Wi-Fi");
            publish_wifi_ui_state();
            return;
        }

        if (wifi_suspended_until_ms != 0 && !service_time_reached(millis(), wifi_suspended_until_ms))
        {
            wifi_cached_status_code = WL_IDLE_STATUS;
            wifi_cached_connected = false;
            wifi_cached_ready = false;
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "Wi-Fi cooling down after fault (%us)", static_cast<unsigned>((wifi_suspended_until_ms - millis()) / 1000U));
            publish_wifi_ui_state();
            return;
        }

        if (wifi_start_requested && !wifi_started)
        {
            wifi_cached_status_code = WL_IDLE_STATUS;
            wifi_cached_connected = false;
            wifi_cached_ready = false;
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Wi-Fi waiting for hosted link");
            publish_wifi_ui_state();
            return;
        }

        if (wifi_session_started_ms != 0 &&
            !service_time_reached(millis(), wifi_session_started_ms + kWifiPostBeginQuietMs))
        {
            wifi_cached_status_code = WL_IDLE_STATUS;
            wifi_cached_connected = false;
            wifi_cached_ready = false;
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Wi-Fi starting...");
            publish_wifi_ui_state();
            return;
        }

        const wl_status_t current_status = WiFi.status();
        wifi_cached_status_code = static_cast<int>(current_status);

        if (current_status != last_wifi_status)
        {
            if (current_status == WL_CONNECTED)
            {
                wifi_connected_since_ms = millis();
                wifi_disconnected_since_ms = 0;
                wifi_has_connected_once = true;
                time_configured = false;
                time_sync_requested = false;
                last_time_sync_request_ms = 0;
                wifi_fault_count = 0;
                wifi_manual_reconnect_count = 0;
                clear_service_fault_window();
                if (wifi_credentials_commit_pending)
                {
                    settings_store.save_wifi_credentials(configured_wifi_credentials);
                    wifi_credentials_commit_pending = false;
                    pending_wifi_credentials = {};
                    set_wifi_setup_status("Wi-Fi connected and saved.");
                    store_runtime_event("Wi-Fi credentials saved after successful connection");
                }
                char event_text[96];
                snprintf(event_text, sizeof(event_text), "Wi-Fi connected to %s", WiFi.SSID().c_str());
                store_runtime_event(event_text);
            }
            else
            {
                wifi_connected_since_ms = 0;
                if (wifi_disconnected_since_ms == 0)
                {
                    wifi_disconnected_since_ms = millis();
                }
                if (last_wifi_status == WL_CONNECTED)
                {
                    store_runtime_event("Wi-Fi disconnected");
                }
            }
            last_wifi_status = current_status;
            log_wifi_debug("status transition", true);
            log_hosted_debug("status transition");
        }

        if (current_status == WL_CONNECTED)
        {
            configure_time_if_needed();
            wifi_cached_connected = true;
            wifi_cached_ready = wifi_ready_for_requests();
            if (wifi_cached_ready)
            {
                snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "Wi-Fi connected to %s", WiFi.SSID().c_str());
            }
            else
            {
                snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Wi-Fi warm-up before live fetches");
            }
            publish_wifi_ui_state();
            return;
        }

        wifi_cached_connected = false;
        wifi_cached_ready = false;
        if (wifi_disconnected_since_ms == 0)
        {
            wifi_disconnected_since_ms = millis();
        }
        snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Wi-Fi reconnecting in background...");
        publish_wifi_ui_state();
        log_wifi_debug("poll disconnected");
    }

    void perform_wifi_scan()
    {
        if (kUseC6WorkerData)
        {
            set_wifi_setup_status("Wi-Fi scanning is unavailable while the C6 worker owns networking.");
            return;
        }

        if (wifi_scan_in_progress)
        {
            set_wifi_setup_status("Wi-Fi scan already in progress.");
            return;
        }

        wifi_scan_in_progress = true;
        wifi_scan_requested_async = true;
        set_wifi_setup_status("Scanning nearby Wi-Fi networks...");

        if (!wifi_started)
        {
            WiFi.mode(WIFI_STA);
            delay(50);
        }

        WiFi.scanDelete();
        const bool started = WiFi.scanNetworks(true, false);
        if (!started)
        {
            wifi_scan_in_progress = false;
            wifi_scan_requested_async = false;
            set_wifi_setup_status("Wi-Fi scan could not be started.");
        }
    }

    void poll_wifi_scan_progress()
    {
        if (!wifi_scan_in_progress)
        {
            return;
        }

        const int scan_status = WiFi.scanComplete();
        if (scan_status == WIFI_SCAN_RUNNING)
        {
            return;
        }

        wifi_scan_requested_async = false;

        if (scan_status <= 0)
        {
            set_wifi_scan_options("No networks found");
            set_wifi_setup_status("No nearby Wi-Fi networks were found.");
            wifi_scan_in_progress = false;
            WiFi.scanDelete();
            return;
        }

        char options[1024] = "";
        bool first = true;
        int visible_count = 0;
        for (int i = 0; i < scan_status; ++i)
        {
            const String ssid = WiFi.SSID(i);
            if (ssid.length() == 0)
            {
                continue;
            }

            if (!first)
            {
                strncat(options, "\n", sizeof(options) - strlen(options) - 1);
            }
            strncat(options, ssid.c_str(), sizeof(options) - strlen(options) - 1);
            first = false;
            ++visible_count;
        }

        if (first)
        {
            set_wifi_scan_options("No networks found");
            set_wifi_setup_status("Nearby networks were hidden or empty.");
        }
        else
        {
            set_wifi_scan_options(options);
            char status[96];
            snprintf(status, sizeof(status), "Found %d Wi-Fi network%s.", visible_count, visible_count == 1 ? "" : "s");
            set_wifi_setup_status(status);
        }

        wifi_scan_in_progress = false;
        WiFi.scanDelete();
    }

    void connect_to_selected_wifi(const char *ssid, const char *password)
    {
        if (ssid == nullptr || ssid[0] == '\0')
        {
            set_wifi_setup_status("Enter a Wi-Fi name.");
            return;
        }

        pending_wifi_credentials = {};
        snprintf(pending_wifi_credentials.ssid, sizeof(pending_wifi_credentials.ssid), "%s", ssid);
        snprintf(pending_wifi_credentials.password, sizeof(pending_wifi_credentials.password), "%s", password != nullptr ? password : "");
        configured_wifi_credentials = pending_wifi_credentials;
        wifi_credentials_commit_pending = true;

        char status[96];
        snprintf(status, sizeof(status), "Connecting to %s...", configured_wifi_credentials.ssid);
        set_wifi_setup_status(status);
        store_runtime_event("Wi-Fi network change requested");
        wifi_suspended_until_ms = 0;
        wifi_start_requested = false;
        restart_wifi_stack("settings change");
    }

    void mark_display_settings_dirty()
    {
        display_settings_dirty = true;
        display_settings_dirty_since_ms = millis();
    }

    void persist_display_settings_if_due()
    {
        if (!display_settings_dirty)
        {
            return;
        }

        if (!service_time_reached(millis(), display_settings_dirty_since_ms + kSettingsPersistDelayMs))
        {
            return;
        }

        settings_store.save_brightness(display_settings.brightness_percent);
        settings_store.save_dark_mode(display_settings.dark_mode);
        display_settings_dirty = false;
    }

    void normalize_display_settings()
    {
        if (display_settings.brightness_percent < kMinimumReadableBrightnessPercent ||
            display_settings.brightness_percent > 100)
        {
            char event_text[96];
            snprintf(
                event_text,
                sizeof(event_text),
                "Brightness recovered from %u%%",
                static_cast<unsigned>(display_settings.brightness_percent));
            store_runtime_event(event_text);
            display_settings.brightness_percent = kDefaultBrightnessPercent;
            mark_display_settings_dirty();
        }
    }

    void draw_display_probe_pattern()
    {
        if (!kEnableDisplayProbePattern)
        {
            return;
        }

        constexpr uint16_t kProbeChunkLines = 40;
        constexpr uint16_t kProbeColors[] = {
            0xF800, // red
            0x07E0, // green
            0x001F, // blue
            0xFFFF, // white
            0xFFE0, // yellow
        };
        constexpr uint16_t kProbeBandCount = sizeof(kProbeColors) / sizeof(kProbeColors[0]);
        constexpr uint16_t kProbeBandHeight = LCD_V_RES / kProbeBandCount;
        const size_t buffer_pixels = LCD_H_RES * kProbeChunkLines;
        uint16_t *buffer = static_cast<uint16_t *>(heap_caps_malloc(buffer_pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buffer == nullptr)
        {
            buffer = static_cast<uint16_t *>(heap_caps_malloc(buffer_pixels * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (buffer == nullptr)
        {
            log_app_stage("display probe skipped: no buffer");
            return;
        }

        log_app_stage("display probe pattern start");
        for (uint16_t y = 0; y < LCD_V_RES; y = static_cast<uint16_t>(y + kProbeChunkLines))
        {
            uint16_t lines = kProbeChunkLines;
            if (y + lines > LCD_V_RES)
            {
                lines = LCD_V_RES - y;
            }
            uint16_t band = y / kProbeBandHeight;
            if (band >= kProbeBandCount)
            {
                band = kProbeBandCount - 1;
            }

            const uint16_t color = kProbeColors[band];
            const size_t active_pixels = LCD_H_RES * lines;
            for (size_t i = 0; i < active_pixels; ++i)
            {
                buffer[i] = color;
            }

            lcd_display.lcd_draw_bitmap(
                0,
                y,
                LCD_H_RES,
                static_cast<uint16_t>(y + lines),
                reinterpret_cast<uint8_t *>(buffer));
        }

        free(buffer);
        delay(kDisplayProbePatternMs);
        log_app_stage("display probe pattern complete");
    }

    void configure_time_if_needed()
    {
        if (!wifi_cached_connected)
        {
            return;
        }

        const time_t now = time(nullptr);
        if (service_wall_clock_valid(now))
        {
            time_configured = true;
            return;
        }

        if (time_sync_requested &&
            !service_time_reached(millis(), last_time_sync_request_ms + kTimeSyncRetryMs))
        {
            return;
        }

        configTzTime("GMT0BST,M3.5.0/1,M10.5.0/2", "pool.ntp.org", "time.google.com", "time.nist.gov");
        time_sync_requested = true;
        last_time_sync_request_ms = millis();
        store_runtime_event("Clock sync requested");
    }

    void update_clock()
    {
        if (millis() - last_clock_update_ms < kClockUpdatePeriodMs)
        {
            return;
        }
        last_clock_update_ms = millis();

        char time_text[16];
        char day_text[16];
        time_t now = time(nullptr);
        if (!service_wall_clock_valid(now))
        {
            snprintf(time_text, sizeof(time_text), "--:--");
            snprintf(day_text, sizeof(day_text), "Waiting");
        }
        else
        {
            struct tm local_time = {};
            localtime_r(&now, &local_time);
            strftime(time_text, sizeof(time_text), "%H:%M", &local_time);
            strftime(day_text, sizeof(day_text), "%a %d %b", &local_time);
        }
        dashboard.update_clock(time_text, day_text);
    }

    void update_wifi_status(bool force = false)
    {
        drain_wifi_ui_state();

        if (kDisableWifiForTouchIsolation)
        {
            dashboard.update_connectivity(false, false, "Wi-Fi disabled for touch isolation");
            return;
        }

        if (!force && millis() - last_wifi_status_poll_ms < kWifiStatusPollMs)
        {
            return;
        }
        last_wifi_status_poll_ms = millis();

        dashboard.update_connectivity(
            latest_wifi_ui_state.configured,
            latest_wifi_ui_state.connected,
            latest_wifi_ui_state.status_text);
    }

    void begin_c6_transport()
    {
        if (!kUseC6WorkerData)
        {
            return;
        }

        if (kUseC6SdioTransport)
        {
            c6_receiver.begin(nullptr);
            c6_transport_started = false;
            store_runtime_event("C6 SDIO snapshot transport selected");
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", c6_transport_wait_text());
            publish_wifi_ui_state();
            return;
        }

        if (!kUseC6UartTransport)
        {
            c6_receiver.begin(nullptr);
            store_runtime_event("C6 worker transport not selected");
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "C6 transport not selected");
            publish_wifi_ui_state();
            return;
        }

        if (!kC6SerialPinsConfigured)
        {
            c6_receiver.begin(nullptr);
            store_runtime_event("C6 transport pins not configured");
            snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", c6_transport_wait_text());
            publish_wifi_ui_state();
            return;
        }

        c6_worker_serial.begin(LONDONBRIEF_C6_SERIAL_BAUD, SERIAL_8N1, kC6SerialRxPin, kC6SerialTxPin);
        c6_receiver.begin(&c6_worker_serial);
        c6_transport_started = true;
        char event_text[112];
        snprintf(
            event_text,
            sizeof(event_text),
            "C6 UART%d started RX%d TX%d",
            kC6SerialPort,
            kC6SerialRxPin,
            kC6SerialTxPin);
        store_runtime_event(event_text);
    }

    void send_c6_refresh_request(brief::Domain domain)
    {
        if (kUseC6SdioTransport && !c6_transport_started)
        {
            c6_refresh_request_pending = false;
            store_runtime_event("C6 refresh waiting for SDIO transport driver");
            return;
        }

        if (!kUseC6WorkerData || !c6_transport_started)
        {
            c6_refresh_request_pending = true;
            store_runtime_event("C6 refresh requested before transport was ready");
            return;
        }

        brief::RefreshRequest request = {};
        request.header.version = brief::kProtocolVersion;
        request.header.type = static_cast<uint8_t>(brief::kMsgRefreshRequest);
        request.header.payload_size = sizeof(brief::RefreshRequest) - sizeof(brief::PacketHeader);
        request.header.sequence = c6_snapshot_count + 1U;
        request.header.uptime_ms = millis();
        request.domain = static_cast<uint8_t>(domain);
        request.force = 1;

        const auto frame_header = brief_transport::make_header(
            static_cast<uint8_t>(brief::kMsgRefreshRequest),
            request.header.sequence,
            millis(),
            reinterpret_cast<const uint8_t *>(&request),
            sizeof(request));
        c6_worker_serial.write(reinterpret_cast<const uint8_t *>(&frame_header), sizeof(frame_header));
        c6_worker_serial.write(reinterpret_cast<const uint8_t *>(&request), sizeof(request));
        c6_worker_serial.flush();
        c6_refresh_request_pending = false;
        store_runtime_event("C6 refresh request sent");
    }

    void poll_c6_transport()
    {
        if (!kUseC6WorkerData || !c6_transport_started)
        {
            return;
        }

        brief::DashboardSnapshot incoming = {};
        bool received = false;
        while (c6_receiver.poll(&incoming))
        {
            c6_dashboard_snapshot = incoming;
            c6_snapshot_valid = true;
            c6_snapshot_count++;
            last_c6_snapshot_ms = millis();
            received = true;
        }

        if (received)
        {
            update_wifi_cache_from_live();
        }

        if (c6_refresh_request_pending)
        {
            send_c6_refresh_request(brief::kDomainAll);
        }
    }

    uint8_t estimate_battery_percent(float voltage)
    {
        if (voltage <= LONDONBRIEF_BATTERY_EMPTY_V)
        {
            return 0;
        }
        if (voltage >= LONDONBRIEF_BATTERY_FULL_V)
        {
            return 100;
        }

        const float span = LONDONBRIEF_BATTERY_FULL_V - LONDONBRIEF_BATTERY_EMPTY_V;
        if (span <= 0.01f)
        {
            return 0;
        }

        return static_cast<uint8_t>(constrain(static_cast<int>(lroundf(((voltage - LONDONBRIEF_BATTERY_EMPTY_V) * 100.0f) / span)), 0, 100));
    }

    void publish_battery_status()
    {
        dashboard.update_battery(battery_status.percent, battery_status.voltage, battery_status.status_text);
    }

    void update_battery_monitor(bool force = false)
    {
        if (!battery_status.enabled)
        {
            if (force)
            {
                battery_status.valid = false;
                battery_status.percent = 0;
                battery_status.voltage = 0.0f;
                snprintf(battery_status.status_text, sizeof(battery_status.status_text), "%s", "Power connected / battery monitor off");
                publish_battery_status();
            }
            return;
        }

#if LONDONBRIEF_ENABLE_BATTERY_MONITOR
        const uint32_t now = millis();
        if (!force && !service_time_reached(now, battery_status.last_sample_ms + kBatterySamplePeriodMs))
        {
            return;
        }

        if (!service_time_reached(now, kBatteryStartupDelayMs))
        {
            battery_status.valid = false;
            snprintf(battery_status.status_text, sizeof(battery_status.status_text), "%s", "Settling after boot");
            publish_battery_status();
            return;
        }

        if (!force && (touch_status_pressed || (last_touch_seen_ms != 0 && !service_time_reached(now, last_touch_seen_ms + kBatteryTouchQuietMs))))
        {
            return;
        }

        if (!battery_adc_checked)
        {
            battery_adc_checked = true;
            if (digitalPinToAnalogChannel(BATTERY_SENSE_PIN) < 0)
            {
                battery_status.enabled = false;
                battery_status.valid = false;
                battery_status.percent = 0;
                battery_status.voltage = 0.0f;
                snprintf(
                    battery_status.status_text,
                    sizeof(battery_status.status_text),
                    "GPIO%d is not exposed as an Arduino ADC pin",
                    BATTERY_SENSE_PIN);
                publish_battery_status();
                store_runtime_event("Battery monitor disabled: ADC pin unsupported");
                return;
            }
        }

        analogReadResolution(12);
        analogSetPinAttenuation(BATTERY_SENSE_PIN, ADC_11db);

        uint32_t raw_sum = 0;
        constexpr uint8_t kSamples = 8;
        for (uint8_t i = 0; i < kSamples; ++i)
        {
            raw_sum += static_cast<uint32_t>(analogRead(BATTERY_SENSE_PIN));
            delay(2);
        }

        const float raw_average = static_cast<float>(raw_sum) / static_cast<float>(kSamples);
        const float adc_voltage = (raw_average / 4095.0f) * 3.3f;
        const float battery_voltage = adc_voltage * LONDONBRIEF_BATTERY_DIVIDER_RATIO;
        const uint8_t percent = estimate_battery_percent(battery_voltage);

        battery_status.valid = true;
        battery_status.percent = percent;
        battery_status.voltage = battery_voltage;
        battery_status.last_sample_ms = now;

        if (percent <= 10)
        {
            snprintf(battery_status.status_text, sizeof(battery_status.status_text), "%s", "Critical");
        }
        else if (percent <= 20)
        {
            snprintf(battery_status.status_text, sizeof(battery_status.status_text), "%s", "Low");
        }
        else
        {
            snprintf(battery_status.status_text, sizeof(battery_status.status_text), "%s", "Healthy");
        }
        publish_battery_status();
#else
        (void)force;
#endif
    }

    void enter_shutdown_sleep()
    {
        store_runtime_event("Shutdown requested");
        dashboard.update_runtime("Shutdown requested", "Backlight is off. Power-cycle the device to wake.");
        lcd_display.set_backlight_percent(0);

        if (!kUseC6WorkerData && wifi_started && !kHostedWiFiRecoveryConservative)
        {
            WiFi.disconnect(false, true);
            WiFi.mode(WIFI_OFF);
        }

        delay(300);
        esp_deep_sleep_start();
    }

    void poll_touch_input()
    {
        if (!touch_enabled)
        {
            if (kEnableDiagnostics && millis() - last_touch_debug_log_ms >= 5000)
            {
                last_touch_debug_log_ms = millis();
                touch_input.getDebugSnapshot(&touch_debug_snapshot);
                refresh_touch_debug_text();
                Serial.printf("[touch] disabled | %s\n", touch_debug_text);
            }
            return;
        }

        if (millis() - last_touch_poll_ms < kTouchPollPeriodMs)
        {
            return;
        }

        last_touch_poll_ms = millis();
        bump_counter(&touch_poll_count);

        uint16_t x = 0;
        uint16_t y = 0;
        const bool pressed_now = touch_input.getTouch(&x, &y);
        touch_input.getDebugSnapshot(&touch_debug_snapshot);
        refresh_touch_debug_text();
        touch_sample_pressed = pressed_now;

        if (touch_debug_snapshot.int_level == 0)
        {
            last_touch_interrupt_low_ms = last_touch_poll_ms;
            touch_activity_ever_seen = true;
        }

        if (touch_debug_snapshot.controller_ready && touch_debug_snapshot.last_read_ok)
        {
            consecutive_touch_read_failures = 0;
            last_touch_read_ok_ms = last_touch_poll_ms;
            touch_read_ever_succeeded = true;
        }
        else if (consecutive_touch_read_failures < UINT8_MAX)
        {
            ++consecutive_touch_read_failures;
        }

        if (pressed_now)
        {
            bump_counter(&touch_press_sample_count);
            last_touch_x = x;
            last_touch_y = y;
            last_touch_seen_ms = last_touch_poll_ms;
            last_touch_press_ms = last_touch_seen_ms;
            touch_status_pressed = true;
            touch_status_dirty = true;
            touch_status_idle = false;
            touch_point_ever_seen = true;

            if (!last_touch_pressed)
            {
                touch_feedback_pending = true;
                touch_feedback_x = x;
                touch_feedback_y = y;
                touch_seen_once = true;
                touch_ready_confirmed = true;
                touch_activity_ever_seen = true;
                store_runtime_event("Touch confirmed after first valid point");
            }
        }
        else if (last_touch_pressed)
        {
            touch_status_pressed = false;
            touch_status_dirty = true;
        }

        if (kEnableDiagnostics && millis() - last_touch_debug_log_ms >= 1000)
        {
            last_touch_debug_log_ms = millis();
            Serial.printf("[touch] %s\n", touch_debug_text);
        }

        last_touch_pressed = pressed_now;
    }

    void recover_touch_if_stalled()
    {
        if (!touch_enabled || !touch_seen_once)
        {
            return;
        }

        if (touch_status_pressed || touch_sample_pressed)
        {
            return;
        }

        const uint32_t now = millis();
        const bool read_fault =
            !touch_debug_snapshot.controller_ready ||
            !touch_debug_snapshot.last_read_ok ||
            consecutive_touch_read_failures >= kTouchRecoveryReadFailureThreshold;
        if (!read_fault)
        {
            return;
        }

        if (!service_time_reached(now, last_touch_recovery_attempt_ms + kTouchRecoveryCooldownMs))
        {
            return;
        }

        last_touch_recovery_attempt_ms = now;
        Serial.println("[touch] recovery: controller silent after prior touches, reinitializing");
        store_runtime_event("Touch recovery reinit requested");

        touch_enabled = touch_input.begin();
        touch_input.getDebugSnapshot(&touch_debug_snapshot);
        refresh_touch_debug_text();
        Serial.printf("[touch] recovery result | enabled=%d | %s\n", touch_enabled ? 1 : 0, touch_debug_text);

        touch_sample_pressed = false;
        last_touch_pressed = false;
        touch_status_pressed = false;
        touch_status_dirty = true;
        touch_status_idle = true;
        last_touch_poll_ms = now;
        consecutive_touch_read_failures = 0;
        last_touch_read_ok_ms = touch_enabled ? now : 0;
        touch_init_completed_ms = touch_enabled ? now : 0;
        last_touch_interrupt_low_ms = 0;
        touch_ready_confirmed = false;
        touch_read_ever_succeeded = false;
        touch_activity_ever_seen = false;
        touch_point_ever_seen = false;
        touch_seen_once = false;

        if (touch_enabled)
        {
            last_touch_press_ms = now;
            store_runtime_event("Touch recovery succeeded");
        }
        else
        {
            store_runtime_event("Touch recovery failed");
        }
    }

    void emit_touch_heartbeat()
    {
        if (!kEnableDiagnostics)
        {
            return;
        }

        if (millis() - last_touch_heartbeat_ms < 5000)
        {
            return;
        }

        last_touch_heartbeat_ms = millis();
        touch_input.getDebugSnapshot(&touch_debug_snapshot);
        refresh_touch_debug_text();
        Serial.printf(
            "[touch-heartbeat] enabled=%d seen=%d pressed=%d sample=%d | %s\n",
            touch_enabled ? 1 : 0,
            touch_seen_once ? 1 : 0,
            touch_status_pressed ? 1 : 0,
            touch_sample_pressed ? 1 : 0,
            touch_debug_text);
    }

    void emit_loop_heartbeat()
    {
        if (!kEnableDiagnostics)
        {
            return;
        }

        if (millis() - last_loop_heartbeat_ms < 5000)
        {
            return;
        }

        last_loop_heartbeat_ms = millis();
        Serial.printf(
            "[loop-heartbeat] iter=%lu wifi=%d touch_enabled=%d display=%d heap_kb=%u flush=%lu indev=%lu polls=%lu presses=%lu\n",
            static_cast<unsigned long>(loop_iteration_count),
            latest_wifi_ui_state.status_code,
            touch_enabled ? 1 : 0,
            display != nullptr ? 1 : 0,
            static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
            static_cast<unsigned long>(lvgl_flush_count),
            static_cast<unsigned long>(lvgl_touch_cb_count),
            static_cast<unsigned long>(touch_poll_count),
            static_cast<unsigned long>(touch_press_sample_count));
        ESP_LOGW(
            kAppLogTag,
            "loop iter=%lu wifi=%d touch=%d display=%d heap_kb=%u flush=%lu indev=%lu polls=%lu presses=%lu",
            static_cast<unsigned long>(loop_iteration_count),
            latest_wifi_ui_state.status_code,
            touch_enabled ? 1 : 0,
            display != nullptr ? 1 : 0,
            static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
            static_cast<unsigned long>(lvgl_flush_count),
            static_cast<unsigned long>(lvgl_touch_cb_count),
            static_cast<unsigned long>(touch_poll_count),
            static_cast<unsigned long>(touch_press_sample_count));
    }

    void process_touch_feedback()
    {
        if (touch_feedback_pending)
        {
            touch_feedback_pending = false;
            dashboard.show_touch_feedback(
                static_cast<lv_coord_t>(touch_feedback_x),
                static_cast<lv_coord_t>(touch_feedback_y));
        }

        if (touch_status_dirty)
        {
            touch_status_dirty = false;
            if (touch_status_pressed)
            {
                touch_status_idle = false;
                dashboard.update_touch_status(true, true, last_touch_x, last_touch_y);
            }
        }

        if (touch_enabled && !touch_status_pressed && !touch_status_idle && millis() - last_touch_seen_ms >= 900)
        {
            touch_status_idle = true;
            dashboard.update_touch_status(true, false, last_touch_x, last_touch_y);
        }
    }

    void update_runtime_panel(const WeatherData &weather, const TflData &tfl, const NewsData &news, const CalendarData &calendar)
    {
        drain_wifi_ui_state();
        const char *wifi_text = londonbrief_runtime_wifi_text(
            latest_wifi_ui_state.configured,
            latest_wifi_ui_state.connected,
            latest_wifi_ui_state.status_text);
        const char *touch_text = !touch_enabled ? "Unavailable" : touch_status_pressed ? "Pressed"
                                                              : touch_seen_once        ? "Ready"
                                                                                       : "No samples yet";

        snprintf(
            runtime_summary_text,
            sizeof(runtime_summary_text),
            "Wi-Fi: %s\nTouch: %s\nQueue: %s\nHeap free: %u KB\nReset reason: %s",
            wifi_text,
            touch_text,
            service_scheduler_status_text,
            static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
            startup_reset_reason);

        if (weather.loading || tfl.loading || news.loading || calendar.loading)
        {
            snprintf(runtime_detail_text, sizeof(runtime_detail_text), "Queue order: %s\nBackoff level: %u\nRecent event: %s", service_queue_preview_text, static_cast<unsigned>(service_backoff_level), last_runtime_event);
        }
        else if (!touch_enabled)
        {
            snprintf(runtime_detail_text, sizeof(runtime_detail_text), "%s\nQueue order: %s\nRecent event: %s", touch_debug_text, service_queue_preview_text, last_runtime_event);
        }
        else if (touch_enabled && !touch_seen_once)
        {
            snprintf(runtime_detail_text, sizeof(runtime_detail_text), "%s\nQueue order: %s\nRecent event: %s", touch_debug_text, service_queue_preview_text, last_runtime_event);
        }
        else if (weather.stale || !weather.valid)
        {
            snprintf(runtime_detail_text, sizeof(runtime_detail_text), "Weather: %s\nQueue order: %s\nRecent event: %s", weather.error, service_queue_preview_text, last_runtime_event);
        }
        else if (tfl.stale || !tfl.valid)
        {
            snprintf(runtime_detail_text, sizeof(runtime_detail_text), "TfL: %s\nQueue order: %s\nRecent event: %s", tfl.error, service_queue_preview_text, last_runtime_event);
        }
        else if (news.stale || !news.valid)
        {
            snprintf(runtime_detail_text, sizeof(runtime_detail_text), "News: %s\nQueue order: %s\nRecent event: %s", news.error, service_queue_preview_text, last_runtime_event);
        }
        else if (calendar.stale || !calendar.valid)
        {
            snprintf(runtime_detail_text, sizeof(runtime_detail_text), "Calendar: %s\nQueue order: %s\nRecent event: %s", calendar.error, service_queue_preview_text, last_runtime_event);
        }
        else
        {
            snprintf(runtime_detail_text, sizeof(runtime_detail_text), "All dashboard services healthy\nQueue order: %s\nBackoff level: %u\nRecent event: %s", service_queue_preview_text, static_cast<unsigned>(service_backoff_level), last_runtime_event);
        }

        dashboard.update_runtime(runtime_summary_text, runtime_detail_text);
    }

    void update_runtime_panel_from_c6()
    {
        const uint32_t now = millis();
        const bool transport_ready = c6_transport_started && c6_receiver.active();
        const uint32_t age_ms = c6_snapshot_valid ? now - last_c6_snapshot_ms : 0;
        char age_text[32];
        if (c6_snapshot_valid)
        {
            snprintf(age_text, sizeof(age_text), "%lus ago", static_cast<unsigned long>(age_ms / 1000U));
        }
        else
        {
            snprintf(age_text, sizeof(age_text), "%s", "none yet");
        }

        snprintf(
            runtime_summary_text,
            sizeof(runtime_summary_text),
            "Data source: C6 worker\nTransport: %s%s\nSnapshots: %lu\nHeap free: %u KB",
            c6_transport_name(),
            transport_ready ? " listening" : " not ready",
            static_cast<unsigned long>(c6_snapshot_count),
            static_cast<unsigned>(ESP.getFreeHeap() / 1024U));

        snprintf(
            runtime_detail_text,
            sizeof(runtime_detail_text),
            "%s\nLast snapshot: %s\nFrame errors: %lu CRC, %lu framing\nBattery: %s\nRecent event: %s",
            c6_transport_wait_text(),
            age_text,
            static_cast<unsigned long>(c6_receiver.crc_errors()),
            static_cast<unsigned long>(c6_receiver.framing_errors()),
            battery_status.status_text,
            last_runtime_event);

        dashboard.update_runtime(runtime_summary_text, runtime_detail_text);
    }

    void sync_dashboard()
    {
        if (millis() - last_ui_sync_ms < kUiSyncPeriodMs)
        {
            return;
        }
        last_ui_sync_ms = millis();
        dashboard.tick_animations();

        drain_wifi_ui_state();
        weather_service.snapshot(&dashboard_weather_snapshot);
        tfl_service.snapshot(&dashboard_tfl_snapshot);
        news_service.snapshot(&dashboard_news_snapshot);
        calendar_service.snapshot(&dashboard_calendar_snapshot);

        if (kDiagnosticLightDashboardSync)
        {
            update_runtime_panel(dashboard_weather_snapshot, dashboard_tfl_snapshot, dashboard_news_snapshot, dashboard_calendar_snapshot);
            return;
        }

        if (kUseC6WorkerData)
        {
            dashboard.update_calendar(dashboard_calendar_snapshot);
            if (c6_snapshot_valid)
            {
                dashboard.update_snapshot(c6_dashboard_snapshot);
            }
            else
            {
                brief::DashboardSnapshot waiting_snapshot = {};
                waiting_snapshot.weather.now.state = brief::kDataStateWaiting;
                snprintf(waiting_snapshot.weather.now.location, sizeof(waiting_snapshot.weather.now.location), "%s", LONDONBRIEF_LOCATION_NAME);
                snprintf(waiting_snapshot.weather.summary_text, sizeof(waiting_snapshot.weather.summary_text), "%s", "Waiting for C6 worker snapshot");
                snprintf(waiting_snapshot.weather.next_change_text, sizeof(waiting_snapshot.weather.next_change_text), "%s", c6_transport_wait_text());
                waiting_snapshot.tfl.state = brief::kDataStateWaiting;
                snprintf(waiting_snapshot.tfl.summary_text, sizeof(waiting_snapshot.tfl.summary_text), "%s", "Waiting for C6 worker snapshot");
                waiting_snapshot.news.state = brief::kDataStateWaiting;
                waiting_snapshot.news.headline_count = 1;
                waiting_snapshot.news.headlines[0].available = 1;
                snprintf(waiting_snapshot.news.headlines[0].title, sizeof(waiting_snapshot.news.headlines[0].title), "%s", "Waiting for C6 data");
                snprintf(waiting_snapshot.news.headlines[0].summary, sizeof(waiting_snapshot.news.headlines[0].summary), "%s", c6_transport_wait_text());
                dashboard.update_snapshot(waiting_snapshot);
            }
            update_runtime_panel_from_c6();
            return;
        }

        if (current_app_phase < kPhaseSetupComplete)
        {
            dashboard.update_connectivity(
                latest_wifi_ui_state.configured,
                latest_wifi_ui_state.connected,
                latest_wifi_ui_state.status_text);
            update_runtime_panel(dashboard_weather_snapshot, dashboard_tfl_snapshot, dashboard_news_snapshot, dashboard_calendar_snapshot);
            return;
        }

        // Local-hosted builds already have strongly typed service snapshots for each
        // card, so avoid the extra aggregate snapshot render pass here. The richer
        // per-card updaters below are the source of truth, and skipping the duplicate
        // snapshot path keeps first-boot rendering simpler and safer.
        const uint32_t weather_revision = weather_service.revision();
        const uint32_t tfl_revision = tfl_service.revision();
        const uint32_t news_revision = news_service.revision();
        const uint32_t calendar_revision = calendar_service.revision();

        if (weather_revision != last_rendered_weather_revision)
        {
            dashboard.update_weather(dashboard_weather_snapshot);
            last_rendered_weather_revision = weather_revision;
        }
        if (tfl_revision != last_rendered_tfl_revision)
        {
            dashboard.update_tfl(dashboard_tfl_snapshot);
            last_rendered_tfl_revision = tfl_revision;
        }
        if (news_revision != last_rendered_news_revision)
        {
            dashboard.update_news(dashboard_news_snapshot);
            last_rendered_news_revision = news_revision;
        }
        if (calendar_revision != last_rendered_calendar_revision)
        {
            dashboard.update_calendar(dashboard_calendar_snapshot);
            last_rendered_calendar_revision = calendar_revision;
        }
        dashboard.update_wifi_setup(
            configured_wifi_credentials.ssid,
            configured_wifi_credentials.password,
            wifi_scan_options_text,
            wifi_setup_status_text,
            wifi_scan_in_progress);
        last_rendered_wifi_setup_revision = wifi_setup_revision;
        update_runtime_panel(dashboard_weather_snapshot, dashboard_tfl_snapshot, dashboard_news_snapshot, dashboard_calendar_snapshot);
    }

    bool init_lvgl_display()
    {
        const uint32_t draw_buffer_pixels = LCD_H_RES * kLvglBufferLines;
        const uint32_t draw_buffer_bytes = draw_buffer_pixels * kBytesPerPixel;

        draw_buffer = static_cast<uint8_t *>(heap_caps_malloc(draw_buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (draw_buffer == nullptr)
        {
            draw_buffer = static_cast<uint8_t *>(heap_caps_malloc(draw_buffer_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }

        if (draw_buffer == nullptr)
        {
            Serial.println("Failed to allocate LVGL draw buffer");
            store_runtime_event("LVGL draw buffer allocation failed");
            return false;
        }

        display = lv_display_create(LCD_H_RES, LCD_V_RES);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display, draw_buffer, nullptr, draw_buffer_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display, lvgl_flush_cb);
        if (!kLvglSynchronousFlushReady && !lcd_display.register_color_trans_done_callback(lcd_color_trans_done_cb, display))
        {
            Serial.println("Failed to register LCD flush callback");
            store_runtime_event("LCD flush callback registration failed");
            return false;
        }

        return true;
    }

    void render_lvgl_probe_screen()
    {
        if (!kEnableLvglProbeScreen || display == nullptr)
        {
            return;
        }

        log_app_stage("LVGL probe screen start");
        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xF97316), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

        lv_obj_t *panel = lv_obj_create(screen);
        lv_obj_remove_style_all(panel);
        lv_obj_set_size(panel, lv_pct(88), 260);
        lv_obj_center(panel);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(panel, 28, 0);
        lv_obj_set_style_pad_all(panel, 28, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *title = lv_label_create(panel);
        lv_label_set_text(title, "LVGL RENDER OK");
        lv_obj_set_style_text_color(title, lv_color_hex(0x111827), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

        lv_obj_t *subtitle = lv_label_create(panel);
        lv_label_set_text(subtitle, "If you can see this, the dashboard layout is the next suspect.");
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(subtitle, lv_pct(100));
        lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0x334155), 0);
        lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);

        lv_obj_invalidate(screen);
        lv_refr_now(display);
        const uint32_t start_ms = millis();
        while (millis() - start_ms < kLvglProbeScreenMs)
        {
            lv_timer_handler();
            delay(20);
        }

        lv_obj_delete(panel);
        lv_obj_clean(screen);
        lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
        lv_refr_now(display);
        log_app_stage("LVGL probe screen complete");
    }

    void init_lvgl_touch()
    {
        touch_enabled = touch_input.begin();
        touch_input.getDebugSnapshot(&touch_debug_snapshot);
        refresh_touch_debug_text();
        Serial.printf("[touch] init snapshot | %s\n", touch_debug_text);
        last_touch_press_ms = millis();
        last_touch_recovery_attempt_ms = 0;
        touch_init_completed_ms = touch_enabled ? millis() : 0;
        last_touch_interrupt_low_ms = 0;
        touch_ready_confirmed = false;
        touch_read_ever_succeeded = false;
        touch_activity_ever_seen = false;
        touch_point_ever_seen = false;
        touch_seen_once = false;

        if (!touch_enabled)
        {
            Serial.printf("Touch initialization failed, continuing without touch input: %s\n", touch_debug_text);
            char event_text[160];
            snprintf(event_text, sizeof(event_text), "Touch init failed: %s", touch_debug_snapshot.last_error);
            store_runtime_event(event_text);
            return;
        }

        store_runtime_event("Touch controller ready, awaiting first point");

        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lvgl_touch_cb);
        lv_indev_set_display(indev, display);
    }

    void recover_services_if_stale()
    {
        if (kUseC6WorkerData)
        {
            return;
        }

        if (!wifi_ready_for_requests())
        {
            return;
        }

        weather_service.snapshot(&dashboard_weather_snapshot);
        tfl_service.snapshot(&dashboard_tfl_snapshot);
        news_service.snapshot(&dashboard_news_snapshot);
        calendar_service.snapshot(&dashboard_calendar_snapshot);

        if (network_mode_allows_domain(brief::kDomainWeather) &&
            (dashboard_weather_snapshot.stale || !dashboard_weather_snapshot.valid))
        {
            mark_domain_refresh_pending(brief::kDomainWeather);
        }
        if (network_mode_allows_domain(brief::kDomainTfl) &&
            (dashboard_tfl_snapshot.stale || !dashboard_tfl_snapshot.valid))
        {
            mark_domain_refresh_pending(brief::kDomainTfl);
        }
        if (network_mode_allows_domain(brief::kDomainNews) &&
            (dashboard_news_snapshot.stale || !dashboard_news_snapshot.valid))
        {
            mark_domain_refresh_pending(brief::kDomainNews);
        }
        if (network_mode_allows_domain(brief::kDomainCalendar) &&
            (dashboard_calendar_snapshot.stale || !dashboard_calendar_snapshot.valid))
        {
            mark_domain_refresh_pending(brief::kDomainCalendar);
        }
    }

    bool select_next_service_domain(brief::Domain *out_domain, bool *startup_phase)
    {
        const uint32_t now = millis();
        if (out_domain == nullptr || startup_phase == nullptr)
        {
            return false;
        }

        for (uint8_t i = 0; i < kServiceDomainCount; ++i)
        {
            const brief::Domain domain = kStartupServiceOrder[i];
            const int index = domain_to_index(domain);
            if (index < 0 || !network_mode_allows_domain(domain))
            {
                continue;
            }
            if (!startup_service_attempted[index])
            {
                *out_domain = domain;
                *startup_phase = true;
                return true;
            }
        }

        for (uint8_t offset = 0; offset < kServiceDomainCount; ++offset)
        {
            const uint8_t slot = static_cast<uint8_t>((next_service_slot + offset) % kServiceDomainCount);
            const brief::Domain domain = kSteadyServiceOrder[slot];
            const int index = domain_to_index(domain);
            if (index < 0 || !network_mode_allows_domain(domain))
            {
                continue;
            }
            if (pending_service_refresh[index])
            {
                *out_domain = domain;
                *startup_phase = false;
                next_service_slot = static_cast<uint8_t>((slot + 1U) % kServiceDomainCount);
                return true;
            }
        }

        for (uint8_t offset = 0; offset < kServiceDomainCount; ++offset)
        {
            const uint8_t slot = static_cast<uint8_t>((next_service_slot + offset) % kServiceDomainCount);
            const brief::Domain domain = kSteadyServiceOrder[slot];
            if (!network_mode_allows_domain(domain))
            {
                continue;
            }
            const bool due_now =
                (domain == brief::kDomainWeather && weather_service.due(now)) ||
                (domain == brief::kDomainTfl && tfl_service.due(now)) ||
                (domain == brief::kDomainNews && news_service.due(now)) ||
                (domain == brief::kDomainCalendar && calendar_service.due(now));
            if (!due_now)
            {
                continue;
            }

            bool valid = false;
            bool loading = false;
            bool stale = false;
            if (!snapshot_domain_state(domain, &valid, &loading, &stale) || loading)
            {
                continue;
            }

            if (!valid || stale)
            {
                *out_domain = domain;
                *startup_phase = false;
                next_service_slot = static_cast<uint8_t>((slot + 1U) % kServiceDomainCount);
                return true;
            }
        }

        return false;
    }

    bool start_lvgl_tick_timer()
    {
        const esp_timer_create_args_t timer_args = {
            .callback = &lv_tick_task,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "lvgl_tick",
            .skip_unhandled_events = false,
        };

        esp_err_t err = esp_timer_create(&timer_args, &lvgl_tick_timer);
        if (err != ESP_OK)
        {
            Serial.printf("LVGL tick timer create failed: %s\n", esp_err_to_name(err));
            store_runtime_event("LVGL tick timer create failed");
            return false;
        }

        err = esp_timer_start_periodic(lvgl_tick_timer, kLvglTickPeriodUs);
        if (err != ESP_OK)
        {
            Serial.printf("LVGL tick timer start failed: %s\n", esp_err_to_name(err));
            store_runtime_event("LVGL tick timer start failed");
            return false;
        }

        return true;
    }

    void data_task(void *arg)
    {
        (void)arg;
        uint32_t last_wifi_housekeeping_ms = 0;
        uint32_t next_service_dispatch_ms = 0;

        for (;;)
        {
            if (kDisableWifiForTouchIsolation)
            {
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            if (service_time_reached(millis(), last_wifi_housekeeping_ms + kWifiStatusPollMs))
            {
                last_wifi_housekeeping_ms = millis();
                log_wifi_debug("housekeeping");

                if (wifi_suspended_until_ms != 0)
                {
                    if (service_time_reached(millis(), wifi_suspended_until_ms))
                    {
                        wifi_suspended_until_ms = 0;
                        wifi_start_requested = true;
                        wifi_started = false;
                        wifi_start_requested_ms = millis();
                        wifi_session_started_ms = 0;
                        snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Wi-Fi waiting for hosted link");
                        store_runtime_event("Wi-Fi cooldown complete");
                        publish_wifi_ui_state();
                        log_wifi_debug("cooldown complete", true);
                    }
                    else
                    {
                        update_wifi_cache_from_live();
                        vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                        continue;
                    }
                }

                if (wifi_start_requested && !wifi_started)
                {
                    if (service_time_reached(millis(), wifi_start_requested_ms + kWifiStartupDelayMs))
                    {
                        restart_wifi_stack("startup");
                    }
                    else
                    {
                        wifi_cached_configured = true;
                        wifi_cached_connected = false;
                        wifi_cached_ready = false;
                        wifi_cached_status_code = WL_IDLE_STATUS;
                        snprintf(wifi_cached_status_text, sizeof(wifi_cached_status_text), "%s", "Wi-Fi waiting for hosted link");
                        publish_wifi_ui_state();
                    }
                }

                update_wifi_cache_from_live();
                maybe_relax_service_backoff();

                if (!wifi_started)
                {
                    vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                    continue;
                }

                if (!wifi_cached_connected)
                {
                    reset_service_scheduler();
                    next_service_dispatch_ms = 0;

                    const bool startup_deadline_elapsed =
                        wifi_session_started_ms != 0 &&
                        service_time_reached(millis(), wifi_session_started_ms + kWifiSessionStartupDeadlineMs);
                    if (!wifi_has_connected_once && startup_deadline_elapsed)
                    {
                        suspend_wifi_stack("startup/connect timeout");
                        update_wifi_cache_from_live();
                        vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                        continue;
                    }

                    if (wifi_has_connected_once &&
                        wifi_disconnected_since_ms != 0 &&
                        service_time_reached(millis(), wifi_disconnected_since_ms + kWifiDisconnectFaultMs))
                    {
                        suspend_wifi_stack("hosted link unstable");
                        update_wifi_cache_from_live();
                        vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                        continue;
                    }

                    if (!kHostedWiFiRecoveryConservative &&
                        wifi_manual_reconnect_count == 0 &&
                        millis() - last_wifi_attempt_ms >= kWifiRetryMs)
                    {
                        WiFi.reconnect();
                        last_wifi_attempt_ms = millis();
                        wifi_manual_reconnect_count = 1;
                        store_runtime_event("Wi-Fi reconnect requested");
                        log_wifi_debug("WiFi.reconnect issued", true);
                    }

                    if (startup_deadline_elapsed &&
                        wifi_disconnected_since_ms != 0 &&
                        service_time_reached(millis(), wifi_disconnected_since_ms + kWifiFullRecoveryMs) &&
                        service_time_reached(millis(), last_wifi_recovery_ms + kWifiRecoveryCooldownMs))
                    {
                        suspend_wifi_stack("stuck disconnected");
                        update_wifi_cache_from_live();
                    }
                }
            }

            if (!wifi_cached_connected)
            {
                snprintf(service_scheduler_status_text, sizeof(service_scheduler_status_text), "%s", "Waiting for Wi-Fi");
                refresh_service_queue_preview();
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            if (!wifi_ready_for_requests())
            {
                snprintf(service_scheduler_status_text, sizeof(service_scheduler_status_text), "%s", "Wi-Fi warm-up");
                refresh_service_queue_preview();
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            if (!service_time_reached(millis(), wifi_connected_since_ms + kWifiPostConnectIdleMs))
            {
                snprintf(service_scheduler_status_text, sizeof(service_scheduler_status_text), "%s", "Post-connect quiet");
                refresh_service_queue_preview();
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            if (service_fragile_mode &&
                !service_time_reached(millis(), wifi_session_started_ms + kWifiPostBeginQuietMs + kFragileSessionExtraQuietMs))
            {
                snprintf(service_scheduler_status_text, sizeof(service_scheduler_status_text), "%s", "Fragile Wi-Fi settling");
                refresh_service_queue_preview();
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            if (service_network_pause_until_ms != 0 &&
                !service_time_reached(millis(), service_network_pause_until_ms))
            {
                const uint32_t remaining_ms = service_network_pause_until_ms - millis();
                snprintf(
                    service_scheduler_status_text,
                    sizeof(service_scheduler_status_text),
                    "Network recovery pause %lus",
                    static_cast<unsigned long>((remaining_ms + 999U) / 1000U));
                refresh_service_queue_preview();
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            if (next_service_dispatch_ms != 0 &&
                !service_time_reached(millis(), next_service_dispatch_ms))
            {
                const uint32_t remaining_ms = next_service_dispatch_ms - millis();
                snprintf(
                    service_scheduler_status_text,
                    sizeof(service_scheduler_status_text),
                    "Cooling down %lus",
                    static_cast<unsigned long>((remaining_ms + 999U) / 1000U));
                refresh_service_queue_preview();
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            if (kNetworkMode == brief::kNetworkModeManualRefresh)
            {
                snprintf(service_scheduler_status_text, sizeof(service_scheduler_status_text), "%s", "Manual refresh mode");
                refresh_service_queue_preview();
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            brief::Domain domain = brief::kDomainAll;
            bool startup_phase = false;
            if (!select_next_service_domain(&domain, &startup_phase))
            {
                if (!wifi_scan_in_progress &&
                    last_successful_service_ms != 0 &&
                    service_time_reached(millis(), last_successful_service_ms + kHostedIdleSessionRolloverMs))
                {
                    store_runtime_event("Hosted Wi-Fi idle rollover requested");
                    clear_service_fault_window();
                    suspend_wifi_stack("hosted idle rollover");
                    update_wifi_cache_from_live();
                    vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                    continue;
                }

                next_service_dispatch_ms = millis() + kServiceIdleProbeMs;
                snprintf(service_scheduler_status_text, sizeof(service_scheduler_status_text), "%s", "Queue idle");
                refresh_service_queue_preview();
                vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
                continue;
            }

            snprintf(
                service_scheduler_status_text,
                sizeof(service_scheduler_status_text),
                "%s %s",
                startup_phase ? "Priming" : "Fetching",
                domain_name(domain));
            refresh_service_queue_preview();

            const bool force_refresh = consume_domain_refresh_pending(domain);
            if (force_refresh)
            {
                arm_domain_refresh(domain);
            }

            const uint32_t revision_before = domain_revision(domain);
            update_domain_now(domain);
            const uint32_t revision_after = domain_revision(domain);
            const int domain_index = domain_to_index(domain);
            if (domain_index >= 0 && startup_phase)
            {
                startup_service_attempted[domain_index] = true;
            }

            if (revision_after != revision_before)
            {
                bool valid = false;
                bool loading = false;
                bool stale = false;
                if (snapshot_domain_state(domain, &valid, &loading, &stale) && valid)
                {
                    clear_service_fault_window();
                    last_successful_service_ms = millis();
                    const int success_index = domain_to_index(domain);
                    if (success_index >= 0)
                    {
                        service_success_mask |= static_cast<uint8_t>(1U << success_index);
                    }
                    const uint8_t required_mask = allowed_service_domain_mask();
                    if (service_fragile_mode && required_mask != 0 && (service_success_mask & required_mask) == required_mask)
                    {
                        service_fragile_mode = false;
                        recompute_service_timing();
                        store_runtime_event("Fragile startup mode cleared after full service cycle");
                    }
                }

                switch (domain)
                {
                case brief::kDomainWeather:
                    store_runtime_event(startup_phase ? "Weather refresh completed" : "Weather refresh cycle completed");
                    break;
                case brief::kDomainTfl:
                    store_runtime_event(startup_phase ? "TfL refresh completed" : "TfL refresh cycle completed");
                    break;
                case brief::kDomainNews:
                    store_runtime_event(startup_phase ? "News refresh completed" : "News refresh cycle completed");
                    break;
                case brief::kDomainCalendar:
                    store_runtime_event(startup_phase ? "Calendar refresh completed" : "Calendar refresh cycle completed");
                    break;
                default:
                    break;
                }
            }

            if (service_suspend_requested)
            {
                char suspend_reason[sizeof(service_suspend_reason)];
                snprintf(suspend_reason, sizeof(suspend_reason), "%s", service_suspend_reason[0] != '\0' ? service_suspend_reason : "service fault recovery");
                clear_service_fault_window();
                suspend_wifi_stack(suspend_reason);
            }

            next_service_dispatch_ms = millis() + (startup_phase ? startup_service_gap_ms : service_dispatch_gap_ms);
            snprintf(
                service_scheduler_status_text,
                sizeof(service_scheduler_status_text),
                "%s ready, cooldown %lus",
                domain_name(domain),
                static_cast<unsigned long>(((startup_phase ? startup_service_gap_ms : service_dispatch_gap_ms) + 999U) / 1000U));
            refresh_service_queue_preview();
            vTaskDelay(pdMS_TO_TICKS(kDataTaskDelayMs));
        }
    }

    void start_data_task()
    {
        if (kUseC6WorkerData)
        {
            return;
        }

        if (kDisableWifiForTouchIsolation)
        {
            return;
        }

        if (data_task_handle != nullptr)
        {
            return;
        }

        const BaseType_t created = xTaskCreatePinnedToCore(
            data_task,
            "data_task",
            20480,
            nullptr,
            1,
            &data_task_handle,
            kDataTaskCore);
        if (created != pdPASS)
        {
            data_task_handle = nullptr;
            store_runtime_event("Data task creation failed");
            dashboard.update_runtime("Data task failed", "Networking is paused because the background task could not be created.");
        }
    }

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(200);
    snprintf(
        startup_reset_reason,
        sizeof(startup_reset_reason),
        "%s",
        reset_reason_to_text(esp_reset_reason()));
    Serial.println("LondonBrief_P4 application starting");
    Serial.printf("Reset reason: %s\n", startup_reset_reason);
    wifi_ui_state_queue = xQueueCreate(1, sizeof(WifiUiState));
    if (wifi_ui_state_queue == nullptr)
    {
        Serial.println("Wi-Fi UI state queue allocation failed; falling back to direct state reads");
    }
    publish_wifi_ui_state();
    store_runtime_event("System startup");
    recompute_service_timing();
    service_set_network_ready_callback(wifi_ready_for_requests);
    service_set_network_fault_callback(note_service_network_fault);
    refresh_service_queue_preview();
    set_app_phase(kPhaseBoot, "setup begin");

    settings_store.begin();
    set_app_phase(kPhaseSettingsReady, "settings store ready");
    load_configured_wifi_credentials();
    set_wifi_setup_status(wifi_is_configured() ? "Saved Wi-Fi ready. Scan to switch networks." : "Enter Wi-Fi details, then connect.");
    display_settings = settings_store.load_display_settings();
    normalize_display_settings();
    char display_settings_event[96];
    snprintf(
        display_settings_event,
        sizeof(display_settings_event),
        "display settings loaded | brightness=%u%% dark=%u",
        static_cast<unsigned>(display_settings.brightness_percent),
        display_settings.dark_mode ? 1U : 0U);
    log_app_stage(display_settings_event);
    begin_c6_transport();
    log_app_stage("c6 transport configured");

    lv_init();
    set_app_phase(kPhaseLvInitDone, "lv_init complete");

    Serial.println("Initializing JD9365 display");
    set_app_phase(kPhaseDisplayBegin, "lcd_display.begin start");
    if (!lcd_display.begin())
    {
        enter_fatal_boot_error("Display initialization failed");
        return;
    }
    lcd_display.set_backlight_percent(kSafeBootBrightnessPercent);
    log_app_stage("boot backlight forced on");
    draw_display_probe_pattern();
    set_app_phase(kPhaseDisplayReady, "lcd_display.begin complete");

    Serial.println("Initializing LVGL display");
    if (!init_lvgl_display())
    {
        enter_fatal_boot_error("LVGL display init failed");
        return;
    }
    log_app_stage("LVGL display init complete");
    render_lvgl_probe_screen();

    Serial.println("Initializing GSL3680 touch");
    delay(kTouchInitSettleDelayMs);
    set_app_phase(kPhaseTouchInit, "touch init start");
    init_lvgl_touch();
    set_app_phase(kPhaseTouchReady, "touch init complete");

    if (start_lvgl_tick_timer())
    {
        set_app_phase(kPhaseTickReady, "lvgl tick timer started");
    }
    else
    {
        set_app_phase(kPhaseTickReady, "lvgl tick timer unavailable");
    }
    dashboard.set_dark_mode(display_settings.dark_mode);
    log_app_stage("dashboard theme configured");
    dashboard.set_brightness(display_settings.brightness_percent);
    log_app_stage("dashboard brightness configured");
    set_app_phase(kPhaseDashboardInit, "dashboard init start");
    dashboard.init();
    set_app_phase(kPhaseDashboardReady, "dashboard init complete");
    lcd_display.set_backlight_percent(display_settings.brightness_percent);
    log_app_stage("backlight configured");
    dashboard.update_touch_status(touch_enabled, false, 0, 0);
    update_battery_monitor(true);
    log_app_stage("dashboard status primed");
    weather_service.init();
    log_app_stage("weather service init complete");
    tfl_service.init();
    log_app_stage("tfl service init complete");
    news_service.init();
    log_app_stage("news service init complete");
    calendar_service.init();
    log_app_stage("calendar service init complete");
    restore_cached_service_data();
    set_app_phase(kPhaseServicesReady, "services init complete");
    begin_wifi();
    const char *wifi_phase_label =
        kUseC6WorkerData ? "c6 worker networking selected" : !wifi_is_configured()                                                       ? "wifi credentials missing"
                                                         : (kDisableWifiForTouchIsolation || kNetworkMode == brief::kNetworkModeOffline) ? "wifi disabled"
                                                                                                                                         : "wifi startup queued";
    set_app_phase(kPhaseWifiBegin, wifi_phase_label);
    start_data_task();
    set_app_phase(kPhaseDataTaskStarted, "data task started");
    update_wifi_cache_from_live();
    update_wifi_status(true);
    log_app_stage("wifi status synced");
    update_clock();
    log_app_stage("clock synced");
    sync_dashboard();
    lv_timer_handler();
    delay(20);
    lv_timer_handler();
    set_app_phase(kPhaseSetupComplete, "dashboard sync complete");

    Serial.println("LondonBrief_P4 live dashboard ready");
    log_app_stage("setup complete");
}

void loop()
{
    if (fatal_boot_error)
    {
        service_fatal_boot_error();
        return;
    }

    ++loop_iteration_count;
    poll_c6_transport();
    update_wifi_status();
    poll_wifi_scan_progress();
    update_clock();
    if (dashboard.consume_refresh_request())
    {
        if (kUseC6WorkerData)
        {
            send_c6_refresh_request(brief::kDomainAll);
        }
        else if (kNetworkMode == brief::kNetworkModeManualRefresh)
        {
            request_domain_refresh(brief::kDomainAll);
        }
        else
        {
            request_domain_refresh(brief::kDomainAll);
        }
        store_runtime_event("Manual refresh requested");
    }
    if (dashboard.consume_weather_refresh_request())
    {
        if (kUseC6WorkerData)
        {
            send_c6_refresh_request(brief::kDomainWeather);
        }
        else
        {
            request_domain_refresh(brief::kDomainWeather);
        }
        store_runtime_event("Weather refresh requested");
    }
    if (dashboard.consume_tfl_refresh_request())
    {
        if (kUseC6WorkerData)
        {
            send_c6_refresh_request(brief::kDomainTfl);
        }
        else
        {
            request_domain_refresh(brief::kDomainTfl);
        }
        store_runtime_event("TfL refresh requested");
    }
    if (dashboard.consume_news_refresh_request())
    {
        if (kUseC6WorkerData)
        {
            send_c6_refresh_request(brief::kDomainNews);
        }
        else
        {
            request_domain_refresh(brief::kDomainNews);
        }
        store_runtime_event("News refresh requested");
    }
    if (dashboard.consume_calendar_refresh_request())
    {
        if (!kUseC6WorkerData)
        {
            request_domain_refresh(brief::kDomainCalendar);
        }
        store_runtime_event("Calendar refresh requested");
    }
    if (dashboard.consume_wifi_scan_request())
    {
        perform_wifi_scan();
    }
    char requested_wifi_ssid[33] = {};
    char requested_wifi_password[65] = {};
    if (dashboard.consume_wifi_connect_request(
            requested_wifi_ssid,
            sizeof(requested_wifi_ssid),
            requested_wifi_password,
            sizeof(requested_wifi_password)))
    {
        connect_to_selected_wifi(requested_wifi_ssid, requested_wifi_password);
    }
    uint8_t brightness_percent = 0;
    if (dashboard.consume_brightness_request(&brightness_percent))
    {
        lcd_display.set_backlight_percent(brightness_percent);
        display_settings.brightness_percent = brightness_percent;
        mark_display_settings_dirty();
        char event_text[96];
        snprintf(event_text, sizeof(event_text), "Brightness set to %u%%", static_cast<unsigned>(brightness_percent));
        store_runtime_event(event_text);
    }
    bool dark_mode = false;
    if (dashboard.consume_theme_change_request(&dark_mode))
    {
        display_settings.dark_mode = dark_mode;
        mark_display_settings_dirty();
        store_runtime_event(dark_mode ? "Dark mode enabled" : "Light mode enabled");
    }
    if (dashboard.consume_shutdown_request())
    {
        enter_shutdown_sleep();
    }
    poll_touch_input();
    emit_touch_heartbeat();
    emit_loop_heartbeat();
    process_touch_feedback();
    recover_touch_if_stalled();
    recover_services_if_stale();
    update_battery_monitor();
    sync_dashboard();
    persist_cached_service_data_if_changed();
    persist_display_settings_if_due();
    lv_timer_handler();
    delay(5);
    
}
