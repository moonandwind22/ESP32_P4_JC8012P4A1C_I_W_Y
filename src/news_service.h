#ifndef NEWS_SERVICE_H
#define NEWS_SERVICE_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

struct NewsData {
    bool valid;
    bool loading;
    bool stale;
    uint8_t headline_count;
    char updated[24];
    char status[32];
    char error[96];
    char headlines[3][144];
    char summaries[3][256];
    char links[3][192];
};

class NewsService {
public:
    NewsService();

    void init();
    void update();
    void request_refresh();
    void restore_cached(const NewsData &cached);
    void snapshot(NewsData *out) const;
    uint32_t revision() const;
    bool due(uint32_t now_ms) const;

private:
    bool fetch_(NewsData *out);
    void set_loading_state_();
    void apply_success_(const NewsData &fresh_data);
    void apply_failure_(const char *message);

    mutable SemaphoreHandle_t mutex_;
    volatile bool refresh_requested_;
    uint32_t next_update_ms_;
    uint32_t revision_;
    NewsData data_;
};

#endif
