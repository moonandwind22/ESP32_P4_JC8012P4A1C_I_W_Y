#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "Arduino.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_gsl3680.h"
#include "gsl3680_touch.h"

#define CONFIG_LCD_HRES 800
#define CONFIG_LCD_VRES 1280

static const char *TAG = "gsl3680_touch";
static constexpr uint8_t kTouchInitAttempts = 3;
static constexpr uint32_t kTouchInitRetryDelayMs = 250;
static bool first_touch_read_logged = false;

static esp_lcd_touch_handle_t tp = NULL;
static esp_lcd_panel_io_handle_t tp_io_handle = NULL;
static i2c_master_bus_handle_t tp_i2c_bus = NULL;
static uint16_t touch_strength[1];
static uint8_t touch_cnt = 0;
static uint32_t last_touch_error_log_ms = 0;
static gsl3680_touch_debug touch_debug = {
    false,
    false,
    false,
    -1,
    0,
    0,
    0,
    "Not started",
};

static bool log_touch_error(esp_err_t err, const char *operation)
{
    if (err == ESP_OK) {
        return true;
    }

    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(err));
    snprintf(
        touch_debug.last_error,
        sizeof(touch_debug.last_error),
        "%s: %s",
        operation,
        esp_err_to_name(err)
    );
    return false;
}

static void cleanup_touch_stack(bool drop_bus)
{
    if (tp != NULL) {
        esp_lcd_touch_del(tp);
        tp = NULL;
    }

    if (tp_io_handle != NULL) {
        esp_lcd_panel_io_del(tp_io_handle);
        tp_io_handle = NULL;
    }

    if (drop_bus && tp_i2c_bus != NULL) {
        i2c_del_master_bus(tp_i2c_bus);
        tp_i2c_bus = NULL;
    }
}

gsl3680_touch::gsl3680_touch(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin)
{
    _sda = sda_pin;
    _scl = scl_pin;
    _rst = rst_pin;
    _int = int_pin;
}

bool gsl3680_touch::begin()
{
    cleanup_touch_stack(true);
    first_touch_read_logged = false;

    touch_debug.controller_ready = false;
    touch_debug.last_read_ok = false;
    touch_debug.last_touch_found = false;
    touch_debug.point_count = 0;
    touch_debug.x = 0;
    touch_debug.y = 0;
    snprintf(touch_debug.last_error, sizeof(touch_debug.last_error), "%s", "Init pending");

    for (uint8_t attempt = 1; attempt <= kTouchInitAttempts; ++attempt) {
        ESP_LOGI(TAG, "Touch init attempt %u/%u", static_cast<unsigned>(attempt), static_cast<unsigned>(kTouchInitAttempts));
        Serial.printf("[touch] attempt %u/%u: begin\n", static_cast<unsigned>(attempt), static_cast<unsigned>(kTouchInitAttempts));

        i2c_master_bus_config_t i2c_bus_conf = {};
        i2c_bus_conf.i2c_port = I2C_NUM_1;
        i2c_bus_conf.sda_io_num = static_cast<gpio_num_t>(_sda);
        i2c_bus_conf.scl_io_num = static_cast<gpio_num_t>(_scl);
        i2c_bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
        i2c_bus_conf.glitch_ignore_cnt = 7;
        i2c_bus_conf.flags.enable_internal_pullup = true;

        Serial.println("[touch] stage: bus creation");
        if (!log_touch_error(i2c_new_master_bus(&i2c_bus_conf, &tp_i2c_bus), "i2c_new_master_bus")) {
            Serial.println("[touch] stage failed: bus creation");
            cleanup_touch_stack(true);
            if (attempt < kTouchInitAttempts) {
                delay(kTouchInitRetryDelayMs);
                continue;
            }
            return false;
        }
        Serial.println("[touch] stage ok: bus creation");

        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
        tp_io_config.scl_speed_hz = 100000;
        ESP_LOGI(TAG, "Initializing GSL3680 touch IO");
        Serial.println("[touch] stage: panel IO creation");
        if (!log_touch_error(
                esp_lcd_new_panel_io_i2c(tp_i2c_bus, &tp_io_config, &tp_io_handle),
                "esp_lcd_new_panel_io_i2c"
            )) {
            Serial.println("[touch] stage failed: panel IO creation");
            cleanup_touch_stack(true);
            if (attempt < kTouchInitAttempts) {
                delay(kTouchInitRetryDelayMs);
                continue;
            }
            return false;
        }
        Serial.println("[touch] stage ok: panel IO creation");

        const esp_lcd_touch_config_t tp_cfg = {
            .x_max = CONFIG_LCD_HRES,
            .y_max = CONFIG_LCD_VRES,
            .rst_gpio_num = static_cast<gpio_num_t>(_rst),
            .int_gpio_num = static_cast<gpio_num_t>(_int),
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 1,
            },
        };

        ESP_LOGI(TAG, "Initializing GSL3680 controller");
        Serial.println("[touch] stage: controller init");
        if (!log_touch_error(esp_lcd_touch_new_i2c_gsl3680(tp_io_handle, &tp_cfg, &tp), "esp_lcd_touch_new_i2c_gsl3680")) {
            Serial.println("[touch] stage failed: controller init");
            cleanup_touch_stack(true);
            if (attempt < kTouchInitAttempts) {
                delay(kTouchInitRetryDelayMs);
                continue;
            }
            return false;
        }
        Serial.println("[touch] stage ok: controller init");

        touch_debug.controller_ready = true;
        snprintf(touch_debug.last_error, sizeof(touch_debug.last_error), "%s", "OK");
        Serial.println("[touch] init complete");
        return true;
    }

    return false;
}

