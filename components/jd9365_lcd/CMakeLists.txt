# JD9365 MIPI-DSI LCD driver (JC8012P4A1C_I_W_Y panel, 800x1280).
idf_component_register(
    SRCS
        "esp_lcd_jd9365.c"
        "jd9365_lcd.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        arduino
        esp_lcd
    PRIV_REQUIRES
        driver
        esp_timer
        esp_hw_support
        esp_mm
        heap
)
