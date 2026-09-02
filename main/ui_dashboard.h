#ifndef UI_DASHBOARD_H
#define UI_DASHBOARD_H

#include <lvgl.h>
#include <stdint.h>

#include "brief_protocol.h"
#include "calendar_service.h"
#include "news_service.h"
#include "tfl_service.h"
#include "weather_service.h"

class UIDashboard {
public:
    UIDashboard();

    void init();
    void update_clock(const char *time_text, const char *day_text);
    void update_connectivity(bool configured, bool connected, const char *status_text);
    void update_touch_status(bool enabled, bool touching, lv_coord_t x, lv_coord_t y);
    void update_runtime(const char *summary_text, const char *detail_text);
    void update_snapshot(const brief::DashboardSnapshot &snapshot);
    void update_weather(const WeatherData &data);
    void update_battery(uint8_t percent, float voltage, const char *status_text);
    void update_tfl(const TflData &data);
    void update_news(const NewsData &data);
    void update_calendar(const CalendarData &data);
    void update_wifi_setup(const char *saved_ssid, const char *saved_password, const char *scan_options, const char *status_text, bool scanning);
    void show_touch_feedback(lv_coord_t x, lv_coord_t y);
    void set_brightness(uint8_t percent);
    void set_dark_mode(bool dark_mode);
    void tick_animations();

    bool consume_refresh_request();
    bool consume_weather_refresh_request();
    bool consume_tfl_refresh_request();
    bool consume_news_refresh_request();
    bool consume_calendar_refresh_request();
    bool consume_wifi_scan_request();
    bool consume_wifi_connect_request(char *ssid, size_t ssid_size, char *password, size_t password_size);
    bool consume_brightness_request(uint8_t *percent);
    bool consume_theme_change_request(bool *dark_mode);
    bool consume_shutdown_request();
    void handle_refresh_button();
    void handle_weather_refresh_button();
    void handle_tfl_refresh_button();
    void handle_news_refresh_button();
    void handle_calendar_refresh_button();
    void handle_wifi_scan_button();
    void handle_wifi_use_selected_button();
    void handle_wifi_connect_button();
    void handle_wifi_textarea_focus(lv_obj_t *target);
    void handle_settings_network_tools_button();
    void handle_weather_details_button();
    void hide_weather_detail_panel();
    void handle_theme_button();
    void handle_settings_button();
    void handle_shutdown_button();
    void handle_brightness_slider(lv_obj_t *target);
    void hide_settings_panel();

private:
    static constexpr uint8_t kTflRows = 8;
    static constexpr uint8_t kNewsRows = 3;
    static constexpr uint8_t kWeatherDailyRows = kWeatherDailyPoints;
    static constexpr uint8_t kCalendarRows = kCalendarVisibleEvents;
    static constexpr uint8_t kWeatherHourlyRows = kWeatherHourlyPoints;
    static constexpr uint8_t kWeatherIconPixels = 81;

    void apply_theme_();
    void update_theme_button_label_();
    void update_brightness_label_();
    void update_settings_visibility_();
    void update_weather_detail_visibility_();
    void service_deferred_ui_creation_();
    void update_wifi_chip_();
    void update_wifi_keyboard_visibility_(lv_obj_t *target);
    void ensure_wifi_keyboard_();
    void ensure_settings_overlay_();
    void destroy_settings_overlay_();
    void ensure_wifi_setup_controls_();
    void style_wifi_setup_controls_();
    void apply_settings_safe_style_();
    void animate_weather_icons_();
    void apply_weather_demo_frame_();
    void restore_live_weather_icon_();
    void update_weather_demo_labels_();
    void init_weather_icon_grid_(lv_obj_t *target, lv_obj_t **cells, lv_coord_t cell_size);
    void render_weather_icon_(lv_obj_t *target, uint8_t weather_code, bool is_day, lv_coord_t cell_size);
    void update_weather_daily_tiles_(const WeatherData &data);
    void update_weather_daily_tiles_(const brief::WeatherPayload &weather);
    void update_weather_detail_page_(const WeatherData &data);
    void update_weather_detail_page_(const brief::WeatherPayload &weather);
    void populate_calendar_grid_(const CalendarData &data);
    lv_color_t state_color_(const char *status) const;

    lv_obj_t *screen_;
    lv_obj_t *root_;
    lv_obj_t *header_;
    lv_obj_t *content_;
    lv_obj_t *title_label_;
    lv_obj_t *subtitle_label_;
    lv_obj_t *clock_label_;
    lv_obj_t *day_label_;
    lv_obj_t *wifi_dot_;
    lv_obj_t *wifi_label_;
    lv_obj_t *battery_label_;
    lv_obj_t *touch_label_;
    lv_obj_t *offline_banner_;
    lv_obj_t *offline_banner_label_;
    lv_obj_t *refresh_button_;
    lv_obj_t *refresh_button_label_;
    lv_obj_t *theme_button_;
    lv_obj_t *theme_button_label_;
    lv_obj_t *settings_button_;
    lv_obj_t *settings_button_label_;

