# GSL3680 I2C touch controller driver (JC8012P4A1C_I_W_Y touch panel).
idf_component_register(
    SRCS
        "esp_lcd_gsl3680.c"
        "esp_lcd_touch.c"
        "gsl_point_id.c"
        "gsl3680_touch.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        arduino
    PRIV_REQUIRES
        driver
        esp_timer
        esp_lcd
        esp_mm
        heap
)
