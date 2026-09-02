#ifndef GSL3680_TOUCH_H
#define GSL3680_TOUCH_H

#include <stdint.h>

struct gsl3680_touch_debug {
    bool controller_ready;
    bool last_read_ok;
    bool last_touch_found;
    int8_t int_level;
    uint8_t point_count;
    uint16_t x;
    uint16_t y;
    char last_error[64];
};

class gsl3680_touch
{
public:
    gsl3680_touch(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin = -1, int8_t int_pin = -1);

    bool begin();
    bool getTouch(uint16_t *x, uint16_t *y);
    void getDebugSnapshot(gsl3680_touch_debug *debug) const;
    void set_rotation(uint8_t r);

private:
    int8_t _sda;
    int8_t _scl;
    int8_t _rst;
    int8_t _int;
};

#endif
