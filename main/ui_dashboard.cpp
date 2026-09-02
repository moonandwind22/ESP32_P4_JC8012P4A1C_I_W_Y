#include "ui_dashboard.h"

#include "app_config.h"
#include "service_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

constexpr uint8_t kDefaultBrightnessPercent = 42;
constexpr uint8_t kMinBrightnessPercent = 20;
constexpr bool kPixelArtEnabled = LONDONBRIEF_ENABLE_PIXEL_ART != 0;
constexpr bool kEnableDynamicWeatherArt = true;
constexpr bool kEnableDynamicWeatherDailyArt = false;
constexpr lv_coord_t kWeatherMainCellSize = kPixelArtEnabled ? 11 : 9;
constexpr lv_coord_t kWeatherMainIconSize = kWeatherMainCellSize * 9;
constexpr uint32_t kWeatherDemoAutoStepMs = 4000U;
constexpr uint32_t kWeatherDemoAutoExitMs = 30000U;

constexpr uint8_t kWeatherDemoCodes[] = {0U, 0U, 3U, 45U, 61U, 71U, 95U};
constexpr bool kWeatherDemoDay[] = {true, false, true, true, true, true, true};
constexpr const char *kWeatherDemoNames[] = {
    "Clear day",
    "Clear night",
    "Cloud",
    "Fog",
    "Rain",
    "Snow",
    "Thunder",
};

struct Palette {
    lv_color_t screen;
    lv_color_t header;
    lv_color_t card;
    lv_color_t card_alt;
    lv_color_t border;
    lv_color_t text;
    lv_color_t muted;
    lv_color_t dim;
    lv_color_t accent;
    lv_color_t good;
    lv_color_t warn;
    lv_color_t bad;
    lv_color_t button;
};

const Palette kDark = {
    lv_color_hex(0x111B16),
    lv_color_hex(0x2B1B13),
    lv_color_hex(0x3A2518),
    lv_color_hex(0x4A3020),
    lv_color_hex(0xC77D33),
    lv_color_hex(0xFFE7B0),
    lv_color_hex(0xD8B982),
    lv_color_hex(0xB78E5A),
    lv_color_hex(0xF2B84B),
    lv_color_hex(0x86EFAC),
    lv_color_hex(0xFFD166),
    lv_color_hex(0xFCA5A5),
    lv_color_hex(0x63351F),
};

const Palette kLight = {
    lv_color_hex(0x6F9650),
    lv_color_hex(0xE7B86D),
    lv_color_hex(0xFFDFA3),
    lv_color_hex(0xE7B86D),
    lv_color_hex(0x7A3E1F),
    lv_color_hex(0x2E1A12),
    lv_color_hex(0x604023),
    lv_color_hex(0x76512B),
    lv_color_hex(0x7C3F1D),
    lv_color_hex(0x1D7438),
    lv_color_hex(0x915300),
    lv_color_hex(0xA93624),
    lv_color_hex(0xD99558),
};

const Palette &palette_for(bool dark_mode)
{
    return dark_mode ? kDark : kLight;
}

constexpr size_t weather_demo_count()
{
    return sizeof(kWeatherDemoCodes) / sizeof(kWeatherDemoCodes[0]);
}

const lv_font_t *font_body()
{
#if LV_FONT_UNSCII_16
    return kPixelArtEnabled ? &lv_font_unscii_16 : &lv_font_montserrat_16;
#else
    return &lv_font_montserrat_16;
#endif
}

const lv_font_t *font_caption()
{
#if LV_FONT_UNSCII_16
    return kPixelArtEnabled ? &lv_font_unscii_16 : &lv_font_montserrat_14;
#else
    return &lv_font_montserrat_14;
#endif
}

const lv_font_t *font_title()
{
#if LV_FONT_UNSCII_16
    return kPixelArtEnabled ? &lv_font_unscii_16 : &lv_font_montserrat_28;
#else
    return &lv_font_montserrat_28;
#endif
}

const lv_font_t *font_brand()
{
#if LV_FONT_UNSCII_16
    return kPixelArtEnabled ? &lv_font_unscii_16 : &lv_font_montserrat_32;
#else
    return &lv_font_montserrat_32;
#endif
}

lv_color_t pixel_shadow_color(bool dark_mode)
{
    return dark_mode ? lv_color_hex(0x050302) : lv_color_hex(0x4D2A16);
}

void ui_stage(const char *text)
{
    printf("[ui] stage=%s\n", text != nullptr ? text : "unknown");
}

void ui_debug(const char *text)
{
    printf("[ui-debug] %s\n", text != nullptr ? text : "unknown");
}

void safe_set_text(lv_obj_t *label, const char *text)
{
    if (label == nullptr) {
        return;
    }

    const char *safe_text = text != nullptr ? text : "";
    const char *current_text = lv_label_get_text(label);
    if (current_text != nullptr && strcmp(current_text, safe_text) == 0) {
        return;
    }

    lv_label_set_text(label, safe_text);
}

void safe_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (obj == nullptr) {
        return;
    }

    const bool already_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (already_hidden == hidden) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    snprintf(dest, dest_size, "%s", src != nullptr ? src : "");
}

void configure_column(lv_obj_t *obj, lv_coord_t row_pad)
{
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(obj, row_pad, 0);
}

void configure_row(lv_obj_t *obj, lv_coord_t column_pad)
{
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(obj, column_pad, 0);
}

lv_obj_t *create_clean_obj(lv_obj_t *parent)
{
    if (parent == nullptr) {
        return nullptr;
    }

    lv_obj_t *obj = lv_obj_create(parent);
    if (obj == nullptr) {
        return nullptr;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

void apply_pixel_panel_style(lv_obj_t *obj, const Palette &palette, bool dark_mode)
{
    if (obj == nullptr) {
        return;
    }

    lv_obj_set_style_radius(obj, kPixelArtEnabled ? 0 : 14, 0);
    lv_obj_set_style_border_width(obj, kPixelArtEnabled ? 4 : 2, 0);
    lv_obj_set_style_border_color(obj, palette.border, 0);
    lv_obj_set_style_outline_width(obj, kPixelArtEnabled ? 2 : 0, 0);
    lv_obj_set_style_outline_color(obj, kPixelArtEnabled ? pixel_shadow_color(dark_mode) : palette.border, 0);
    lv_obj_set_style_outline_pad(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, kPixelArtEnabled ? 1 : 0, 0);
    lv_obj_set_style_shadow_spread(obj, kPixelArtEnabled ? 4 : 0, 0);
    lv_obj_set_style_shadow_offset_x(obj, kPixelArtEnabled ? 5 : 0, 0);
    lv_obj_set_style_shadow_offset_y(obj, kPixelArtEnabled ? 5 : 0, 0);
    lv_obj_set_style_shadow_color(obj, pixel_shadow_color(dark_mode), 0);
    lv_obj_set_style_shadow_opa(obj, kPixelArtEnabled ? LV_OPA_70 : LV_OPA_TRANSP, 0);
}

void apply_pixel_label_style(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    if (label == nullptr) {
        return;
    }

    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_line_space(label, kPixelArtEnabled ? 2 : 4, 0);
    lv_obj_set_style_text_letter_space(label, kPixelArtEnabled ? 1 : 0, 0);
}

void add_pixel_divider(lv_obj_t *parent, const Palette &palette)
{
    if (!kPixelArtEnabled || parent == nullptr) {
        return;
    }

    lv_obj_t *divider = create_clean_obj(parent);
    lv_obj_set_width(divider, lv_pct(32));
    lv_obj_set_height(divider, 8);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(divider, palette.border, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_margin_top(divider, 2, 0);
    lv_obj_set_style_margin_bottom(divider, 4, 0);
}

lv_obj_t *create_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    if (parent == nullptr) {
        return nullptr;
    }

    lv_obj_t *label = lv_label_create(parent);
    if (label == nullptr) {
        return nullptr;
    }
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    apply_pixel_label_style(label, font, color);
    return label;
}

lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    if (parent == nullptr) {
        return nullptr;
    }

    lv_obj_t *button = lv_button_create(parent);
    if (button == nullptr) {
        return nullptr;
    }
    lv_obj_remove_style_all(button);
    lv_obj_set_height(button, kPixelArtEnabled ? 52 : 48);
    lv_obj_set_style_radius(button, kPixelArtEnabled ? 0 : 10, 0);
    lv_obj_set_style_pad_left(button, 18, 0);
    lv_obj_set_style_pad_right(button, 18, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, kPixelArtEnabled ? 4 : 1, 0);
    lv_obj_set_style_outline_width(button, kPixelArtEnabled ? 2 : 0, 0);
    lv_obj_set_style_outline_pad(button, 0, 0);
    lv_obj_set_style_translate_y(button, kPixelArtEnabled ? 3 : 0, LV_STATE_PRESSED);
    lv_obj_set_layout(button, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(button);
    if (label == nullptr) {
        return button;
    }
    lv_label_set_text(label, text != nullptr ? text : "");
    apply_pixel_label_style(label, font_body(), lv_color_white());
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return button;
}

lv_obj_t *button_label(lv_obj_t *button)
{
    return button != nullptr ? lv_obj_get_child(button, 0) : nullptr;
}

lv_obj_t *create_card(lv_obj_t *parent, const char *eyebrow, const char *title, const Palette &palette)
{
    lv_obj_t *card = create_clean_obj(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, palette.card, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    apply_pixel_panel_style(card, palette, false);
    configure_column(card, 12);

    lv_obj_t *eyebrow_label = create_label(card, eyebrow, font_caption(), palette.accent);
    lv_obj_set_style_text_letter_space(eyebrow_label, kPixelArtEnabled ? 2 : 2, 0);

    create_label(card, title, font_title(), palette.text);
    return card;
}

void apply_card_header_theme(lv_obj_t *card, const Palette &palette)
{
    if (card == nullptr) {
        return;
    }

    lv_obj_t *eyebrow = lv_obj_get_child(card, 0);
    lv_obj_t *title = lv_obj_get_child(card, 1);
    apply_pixel_label_style(eyebrow, font_caption(), palette.accent);
    apply_pixel_label_style(title, font_title(), palette.text);
}

void update_button_style(lv_obj_t *button, const Palette &palette, bool dark_mode)
{
    if (button == nullptr) {
        return;
    }

    lv_obj_set_style_bg_color(button, palette.button, 0);
    lv_obj_set_style_border_color(button, palette.border, 0);
    lv_obj_set_style_outline_color(button, kPixelArtEnabled ? pixel_shadow_color(dark_mode) : palette.border, 0);
    lv_obj_set_style_shadow_width(button, kPixelArtEnabled ? 1 : 0, 0);
    lv_obj_set_style_shadow_spread(button, kPixelArtEnabled ? 3 : 0, 0);
    lv_obj_set_style_shadow_offset_x(button, kPixelArtEnabled ? 4 : 0, 0);
    lv_obj_set_style_shadow_offset_y(button, kPixelArtEnabled ? 4 : 0, 0);
    lv_obj_set_style_shadow_color(button, pixel_shadow_color(dark_mode), 0);
    lv_obj_set_style_shadow_opa(button, kPixelArtEnabled ? LV_OPA_60 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(button, palette.accent, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_offset_x(button, 1, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_offset_y(button, 1, LV_STATE_PRESSED);
    lv_obj_t *label = button_label(button);
    if (label != nullptr) {
        apply_pixel_label_style(label, font_body(), palette.text);
    }
}

const char *snapshot_state_text(uint8_t state)
{
    switch (state) {
        case brief::kDataStateLoading:
            return "Refreshing";
        case brief::kDataStateLive:
            return "Live";
        case brief::kDataStateStale:
            return "Stale";
        case brief::kDataStateError:
            return "Offline";
        default:
            return "Waiting";
    }
}

void format_tenths(char *out, size_t out_size, int16_t tenths, const char *suffix)
{
    const int value = static_cast<int>(tenths);
    const int magnitude = abs(value);
    const int whole = magnitude / 10;
    const int fraction = magnitude % 10;
    snprintf(out, out_size, "%s%d.%d%s", value < 0 ? "-" : "", whole, fraction, suffix != nullptr ? suffix : "");
}

void format_whole_tenths(char *out, size_t out_size, int16_t tenths)
{
    const int whole = static_cast<int>(tenths) / 10;
    snprintf(out, out_size, "%d", whole);
}

bool parse_iso_date_local(const char *iso_date, struct tm *out)
{
    if (iso_date == nullptr || out == nullptr) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(iso_date, "%d-%d-%d", &year, &month, &day) != 3) {
        return false;
    }

    struct tm parsed = {};
    parsed.tm_year = year - 1900;
    parsed.tm_mon = month - 1;
    parsed.tm_mday = day;
    parsed.tm_hour = 12;
    parsed.tm_isdst = -1;
    if (mktime(&parsed) < 0) {
        return false;
    }

    *out = parsed;
    return true;
}

uint8_t weekday_monday_first(const struct tm &date)
{
    return static_cast<uint8_t>((date.tm_wday + 6) % 7);
}

int days_in_month_for(int full_year, int zero_based_month)
{
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (zero_based_month < 0 || zero_based_month > 11) {
        return 30;
    }
    if (zero_based_month == 1 && (((full_year % 4) == 0 && (full_year % 100) != 0) || ((full_year % 400) == 0))) {
        return 29;
    }
    return kDays[zero_based_month];
}

enum class WeatherIconKind : uint8_t {
    ClearDay,
    ClearNight,
    Cloud,
    Fog,
    Rain,
    Snow,
    Thunder,
};

WeatherIconKind icon_kind_from_code(uint8_t code, bool is_day)
{
    if (code <= 1) {
        return is_day ? WeatherIconKind::ClearDay : WeatherIconKind::ClearNight;
    }
    if (code == 2 || code == 3) {
        return WeatherIconKind::Cloud;
    }
    if (code == 45 || code == 48) {
        return WeatherIconKind::Fog;
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        return WeatherIconKind::Rain;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return WeatherIconKind::Snow;
    }
    if (code >= 95) {
        return WeatherIconKind::Thunder;
    }
    return WeatherIconKind::Cloud;
}

const char *const *icon_pattern(WeatherIconKind kind)
{
    static const char *const clear_day[] = {
        "...s.s...",
        "....s....",
        "..sssss..",
        "..sssss..",
        "sssssssss",
        "..sssss..",
        "..sssss..",
        "....s....",
        "...s.s...",
    };
    static const char *const clear_night[] = {
        "......n..",
        "....nn...",
        "...nnnn..",
        "...nnn...",
        "...nn....",
        "...nnn...",
        "....nnnn.",
        ".....nn..",
        "..n......",
    };
    static const char *const cloud[] = {
        ".........",
        "...cc....",
        "..cccc...",
        ".cccccc..",
        "cccccccc.",
        "cccccccc.",
        ".dddddd..",
        ".........",
        ".........",
    };
    static const char *const fog[] = {
        ".........",
        "..fffff..",
        ".fffffff.",
        "..fffff..",
        ".fffffff.",
        "..fffff..",
        ".fffffff.",
        "..fffff..",
        ".........",
    };
    static const char *const rain[] = {
        ".........",
        "...cc....",
        "..cccc...",
        ".cccccc..",
        "cccccccc.",
        ".dddddd..",
        "..b.b....",
        ".b.b.b.b.",
        "..b.b....",
    };
    static const char *const snow[] = {
        ".........",
        "...cc....",
        "..cccc...",
        ".cccccc..",
        "cccccccc.",
        ".dddddd..",
        "..w.w....",
        "...w.....",
        ".w...w.w.",
    };
    static const char *const thunder[] = {
        ".........",
        "..cccc...",
        ".cccccc..",
        "cccccccc.",
        ".dddddd..",
        "...ll....",
        "..lll....",
        "...ll....",
        "..ll.....",
        ".........",
    };

    switch (kind) {
        case WeatherIconKind::ClearDay:
            return clear_day;
        case WeatherIconKind::ClearNight:
            return clear_night;
        case WeatherIconKind::Fog:
            return fog;
        case WeatherIconKind::Rain:
            return rain;
        case WeatherIconKind::Snow:
            return snow;
        case WeatherIconKind::Thunder:
            return thunder;
        case WeatherIconKind::Cloud:
        default:
            return cloud;
    }
}

lv_color_t icon_pixel_color(char pixel, const Palette &palette, bool dark_mode)
{
    switch (pixel) {
        case 's':
            return dark_mode ? lv_color_hex(0xFFD166) : lv_color_hex(0xD99216);
        case 'n':
            return dark_mode ? lv_color_hex(0xBFD7FF) : lv_color_hex(0x4F6EA5);
        case 'c':
            return dark_mode ? lv_color_hex(0xD8B982) : lv_color_hex(0xFFF0C7);
        case 'd':
            return dark_mode ? lv_color_hex(0x8B6B46) : lv_color_hex(0xA07B4D);
        case 'b':
            return dark_mode ? lv_color_hex(0x38BDF8) : lv_color_hex(0x1D76D3);
        case 'w':
            return dark_mode ? lv_color_hex(0xE0F2FE) : lv_color_hex(0x6A8FB8);
        case 'f':
            return palette.dim;
        case 'l':
            return dark_mode ? lv_color_hex(0xFACC15) : lv_color_hex(0xB7791F);
        default:
            return palette.card;
    }
}

void refresh_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_refresh_button();
    }
}

void theme_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_theme_button();
    }
}

void settings_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_settings_button();
    }
}