    lv_obj_t *weather_card_;
    lv_obj_t *weather_icon_;
    lv_obj_t *weather_temp_label_;
    lv_obj_t *weather_condition_label_;
    lv_obj_t *weather_detail_label_;
    lv_obj_t *weather_change_label_;
    lv_obj_t *weather_meta_label_;
    lv_obj_t *weather_details_button_;
    lv_obj_t *weather_details_button_label_;
    lv_obj_t *weather_refresh_button_;
    lv_obj_t *weather_refresh_button_label_;
    lv_obj_t *weather_icon_pixel_[kWeatherIconPixels];
    lv_obj_t *weather_daily_row_;
    lv_obj_t *weather_day_tile_[kWeatherDailyRows];
    lv_obj_t *weather_day_icon_[kWeatherDailyRows];
    lv_obj_t *weather_day_icon_pixel_[kWeatherDailyRows][kWeatherIconPixels];
    lv_obj_t *weather_day_label_[kWeatherDailyRows];
    lv_obj_t *weather_day_temp_label_[kWeatherDailyRows];
    lv_obj_t *weather_day_rain_label_[kWeatherDailyRows];

    lv_obj_t *tfl_card_;
    lv_obj_t *tfl_summary_label_;
    lv_obj_t *tfl_status_label_;
    lv_obj_t *tfl_refresh_button_;
    lv_obj_t *tfl_refresh_button_label_;
    lv_obj_t *tfl_row_[kTflRows];
    lv_obj_t *tfl_line_label_[kTflRows];
    lv_obj_t *tfl_state_label_[kTflRows];

    lv_obj_t *news_card_;
    lv_obj_t *news_status_label_;
    lv_obj_t *news_refresh_button_;
    lv_obj_t *news_refresh_button_label_;
    lv_obj_t *news_title_label_[kNewsRows];
    lv_obj_t *news_summary_label_[kNewsRows];

    lv_obj_t *calendar_card_;
    lv_obj_t *calendar_summary_label_;
    lv_obj_t *calendar_status_label_;
    lv_obj_t *calendar_refresh_button_;
    lv_obj_t *calendar_refresh_button_label_;
    lv_obj_t *calendar_grid_;
    lv_obj_t *calendar_today_marker_;
    lv_obj_t *calendar_event_tile_[kCalendarRows];
    lv_obj_t *calendar_date_badge_[kCalendarRows];
    lv_obj_t *calendar_date_label_[kCalendarRows];
    lv_obj_t *calendar_title_label_[kCalendarRows];
    lv_obj_t *calendar_relative_label_[kCalendarRows];
    lv_obj_t *calendar_detail_label_[kCalendarRows];

    lv_obj_t *weather_detail_overlay_;
    lv_obj_t *weather_detail_panel_;
    lv_obj_t *weather_detail_close_button_;
    lv_obj_t *weather_detail_close_label_;
    lv_obj_t *weather_detail_status_label_;
    lv_obj_t *weather_detail_summary_label_;
    lv_obj_t *weather_hourly_label_[kWeatherHourlyRows];
    lv_obj_t *weather_daily_detail_label_[kWeatherDailyRows];

    lv_obj_t *settings_overlay_;
    lv_obj_t *settings_panel_;
    lv_obj_t *settings_display_label_;
    lv_obj_t *brightness_label_;
    lv_obj_t *brightness_slider_;
    lv_obj_t *settings_system_label_;
    lv_obj_t *settings_network_label_;
    lv_obj_t *wifi_setup_status_label_;
    lv_obj_t *wifi_setup_editor_status_label_;
    lv_obj_t *settings_network_tools_button_;
    lv_obj_t *settings_network_tools_button_label_;
    lv_obj_t *wifi_network_dropdown_;
    lv_obj_t *wifi_scan_button_;
    lv_obj_t *wifi_scan_button_label_;
    lv_obj_t *wifi_use_selected_button_;
    lv_obj_t *wifi_use_selected_button_label_;
    lv_obj_t *wifi_ssid_textarea_;
    lv_obj_t *wifi_password_textarea_;
    lv_obj_t *wifi_connect_button_;
    lv_obj_t *wifi_connect_button_label_;
    lv_obj_t *wifi_keyboard_;
    lv_obj_t *settings_battery_label_;
    lv_obj_t *settings_network_saved_label_;
    lv_obj_t *settings_network_hint_label_;
    lv_obj_t *runtime_summary_label_;
    lv_obj_t *runtime_detail_label_;
    lv_obj_t *shutdown_button_;
    lv_obj_t *shutdown_button_label_;
    lv_obj_t *settings_close_button_;
    lv_obj_t *settings_close_label_;

    bool refresh_requested_;
    bool weather_refresh_requested_;
    bool tfl_refresh_requested_;
    bool news_refresh_requested_;
    bool calendar_refresh_requested_;
    bool wifi_scan_requested_;
    bool wifi_connect_requested_;
    bool brightness_requested_;
    bool theme_change_requested_;
    bool shutdown_requested_;
    bool settings_visible_;
    bool settings_creation_pending_;
    bool settings_hide_pending_;
    bool weather_detail_visible_;
    bool wifi_setup_ui_initialized_;
    bool dark_mode_;
    uint8_t brightness_percent_;
    bool connectivity_configured_;
    bool connectivity_connected_;
    char connectivity_status_text_[128];
    char wifi_scan_options_[1024];
    uint8_t weather_icon_code_;
    bool weather_icon_day_;
    uint8_t weather_live_code_;
    bool weather_live_day_;
    uint8_t weather_day_icon_code_[kWeatherDailyRows];
    uint32_t weather_animation_tick_;
    bool weather_demo_mode_;
    uint8_t weather_demo_index_;
    uint32_t weather_demo_last_step_ms_;
    uint32_t weather_demo_interaction_ms_;
    char weather_live_condition_text_[96];
    char weather_live_detail_text_[160];
    char weather_live_change_text_[128];
    char weather_live_meta_text_[96];
};

#endif
