#ifndef TFL_SERVICE_H
#define TFL_SERVICE_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

constexpr uint8_t kTflVisibleLines = 24;

struct TflLineStatus {
    bool available;
    bool disrupted;
    uint8_t severity;
    char name[32];
    char status[48];
};

struct TflData {
    bool valid;
    bool loading;
    bool stale;
    char summary[64];
    char updated[24];
    char status[32];
    char error[96];
    uint8_t line_count;
    TflLineStatus lines[kTflVisibleLines];
};

class TflService {
public:
    TflService();

    void init();
    void update();
    void request_refresh();
    void restore_cached(const TflData &cached);
    void snapshot(TflData *out) const;
    uint32_t revision() const;
    bool due(uint32_t now_ms) const;

private:
    bool fetch_(TflData *out);
    void set_loading_state_();
    void apply_success_(const TflData &fresh_data);
    void apply_failure_(const char *message);

    mutable SemaphoreHandle_t mutex_;
    volatile bool refresh_requested_;
    uint32_t next_update_ms_;
    uint32_t revision_;
    TflData data_;
};

#endif
