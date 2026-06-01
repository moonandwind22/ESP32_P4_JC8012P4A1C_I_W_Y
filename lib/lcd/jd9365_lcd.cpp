#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "Arduino.h"

#include "esp_lcd_jd9365.h"
#include "jd9365_lcd.h"

#define LCD_H_RES 800
#define LCD_V_RES 1280

#define MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB565)
#define LCD_BIT_PER_PIXEL (16)

#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN 3
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT GPIO_NUM_23
constexpr uint32_t kBacklightPwmFreqHz = 12000;
constexpr uint8_t kBacklightPwmResolution = 10;
constexpr uint32_t kBacklightMaxDuty = (1U << kBacklightPwmResolution) - 1U;
constexpr uint32_t kBacklightMinReadableDuty = kBacklightMaxDuty / 24U;

static const char *TAG = "jd9365_lcd";
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static bool backlight_pwm_ready = false;

static bool log_if_error(esp_err_t err, const char *operation)
{
    if (err == ESP_OK) {
        return true;
    }

    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(err));
    return false;
}

jd9365_lcd::jd9365_lcd(int8_t lcd_rst)
{
    _lcd_rst = lcd_rst;
}

void jd9365_lcd::example_bsp_enable_dsi_phy_power()
{
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN
    const esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    if (log_if_error(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy), "esp_ldo_acquire_channel")) {
        ESP_LOGI(TAG, "MIPI DSI PHY powered on");
    }
#endif
    (void)ldo_mipi_phy;
}

void jd9365_lcd::example_bsp_init_lcd_backlight()
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    if (!ledcAttach(EXAMPLE_PIN_NUM_BK_LIGHT, kBacklightPwmFreqHz, kBacklightPwmResolution)) {
        ESP_LOGW(TAG, "LEDC attach failed, falling back to GPIO backlight control");
        const gpio_config_t bk_gpio_config = {
            .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT,
            .mode = GPIO_MODE_OUTPUT,
        };
        log_if_error(gpio_config(&bk_gpio_config), "gpio_config backlight");
        backlight_pwm_ready = false;
        return;
    }

    backlight_pwm_ready = true;
    ledcWrite(EXAMPLE_PIN_NUM_BK_LIGHT, 0);
#endif
}

void jd9365_lcd::example_bsp_set_lcd_backlight(uint32_t level)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    if (backlight_pwm_ready) {
        const uint32_t duty = level ? kBacklightMaxDuty : 0;
        if (!ledcWrite(EXAMPLE_PIN_NUM_BK_LIGHT, duty)) {
            ESP_LOGW(TAG, "LEDC write failed, switching to GPIO backlight control");
            backlight_pwm_ready = false;
            log_if_error(gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, level), "gpio_set_level backlight");
        }
        return;
    }

    log_if_error(gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, level), "gpio_set_level backlight");
#endif
}

void jd9365_lcd::set_backlight_percent(uint8_t percent)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    if (percent > 100) {
        percent = 100;
    }

    backlight_percent_ = percent;

    if (!backlight_pwm_ready) {
        example_bsp_set_lcd_backlight(percent > 0 ? EXAMPLE_LCD_BK_LIGHT_ON_LEVEL : EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);
        esp_rom_printf("[lcd-rom] backlight percent=%u gpio=1\n", static_cast<unsigned>(percent));
        return;
    }

    uint32_t duty = 0;
    if (percent > 0) {
        duty = kBacklightMinReadableDuty +
               ((kBacklightMaxDuty - kBacklightMinReadableDuty) * static_cast<uint32_t>(percent)) / 100U;
    }

    if (!ledcWrite(EXAMPLE_PIN_NUM_BK_LIGHT, duty)) {
        ESP_LOGE(TAG, "Failed to set backlight duty");
    } else {
        esp_rom_printf(
            "[lcd-rom] backlight percent=%u duty=%lu pwm=1\n",
            static_cast<unsigned>(percent),
            static_cast<unsigned long>(duty)
        );
    }
#else
    backlight_percent_ = percent;
#endif
}

uint8_t jd9365_lcd::get_backlight_percent() const
{
    return backlight_percent_;
}

