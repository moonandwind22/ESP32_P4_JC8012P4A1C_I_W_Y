#ifndef JD9365_LCD_H
#define JD9365_LCD_H

#include <stdint.h>
#include "esp_lcd_mipi_dsi.h"

class jd9365_lcd
{
public:
    explicit jd9365_lcd(int8_t lcd_rst);

    bool begin();
    void example_bsp_enable_dsi_phy_power();
    void example_bsp_init_lcd_backlight();
    void example_bsp_set_lcd_backlight(uint32_t level);
    void set_backlight_percent(uint8_t percent);
    uint8_t get_backlight_percent() const;
    bool lcd_draw_bitmap(uint16_t x_start, uint16_t y_start,
                         uint16_t x_end, uint16_t y_end, uint8_t *color_data);
    void draw16bitbergbbitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *color_data);
    void fillScreen(uint16_t color);
    void te_on();
    void te_off();
    uint16_t width();
    uint16_t height();
    bool register_color_trans_done_callback(esp_lcd_dpi_panel_color_trans_done_cb_t callback, void *user_ctx);

private:
    int8_t _lcd_rst;
    uint8_t backlight_percent_ = 100;
};

#endif