void shutdown_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_shutdown_button();
    }
}

void slider_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_brightness_slider(static_cast<lv_obj_t *>(lv_event_get_target(event)));
    }
}

void weather_refresh_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_weather_refresh_button();
    }
}

void tfl_refresh_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_tfl_refresh_button();
    }
}

void news_refresh_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_news_refresh_button();
    }
}

void calendar_refresh_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_calendar_refresh_button();
    }
}

void wifi_scan_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_wifi_scan_button();
    }
}

void wifi_use_selected_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_wifi_use_selected_button();
    }
}

void wifi_connect_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_wifi_connect_button();
    }
}

void wifi_textarea_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED) {
        dashboard->handle_wifi_textarea_focus(static_cast<lv_obj_t *>(lv_event_get_target(event)));
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        dashboard->handle_wifi_textarea_focus(nullptr);
    }
}

void settings_network_tools_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_settings_network_tools_button();
    }
}

void weather_details_event_cb(lv_event_t *event)
{
    auto *dashboard = static_cast<UIDashboard *>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->handle_weather_details_button();
    }
}

}  // namespace

UIDashboard::UIDashboard()
    : screen_(nullptr),
      root_(nullptr),
      header_(nullptr),
      content_(nullptr),
      title_label_(nullptr),
      subtitle_label_(nullptr),
      clock_label_(nullptr),
      day_label_(nullptr),
      wifi_dot_(nullptr),
      wifi_label_(nullptr),
      battery_label_(nullptr),
      touch_label_(nullptr),
      offline_banner_(nullptr),
      offline_banner_label_(nullptr),
      refresh_button_(nullptr),
      refresh_button_label_(nullptr),
      theme_button_(nullptr),
      theme_button_label_(nullptr),
      settings_button_(nullptr),
      settings_button_label_(nullptr),
      weather_card_(nullptr),
      weather_icon_(nullptr),
      weather_temp_label_(nullptr),
      weather_condition_label_(nullptr),
      weather_detail_label_(nullptr),
      weather_change_label_(nullptr),
      weather_meta_label_(nullptr),
      weather_details_button_(nullptr),
      weather_details_button_label_(nullptr),
      weather_refresh_button_(nullptr),
      weather_refresh_button_label_(nullptr),
      weather_daily_row_(nullptr),
      tfl_card_(nullptr),
      tfl_summary_label_(nullptr),
      tfl_status_label_(nullptr),
      tfl_refresh_button_(nullptr),
      tfl_refresh_button_label_(nullptr),
      news_card_(nullptr),
      news_status_label_(nullptr),
      news_refresh_button_(nullptr),
      news_refresh_button_label_(nullptr),
      calendar_card_(nullptr),
      calendar_summary_label_(nullptr),
      calendar_status_label_(nullptr),
      calendar_refresh_button_(nullptr),
      calendar_refresh_button_label_(nullptr),
      calendar_grid_(nullptr),
      calendar_today_marker_(nullptr),
      weather_detail_overlay_(nullptr),
      weather_detail_panel_(nullptr),
      weather_detail_close_button_(nullptr),
      weather_detail_close_label_(nullptr),
      weather_detail_status_label_(nullptr),
      weather_detail_summary_label_(nullptr),
      settings_overlay_(nullptr),
      settings_panel_(nullptr),
      settings_display_label_(nullptr),
      brightness_label_(nullptr),
      brightness_slider_(nullptr),
      settings_system_label_(nullptr),
      settings_network_label_(nullptr),
      wifi_setup_status_label_(nullptr),
      wifi_setup_editor_status_label_(nullptr),
      settings_network_tools_button_(nullptr),
      settings_network_tools_button_label_(nullptr),
      wifi_network_dropdown_(nullptr),
      wifi_scan_button_(nullptr),
      wifi_scan_button_label_(nullptr),
      wifi_use_selected_button_(nullptr),
      wifi_use_selected_button_label_(nullptr),
      wifi_ssid_textarea_(nullptr),
      wifi_password_textarea_(nullptr),
      wifi_connect_button_(nullptr),
      wifi_connect_button_label_(nullptr),
      wifi_keyboard_(nullptr),
      settings_battery_label_(nullptr),
      settings_network_saved_label_(nullptr),
      settings_network_hint_label_(nullptr),
      runtime_summary_label_(nullptr),
      runtime_detail_label_(nullptr),
      shutdown_button_(nullptr),
      shutdown_button_label_(nullptr),
      settings_close_button_(nullptr),
      settings_close_label_(nullptr),
      refresh_requested_(false),
      weather_refresh_requested_(false),
      tfl_refresh_requested_(false),
      news_refresh_requested_(false),
      calendar_refresh_requested_(false),
      wifi_scan_requested_(false),
      wifi_connect_requested_(false),
      brightness_requested_(false),
      theme_change_requested_(false),
      shutdown_requested_(false),
      settings_visible_(false),
      settings_creation_pending_(false),
      settings_hide_pending_(false),
      weather_detail_visible_(false),
      wifi_setup_ui_initialized_(false),
      dark_mode_(true),
      brightness_percent_(kDefaultBrightnessPercent),
      connectivity_configured_(false),
      connectivity_connected_(false),
      weather_icon_code_(255),
      weather_icon_day_(true),
      weather_live_code_(255),
      weather_live_day_(true),
      weather_animation_tick_(0),
      weather_demo_mode_(false),
      weather_demo_index_(0),
      weather_demo_last_step_ms_(0),
      weather_demo_interaction_ms_(0)
{
    weather_live_condition_text_[0] = '\0';
    weather_live_detail_text_[0] = '\0';
    weather_live_change_text_[0] = '\0';
    weather_live_meta_text_[0] = '\0';
    memset(weather_icon_pixel_, 0, sizeof(weather_icon_pixel_));
    memset(weather_day_tile_, 0, sizeof(weather_day_tile_));
    memset(weather_day_icon_, 0, sizeof(weather_day_icon_));
    memset(weather_day_icon_pixel_, 0, sizeof(weather_day_icon_pixel_));
    memset(weather_day_label_, 0, sizeof(weather_day_label_));
    memset(weather_day_temp_label_, 0, sizeof(weather_day_temp_label_));
    memset(weather_day_rain_label_, 0, sizeof(weather_day_rain_label_));
    memset(weather_day_icon_code_, 0xFF, sizeof(weather_day_icon_code_));
    memset(tfl_row_, 0, sizeof(tfl_row_));
    memset(tfl_line_label_, 0, sizeof(tfl_line_label_));
    memset(tfl_state_label_, 0, sizeof(tfl_state_label_));
    memset(news_title_label_, 0, sizeof(news_title_label_));
    memset(news_summary_label_, 0, sizeof(news_summary_label_));
    memset(calendar_event_tile_, 0, sizeof(calendar_event_tile_));
    memset(calendar_date_badge_, 0, sizeof(calendar_date_badge_));
    memset(calendar_date_label_, 0, sizeof(calendar_date_label_));
    memset(calendar_title_label_, 0, sizeof(calendar_title_label_));
    memset(calendar_relative_label_, 0, sizeof(calendar_relative_label_));
    memset(calendar_detail_label_, 0, sizeof(calendar_detail_label_));
    memset(weather_hourly_label_, 0, sizeof(weather_hourly_label_));
    memset(weather_daily_detail_label_, 0, sizeof(weather_daily_detail_label_));
    copy_text(connectivity_status_text_, sizeof(connectivity_status_text_), "Wi-Fi starting");
    copy_text(wifi_scan_options_, sizeof(wifi_scan_options_), "No networks scanned yet");
}