bool jd9365_lcd::begin()
{
    ESP_LOGW(TAG, "stage=lcd begin start");
    esp_rom_printf("[lcd-rom] stage=begin start\n");
    example_bsp_enable_dsi_phy_power();
    ESP_LOGW(TAG, "stage=dsi phy power ready");
    esp_rom_printf("[lcd-rom] stage=dsi phy power ready\n");
    example_bsp_init_lcd_backlight();
    ESP_LOGW(TAG, "stage=backlight init ready");
    esp_rom_printf("[lcd-rom] stage=backlight init ready\n");
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    const esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_LOGW(TAG, "stage=new dsi bus");
    esp_rom_printf("[lcd-rom] stage=new dsi bus\n");
    if (!log_if_error(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), "esp_lcd_new_dsi_bus")) {
        return false;
    }

    ESP_LOGI(TAG, "Installing JD9365 panel IO");
    ESP_LOGW(TAG, "stage=panel io dbi");
    esp_rom_printf("[lcd-rom] stage=panel io dbi\n");
    const esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    if (!log_if_error(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_handle), "esp_lcd_new_panel_io_dbi")) {
        return false;
    }

    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(MIPI_DPI_PX_FORMAT);
    jd9365_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = _lcd_rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    ESP_LOGW(TAG, "stage=new panel jd9365");
    esp_rom_printf("[lcd-rom] stage=new panel jd9365\n");
    if (!log_if_error(esp_lcd_new_panel_jd9365(io_handle, &panel_config, &panel_handle), "esp_lcd_new_panel_jd9365")) {
        return false;
    }
    ESP_LOGW(TAG, "stage=panel reset");
    esp_rom_printf("[lcd-rom] stage=panel reset\n");
    if (!log_if_error(esp_lcd_panel_reset(panel_handle), "esp_lcd_panel_reset")) {
        return false;
    }
    ESP_LOGW(TAG, "stage=panel init");
    esp_rom_printf("[lcd-rom] stage=panel init\n");
    if (!log_if_error(esp_lcd_panel_init(panel_handle), "esp_lcd_panel_init")) {
        return false;
    }
    ESP_LOGW(TAG, "stage=panel mirror");
    esp_rom_printf("[lcd-rom] stage=panel mirror\n");
    if (!log_if_error(esp_lcd_panel_mirror(panel_handle, true, true), "esp_lcd_panel_mirror")) {
        return false;
    }

    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
    backlight_percent_ = 100;
    ESP_LOGW(TAG, "stage=lcd begin complete");
    esp_rom_printf("[lcd-rom] stage=begin complete\n");
    return true;
}

bool jd9365_lcd::lcd_draw_bitmap(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint8_t *color_data)
{
    return log_if_error(
        esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data),
        "esp_lcd_panel_draw_bitmap"
    );
}

void jd9365_lcd::draw16bitbergbbitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *color_data)
{
    const uint16_t x_start = x;
    const uint16_t y_start = y;
    const uint16_t x_end = w + x;
    const uint16_t y_end = h + y;

    log_if_error(
        esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data),
        "esp_lcd_panel_draw_bitmap"
    );
}

void jd9365_lcd::fillScreen(uint16_t color)
{
    const size_t pixel_count = LCD_H_RES * LCD_V_RES;
    uint16_t *color_data = static_cast<uint16_t *>(heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!color_data) {
        color_data = static_cast<uint16_t *>(heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!color_data) {
        ESP_LOGE(TAG, "Failed to allocate fill buffer");
        return;
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        color_data[i] = color;
    }

    draw16bitbergbbitmap(0, 0, LCD_H_RES, LCD_V_RES, color_data);
    free(color_data);
}

void jd9365_lcd::te_on()
{
    static const uint8_t tearing_param[] = {0x00};
    log_if_error(esp_lcd_panel_io_tx_param(io_handle, 0x35, tearing_param, sizeof(tearing_param)), "te_on");
}

void jd9365_lcd::te_off()
{
    log_if_error(esp_lcd_panel_io_tx_param(io_handle, 0x34, NULL, 0), "te_off");
}

uint16_t jd9365_lcd::width()
{
    return LCD_H_RES;
}

uint16_t jd9365_lcd::height()
{
    return LCD_V_RES;
}

bool jd9365_lcd::register_color_trans_done_callback(esp_lcd_dpi_panel_color_trans_done_cb_t callback, void *user_ctx)
{
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_color_trans_done = callback,
        .on_refresh_done = nullptr,
    };
    return log_if_error(
        esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &callbacks, user_ctx),
        "esp_lcd_dpi_panel_register_event_callbacks"
    );
}