bool gsl3680_touch::getTouch(uint16_t *x, uint16_t *y)
{
    if (tp == NULL) {
        touch_debug.controller_ready = false;
        touch_debug.last_read_ok = false;
        touch_debug.last_touch_found = false;
        touch_debug.point_count = 0;
        touch_debug.int_level = -1;
        snprintf(touch_debug.last_error, sizeof(touch_debug.last_error), "%s", "No handle");
        return false;
    }

    touch_debug.int_level = _int >= 0 ? static_cast<int8_t>(gpio_get_level(static_cast<gpio_num_t>(_int))) : -1;

    const esp_err_t err = esp_lcd_touch_read_data(tp);
    if (err != ESP_OK) {
        touch_debug.last_read_ok = false;
        touch_debug.last_touch_found = false;
        touch_debug.point_count = 0;
        const uint32_t now = millis();
        if (now - last_touch_error_log_ms >= 5000) {
            last_touch_error_log_ms = now;
            ESP_LOGE(TAG, "esp_lcd_touch_read_data failed: %s", esp_err_to_name(err));
        }
        snprintf(touch_debug.last_error, sizeof(touch_debug.last_error), "%s", esp_err_to_name(err));
        return false;
    }

    if (!first_touch_read_logged) {
        first_touch_read_logged = true;
        Serial.println("[touch] first touch read succeeded");
    }

    touch_debug.last_read_ok = true;
    touch_debug.last_touch_found = esp_lcd_touch_get_coordinates(tp, x, y, touch_strength, &touch_cnt, 1);
    touch_debug.point_count = touch_cnt;
    if (touch_debug.last_touch_found) {
        touch_debug.x = x != nullptr ? *x : 0;
        touch_debug.y = y != nullptr ? *y : 0;
        snprintf(touch_debug.last_error, sizeof(touch_debug.last_error), "%s", "Touch data");
    } else {
        touch_debug.x = 0;
        touch_debug.y = 0;
        snprintf(touch_debug.last_error, sizeof(touch_debug.last_error), "%s", "No touch points");
    }

    return touch_debug.last_touch_found;
}

void gsl3680_touch::getDebugSnapshot(gsl3680_touch_debug *debug) const
{
    if (debug == nullptr) {
        return;
    }

    *debug = touch_debug;
}

void gsl3680_touch::set_rotation(uint8_t r)
{
    if (tp == NULL) {
        return;
    }

    switch (r) {
        case 0:
            esp_lcd_touch_set_swap_xy(tp, false);
            esp_lcd_touch_set_mirror_x(tp, false);
            esp_lcd_touch_set_mirror_y(tp, false);
            break;
        case 1:
            esp_lcd_touch_set_swap_xy(tp, false);
            esp_lcd_touch_set_mirror_x(tp, false);
            esp_lcd_touch_set_mirror_y(tp, true);
            break;
        case 2:
            esp_lcd_touch_set_swap_xy(tp, false);
            esp_lcd_touch_set_mirror_x(tp, false);
            esp_lcd_touch_set_mirror_y(tp, false);
            break;
        case 3:
            esp_lcd_touch_set_swap_xy(tp, false);
            esp_lcd_touch_set_mirror_x(tp, true);
            esp_lcd_touch_set_mirror_y(tp, true);
            break;
        default:
            break;
    }
}