void UIDashboard::init()
{
    ui_stage("init start");
    const Palette &palette = palette_for(dark_mode_);
    screen_ = lv_screen_active();
    lv_obj_clean(screen_);
    lv_obj_remove_style_all(screen_);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen_, palette.screen, 0);

    root_ = create_clean_obj(screen_);
    lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, kPixelArtEnabled ? 14 : 18, 0);
    configure_column(root_, kPixelArtEnabled ? 14 : 16);

    header_ = create_clean_obj(root_);
    lv_obj_set_width(header_, lv_pct(100));
    lv_obj_set_height(header_, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(header_, 190, 0);
    lv_obj_set_style_pad_all(header_, 18, 0);
    lv_obj_set_style_bg_opa(header_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(header_, palette.header, 0);
    apply_pixel_panel_style(header_, palette, dark_mode_);
    configure_column(header_, 12);

    lv_obj_t *top_row = create_clean_obj(header_);
    lv_obj_set_width(top_row, lv_pct(100));
    lv_obj_set_height(top_row, 72);
    configure_row(top_row, 16);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_wrap = create_clean_obj(top_row);
    lv_obj_set_height(title_wrap, lv_pct(100));
    lv_obj_set_flex_grow(title_wrap, 1);
    configure_column(title_wrap, 2);
    title_label_ = create_label(title_wrap, kPixelArtEnabled ? "LONDON BRIEF" : "London Brief", font_brand(), palette.text);
    subtitle_label_ = create_label(title_wrap,
                                   kPixelArtEnabled ? "WEATHER  |  TFL  |  NEWS BRIEFING"
                                                    : "Weather, TfL, and a clean news briefing",
                                   font_body(), palette.muted);

    lv_obj_t *time_wrap = create_clean_obj(top_row);
    lv_obj_set_width(time_wrap, 170);
    lv_obj_set_height(time_wrap, lv_pct(100));
    configure_column(time_wrap, 0);
    lv_obj_set_flex_align(time_wrap, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    clock_label_ = create_label(time_wrap, "--:--", font_brand(), palette.text);
    lv_obj_set_style_text_align(clock_label_, LV_TEXT_ALIGN_RIGHT, 0);
    day_label_ = create_label(time_wrap, "Waiting", font_caption(), palette.muted);
    lv_obj_set_style_text_align(day_label_, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *status_row = create_clean_obj(header_);
    lv_obj_set_width(status_row, lv_pct(100));
    lv_obj_set_height(status_row, 46);
    configure_row(status_row, 10);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    wifi_dot_ = create_clean_obj(status_row);
    lv_obj_set_size(wifi_dot_, kPixelArtEnabled ? 14 : 16, kPixelArtEnabled ? 14 : 16);
    lv_obj_set_style_radius(wifi_dot_, kPixelArtEnabled ? 0 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(wifi_dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_dot_, kPixelArtEnabled ? 2 : 0, 0);
    lv_obj_set_style_border_color(wifi_dot_, palette.border, 0);
    wifi_label_ = create_label(status_row, "Wi-Fi starting", font_caption(), palette.muted);
    lv_obj_set_width(wifi_label_, 0);
    lv_obj_set_flex_grow(wifi_label_, 1);
    battery_label_ = create_label(status_row, "Battery --", font_caption(), palette.muted);
    lv_obj_set_width(battery_label_, 150);
    lv_obj_set_style_text_align(battery_label_, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *button_row = create_clean_obj(header_);
    lv_obj_set_width(button_row, lv_pct(100));
    lv_obj_set_height(button_row, kPixelArtEnabled ? 56 : 50);
    configure_row(button_row, 10);
    refresh_button_ = create_button(button_row, "Refresh", refresh_event_cb, this);
    refresh_button_label_ = button_label(refresh_button_);
    lv_obj_set_flex_grow(refresh_button_, 1);
    theme_button_ = create_button(button_row, "Light", theme_event_cb, this);
    theme_button_label_ = button_label(theme_button_);
    lv_obj_set_flex_grow(theme_button_, 1);
    settings_button_ = create_button(button_row, "Settings", settings_event_cb, this);
    settings_button_label_ = button_label(settings_button_);
    lv_obj_set_flex_grow(settings_button_, 1);
    ui_stage("header ready");

    offline_banner_ = create_clean_obj(root_);
    lv_obj_set_width(offline_banner_, lv_pct(100));
    lv_obj_set_height(offline_banner_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(offline_banner_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(offline_banner_, 12, 0);
    lv_obj_set_style_border_width(offline_banner_, kPixelArtEnabled ? 3 : 1, 0);
    configure_row(offline_banner_, 10);
    offline_banner_label_ = create_label(offline_banner_, "Offline", font_body(), palette.text);
    safe_set_hidden(offline_banner_, true);

    content_ = create_clean_obj(root_);
    lv_obj_set_width(content_, lv_pct(100));
    lv_obj_set_height(content_, 0);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_add_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(content_, 2, 0);
    configure_column(content_, kPixelArtEnabled ? 20 : 16);

    weather_card_ = create_card(content_, "WEATHER", "London now", palette);
    add_pixel_divider(weather_card_, palette);
    lv_obj_set_height(weather_card_, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(weather_card_, 500, 0);
    lv_obj_t *weather_action_row = create_clean_obj(weather_card_);
    lv_obj_set_width(weather_action_row, lv_pct(100));
    configure_row(weather_action_row, 10);
    lv_obj_set_flex_align(weather_action_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    weather_details_button_ = create_button(weather_action_row, "Preview", weather_details_event_cb, this);
    weather_details_button_label_ = button_label(weather_details_button_);
    lv_obj_set_width(weather_details_button_, 150);
    weather_refresh_button_ = create_button(weather_action_row, "Refresh Weather", weather_refresh_event_cb, this);
    weather_refresh_button_label_ = button_label(weather_refresh_button_);
    lv_obj_set_width(weather_refresh_button_, 210);

    lv_obj_t *weather_now_row = create_clean_obj(weather_card_);
    lv_obj_set_width(weather_now_row, lv_pct(100));
    lv_obj_set_height(weather_now_row, LV_SIZE_CONTENT);
    configure_column(weather_now_row, 18);
    lv_obj_set_flex_align(weather_now_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    weather_icon_ = create_clean_obj(weather_now_row);
    lv_obj_set_size(weather_icon_, kWeatherMainIconSize, kWeatherMainIconSize);
    lv_obj_set_style_pad_all(weather_icon_, 0, 0);
    lv_obj_set_style_margin_top(weather_icon_, 4, 0);
    if (kEnableDynamicWeatherArt) {
        init_weather_icon_grid_(weather_icon_, weather_icon_pixel_, kWeatherMainCellSize);
        ui_stage("weather main icon grid ready");
    } else {
        safe_set_hidden(weather_icon_, true);
        ui_stage("weather main icon grid skipped");
    }

    lv_obj_t *weather_now_text = create_clean_obj(weather_now_row);
    lv_obj_set_width(weather_now_text, lv_pct(100));
    lv_obj_set_height(weather_now_text, LV_SIZE_CONTENT);
    configure_column(weather_now_text, 6);
    lv_obj_set_flex_align(weather_now_text, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    weather_temp_label_ = create_label(weather_now_text, "--", font_brand(), palette.text);
    lv_obj_set_style_text_align(weather_temp_label_, LV_TEXT_ALIGN_CENTER, 0);
    weather_condition_label_ = create_label(weather_now_text, "Forecast waiting", font_body(), palette.accent);
    lv_obj_set_width(weather_condition_label_, lv_pct(100));
    lv_obj_set_style_text_align(weather_condition_label_, LV_TEXT_ALIGN_CENTER, 0);

    weather_detail_label_ = create_label(weather_card_, "The forecast card will update when networking is ready.", font_body(), palette.muted);
    lv_obj_set_width(weather_detail_label_, lv_pct(100));
    lv_obj_set_style_text_align(weather_detail_label_, LV_TEXT_ALIGN_CENTER, 0);
    weather_change_label_ = create_label(weather_card_, "Next change: waiting for data", font_body(), palette.text);
    lv_obj_set_width(weather_change_label_, lv_pct(100));
    lv_obj_set_style_text_align(weather_change_label_, LV_TEXT_ALIGN_CENTER, 0);

    weather_daily_row_ = create_clean_obj(weather_card_);
    lv_obj_set_width(weather_daily_row_, lv_pct(100));
    lv_obj_set_height(weather_daily_row_, kPixelArtEnabled ? 104 : 112);
    configure_row(weather_daily_row_, 8);
    lv_obj_set_flex_align(weather_daily_row_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
        weather_day_tile_[i] = create_clean_obj(weather_daily_row_);
        lv_obj_set_width(weather_day_tile_[i], 0);
        lv_obj_set_height(weather_day_tile_[i], lv_pct(100));
        lv_obj_set_flex_grow(weather_day_tile_[i], 1);
        lv_obj_set_style_bg_opa(weather_day_tile_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(weather_day_tile_[i], palette.card_alt, 0);
        lv_obj_set_style_border_width(weather_day_tile_[i], kPixelArtEnabled ? 2 : 1, 0);
        lv_obj_set_style_border_color(weather_day_tile_[i], palette.border, 0);
        lv_obj_set_style_pad_all(weather_day_tile_[i], 6, 0);
        configure_column(weather_day_tile_[i], 2);
        lv_obj_set_flex_align(weather_day_tile_[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        weather_day_label_[i] = create_label(weather_day_tile_[i], "--", font_caption(), palette.text);
        lv_obj_set_style_text_align(weather_day_label_[i], LV_TEXT_ALIGN_CENTER, 0);
        weather_day_icon_[i] = create_clean_obj(weather_day_tile_[i]);
        lv_obj_set_size(weather_day_icon_[i], kPixelArtEnabled ? 36 : 32, kPixelArtEnabled ? 36 : 32);
        if (kEnableDynamicWeatherArt && kEnableDynamicWeatherDailyArt) {
            init_weather_icon_grid_(weather_day_icon_[i], weather_day_icon_pixel_[i], 4);
        }
        safe_set_hidden(weather_day_icon_[i], true);
        weather_day_temp_label_[i] = create_label(weather_day_tile_[i], "--", font_caption(), palette.text);
        lv_obj_set_style_text_align(weather_day_temp_label_[i], LV_TEXT_ALIGN_CENTER, 0);
        weather_day_rain_label_[i] = create_label(weather_day_tile_[i], "--", font_caption(), palette.dim);
        lv_obj_set_style_text_align(weather_day_rain_label_[i], LV_TEXT_ALIGN_CENTER, 0);
        safe_set_hidden(weather_day_tile_[i], true);
    }

    ui_stage((kEnableDynamicWeatherArt && kEnableDynamicWeatherDailyArt) ? "weather daily icon grids ready" : "weather daily icon grids skipped");

    weather_meta_label_ = create_label(weather_card_, "Weather idle", font_caption(), palette.dim);
    lv_obj_set_width(weather_meta_label_, lv_pct(100));
    lv_obj_set_style_text_align(weather_meta_label_, LV_TEXT_ALIGN_CENTER, 0);
    ui_stage("weather card ready");

    tfl_card_ = create_card(content_, "TRANSPORT", "TfL line status", palette);
    add_pixel_divider(tfl_card_, palette);
    lv_obj_set_height(tfl_card_, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(tfl_card_, 500, 0);
    lv_obj_t *tfl_action_row = create_clean_obj(tfl_card_);
    lv_obj_set_width(tfl_action_row, lv_pct(100));
    configure_row(tfl_action_row, 10);
    lv_obj_set_flex_align(tfl_action_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    tfl_refresh_button_ = create_button(tfl_action_row, "Refresh TfL", tfl_refresh_event_cb, this);
    tfl_refresh_button_label_ = button_label(tfl_refresh_button_);
    lv_obj_set_width(tfl_refresh_button_, 180);
    tfl_summary_label_ = create_label(tfl_card_, "Waiting for transport data", font_body(), palette.text);
    tfl_status_label_ = create_label(tfl_card_, "TfL idle", font_caption(), palette.dim);
    for (uint8_t i = 0; i < kTflRows; ++i) {
        tfl_row_[i] = create_clean_obj(tfl_card_);
        lv_obj_set_width(tfl_row_[i], lv_pct(100));
        lv_obj_set_height(tfl_row_[i], kPixelArtEnabled ? 42 : 48);
        lv_obj_set_style_radius(tfl_row_[i], kPixelArtEnabled ? 0 : 8, 0);
        lv_obj_set_style_bg_opa(tfl_row_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tfl_row_[i], kPixelArtEnabled ? 2 : 0, 0);
        lv_obj_set_style_border_color(tfl_row_[i], palette.border, 0);
        lv_obj_set_style_pad_left(tfl_row_[i], 14, 0);
        lv_obj_set_style_pad_right(tfl_row_[i], 14, 0);
        configure_row(tfl_row_[i], 10);
        lv_obj_set_flex_align(tfl_row_[i], LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        tfl_line_label_[i] = create_label(tfl_row_[i], "-", font_body(), palette.text);
        lv_obj_set_flex_grow(tfl_line_label_[i], 1);
        tfl_state_label_[i] = create_label(tfl_row_[i], "-", font_caption(), palette.muted);
        lv_obj_set_width(tfl_state_label_[i], 250);
        lv_obj_set_style_text_align(tfl_state_label_[i], LV_TEXT_ALIGN_RIGHT, 0);
        safe_set_hidden(tfl_row_[i], true);
    }
    ui_stage("tfl card ready");

    news_card_ = create_card(content_, "BRIEFING", "News scan", palette);
    add_pixel_divider(news_card_, palette);
    lv_obj_set_height(news_card_, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(news_card_, 420, 0);
    lv_obj_t *news_action_row = create_clean_obj(news_card_);
    lv_obj_set_width(news_action_row, lv_pct(100));
    configure_row(news_action_row, 10);
    lv_obj_set_flex_align(news_action_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    news_refresh_button_ = create_button(news_action_row, "Refresh News", news_refresh_event_cb, this);
    news_refresh_button_label_ = button_label(news_refresh_button_);
    lv_obj_set_width(news_refresh_button_, 190);
    news_status_label_ = create_label(news_card_, "News idle", font_caption(), palette.dim);
    for (uint8_t i = 0; i < kNewsRows; ++i) {
        news_title_label_[i] = create_label(news_card_, "-", font_body(), palette.text);
        news_summary_label_[i] = create_label(news_card_, "", font_caption(), palette.muted);
        safe_set_hidden(news_title_label_[i], true);
        safe_set_hidden(news_summary_label_[i], true);
    }
    ui_stage("news card ready");

    calendar_card_ = create_card(content_, "CALENDAR", "UK bank holidays", palette);
    add_pixel_divider(calendar_card_, palette);
    lv_obj_set_height(calendar_card_, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(calendar_card_, 340, 0);
    lv_obj_t *calendar_action_row = create_clean_obj(calendar_card_);
    lv_obj_set_width(calendar_action_row, lv_pct(100));
    configure_row(calendar_action_row, 10);
    lv_obj_set_flex_align(calendar_action_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    calendar_refresh_button_ = create_button(calendar_action_row, "Refresh Calendar", calendar_refresh_event_cb, this);
    calendar_refresh_button_label_ = button_label(calendar_refresh_button_);
    lv_obj_set_width(calendar_refresh_button_, 220);
    calendar_summary_label_ = create_label(calendar_card_, "Upcoming UK holidays will appear here", font_body(), palette.text);
    calendar_status_label_ = create_label(calendar_card_, "Calendar idle", font_caption(), palette.dim);
    calendar_grid_ = lv_table_create(calendar_card_);
    lv_obj_set_width(calendar_grid_, lv_pct(100));
    lv_obj_set_height(calendar_grid_, 260);
    lv_table_set_row_count(calendar_grid_, 7);
    lv_table_set_column_count(calendar_grid_, 7);
    lv_obj_clear_flag(calendar_grid_, LV_OBJ_FLAG_SCROLLABLE);
    calendar_today_marker_ = create_clean_obj(calendar_card_);
    lv_obj_set_style_bg_opa(calendar_today_marker_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(calendar_today_marker_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(calendar_today_marker_, 3, 0);
    lv_obj_set_style_border_opa(calendar_today_marker_, LV_OPA_COVER, 0);
    safe_set_hidden(calendar_today_marker_, true);
    for (uint8_t i = 0; i < kCalendarRows; ++i) {
        calendar_event_tile_[i] = create_clean_obj(calendar_card_);
        lv_obj_set_width(calendar_event_tile_[i], lv_pct(100));
        lv_obj_set_height(calendar_event_tile_[i], LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(calendar_event_tile_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(calendar_event_tile_[i], palette.card_alt, 0);
        lv_obj_set_style_border_width(calendar_event_tile_[i], kPixelArtEnabled ? 2 : 1, 0);
        lv_obj_set_style_border_color(calendar_event_tile_[i], palette.border, 0);
        lv_obj_set_style_pad_all(calendar_event_tile_[i], 10, 0);
        configure_row(calendar_event_tile_[i], 12);
        lv_obj_set_flex_align(calendar_event_tile_[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        calendar_date_badge_[i] = create_clean_obj(calendar_event_tile_[i]);
        lv_obj_set_width(calendar_date_badge_[i], 86);
        lv_obj_set_height(calendar_date_badge_[i], 72);
        lv_obj_set_style_bg_opa(calendar_date_badge_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(calendar_date_badge_[i], palette.accent, 0);
        lv_obj_set_style_border_width(calendar_date_badge_[i], kPixelArtEnabled ? 2 : 1, 0);
        lv_obj_set_style_border_color(calendar_date_badge_[i], palette.border, 0);
        configure_column(calendar_date_badge_[i], 2);
        lv_obj_set_flex_align(calendar_date_badge_[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        calendar_date_label_[i] = create_label(calendar_date_badge_[i], "--", font_body(), palette.text);
        lv_obj_set_style_text_align(calendar_date_label_[i], LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *calendar_text_col = create_clean_obj(calendar_event_tile_[i]);
        lv_obj_set_width(calendar_text_col, 0);
        lv_obj_set_flex_grow(calendar_text_col, 1);
        lv_obj_set_height(calendar_text_col, LV_SIZE_CONTENT);
        configure_column(calendar_text_col, 4);

        calendar_title_label_[i] = create_label(calendar_text_col, "-", font_body(), palette.text);
        calendar_relative_label_[i] = create_label(calendar_text_col, "", font_caption(), palette.accent);
        calendar_detail_label_[i] = create_label(calendar_text_col, "", font_caption(), palette.muted);
        safe_set_hidden(calendar_event_tile_[i], true);
    }
    ui_stage("calendar card ready");

    ui_stage("overlay boot path skipped");
    ui_stage("settings apply theme");
    apply_theme_();
    ui_stage("settings wifi chip");
    update_wifi_chip_();
    ui_stage("init complete");
}

void UIDashboard::update_clock(const char *time_text, const char *day_text)
{
    service_deferred_ui_creation_();
    safe_set_text(clock_label_, time_text != nullptr && time_text[0] != '\0' ? time_text : "--:--");
    safe_set_text(day_label_, day_text != nullptr && day_text[0] != '\0' ? day_text : "Waiting");
    animate_weather_icons_();
}

void UIDashboard::tick_animations()
{
    animate_weather_icons_();
}

void UIDashboard::update_connectivity(bool configured, bool connected, const char *status_text)
{
    connectivity_configured_ = configured;
    connectivity_connected_ = connected;
    copy_text(connectivity_status_text_, sizeof(connectivity_status_text_), status_text);
    const bool show_banner = !configured || !connected;
    if (offline_banner_label_ != nullptr) {
        if (!configured) {
            safe_set_text(offline_banner_label_, "Wi-Fi setup needed before live data can load.");
        } else {
            safe_set_text(offline_banner_label_, status_text != nullptr && status_text[0] != '\0' ? status_text : "Offline");
        }
    }
    safe_set_hidden(offline_banner_, !show_banner);
    update_wifi_chip_();
}

void UIDashboard::update_touch_status(bool enabled, bool touching, lv_coord_t x, lv_coord_t y)
{
    (void)x;
    (void)y;

    if (!enabled) {
        safe_set_text(touch_label_, "Touch offline");
        return;
    }

    if (touching) {
        safe_set_text(touch_label_, "Touch active");
    } else {
        safe_set_text(touch_label_, "Touch ready");
    }
}

void UIDashboard::update_runtime(const char *summary_text, const char *detail_text)
{
    safe_set_text(runtime_summary_label_, summary_text != nullptr ? summary_text : "Runtime idle");
    safe_set_text(runtime_detail_label_, detail_text != nullptr ? detail_text : "");
}

void UIDashboard::update_snapshot(const brief::DashboardSnapshot &snapshot)
{
    update_connectivity(
        true,
        snapshot.system.wifi_connected != 0,
        snapshot.system.status_text[0] != '\0' ? snapshot.system.status_text : "C6 snapshot"
    );

    char temperature[24];
    const bool weather_available = snapshot.weather.now.state != brief::kDataStateWaiting &&
                                   snapshot.weather.now.state != brief::kDataStateError;
    format_tenths(temperature, sizeof(temperature), snapshot.weather.now.temperature_tenths_c, " C");
    safe_set_text(weather_temp_label_, weather_available ? temperature : "--");
    copy_text(
        weather_live_condition_text_,
        sizeof(weather_live_condition_text_),
        snapshot.weather.now.condition[0] != '\0' ? snapshot.weather.now.condition : "Weather waiting");
    safe_set_text(weather_condition_label_, weather_live_condition_text_);
    safe_set_hidden(weather_icon_, true);
    copy_text(
        weather_live_detail_text_,
        sizeof(weather_live_detail_text_),
        snapshot.weather.summary_text[0] != '\0' ? snapshot.weather.summary_text : "Waiting for forecast data.");
    safe_set_text(weather_detail_label_, weather_live_detail_text_);
    copy_text(
        weather_live_change_text_,
        sizeof(weather_live_change_text_),
        snapshot.weather.next_change_text[0] != '\0' ? snapshot.weather.next_change_text : "Next change: waiting");
    safe_set_text(weather_change_label_, weather_live_change_text_);
    update_weather_daily_tiles_(snapshot.weather);
    update_weather_detail_page_(snapshot.weather);
    char weather_meta[64];
    snprintf(weather_meta, sizeof(weather_meta), "%s | %s", snapshot_state_text(snapshot.weather.now.state), snapshot.weather.now.updated_hhmm);
    copy_text(weather_live_meta_text_, sizeof(weather_live_meta_text_), weather_meta);
    safe_set_text(weather_meta_label_, weather_live_meta_text_);

    safe_set_text(tfl_summary_label_, snapshot.tfl.summary_text[0] != '\0' ? snapshot.tfl.summary_text : "Waiting for TfL data");
    char tfl_meta[64];
    snprintf(tfl_meta, sizeof(tfl_meta), "%s | %s", snapshot_state_text(snapshot.tfl.state), snapshot.tfl.updated_hhmm);
    safe_set_text(tfl_status_label_, tfl_meta);
    const uint8_t tfl_count = snapshot.tfl.line_count < kTflRows ? snapshot.tfl.line_count : kTflRows;
    for (uint8_t i = 0; i < kTflRows; ++i) {
        const bool show = i < tfl_count && snapshot.tfl.lines[i].available != 0;
        safe_set_hidden(tfl_row_[i], !show);
        if (show) {
            safe_set_text(tfl_line_label_[i], snapshot.tfl.lines[i].line_name);
            safe_set_text(tfl_state_label_[i], snapshot.tfl.lines[i].status);
            lv_obj_set_style_text_color(tfl_state_label_[i], snapshot.tfl.lines[i].disrupted ? state_color_("Offline") : state_color_("Live"), 0);
        }
    }

    char news_meta[64];
    snprintf(news_meta, sizeof(news_meta), "%s | %s", snapshot_state_text(snapshot.news.state), snapshot.news.updated_hhmm);
    safe_set_text(news_status_label_, news_meta);
    const uint8_t news_count = snapshot.news.headline_count < kNewsRows ? snapshot.news.headline_count : kNewsRows;
    for (uint8_t i = 0; i < kNewsRows; ++i) {
        const bool show = i < news_count && snapshot.news.headlines[i].available != 0;
        safe_set_hidden(news_title_label_[i], !show);
        safe_set_hidden(news_summary_label_[i], !show);
        if (show) {
            safe_set_text(news_title_label_[i], snapshot.news.headlines[i].title);
            safe_set_text(news_summary_label_[i], snapshot.news.headlines[i].summary);
        }
    }
}

void UIDashboard::update_weather(const WeatherData &data)
{
    char weather_log[224];
    snprintf(
        weather_log,
        sizeof(weather_log),
        "weather update | valid=%d loading=%d code=%u day=%d daily_count=%u updated=%s status=%s",
        data.valid ? 1 : 0,
        data.loading ? 1 : 0,
        static_cast<unsigned>(data.current_weather_code),
        data.is_day ? 1 : 0,
        static_cast<unsigned>(data.daily_count),
        data.updated[0] != '\0' ? data.updated : "<none>",
        data.status[0] != '\0' ? data.status : "<none>"
    );
    ui_debug(weather_log);

    const Palette &palette = palette_for(dark_mode_);
    if (!data.valid) {
        ui_debug("weather update | invalid payload path");
        safe_set_text(weather_temp_label_, "--");
        safe_set_text(weather_condition_label_, data.loading ? "Refreshing forecast" : "Weather waiting");
        safe_set_text(weather_detail_label_, data.error[0] != '\0' ? data.error : "No live weather yet. Tap Refresh once Wi-Fi is ready.");
        safe_set_text(weather_change_label_, "Next change: waiting for data");
        safe_set_text(weather_meta_label_, data.status[0] != '\0' ? data.status : "Weather idle");
        lv_obj_set_style_text_color(weather_condition_label_, palette.warn, 0);
        safe_set_hidden(weather_icon_, true);
        for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
            safe_set_hidden(weather_day_tile_[i], true);
            safe_set_hidden(weather_day_icon_[i], true);
        }
        return;
    }

    safe_set_text(weather_temp_label_, data.temperature);
    copy_text(weather_live_condition_text_, sizeof(weather_live_condition_text_), data.condition);
    safe_set_text(weather_condition_label_, weather_live_condition_text_);
    weather_live_code_ = data.current_weather_code;
    weather_live_day_ = data.is_day;
    if (kEnableDynamicWeatherArt) {
        ui_debug("weather update | dynamic icon render enabled");
        safe_set_hidden(weather_icon_, false);
        if (!weather_demo_mode_ && (data.current_weather_code != weather_icon_code_ || data.is_day != weather_icon_day_)) {
            snprintf(
                weather_log,
                sizeof(weather_log),
                "weather update | rendering main icon old_code=%u new_code=%u old_day=%d new_day=%d",
                static_cast<unsigned>(weather_icon_code_),
                static_cast<unsigned>(data.current_weather_code),
                weather_icon_day_ ? 1 : 0,
                data.is_day ? 1 : 0
            );
            ui_debug(weather_log);
            render_weather_icon_(weather_icon_, data.current_weather_code, data.is_day, kWeatherMainCellSize);
            weather_icon_code_ = data.current_weather_code;
            weather_icon_day_ = data.is_day;
        }
    } else {
        ui_debug("weather update | dynamic icon render disabled");
        safe_set_hidden(weather_icon_, true);
    }
    copy_text(weather_live_detail_text_, sizeof(weather_live_detail_text_), data.detail);
    safe_set_text(weather_detail_label_, weather_live_detail_text_);
    copy_text(
        weather_live_change_text_,
        sizeof(weather_live_change_text_),
        data.next_changes[0] != '\0' ? data.next_changes : "Next change: no sharp changes expected");
    safe_set_text(weather_change_label_, weather_live_change_text_);
    ui_debug("weather update | updating daily tiles");
    update_weather_daily_tiles_(data);

    char meta[96];
    snprintf(meta, sizeof(meta), "Updated %s | %s", data.updated, data.status);
    copy_text(weather_live_meta_text_, sizeof(weather_live_meta_text_), meta);
    safe_set_text(weather_meta_label_, weather_live_meta_text_);
    lv_obj_set_style_text_color(weather_condition_label_, state_color_(data.status), 0);
    ui_debug("weather update | updating detail page");
    update_weather_detail_page_(data);
    if (weather_demo_mode_) {
        update_weather_demo_labels_();
    }
    ui_debug("weather update | complete");
}

void UIDashboard::update_battery(uint8_t percent, float voltage, const char *status_text)
{
    const bool valid = voltage > 0.2f && percent <= 100;
    if (!valid) {
        safe_set_text(battery_label_, "Battery --");
        safe_set_text(settings_battery_label_, status_text != nullptr ? status_text : "Battery monitor waiting");
        return;
    }

    char header[32];
    snprintf(header, sizeof(header), "Battery %u%%", static_cast<unsigned>(percent));
    safe_set_text(battery_label_, header);

    char detail[96];
    snprintf(detail, sizeof(detail), "Battery %u%% | %.2f V | %s", static_cast<unsigned>(percent), voltage, status_text != nullptr ? status_text : "OK");
    safe_set_text(settings_battery_label_, detail);
}

void UIDashboard::update_tfl(const TflData &data)
{
    safe_set_text(tfl_summary_label_, data.valid ? data.summary : (data.loading ? "Refreshing TfL status" : "Waiting for TfL status"));
    char meta[96];
    snprintf(meta, sizeof(meta), "%s | Updated %s", data.status[0] != '\0' ? data.status : "TfL idle", data.updated[0] != '\0' ? data.updated : "--:--");
    safe_set_text(tfl_status_label_, meta);

    uint8_t visible = 0;
    for (uint8_t i = 0; i < kTflRows; ++i) {
        safe_set_hidden(tfl_row_[i], true);
    }

    for (uint8_t i = 0; i < data.line_count && visible < kTflRows; ++i) {
        if (!data.lines[i].available) {
            continue;
        }
        safe_set_hidden(tfl_row_[visible], false);
        safe_set_text(tfl_line_label_[visible], data.lines[i].name);
        safe_set_text(tfl_state_label_[visible], data.lines[i].status);
        lv_obj_set_style_text_color(tfl_state_label_[visible], data.lines[i].disrupted ? state_color_("Offline") : state_color_("Live"), 0);
        ++visible;
    }

    if (visible == 0) {
        safe_set_hidden(tfl_row_[0], false);
        safe_set_text(tfl_line_label_[0], "TfL");
        safe_set_text(tfl_state_label_[0], data.error[0] != '\0' ? data.error : "No line data yet");
        lv_obj_set_style_text_color(tfl_state_label_[0], state_color_("Waiting"), 0);
    }
}

void UIDashboard::update_news(const NewsData &data)
{
    char meta[96];
    snprintf(meta, sizeof(meta), "%s | Updated %s", data.status[0] != '\0' ? data.status : "News idle", data.updated[0] != '\0' ? data.updated : "--:--");
    safe_set_text(news_status_label_, meta);

    for (uint8_t i = 0; i < kNewsRows; ++i) {
        const bool show = data.valid && i < data.headline_count;
        safe_set_hidden(news_title_label_[i], !show);
        safe_set_hidden(news_summary_label_[i], !show);
        if (show) {
            safe_set_text(news_title_label_[i], data.headlines[i]);
            safe_set_text(news_summary_label_[i], data.summaries[i]);
        }
    }

    if (!data.valid) {
        safe_set_hidden(news_title_label_[0], false);
        safe_set_hidden(news_summary_label_[0], false);
        safe_set_text(news_title_label_[0], data.loading ? "Refreshing news" : "News briefing waiting");
        safe_set_text(news_summary_label_[0], data.error[0] != '\0' ? data.error : "Headlines will appear when networking is ready.");
    }
}

void UIDashboard::update_calendar(const CalendarData &data)
{
    char meta[96];
    snprintf(meta, sizeof(meta), "%s | Updated %s", data.status[0] != '\0' ? data.status : "Calendar idle", data.updated[0] != '\0' ? data.updated : "--:--");
    safe_set_text(calendar_status_label_, meta);

    if (!data.valid) {
        safe_set_text(calendar_summary_label_, data.loading ? "Refreshing bank holidays" : "Upcoming UK bank holidays");
        populate_calendar_grid_(data);
        for (uint8_t i = 0; i < kCalendarRows; ++i) {
            safe_set_hidden(calendar_event_tile_[i], true);
        }
        return;
    }

    time_t now = time(nullptr);
    if (!service_wall_clock_valid(now)) {
        if (data.event_count > 0 && data.events[0].available) {
            char summary[160];
            snprintf(summary, sizeof(summary), "Clock syncing | Next: %s on %s", data.events[0].title, data.events[0].date_text);
            safe_set_text(calendar_summary_label_, summary);
        } else {
            safe_set_text(calendar_summary_label_, "Waiting for clock sync");
        }
    } else {
        struct tm now_tm = {};
        localtime_r(&now, &now_tm);
        char month_title[96];
        strftime(month_title, sizeof(month_title), "%B %Y", &now_tm);
        if (data.event_count > 0 && data.events[0].available) {
            char summary[160];
            snprintf(summary, sizeof(summary), "%s | Next: %s on %s", month_title, data.events[0].title, data.events[0].date_text);
            safe_set_text(calendar_summary_label_, summary);
        } else {
            safe_set_text(calendar_summary_label_, month_title);
        }
    }

    populate_calendar_grid_(data);
    for (uint8_t i = 0; i < kCalendarRows; ++i) {
        const bool show = i < data.event_count && data.events[i].available;
        safe_set_hidden(calendar_event_tile_[i], !show);
        if (!show) {
            continue;
        }

        safe_set_text(calendar_date_label_[i], data.events[i].date_text);
        safe_set_text(calendar_title_label_[i], data.events[i].title);
        safe_set_text(calendar_relative_label_[i], data.events[i].relative_text);
        safe_set_text(calendar_detail_label_[i], data.events[i].notes);
    }
}

void UIDashboard::update_wifi_setup(const char *saved_ssid, const char *saved_password, const char *scan_options, const char *status_text, bool scanning)
{
    if (wifi_setup_status_label_ != nullptr) {
        safe_set_text(wifi_setup_status_label_, status_text != nullptr ? status_text : "Scan for nearby access points");
    }
    if (settings_network_saved_label_ != nullptr) {
        char summary[128];
        snprintf(
            summary,
            sizeof(summary),
            "Saved network: %s",
            (saved_ssid != nullptr && saved_ssid[0] != '\0') ? saved_ssid : "none");
        safe_set_text(settings_network_saved_label_, summary);
    }

    if (!wifi_setup_ui_initialized_) {
        return;
    }
    if (wifi_setup_editor_status_label_ != nullptr) {
        safe_set_text(wifi_setup_editor_status_label_, status_text != nullptr ? status_text : "Scan for nearby access points");
    }
    safe_set_text(wifi_scan_button_label_, scanning ? "Scanning..." : "Scan Wi-Fi");

    if (wifi_network_dropdown_ != nullptr && scan_options != nullptr && scan_options[0] != '\0') {
        lv_dropdown_set_options(wifi_network_dropdown_, scan_options);
    }

    if (wifi_ssid_textarea_ != nullptr && !lv_obj_has_state(wifi_ssid_textarea_, LV_STATE_FOCUSED)) {
        const char *current = lv_textarea_get_text(wifi_ssid_textarea_);
        if (current == nullptr || strcmp(current, saved_ssid != nullptr ? saved_ssid : "") != 0) {
            lv_textarea_set_text(wifi_ssid_textarea_, saved_ssid != nullptr ? saved_ssid : "");
        }
    }

    if (wifi_password_textarea_ != nullptr && !lv_obj_has_state(wifi_password_textarea_, LV_STATE_FOCUSED)) {
        const char *current = lv_textarea_get_text(wifi_password_textarea_);
        if (current == nullptr || strcmp(current, saved_password != nullptr ? saved_password : "") != 0) {
            lv_textarea_set_text(wifi_password_textarea_, saved_password != nullptr ? saved_password : "");
        }
    }
}

void UIDashboard::ensure_wifi_setup_controls_()
{
    if (wifi_setup_ui_initialized_ || settings_panel_ == nullptr || settings_overlay_ == nullptr) {
        return;
    }

    ui_debug("wifi setup | creating controls");
    const Palette &palette = palette_for(dark_mode_);
    create_label(settings_panel_, "Wi-Fi setup", font_body(), palette.text);
    wifi_setup_editor_status_label_ = create_label(settings_panel_, "Scan for nearby access points", font_caption(), palette.muted);

    wifi_network_dropdown_ = lv_dropdown_create(settings_panel_);
    lv_obj_set_width(wifi_network_dropdown_, lv_pct(100));
    lv_dropdown_set_options(wifi_network_dropdown_, wifi_scan_options_);

    lv_obj_t *wifi_button_row = create_clean_obj(settings_panel_);
    lv_obj_set_width(wifi_button_row, lv_pct(100));
    configure_row(wifi_button_row, 10);
    wifi_scan_button_ = create_button(wifi_button_row, "Scan Wi-Fi", wifi_scan_event_cb, this);
    wifi_scan_button_label_ = button_label(wifi_scan_button_);
    lv_obj_set_flex_grow(wifi_scan_button_, 1);
    wifi_use_selected_button_ = create_button(wifi_button_row, "Use Selected", wifi_use_selected_event_cb, this);
    wifi_use_selected_button_label_ = button_label(wifi_use_selected_button_);
    lv_obj_set_flex_grow(wifi_use_selected_button_, 1);

    wifi_ssid_textarea_ = lv_textarea_create(settings_panel_);
    lv_obj_set_width(wifi_ssid_textarea_, lv_pct(100));
    lv_textarea_set_one_line(wifi_ssid_textarea_, true);
    lv_textarea_set_placeholder_text(wifi_ssid_textarea_, "Wi-Fi SSID");
    lv_obj_add_event_cb(wifi_ssid_textarea_, wifi_textarea_event_cb, LV_EVENT_ALL, this);

    wifi_password_textarea_ = lv_textarea_create(settings_panel_);
    lv_obj_set_width(wifi_password_textarea_, lv_pct(100));
    lv_textarea_set_one_line(wifi_password_textarea_, true);
    lv_textarea_set_password_mode(wifi_password_textarea_, true);
    lv_textarea_set_placeholder_text(wifi_password_textarea_, "Wi-Fi password");
    lv_obj_add_event_cb(wifi_password_textarea_, wifi_textarea_event_cb, LV_EVENT_ALL, this);

    wifi_connect_button_ = create_button(settings_panel_, "Connect Wi-Fi", wifi_connect_event_cb, this);
    wifi_connect_button_label_ = button_label(wifi_connect_button_);
    lv_obj_set_width(wifi_connect_button_, lv_pct(100));

    wifi_setup_ui_initialized_ = true;
    ui_debug("wifi setup | controls created");
    style_wifi_setup_controls_();
}

void UIDashboard::ensure_settings_overlay_()
{
    if (settings_overlay_ != nullptr || screen_ == nullptr) {
        return;
    }

    ui_debug("settings | lazy create start");
    const Palette &palette = palette_for(dark_mode_);

    settings_overlay_ = create_clean_obj(screen_);
    if (settings_overlay_ == nullptr) {
        ui_debug("settings | lazy create failed overlay");
        return;
    }

    lv_obj_set_size(settings_overlay_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(settings_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(settings_overlay_, 18, 0);
    configure_column(settings_overlay_, 0);
    lv_obj_set_flex_align(settings_overlay_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    settings_panel_ = create_clean_obj(settings_overlay_);
    if (settings_panel_ == nullptr) {
        ui_debug("settings | lazy create failed panel");
        return;
    }

    lv_obj_set_width(settings_panel_, lv_pct(100));
    lv_obj_set_height(settings_panel_, lv_pct(100));
    lv_obj_set_style_bg_opa(settings_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(settings_panel_, 20, 0);
    apply_pixel_panel_style(settings_panel_, palette, dark_mode_);
    configure_column(settings_panel_, 16);

    lv_obj_t *settings_header = create_clean_obj(settings_panel_);
    lv_obj_set_width(settings_header, lv_pct(100));
    lv_obj_set_height(settings_header, 52);
    configure_row(settings_header, 12);
    lv_obj_set_flex_align(settings_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *settings_title = create_label(settings_header, kPixelArtEnabled ? "SETTINGS" : "Settings", font_title(), palette.text);
    lv_obj_set_width(settings_title, 0);
    lv_obj_set_flex_grow(settings_title, 1);

    settings_close_button_ = create_button(settings_header, "Close", settings_event_cb, this);
    settings_close_label_ = button_label(settings_close_button_);
    lv_obj_set_width(settings_close_button_, 120);

    settings_display_label_ = create_label(settings_panel_, "Display", font_body(), palette.text);
    brightness_label_ = create_label(settings_panel_, "Brightness 42%", font_body(), palette.text);
    create_label(
        settings_panel_,
        "Brightness slider is disabled in the stable settings build.",
        font_caption(),
        palette.muted);

    settings_network_label_ = create_label(settings_panel_, "Network", font_body(), palette.text);
    wifi_setup_status_label_ = create_label(
        settings_panel_,
        "Network summary will appear here.",
        font_body(),
        palette.text);
    settings_network_saved_label_ = create_label(
        settings_panel_,
        "Saved network: none",
        font_caption(),
        palette.muted);
    settings_network_tools_button_ = nullptr;
    settings_network_tools_button_label_ = nullptr;
    settings_network_hint_label_ = create_label(
        settings_panel_,
        "Advanced Wi-Fi tools stay disabled while we stabilize the settings screen.",
        font_caption(),
        palette.muted);
    apply_pixel_label_style(settings_display_label_, font_body(), palette.text);
    apply_pixel_label_style(brightness_label_, font_body(), palette.text);
    apply_pixel_label_style(settings_network_label_, font_body(), palette.text);
    apply_pixel_label_style(wifi_setup_status_label_, font_body(), palette.text);
    apply_pixel_label_style(settings_network_saved_label_, font_caption(), palette.muted);
    apply_pixel_label_style(settings_network_hint_label_, font_caption(), palette.dim);
    update_button_style(settings_close_button_, palette, dark_mode_);
    apply_settings_safe_style_();
    update_brightness_label_();
    update_wifi_chip_();
    safe_set_hidden(settings_overlay_, true);
    ui_debug("settings | lazy create complete");
}

void UIDashboard::destroy_settings_overlay_()
{
    if (settings_overlay_ != nullptr) {
        lv_obj_delete(settings_overlay_);
    }

    settings_overlay_ = nullptr;
    settings_panel_ = nullptr;
    settings_display_label_ = nullptr;
    brightness_label_ = nullptr;
    brightness_slider_ = nullptr;
    settings_system_label_ = nullptr;
    settings_network_label_ = nullptr;
    wifi_setup_status_label_ = nullptr;
    wifi_setup_editor_status_label_ = nullptr;
    settings_network_tools_button_ = nullptr;
    settings_network_tools_button_label_ = nullptr;
    wifi_network_dropdown_ = nullptr;
    wifi_scan_button_ = nullptr;
    wifi_scan_button_label_ = nullptr;
    wifi_use_selected_button_ = nullptr;
    wifi_use_selected_button_label_ = nullptr;
    wifi_ssid_textarea_ = nullptr;
    wifi_password_textarea_ = nullptr;
    wifi_connect_button_ = nullptr;
    wifi_connect_button_label_ = nullptr;
    wifi_keyboard_ = nullptr;
    settings_battery_label_ = nullptr;
    settings_network_saved_label_ = nullptr;
    settings_network_hint_label_ = nullptr;
    runtime_summary_label_ = nullptr;
    runtime_detail_label_ = nullptr;
    shutdown_button_ = nullptr;
    shutdown_button_label_ = nullptr;
    settings_close_button_ = nullptr;
    settings_close_label_ = nullptr;
    wifi_setup_ui_initialized_ = false;
    if (screen_ != nullptr) {
        lv_obj_invalidate(screen_);
    }
}

void UIDashboard::populate_calendar_grid_(const CalendarData &data)
{
    if (calendar_grid_ == nullptr) {
        return;
    }

    static const char *kHeaders[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    lv_table_set_row_count(calendar_grid_, 7);
    lv_table_set_column_count(calendar_grid_, 7);
    lv_obj_update_layout(calendar_grid_);
    lv_coord_t grid_w = lv_obj_get_content_width(calendar_grid_);
    if (grid_w <= 0) {
        grid_w = lv_obj_get_width(calendar_grid_);
    }
    const lv_coord_t base_col_w = grid_w > 0 ? static_cast<lv_coord_t>(grid_w / 7) : 1;
    lv_coord_t remainder = grid_w > 0 ? static_cast<lv_coord_t>(grid_w - (base_col_w * 7)) : 0;
    lv_coord_t col_widths[7] = {};
    lv_coord_t col_offsets[7] = {};
    lv_coord_t running_x = 0;
    for (uint8_t col = 0; col < 7; ++col) {
        col_widths[col] = base_col_w + (remainder > 0 ? 1 : 0);
        if (remainder > 0) {
            --remainder;
        }
        col_offsets[col] = running_x;
        running_x = static_cast<lv_coord_t>(running_x + col_widths[col]);
        lv_table_set_col_width(calendar_grid_, col, col_widths[col]);
        lv_table_set_cell_value(calendar_grid_, 0, col, kHeaders[col]);
    }

    time_t now = time(nullptr);
    if (!service_wall_clock_valid(now)) {
        for (uint8_t row = 1; row < 7; ++row) {
            for (uint8_t col = 0; col < 7; ++col) {
                lv_table_set_cell_value(calendar_grid_, row, col, "");
            }
        }
        if (calendar_today_marker_ != nullptr) {
            safe_set_hidden(calendar_today_marker_, true);
        }
        return;
    }

    struct tm today_tm = {};
    localtime_r(&now, &today_tm);

    struct tm month_tm = today_tm;
    month_tm.tm_mday = 1;
    month_tm.tm_hour = 12;
    month_tm.tm_min = 0;
    month_tm.tm_sec = 0;
    month_tm.tm_isdst = -1;
    mktime(&month_tm);

    const int month = month_tm.tm_mon;
    const int year = month_tm.tm_year;
    const int today_day = today_tm.tm_mday;
    const uint8_t first_col = weekday_monday_first(month_tm);
    const int days_in_month = days_in_month_for(year + 1900, month);
    bool today_marker_visible = false;

    for (uint8_t row = 1; row < 7; ++row) {
        for (uint8_t col = 0; col < 7; ++col) {
            lv_table_set_cell_value(calendar_grid_, row, col, "");
        }
    }

    for (int day = 1; day <= days_in_month; ++day) {
        const int slot = static_cast<int>(first_col) + (day - 1);
        const uint8_t row = static_cast<uint8_t>(1 + (slot / 7));
        const uint8_t col = static_cast<uint8_t>(slot % 7);
        char cell[48];
        snprintf(cell, sizeof(cell), "%d", day);

        for (uint8_t i = 0; i < data.event_count; ++i) {
            if (!data.events[i].available) {
                continue;
            }

            struct tm event_tm = {};
            if (!parse_iso_date_local(data.events[i].iso_date, &event_tm)) {
                continue;
            }
            if (event_tm.tm_year == year && event_tm.tm_mon == month && event_tm.tm_mday == day) {
                snprintf(cell, sizeof(cell), "%d\nHoliday", day);
                break;
            }
        }

        lv_table_set_cell_value(calendar_grid_, row, col, cell);

        if (calendar_today_marker_ != nullptr && day == today_day) {
            lv_obj_update_layout(calendar_grid_);
            const lv_coord_t grid_h = lv_obj_get_height(calendar_grid_);
            const lv_coord_t row_h = grid_h / 7;
            const lv_coord_t col_w = col_widths[col];
            const lv_coord_t marker = LV_MIN(row_h, col_w) > 8 ? static_cast<lv_coord_t>(LV_MIN(row_h, col_w) - 8) : LV_MIN(row_h, col_w);
            const lv_coord_t grid_x = lv_obj_get_x(calendar_grid_);
            const lv_coord_t grid_y = lv_obj_get_y(calendar_grid_);
            lv_obj_set_size(calendar_today_marker_, marker, marker);
            lv_obj_set_pos(
                calendar_today_marker_,
                static_cast<lv_coord_t>(grid_x + col_offsets[col] + (col_w - marker) / 2),
                static_cast<lv_coord_t>(grid_y + row * row_h + (row_h - marker) / 2));
            safe_set_hidden(calendar_today_marker_, false);
            today_marker_visible = true;
        }
    }

    if (calendar_today_marker_ != nullptr && !today_marker_visible) {
        safe_set_hidden(calendar_today_marker_, true);
    }
}

void UIDashboard::animate_weather_icons_()
{
    if (!kEnableDynamicWeatherArt) {
        return;
    }

    const uint32_t now_ms = lv_tick_get();
    const uint8_t breath_phase = static_cast<uint8_t>((now_ms / 700U) % 6U);

    if (weather_demo_mode_) {
        if (weather_demo_interaction_ms_ != 0 &&
            static_cast<uint32_t>(now_ms - weather_demo_interaction_ms_) >= kWeatherDemoAutoExitMs) {
            weather_demo_mode_ = false;
            restore_live_weather_icon_();
            return;
        }

        if (weather_demo_last_step_ms_ == 0 ||
            static_cast<uint32_t>(now_ms - weather_demo_last_step_ms_) >= kWeatherDemoAutoStepMs) {
            weather_demo_last_step_ms_ = now_ms;
            weather_demo_index_ = static_cast<uint8_t>((weather_demo_index_ + 1U) % weather_demo_count());
            apply_weather_demo_frame_();
        }
    }

    auto animate_cells = [&](lv_obj_t **cells, uint8_t weather_code, bool is_day, bool compact) {
        if (cells == nullptr || cells[0] == nullptr || weather_code == 255U) {
            return;
        }
        const WeatherIconKind kind = icon_kind_from_code(weather_code, is_day);
        auto phase_for = [&](WeatherIconKind target_kind, bool target_compact) -> uint8_t {
            uint32_t frame_ms = 1000U;
            switch (target_kind) {
                case WeatherIconKind::ClearDay:
                    frame_ms = target_compact ? 1100U : 900U;
                    break;
                case WeatherIconKind::ClearNight:
                    frame_ms = target_compact ? 1400U : 1200U;
                    break;
                case WeatherIconKind::Cloud:
                    frame_ms = target_compact ? 1700U : 1500U;
                    break;
                case WeatherIconKind::Fog:
                    frame_ms = target_compact ? 2200U : 2000U;
                    break;
                case WeatherIconKind::Rain:
                    frame_ms = target_compact ? 600U : 450U;
                    break;
                case WeatherIconKind::Snow:
                    frame_ms = target_compact ? 1500U : 1100U;
                    break;
                case WeatherIconKind::Thunder:
                    frame_ms = target_compact ? 500U : 350U;
                    break;
            }
            return static_cast<uint8_t>((now_ms / frame_ms) % 6U);
        };
        const uint8_t phase = phase_for(kind, compact);

        for (uint8_t i = 0; i < kWeatherIconPixels; ++i) {
            if (cells[i] == nullptr || lv_obj_has_flag(cells[i], LV_OBJ_FLAG_HIDDEN)) {
                continue;
            }
            lv_obj_set_style_bg_opa(cells[i], LV_OPA_COVER, 0);
            lv_obj_set_style_translate_y(cells[i], 0, 0);
            lv_obj_set_style_translate_x(cells[i], 0, 0);
        }

        auto pulse = [&](const uint8_t *pixels, size_t count, lv_opa_t opa, lv_coord_t dx, lv_coord_t dy) {
            for (size_t idx = 0; idx < count; ++idx) {
                const uint8_t pixel_index = pixels[idx];
                if (pixel_index >= kWeatherIconPixels || cells[pixel_index] == nullptr ||
                    lv_obj_has_flag(cells[pixel_index], LV_OBJ_FLAG_HIDDEN)) {
                    continue;
                }
                lv_obj_set_style_bg_opa(cells[pixel_index], opa, 0);
                lv_obj_set_style_translate_x(cells[pixel_index], dx, 0);
                lv_obj_set_style_translate_y(cells[pixel_index], dy, 0);
            }
        };

        if (compact) {
            switch (kind) {
                case WeatherIconKind::ClearDay: {
                    static const uint8_t kFrames[][2] = {{13U, 31U}, {14U, 32U}, {22U, 40U}, {23U, 41U}, {31U, 49U}, {32U, 50U}};
                    pulse(kFrames[phase], 2, LV_OPA_40, 0, 0);
                    break;
                }
                case WeatherIconKind::ClearNight: {
                    static const uint8_t kMoonGlow[][3] = {{13U, 22U, 31U}, {14U, 23U, 32U}, {22U, 31U, 40U}, {23U, 32U, 41U}, {31U, 40U, 49U}, {32U, 41U, 50U}};
                    pulse(kMoonGlow[phase], 3, phase % 2U == 0 ? LV_OPA_30 : LV_OPA_60, 0, 0);
                    static const uint8_t kStars[][2] = {{6U, 74U}, {6U, 74U}, {0U, 56U}, {0U, 56U}, {6U, 74U}, {0U, 56U}};
                    pulse(kStars[phase], 2, phase % 2U == 0 ? LV_OPA_10 : LV_OPA_40, 0, 0);
                    break;
                }
                case WeatherIconKind::Cloud:
                case WeatherIconKind::Fog: {
                    static const uint8_t kFrames[][3] = {{19U, 20U, 21U}, {20U, 21U, 22U}, {21U, 22U, 23U}, {22U, 23U, 24U}, {21U, 22U, 23U}, {20U, 21U, 22U}};
                    pulse(kFrames[phase], 3, kind == WeatherIconKind::Fog ? LV_OPA_50 : LV_OPA_30, kind == WeatherIconKind::Fog ? (phase < 3U ? 1 : -1) : 0, 0);
                    break;
                }
                case WeatherIconKind::Rain:
                case WeatherIconKind::Snow: {
                    static const uint8_t kRainFrames[][3] = {{56U, 58U, 60U}, {65U, 67U, 69U}, {56U, 58U, 60U}, {65U, 67U, 69U}, {56U, 58U, 60U}, {65U, 67U, 69U}};
                    static const uint8_t kSnowFrames[][3] = {{56U, 58U, 69U}, {57U, 67U, 70U}, {54U, 59U, 66U}, {48U, 56U, 68U}, {47U, 60U, 69U}, {57U, 59U, 71U}};
                    pulse(kind == WeatherIconKind::Snow ? kSnowFrames[phase] : kRainFrames[phase], 3, LV_OPA_40, kind == WeatherIconKind::Snow ? (phase % 2U == 0 ? -1 : 1) : 0, kind == WeatherIconKind::Snow ? 0 : 1);
                    break;
                }
                case WeatherIconKind::Thunder: {
                    static const uint8_t kFrames[][4] = {{48U, 56U, 57U, 65U}, {39U, 47U, 48U, 56U}, {48U, 49U, 57U, 65U}, {39U, 47U, 48U, 56U}, {48U, 56U, 57U, 65U}, {39U, 47U, 48U, 56U}};
                    pulse(kFrames[phase], 4, phase % 2U == 0 ? LV_OPA_10 : LV_OPA_60, 0, 0);
                    break;
                }
            }
            return;
        }

        switch (kind) {
            case WeatherIconKind::ClearDay: {
                static const uint8_t kFrames[][2] = {{4U, 40U}, {6U, 42U}, {8U, 44U}, {22U, 58U}, {24U, 60U}, {26U, 62U}};
                pulse(kFrames[phase], 2, LV_OPA_30, 0, -1);
                static const uint8_t kHalo[][4] = {{13U, 21U, 29U, 39U}, {15U, 23U, 31U, 41U}, {17U, 25U, 33U, 43U}, {37U, 45U, 53U, 61U}, {35U, 43U, 51U, 59U}, {33U, 41U, 49U, 57U}};
                pulse(kHalo[phase], 4, LV_OPA_20, 0, 0);
                break;
            }
            case WeatherIconKind::ClearNight: {
                static const uint8_t kMoonGlow[][4] = {{13U, 22U, 31U, 40U}, {14U, 23U, 32U, 41U}, {22U, 31U, 40U, 49U}, {23U, 32U, 41U, 50U}, {31U, 40U, 49U, 58U}, {32U, 41U, 50U, 59U}};
                pulse(kMoonGlow[phase], 4, phase % 2U == 0 ? LV_OPA_20 : LV_OPA_50, 0, -1);
                static const uint8_t kStars[][3] = {{0U, 6U, 74U}, {0U, 6U, 74U}, {17U, 56U, 74U}, {17U, 56U, 65U}, {0U, 6U, 74U}, {17U, 56U, 65U}};
                pulse(kStars[phase], 3, phase % 2U == 0 ? LV_OPA_10 : LV_OPA_40, 0, 0);
                break;
            }
            case WeatherIconKind::Cloud: {
                static const uint8_t kFrames[][5] = {{11U, 12U, 13U, 14U, 15U}, {12U, 13U, 14U, 15U, 16U}, {13U, 14U, 15U, 16U, 17U}, {14U, 15U, 16U, 17U, 18U}, {13U, 14U, 15U, 16U, 17U}, {12U, 13U, 14U, 15U, 16U}};
                pulse(kFrames[phase], 5, LV_OPA_30, phase < 3U ? 1 : -1, 0);
                static const uint8_t kUnderGlow[][4] = {{39U, 40U, 41U, 42U}, {40U, 41U, 42U, 43U}, {41U, 42U, 43U, 44U}, {42U, 43U, 44U, 45U}, {41U, 42U, 43U, 44U}, {40U, 41U, 42U, 43U}};
                pulse(kUnderGlow[phase], 4, LV_OPA_20, phase < 3U ? 1 : -1, 1);
                break;
            }
            case WeatherIconKind::Fog: {
                static const uint8_t kBandLow[][5] = {{46U, 47U, 48U, 49U, 50U}, {47U, 48U, 49U, 50U, 51U}, {48U, 49U, 50U, 51U, 52U}, {47U, 48U, 49U, 50U, 51U}, {46U, 47U, 48U, 49U, 50U}, {45U, 46U, 47U, 48U, 49U}};
                static const uint8_t kBandMid[][5] = {{28U, 29U, 30U, 31U, 32U}, {29U, 30U, 31U, 32U, 33U}, {30U, 31U, 32U, 33U, 34U}, {29U, 30U, 31U, 32U, 33U}, {28U, 29U, 30U, 31U, 32U}, {27U, 28U, 29U, 30U, 31U}};
                pulse(kBandLow[phase], 5, LV_OPA_50, phase < 3U ? 2 : -2, 0);
                pulse(kBandMid[phase], 5, LV_OPA_30, phase < 3U ? -1 : 1, 0);
                break;
            }
            case WeatherIconKind::Rain: {
                static const uint8_t kFramesA[][4] = {{56U, 58U, 60U, 62U}, {65U, 67U, 69U, 71U}, {56U, 58U, 60U, 62U}, {65U, 67U, 69U, 71U}, {56U, 58U, 60U, 62U}, {65U, 67U, 69U, 71U}};
                static const uint8_t kFramesB[][3] = {{47U, 49U, 51U}, {56U, 58U, 60U}, {65U, 67U, 69U}, {48U, 50U, 52U}, {57U, 59U, 61U}, {66U, 68U, 70U}};
                pulse(kFramesA[phase], 4, LV_OPA_40, 0, 2);
                pulse(kFramesB[phase], 3, LV_OPA_30, 0, 1);
                static const uint8_t kCloudFlash[][4] = {{22U, 23U, 24U, 25U}, {23U, 24U, 25U, 26U}, {24U, 25U, 26U, 27U}, {23U, 24U, 25U, 26U}, {22U, 23U, 24U, 25U}, {21U, 22U, 23U, 24U}};
                pulse(kCloudFlash[phase], 4, LV_OPA_10, 0, 0);
                break;
            }
            case WeatherIconKind::Snow: {
                static const uint8_t kFlakesA[][4] = {{56U, 58U, 69U, 71U}, {48U, 58U, 60U, 69U}, {47U, 57U, 59U, 70U}, {48U, 56U, 68U, 70U}, {47U, 58U, 60U, 71U}, {48U, 57U, 69U, 71U}};
                static const uint8_t kFlakesB[][3] = {{54U, 67U, 74U}, {55U, 60U, 68U}, {49U, 57U, 65U}, {50U, 59U, 66U}, {55U, 68U, 75U}, {49U, 58U, 67U}};
                pulse(kFlakesA[phase], 4, LV_OPA_30, phase % 2U == 0 ? -1 : 1, 0);
                pulse(kFlakesB[phase], 3, LV_OPA_20, phase < 3U ? 1 : -1, -1);
                static const uint8_t kSparkle[][4] = {{47U, 56U, 58U, 69U}, {48U, 57U, 59U, 70U}, {56U, 58U, 67U, 69U}, {47U, 57U, 68U, 70U}, {48U, 56U, 58U, 71U}, {47U, 58U, 60U, 69U}};
                pulse(kSparkle[phase], 4, LV_OPA_20, 0, 0);
                break;
            }
            case WeatherIconKind::Thunder: {
                static const uint8_t kFrames[][5] = {{39U, 47U, 48U, 56U, 65U}, {48U, 56U, 57U, 65U, 74U}, {39U, 47U, 56U, 57U, 65U}, {48U, 49U, 57U, 65U, 74U}, {39U, 47U, 48U, 56U, 65U}, {48U, 56U, 57U, 65U, 74U}};
                pulse(kFrames[phase], 5, phase % 2U == 0 ? LV_OPA_10 : LV_OPA_70, 0, 1);
                static const uint8_t kStormGlow[][5] = {{11U, 12U, 13U, 14U, 15U}, {12U, 13U, 14U, 15U, 16U}, {13U, 14U, 15U, 16U, 17U}, {12U, 13U, 14U, 15U, 16U}, {11U, 12U, 13U, 14U, 15U}, {10U, 11U, 12U, 13U, 14U}};
                pulse(kStormGlow[phase], 5, phase % 3U == 0 ? LV_OPA_10 : LV_OPA_20, 0, 0);
                break;
            }
        }
    };

    animate_cells(weather_icon_pixel_, weather_icon_code_, weather_icon_day_, false);
    if (weather_icon_ != nullptr) {
        static const lv_coord_t kBreath[6] = {0, -1, -2, -1, 0, 1};
        lv_obj_set_style_translate_y(weather_icon_, kBreath[breath_phase], 0);
    }
    if (kEnableDynamicWeatherDailyArt) {
        for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
            animate_cells(weather_day_icon_pixel_[i], weather_day_icon_code_[i], true, true);
        }
    }
}

void UIDashboard::apply_weather_demo_frame_()
{
    if (weather_icon_ == nullptr) {
        return;
    }

    render_weather_icon_(
        weather_icon_,
        kWeatherDemoCodes[weather_demo_index_],
        kWeatherDemoDay[weather_demo_index_],
        kWeatherMainCellSize);
    weather_icon_code_ = kWeatherDemoCodes[weather_demo_index_];
    weather_icon_day_ = kWeatherDemoDay[weather_demo_index_];
    update_weather_demo_labels_();
}

void UIDashboard::restore_live_weather_icon_()
{
    safe_set_text(weather_details_button_label_, "Preview");
    if (weather_live_code_ != 255U) {
        render_weather_icon_(weather_icon_, weather_live_code_, weather_live_day_, kWeatherMainCellSize);
        weather_icon_code_ = weather_live_code_;
        weather_icon_day_ = weather_live_day_;
    } else {
        weather_icon_code_ = 255U;
    }
    safe_set_text(
        weather_condition_label_,
        weather_live_condition_text_[0] != '\0' ? weather_live_condition_text_ : "Weather waiting");
    safe_set_text(
        weather_detail_label_,
        weather_live_detail_text_[0] != '\0' ? weather_live_detail_text_ : "Waiting for forecast data.");
    safe_set_text(
        weather_change_label_,
        weather_live_change_text_[0] != '\0' ? weather_live_change_text_ : "Next change: waiting");
    safe_set_text(
        weather_meta_label_,
        weather_live_meta_text_[0] != '\0' ? weather_live_meta_text_ : "Weather idle");
}

void UIDashboard::update_weather_demo_labels_()
{
    char detail[96];
    snprintf(
        detail,
        sizeof(detail),
        "Preview %u/%u: %s",
        static_cast<unsigned>(weather_demo_index_ + 1U),
        static_cast<unsigned>(weather_demo_count()),
        kWeatherDemoNames[weather_demo_index_]);
    safe_set_text(weather_details_button_label_, "Next Demo");
    safe_set_text(weather_condition_label_, detail);

    char step_hint[112];
    snprintf(
        step_hint,
        sizeof(step_hint),
        "Tap Preview to step. Auto cycle every %lus.",
        static_cast<unsigned long>(kWeatherDemoAutoStepMs / 1000U));
    safe_set_text(weather_change_label_, step_hint);
    safe_set_text(weather_meta_label_, "Weather animation preview");
}

void UIDashboard::show_touch_feedback(lv_coord_t x, lv_coord_t y)
{
    update_touch_status(true, true, x, y);
}

void UIDashboard::set_brightness(uint8_t percent)
{
    if (percent < kMinBrightnessPercent) {
        percent = kMinBrightnessPercent;
    }
    if (percent > 100) {
        percent = 100;
    }

    brightness_percent_ = percent;
    if (brightness_slider_ != nullptr) {
        lv_slider_set_value(brightness_slider_, brightness_percent_, LV_ANIM_OFF);
    }
    update_brightness_label_();
}

void UIDashboard::set_dark_mode(bool dark_mode)
{
    dark_mode_ = dark_mode;
    apply_theme_();
    update_theme_button_label_();
}

bool UIDashboard::consume_refresh_request()
{
    const bool requested = refresh_requested_;
    refresh_requested_ = false;
    if (requested) {
        safe_set_text(refresh_button_label_, "Refresh");
    }
    return requested;
}

bool UIDashboard::consume_weather_refresh_request()
{
    const bool requested = weather_refresh_requested_;
    weather_refresh_requested_ = false;
    if (requested) {
        safe_set_text(weather_refresh_button_label_, "Refresh Weather");
    }
    return requested;
}

bool UIDashboard::consume_tfl_refresh_request()
{
    const bool requested = tfl_refresh_requested_;
    tfl_refresh_requested_ = false;
    if (requested) {
        safe_set_text(tfl_refresh_button_label_, "Refresh TfL");
    }
    return requested;
}

bool UIDashboard::consume_news_refresh_request()
{
    const bool requested = news_refresh_requested_;
    news_refresh_requested_ = false;
    if (requested) {
        safe_set_text(news_refresh_button_label_, "Refresh News");
    }
    return requested;
}

bool UIDashboard::consume_calendar_refresh_request()
{
    const bool requested = calendar_refresh_requested_;
    calendar_refresh_requested_ = false;
    if (requested) {
        safe_set_text(calendar_refresh_button_label_, "Refresh Calendar");
    }
    return requested;
}

bool UIDashboard::consume_wifi_scan_request()
{
    const bool requested = wifi_scan_requested_;
    wifi_scan_requested_ = false;
    if (requested) {
        safe_set_text(wifi_scan_button_label_, "Scan Wi-Fi");
    }
    return requested;
}

bool UIDashboard::consume_wifi_connect_request(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    if (!wifi_connect_requested_) {
        return false;
    }

    wifi_connect_requested_ = false;
    if (ssid != nullptr && ssid_size > 0 && wifi_ssid_textarea_ != nullptr) {
        copy_text(ssid, ssid_size, lv_textarea_get_text(wifi_ssid_textarea_));
    }
    if (password != nullptr && password_size > 0 && wifi_password_textarea_ != nullptr) {
        copy_text(password, password_size, lv_textarea_get_text(wifi_password_textarea_));
    }
    safe_set_text(wifi_connect_button_label_, "Connect Wi-Fi");
    return true;
}

bool UIDashboard::consume_brightness_request(uint8_t *percent)
{
    if (!brightness_requested_) {
        return false;
    }

    brightness_requested_ = false;
    if (percent != nullptr) {
        *percent = brightness_percent_;
    }
    return true;
}

bool UIDashboard::consume_theme_change_request(bool *dark_mode)
{
    if (!theme_change_requested_) {
        return false;
    }

    theme_change_requested_ = false;
    if (dark_mode != nullptr) {
        *dark_mode = dark_mode_;
    }
    return true;
}

bool UIDashboard::consume_shutdown_request()
{
    const bool requested = shutdown_requested_;
    shutdown_requested_ = false;
    return requested;
}

void UIDashboard::handle_refresh_button()
{
    refresh_requested_ = true;
    safe_set_text(refresh_button_label_, "Refreshing");
}

void UIDashboard::handle_weather_refresh_button()
{
    weather_refresh_requested_ = true;
    safe_set_text(weather_refresh_button_label_, "Queued");
}

void UIDashboard::handle_tfl_refresh_button()
{
    tfl_refresh_requested_ = true;
    safe_set_text(tfl_refresh_button_label_, "Queued");
}

void UIDashboard::handle_news_refresh_button()
{
    news_refresh_requested_ = true;
    safe_set_text(news_refresh_button_label_, "Queued");
}

void UIDashboard::handle_calendar_refresh_button()
{
    calendar_refresh_requested_ = true;
    safe_set_text(calendar_refresh_button_label_, "Queued");
}

void UIDashboard::handle_wifi_scan_button()
{
    wifi_scan_requested_ = true;
    safe_set_text(wifi_scan_button_label_, "Scanning...");
}

void UIDashboard::handle_wifi_use_selected_button()
{
    if (wifi_network_dropdown_ == nullptr || wifi_ssid_textarea_ == nullptr) {
        return;
    }

    char selected[64];
    lv_dropdown_get_selected_str(wifi_network_dropdown_, selected, sizeof(selected));
    if (selected[0] == '\0' || strcmp(selected, "No networks found") == 0 || strcmp(selected, "No networks scanned yet") == 0) {
        return;
    }

    lv_textarea_set_text(wifi_ssid_textarea_, selected);
}

void UIDashboard::handle_wifi_connect_button()
{
    wifi_connect_requested_ = true;
    safe_set_text(wifi_connect_button_label_, "Connecting...");
    update_wifi_keyboard_visibility_(nullptr);
}

void UIDashboard::handle_wifi_textarea_focus(lv_obj_t *target)
{
    update_wifi_keyboard_visibility_(target);
}

void UIDashboard::handle_settings_network_tools_button()
{
    ensure_settings_overlay_();
    if (!wifi_setup_ui_initialized_) {
        ui_debug("settings | wifi tools requested");
        ensure_wifi_setup_controls_();
        safe_set_text(settings_network_tools_button_label_, "Wi-Fi Tools Ready");
        safe_set_text(settings_network_hint_label_, "Advanced Wi-Fi tools are active below. Use them only when needed.");
    }

    if (wifi_setup_editor_status_label_ != nullptr) {
        lv_obj_scroll_to_view_recursive(wifi_setup_editor_status_label_, LV_ANIM_OFF);
    }
}

void UIDashboard::handle_weather_details_button()
{
    const uint32_t now = lv_tick_get();
    weather_animation_tick_ = 0;
    weather_demo_interaction_ms_ = now;

    if (!weather_demo_mode_) {
        weather_demo_mode_ = true;
        weather_demo_index_ = 0;
        weather_demo_last_step_ms_ = now;
        apply_weather_demo_frame_();
        return;
    }

    weather_demo_index_ = static_cast<uint8_t>((weather_demo_index_ + 1U) % weather_demo_count());
    weather_demo_last_step_ms_ = now;
    apply_weather_demo_frame_();
}

void UIDashboard::handle_theme_button()
{
    dark_mode_ = !dark_mode_;
    theme_change_requested_ = true;
    apply_theme_();
    update_theme_button_label_();
}

void UIDashboard::handle_settings_button()
{
    if (settings_overlay_ == nullptr) {
        ui_debug("settings | queued deferred create");
        settings_creation_pending_ = true;
        settings_hide_pending_ = false;
        settings_visible_ = true;
        return;
    }

    if (settings_visible_) {
        ui_debug("settings | queued deferred hide");
        settings_hide_pending_ = true;
        settings_creation_pending_ = false;
        return;
    }

    ui_debug("settings | opening");
    settings_visible_ = true;
    settings_hide_pending_ = false;
    ui_debug("settings | updating visibility show");
    update_settings_visibility_();
    ui_debug("settings | visible");
}

void UIDashboard::handle_shutdown_button()
{
    shutdown_requested_ = true;
    safe_set_text(shutdown_button_label_, "Shutting down");
}

void UIDashboard::handle_brightness_slider(lv_obj_t *target)
{
    if (target == nullptr) {
        return;
    }

    brightness_percent_ = static_cast<uint8_t>(lv_slider_get_value(target));
    brightness_requested_ = true;
    update_brightness_label_();
}

void UIDashboard::hide_settings_panel()
{
    settings_visible_ = false;
    settings_creation_pending_ = false;
    settings_hide_pending_ = false;
    update_wifi_keyboard_visibility_(nullptr);
    update_settings_visibility_();
}

void UIDashboard::hide_weather_detail_panel()
{
    weather_detail_visible_ = false;
    update_weather_detail_visibility_();
}

void UIDashboard::apply_theme_()
{
    if (screen_ == nullptr) {
        return;
    }

    const Palette &palette = palette_for(dark_mode_);
    lv_obj_set_style_bg_color(screen_, palette.screen, 0);
    lv_obj_set_style_bg_color(root_, palette.screen, 0);
    lv_obj_set_style_bg_color(header_, palette.header, 0);
    lv_obj_set_style_bg_color(content_, palette.screen, 0);
    apply_pixel_panel_style(header_, palette, dark_mode_);

    lv_obj_t *cards[] = {weather_card_, tfl_card_, news_card_, calendar_card_, settings_panel_, weather_detail_panel_};
    for (lv_obj_t *card : cards) {
        if (card != nullptr) {
            lv_obj_set_style_bg_color(card, palette.card, 0);
            lv_obj_set_style_border_color(card, palette.border, 0);
            apply_pixel_panel_style(card, palette, dark_mode_);
        }
    }
    apply_card_header_theme(weather_card_, palette);
    apply_card_header_theme(tfl_card_, palette);
    apply_card_header_theme(news_card_, palette);
    apply_card_header_theme(calendar_card_, palette);
    ui_debug("theme | card headers styled");

    for (uint8_t i = 0; i < kTflRows; ++i) {
        if (tfl_row_[i] != nullptr) {
            lv_obj_set_style_bg_color(tfl_row_[i], palette.card_alt, 0);
            lv_obj_set_style_radius(tfl_row_[i], kPixelArtEnabled ? 0 : 8, 0);
            lv_obj_set_style_border_width(tfl_row_[i], kPixelArtEnabled ? 2 : 0, 0);
            lv_obj_set_style_border_color(tfl_row_[i], palette.border, 0);
        }
    }

    for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
        if (weather_day_tile_[i] != nullptr) {
            lv_obj_set_style_bg_color(weather_day_tile_[i], palette.card_alt, 0);
            lv_obj_set_style_border_color(weather_day_tile_[i], palette.border, 0);
        }
        if (weather_day_label_[i] != nullptr) {
            apply_pixel_label_style(weather_day_label_[i], font_caption(), palette.text);
        }
        if (weather_day_temp_label_[i] != nullptr) {
            apply_pixel_label_style(weather_day_temp_label_[i], font_caption(), palette.text);
        }
        if (weather_day_rain_label_[i] != nullptr) {
            apply_pixel_label_style(weather_day_rain_label_[i], font_caption(), palette.dim);
        }
    }

    for (uint8_t i = 0; i < kCalendarRows; ++i) {
        if (calendar_event_tile_[i] != nullptr) {
            lv_obj_set_style_bg_color(calendar_event_tile_[i], palette.card_alt, 0);
            lv_obj_set_style_border_color(calendar_event_tile_[i], palette.border, 0);
            lv_obj_set_style_radius(calendar_event_tile_[i], kPixelArtEnabled ? 0 : 12, 0);
        }
        if (calendar_date_badge_[i] != nullptr) {
            lv_obj_set_style_bg_color(calendar_date_badge_[i], palette.accent, 0);
            lv_obj_set_style_border_color(calendar_date_badge_[i], palette.border, 0);
            lv_obj_set_style_radius(calendar_date_badge_[i], kPixelArtEnabled ? 0 : 10, 0);
        }
    }
    ui_debug("theme | card rows styled");

    apply_pixel_label_style(title_label_, font_brand(), palette.text);
    apply_pixel_label_style(subtitle_label_, font_body(), palette.muted);
    apply_pixel_label_style(clock_label_, font_brand(), palette.text);
    apply_pixel_label_style(day_label_, font_caption(), palette.muted);
    apply_pixel_label_style(wifi_label_, font_caption(), palette.muted);
    apply_pixel_label_style(battery_label_, font_caption(), palette.muted);
    apply_pixel_label_style(touch_label_, font_body(), palette.muted);
    apply_pixel_label_style(offline_banner_label_, font_body(), palette.text);
    apply_pixel_label_style(weather_temp_label_, font_brand(), palette.text);
    apply_pixel_label_style(weather_condition_label_, font_body(), palette.accent);
    apply_pixel_label_style(weather_detail_label_, font_body(), palette.text);
    apply_pixel_label_style(weather_change_label_, font_body(), palette.text);
    apply_pixel_label_style(weather_meta_label_, font_caption(), palette.dim);
    apply_pixel_label_style(tfl_summary_label_, font_body(), palette.text);
    apply_pixel_label_style(tfl_status_label_, font_caption(), palette.dim);
    apply_pixel_label_style(news_status_label_, font_caption(), palette.dim);
    apply_pixel_label_style(calendar_summary_label_, font_body(), palette.text);
    apply_pixel_label_style(calendar_status_label_, font_caption(), palette.dim);
    apply_pixel_label_style(settings_display_label_, font_body(), palette.text);
    apply_pixel_label_style(brightness_label_, font_body(), palette.text);
    apply_pixel_label_style(settings_system_label_, font_body(), palette.text);
    apply_pixel_label_style(settings_network_label_, font_body(), palette.text);
    apply_pixel_label_style(wifi_setup_status_label_, font_body(), palette.text);
    apply_pixel_label_style(wifi_setup_editor_status_label_, font_caption(), palette.muted);
    apply_pixel_label_style(settings_battery_label_, font_body(), palette.muted);
    apply_pixel_label_style(settings_network_saved_label_, font_caption(), palette.muted);
    apply_pixel_label_style(settings_network_hint_label_, font_caption(), palette.dim);
    apply_pixel_label_style(runtime_summary_label_, font_body(), palette.text);
    apply_pixel_label_style(runtime_detail_label_, font_caption(), palette.muted);
    apply_pixel_label_style(weather_detail_status_label_, font_caption(), palette.dim);
    apply_pixel_label_style(weather_detail_summary_label_, font_body(), palette.text);
    ui_debug("theme | labels styled");

    for (uint8_t i = 0; i < kTflRows; ++i) {
        if (tfl_line_label_[i] != nullptr) {
            apply_pixel_label_style(tfl_line_label_[i], font_body(), palette.text);
        }
        if (tfl_state_label_[i] != nullptr) {
            apply_pixel_label_style(tfl_state_label_[i], font_caption(), palette.muted);
        }
    }
    for (uint8_t i = 0; i < kNewsRows; ++i) {
        if (news_title_label_[i] != nullptr) {
            apply_pixel_label_style(news_title_label_[i], font_body(), palette.text);
        }
        if (news_summary_label_[i] != nullptr) {
            apply_pixel_label_style(news_summary_label_[i], font_caption(), palette.muted);
        }
    }
    for (uint8_t i = 0; i < kCalendarRows; ++i) {
        if (calendar_date_label_[i] != nullptr) {
            apply_pixel_label_style(calendar_date_label_[i], font_body(), dark_mode_ ? palette.screen : lv_color_white());
            lv_obj_set_style_text_align(calendar_date_label_[i], LV_TEXT_ALIGN_CENTER, 0);
        }
        if (calendar_title_label_[i] != nullptr) {
            apply_pixel_label_style(calendar_title_label_[i], font_body(), palette.text);
        }
        if (calendar_relative_label_[i] != nullptr) {
            apply_pixel_label_style(calendar_relative_label_[i], font_caption(), palette.accent);
        }
        if (calendar_detail_label_[i] != nullptr) {
            apply_pixel_label_style(calendar_detail_label_[i], font_caption(), palette.muted);
        }
    }
    for (uint8_t i = 0; i < kWeatherHourlyRows; ++i) {
        if (weather_hourly_label_[i] != nullptr) {
            apply_pixel_label_style(weather_hourly_label_[i], font_caption(), palette.text);
        }
    }
    for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
        if (weather_daily_detail_label_[i] != nullptr) {
            apply_pixel_label_style(weather_daily_detail_label_[i], font_caption(), palette.text);
        }
    }

    update_button_style(refresh_button_, palette, dark_mode_);
    update_button_style(theme_button_, palette, dark_mode_);
    update_button_style(settings_button_, palette, dark_mode_);
    update_button_style(settings_close_button_, palette, dark_mode_);
    update_button_style(shutdown_button_, palette, dark_mode_);
    update_button_style(weather_details_button_, palette, dark_mode_);
    update_button_style(weather_refresh_button_, palette, dark_mode_);
    update_button_style(tfl_refresh_button_, palette, dark_mode_);
    update_button_style(news_refresh_button_, palette, dark_mode_);
    update_button_style(calendar_refresh_button_, palette, dark_mode_);
    update_button_style(weather_detail_close_button_, palette, dark_mode_);
    ui_debug("theme | buttons styled");
    if (shutdown_button_ != nullptr) {
        lv_obj_set_style_bg_color(shutdown_button_, palette.bad, 0);
    }

    if (offline_banner_ != nullptr) {
        lv_obj_set_style_bg_color(offline_banner_, connectivity_connected_ ? palette.good : palette.warn, 0);
        lv_obj_set_style_border_color(offline_banner_, palette.border, 0);
        apply_pixel_panel_style(offline_banner_, palette, dark_mode_);
    }
    if (settings_overlay_ != nullptr) {
        lv_obj_set_style_bg_color(settings_overlay_, palette.screen, 0);
    }
    if (weather_detail_overlay_ != nullptr) {
        lv_obj_set_style_bg_color(weather_detail_overlay_, dark_mode_ ? lv_color_hex(0x050302) : lv_color_hex(0x3C5A32), 0);
    }
    if (calendar_grid_ != nullptr) {
        lv_obj_set_style_border_width(calendar_grid_, kPixelArtEnabled ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(calendar_grid_, palette.border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(calendar_grid_, palette.card_alt, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(calendar_grid_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(calendar_grid_, palette.text, LV_PART_ITEMS);
        lv_obj_set_style_text_font(calendar_grid_, font_caption(), LV_PART_ITEMS);
        lv_obj_set_style_border_width(calendar_grid_, 1, LV_PART_ITEMS);
        lv_obj_set_style_border_color(calendar_grid_, palette.border, LV_PART_ITEMS);
        lv_obj_set_style_pad_all(calendar_grid_, 6, LV_PART_ITEMS);
    }
    if (calendar_today_marker_ != nullptr) {
        lv_obj_set_style_border_color(calendar_today_marker_, palette.accent, 0);
        lv_obj_set_style_bg_opa(calendar_today_marker_, LV_OPA_TRANSP, 0);
    }
    ui_debug("theme | calendar grid styled");
    ui_debug("theme | wifi controls styled");

    apply_settings_safe_style_();
    style_wifi_setup_controls_();
    update_wifi_chip_();
    update_theme_button_label_();
    update_brightness_label_();
    weather_icon_code_ = 255;
    memset(weather_day_icon_code_, 0xFF, sizeof(weather_day_icon_code_));
    ui_debug("theme | complete");
}

void UIDashboard::style_wifi_setup_controls_()
{
    if (!wifi_setup_ui_initialized_) {
        return;
    }

    const Palette &palette = palette_for(dark_mode_);
    apply_pixel_label_style(wifi_setup_status_label_, font_body(), palette.text);
    update_button_style(wifi_scan_button_, palette, dark_mode_);
    update_button_style(wifi_use_selected_button_, palette, dark_mode_);
    update_button_style(wifi_connect_button_, palette, dark_mode_);

    lv_obj_t *inputs[] = {wifi_network_dropdown_, wifi_ssid_textarea_, wifi_password_textarea_};
    for (lv_obj_t *input : inputs) {
        if (input == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_color(input, palette.card_alt, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(input, kPixelArtEnabled ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(input, palette.border, LV_PART_MAIN);
        lv_obj_set_style_text_color(input, palette.text, LV_PART_MAIN);
        lv_obj_set_style_text_font(input, font_body(), LV_PART_MAIN);
    }

    if (wifi_keyboard_ != nullptr) {
        lv_obj_set_style_bg_color(wifi_keyboard_, palette.card_alt, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(wifi_keyboard_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(wifi_keyboard_, kPixelArtEnabled ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(wifi_keyboard_, palette.border, LV_PART_MAIN);
    }
    apply_settings_safe_style_();
    ui_debug("wifi setup | controls styled");
}

void UIDashboard::apply_settings_safe_style_()
{
    lv_obj_t *panel_objects[] = {
        settings_panel_,
        brightness_slider_,
        shutdown_button_,
        settings_close_button_,
        wifi_scan_button_,
        wifi_use_selected_button_,
        wifi_connect_button_,
        wifi_network_dropdown_,
        wifi_ssid_textarea_,
        wifi_password_textarea_,
        wifi_keyboard_};

    for (lv_obj_t *obj : panel_objects) {
        if (obj == nullptr) {
            continue;
        }
        lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_spread(obj, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_offset_x(obj, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_offset_y(obj, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_pad(obj, 0, LV_PART_MAIN);
    }
}

void UIDashboard::update_theme_button_label_()
{
    safe_set_text(theme_button_label_, dark_mode_ ? "Light" : "Dark");
}

void UIDashboard::update_brightness_label_()
{
    char text[40];
    snprintf(text, sizeof(text), "Brightness %u%%", static_cast<unsigned>(brightness_percent_));
    safe_set_text(brightness_label_, text);
}

void UIDashboard::update_settings_visibility_()
{
    if (!settings_visible_) {
        update_wifi_keyboard_visibility_(nullptr);
    }
    safe_set_hidden(header_, settings_visible_);
    safe_set_hidden(offline_banner_, settings_visible_);
    safe_set_hidden(content_, settings_visible_);
    safe_set_hidden(settings_overlay_, !settings_visible_);
    if (settings_visible_ && settings_overlay_ != nullptr) {
        lv_obj_move_foreground(settings_overlay_);
        lv_obj_invalidate(settings_overlay_);
    }
    if (header_ != nullptr) {
        lv_obj_invalidate(header_);
    }
    if (offline_banner_ != nullptr) {
        lv_obj_invalidate(offline_banner_);
    }
    if (content_ != nullptr) {
        lv_obj_invalidate(content_);
    }
    if (screen_ != nullptr) {
        lv_obj_invalidate(screen_);
    }
}

void UIDashboard::service_deferred_ui_creation_()
{
    if (settings_creation_pending_) {
        ui_debug("settings | deferred create begin");
        ensure_settings_overlay_();
        settings_creation_pending_ = false;
        update_settings_visibility_();
        ui_debug(settings_visible_ ? "settings | deferred create visible" : "settings | deferred create hidden");
    }

    if (settings_hide_pending_) {
        ui_debug("settings | deferred hide begin");
        settings_hide_pending_ = false;
        destroy_settings_overlay_();
        settings_visible_ = false;
        update_settings_visibility_();
        ui_debug("settings | deferred hide complete");
    }
}

void UIDashboard::update_weather_detail_visibility_()
{
    safe_set_hidden(weather_detail_overlay_, !weather_detail_visible_);
}

void UIDashboard::ensure_wifi_keyboard_()
{
    if (wifi_keyboard_ != nullptr || settings_overlay_ == nullptr) {
        return;
    }

    ui_debug("wifi setup | creating keyboard");
    wifi_keyboard_ = lv_keyboard_create(settings_overlay_);
    lv_obj_set_width(wifi_keyboard_, lv_pct(100));
    lv_keyboard_set_mode(wifi_keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
    safe_set_hidden(wifi_keyboard_, true);
    style_wifi_setup_controls_();
    ui_debug("wifi setup | keyboard ready");
}

void UIDashboard::update_wifi_keyboard_visibility_(lv_obj_t *target)
{
    if (target != nullptr) {
        ensure_wifi_keyboard_();
    }

    if (wifi_keyboard_ == nullptr) {
        return;
    }

    if (target == nullptr) {
        lv_keyboard_set_textarea(wifi_keyboard_, nullptr);
        safe_set_hidden(wifi_keyboard_, true);
        return;
    }

    lv_keyboard_set_textarea(wifi_keyboard_, target);
    safe_set_hidden(wifi_keyboard_, false);
}

void UIDashboard::update_wifi_chip_()
{
    if (wifi_label_ == nullptr || wifi_dot_ == nullptr) {
        return;
    }

    const Palette &palette = palette_for(dark_mode_);
    const char *label = connectivity_status_text_;
    if (!connectivity_configured_) {
        label = "Wi-Fi setup needed";
    } else if (connectivity_status_text_[0] == '\0') {
        label = connectivity_connected_ ? "Wi-Fi online" : "Wi-Fi waiting";
    }
    safe_set_text(wifi_label_, label);
    lv_obj_set_style_bg_color(wifi_dot_, connectivity_connected_ ? palette.good : (connectivity_configured_ ? palette.warn : palette.bad), 0);
}

void UIDashboard::init_weather_icon_grid_(lv_obj_t *target, lv_obj_t **cells, lv_coord_t cell_size)
{
    if (target == nullptr || cells == nullptr || cell_size <= 0) {
        return;
    }

    lv_obj_set_size(target, cell_size * 9, cell_size * 9);
    lv_obj_set_style_bg_opa(target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(target, 0, 0);
    lv_obj_set_style_pad_all(target, 0, 0);

    for (uint8_t i = 0; i < kWeatherIconPixels; ++i) {
        lv_obj_t *block = create_clean_obj(target);
        if (block == nullptr) {
            cells[i] = nullptr;
            continue;
        }

        const uint8_t x = i % 9U;
        const uint8_t y = i / 9U;
        cells[i] = block;
        lv_obj_set_size(block, cell_size, cell_size);
        lv_obj_set_pos(block, x * cell_size, y * cell_size);
        lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(block, 0, 0);
        lv_obj_set_style_border_width(block, kPixelArtEnabled && cell_size >= 6 ? 1 : 0, 0);
        safe_set_hidden(block, true);
    }
}

void UIDashboard::render_weather_icon_(lv_obj_t *target, uint8_t weather_code, bool is_day, lv_coord_t cell_size)
{
    if (target == nullptr || cell_size <= 0) {
        ui_debug("weather icon render | skipped due to invalid target or cell size");
        return;
    }

    char icon_log[224];
    snprintf(
        icon_log,
        sizeof(icon_log),
        "weather icon render | target=%p code=%u day=%d cell=%d",
        static_cast<void *>(target),
        static_cast<unsigned>(weather_code),
        is_day ? 1 : 0,
        static_cast<int>(cell_size)
    );
    ui_debug(icon_log);

    lv_obj_t **cells = nullptr;
    if (target == weather_icon_) {
        cells = weather_icon_pixel_;
    } else {
        for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
            if (target == weather_day_icon_[i]) {
                cells = weather_day_icon_pixel_[i];
                break;
            }
        }
    }

    if (cells == nullptr) {
        ui_debug("weather icon render | no cell array matched target");
        return;
    }
    if (cells[0] == nullptr) {
        ui_debug("weather icon render | grid missing; skipping render");
        return;
    }

    const Palette &palette = palette_for(dark_mode_);
    const char *const *pattern = icon_pattern(icon_kind_from_code(weather_code, is_day));
    for (uint8_t y = 0; y < 9; ++y) {
        for (uint8_t x = 0; x < 9; ++x) {
            const uint8_t index = static_cast<uint8_t>((y * 9U) + x);
            lv_obj_t *block = cells[index];
            if (block == nullptr) {
                continue;
            }

            const char pixel = pattern[y][x];
            if (pixel == '.') {
                safe_set_hidden(block, true);
                continue;
            }

            safe_set_hidden(block, false);
            lv_obj_set_style_bg_color(block, icon_pixel_color(pixel, palette, dark_mode_), 0);
            lv_obj_set_style_border_color(block, pixel_shadow_color(dark_mode_), 0);
        }
    }
    ui_debug("weather icon render | complete");
}

void UIDashboard::update_weather_daily_tiles_(const WeatherData &data)
{
    char tiles_log[160];
    snprintf(
        tiles_log,
        sizeof(tiles_log),
        "weather daily tiles | valid=%d count=%u",
        data.valid ? 1 : 0,
        static_cast<unsigned>(data.daily_count)
    );
    ui_debug(tiles_log);

    for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
        const bool show = data.valid && i < data.daily_count;
        safe_set_hidden(weather_day_tile_[i], !show);
        if (!show) {
            continue;
        }

        safe_set_text(weather_day_label_[i], data.daily_labels[i]);

        char temp_text[24];
        snprintf(
            temp_text,
            sizeof(temp_text),
            "%d/%d C",
            static_cast<int>(data.daily_temp_min_c[i]),
            static_cast<int>(data.daily_temp_max_c[i])
        );
        safe_set_text(weather_day_temp_label_[i], temp_text);

        char rain_text[16];
        snprintf(rain_text, sizeof(rain_text), "%u%% rain", static_cast<unsigned>(data.daily_precipitation_peak[i]));
        safe_set_text(weather_day_rain_label_[i], rain_text);

        if (!(kEnableDynamicWeatherArt && kEnableDynamicWeatherDailyArt)) {
            safe_set_hidden(weather_day_icon_[i], true);
            continue;
        }

        safe_set_hidden(weather_day_icon_[i], false);
        if (weather_day_icon_code_[i] != data.daily_weather_codes[i]) {
            snprintf(
                tiles_log,
                sizeof(tiles_log),
                "weather daily tiles | row=%u icon old=%u new=%u",
                static_cast<unsigned>(i),
                static_cast<unsigned>(weather_day_icon_code_[i]),
                static_cast<unsigned>(data.daily_weather_codes[i])
            );
            ui_debug(tiles_log);
            render_weather_icon_(weather_day_icon_[i], data.daily_weather_codes[i], true, 4);
            weather_day_icon_code_[i] = data.daily_weather_codes[i];
        }
    }
    ui_debug("weather daily tiles | complete");
}

void UIDashboard::update_weather_daily_tiles_(const brief::WeatherPayload &weather)
{
    for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
        const bool show = i < weather.daily_count;
        safe_set_hidden(weather_day_tile_[i], !show);
        if (!show) {
            continue;
        }

        safe_set_text(weather_day_label_[i], weather.daily[i].label);

        char min_text[12];
        char max_text[12];
        char temp_text[28];
        format_whole_tenths(min_text, sizeof(min_text), weather.daily[i].min_tenths_c);
        format_whole_tenths(max_text, sizeof(max_text), weather.daily[i].max_tenths_c);
        snprintf(temp_text, sizeof(temp_text), "%s/%s C", min_text, max_text);
        safe_set_text(weather_day_temp_label_[i], temp_text);

        char rain_text[16];
        snprintf(rain_text, sizeof(rain_text), "%u%% rain", static_cast<unsigned>(weather.daily[i].rain_probability_percent));
        safe_set_text(weather_day_rain_label_[i], rain_text);

        if (!(kEnableDynamicWeatherArt && kEnableDynamicWeatherDailyArt)) {
            safe_set_hidden(weather_day_icon_[i], true);
            continue;
        }

        safe_set_hidden(weather_day_icon_[i], false);
        if (weather_day_icon_code_[i] != weather.daily[i].weather_code) {
            render_weather_icon_(weather_day_icon_[i], weather.daily[i].weather_code, true, 4);
            weather_day_icon_code_[i] = weather.daily[i].weather_code;
        }
    }
}

void UIDashboard::update_weather_detail_page_(const WeatherData &data)
{
    if (weather_detail_status_label_ == nullptr || weather_detail_summary_label_ == nullptr) {
        return;
    }

    if (!data.valid) {
        safe_set_text(weather_detail_status_label_, data.status[0] != '\0' ? data.status : "Waiting");
        safe_set_text(weather_detail_summary_label_, data.error[0] != '\0' ? data.error : "Weather detail will appear after the first successful forecast.");
        for (uint8_t i = 0; i < kWeatherHourlyRows; ++i) {
            safe_set_text(weather_hourly_label_[i], i == 0 ? "Hourly outlook waiting" : "");
        }
        for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
            safe_set_text(weather_daily_detail_label_[i], i == 0 ? "Daily outlook waiting" : "");
        }
        return;
    }

    char status[160];
    snprintf(
        status,
        sizeof(status),
        "%s  |  Updated %s  |  Humidity %u%%  |  Wind peak %u km/h",
        data.status,
        data.updated,
        static_cast<unsigned>(data.humidity_percent),
        static_cast<unsigned>(data.wind_peak_kmh)
    );
    safe_set_text(weather_detail_status_label_, status);

    char summary[320];
    snprintf(summary, sizeof(summary), "%s\n%s", data.detail, data.next_changes);
    safe_set_text(weather_detail_summary_label_, summary);

    for (uint8_t i = 0; i < kWeatherHourlyRows; ++i) {
        if (i >= data.hourly_count) {
            safe_set_text(weather_hourly_label_[i], "");
            continue;
        }
        char line[160];
        snprintf(
            line,
            sizeof(line),
            "%s  |  %d C  |  %u%% rain  |  %u%% humidity  |  %u km/h",
            data.hourly_labels[i],
            static_cast<int>(data.hourly_temperatures[i]),
            static_cast<unsigned>(data.hourly_precipitation[i]),
            static_cast<unsigned>(data.hourly_humidity[i]),
            static_cast<unsigned>(data.hourly_wind_kmh[i])
        );
        safe_set_text(weather_hourly_label_[i], line);
    }

    for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
        if (i >= data.daily_count) {
            safe_set_text(weather_daily_detail_label_[i], "");
            continue;
        }
        char line[192];
        snprintf(
            line,
            sizeof(line),
            "%s  |  %s  |  %d/%d C  |  %u%% rain  |  %u km/h",
            data.daily_labels[i],
            data.daily_conditions[i],
            static_cast<int>(data.daily_temp_min_c[i]),
            static_cast<int>(data.daily_temp_max_c[i]),
            static_cast<unsigned>(data.daily_precipitation_peak[i]),
            static_cast<unsigned>(data.daily_wind_peak_kmh[i])
        );
        safe_set_text(weather_daily_detail_label_[i], line);
    }
}

void UIDashboard::update_weather_detail_page_(const brief::WeatherPayload &weather)
{
    if (weather_detail_status_label_ == nullptr || weather_detail_summary_label_ == nullptr) {
        return;
    }

    safe_set_text(weather_detail_status_label_, weather.summary_text[0] != '\0' ? weather.summary_text : "Weather detail waiting");
    safe_set_text(weather_detail_summary_label_, weather.next_change_text[0] != '\0' ? weather.next_change_text : "Next change waiting");

    for (uint8_t i = 0; i < kWeatherHourlyRows; ++i) {
        if (i >= weather.hourly_count) {
            safe_set_text(weather_hourly_label_[i], "");
            continue;
        }
        char line[160];
        char temp_text[24];
        format_tenths(temp_text, sizeof(temp_text), weather.hourly[i].temperature_tenths_c, " C");
        snprintf(
            line,
            sizeof(line),
            "%s  |  %s  |  %u%% rain  |  %u%% humidity  |  %.0f km/h",
            weather.hourly[i].hhmm,
            temp_text,
            static_cast<unsigned>(weather.hourly[i].rain_probability_percent),
            static_cast<unsigned>(weather.hourly[i].humidity_percent),
            weather.hourly[i].wind_kmh_tenths / 10.0f
        );
        safe_set_text(weather_hourly_label_[i], line);
    }

    for (uint8_t i = 0; i < kWeatherDailyRows; ++i) {
        if (i >= weather.daily_count) {
            safe_set_text(weather_daily_detail_label_[i], "");
            continue;
        }
        char line[192];
        char min_text[16];
        char max_text[16];
        format_tenths(min_text, sizeof(min_text), weather.daily[i].min_tenths_c, " C");
        format_tenths(max_text, sizeof(max_text), weather.daily[i].max_tenths_c, " C");
        snprintf(
            line,
            sizeof(line),
            "%s  |  %s / %s  |  %u%% rain",
            weather.daily[i].label,
            min_text,
            max_text,
            static_cast<unsigned>(weather.daily[i].rain_probability_percent)
        );
        safe_set_text(weather_daily_detail_label_[i], line);
    }
}

lv_color_t UIDashboard::state_color_(const char *status) const
{
    const Palette &palette = palette_for(dark_mode_);
    if (status == nullptr) {
        return palette.muted;
    }
    if (strstr(status, "Live") != nullptr || strstr(status, "Good") != nullptr || strstr(status, "Healthy") != nullptr) {
        return palette.good;
    }
    if (strstr(status, "Loading") != nullptr || strstr(status, "Refresh") != nullptr || strstr(status, "Wait") != nullptr) {
        return palette.warn;
    }
    if (strstr(status, "Offline") != nullptr || strstr(status, "Error") != nullptr || strstr(status, "Stale") != nullptr || strstr(status, "Low") != nullptr) {
        return palette.bad;
    }
    return palette.muted;
}
