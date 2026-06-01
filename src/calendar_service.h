#ifndef CALENDAR_SERVICE_H
#define CALENDAR_SERVICE_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

constexpr uint8_t kCalendarVisibleEvents = 4;

struct CalendarEntry {
    bool available;
    char title[48];
    char iso_date[16];
    char date_text[24];
    char relative_text[24];
    char notes[64];
};

struct CalendarData {
    bool valid;
    bool loading;
    bool stale;
    uint8_t event_count;
    char region[32];
    char summary[96];
    char updated[24];
    char status[32];
    char error[96];
    CalendarEntry events[kCalendarVisibleEvents];
};

class CalendarService {
public:
    CalendarService();

    void init();
    void update();
    void request_refresh();
    void restore_cached(const CalendarData &cached);
    void snapshot(CalendarData *out) const;
    uint32_t revision() const;
    bool due(uint32_t now_ms) const;

private:
    bool fetch_(CalendarData *out);
    void set_loading_state_();
    void apply_success_(const CalendarData &fresh_data);
    void apply_failure_(const char *message);

    mutable SemaphoreHandle_t mutex_;
    volatile bool refresh_requested_;
    uint32_t next_update_ms_;
    uint32_t revision_;
    CalendarData data_;
};

#endif
