#include "news_service.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "service_support.h"

namespace {

constexpr uint32_t kNewsRefreshMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kNewsRetryMs = 60UL * 1000UL;
constexpr size_t kNewsMaxPayloadBytes = 64UL * 1024UL;
constexpr char kServiceTag[] = "news";

String collapse_whitespace(const String &input)
{
    String output;
    output.reserve(input.length());

    bool previous_was_space = false;
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        const bool is_space = (c == ' ' || c == '\n' || c == '\r' || c == '\t');
        if (is_space) {
            if (!previous_was_space) {
                output += ' ';
            }
            previous_was_space = true;
        } else {
            output += c;
            previous_was_space = false;
        }
    }

    output.trim();
    return output;
}

String decode_html_entities(String text)
{
    text.replace("&amp;", "&");
    text.replace("&quot;", "\"");
    text.replace("&#39;", "'");
    text.replace("&apos;", "'");
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&#x27;", "'");
    return text;
}

String strip_markup(const String &input)
{
    String output;
    output.reserve(input.length());

    bool inside_tag = false;
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        if (c == '<') {
            inside_tag = true;
            continue;
        }
        if (c == '>') {
            inside_tag = false;
            output += ' ';
            continue;
        }
        if (!inside_tag) {
            output += c;
        }
    }

    return collapse_whitespace(decode_html_entities(output));
}

char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }

    return c;
}

bool string_matches_ignore_case(const String &text, int offset, const char *needle)
{
    if (needle == nullptr || offset < 0) {
        return false;
    }

    const size_t needle_len = strlen(needle);
    if (needle_len == 0 || static_cast<size_t>(offset) + needle_len > text.length()) {
        return false;
    }

    for (size_t i = 0; i < needle_len; ++i) {
        if (ascii_lower(text[static_cast<size_t>(offset) + i]) != ascii_lower(needle[i])) {
            return false;
        }
    }

    return true;
}

int index_of_ignore_case(const String &text, const char *needle, int from)
{
    if (needle == nullptr || from < 0) {
        return -1;
    }

    const size_t needle_len = strlen(needle);
    if (needle_len == 0 || text.length() < needle_len) {
        return -1;
    }

    const size_t start = static_cast<size_t>(from);
    if (start >= text.length()) {
        return -1;
    }

    const size_t last = text.length() - needle_len;
    for (size_t i = start; i <= last; ++i) {
        if (string_matches_ignore_case(text, static_cast<int>(i), needle)) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

String extract_tag_value(const String &block, const char *tag)
{
    const String open_tag = String("<") + tag;
    const String close_tag = String("</") + tag + ">";
    const int open_start = index_of_ignore_case(block, open_tag.c_str(), 0);
    if (open_start < 0) {
        return "";
    }

    const int content_start = block.indexOf('>', open_start);
    if (content_start < 0) {
        return "";
    }

    const int close_start = index_of_ignore_case(block, close_tag.c_str(), content_start + 1);
    if (close_start < 0) {
        return "";
    }

    String value = block.substring(content_start + 1, close_start);
    value.replace("<![CDATA[", "");
    value.replace("]]>", "");
    return strip_markup(value);
}

bool extract_item_block(const String &payload, int *search_from, String *block_out)
{
    if (search_from == nullptr || block_out == nullptr) {
        return false;
    }

    const int item_start = index_of_ignore_case(payload, "<item", *search_from);
    const int entry_start = index_of_ignore_case(payload, "<entry", *search_from);

    int start = -1;
    String close_tag;
    if (item_start >= 0 && (entry_start < 0 || item_start < entry_start)) {
        start = item_start;
        close_tag = "</item>";
    } else if (entry_start >= 0) {
        start = entry_start;
        close_tag = "</entry>";
    }

    if (start < 0) {
        return false;
    }

    const int end = index_of_ignore_case(payload, close_tag.c_str(), start);
    if (end < 0) {
        return false;
    }

    *block_out = payload.substring(start, end);
    *search_from = end + close_tag.length();
    return true;
}

}  // namespace

NewsService::NewsService()
    : mutex_(nullptr),
      refresh_requested_(false),
      next_update_ms_(0),
      revision_(0),
      data_{} {}

void NewsService::init()
{
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }

    NewsData initial = {};
    initial.valid = false;
    initial.loading = false;
    initial.stale = false;
    initial.headline_count = 0;
    service_copy_text(initial.updated, sizeof(initial.updated), "--:--");
    service_copy_text(initial.status, sizeof(initial.status), "Waiting");
    service_copy_text(initial.error, sizeof(initial.error), "Waiting for Wi-Fi");

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_ = initial;
        ++revision_;
        xSemaphoreGive(mutex_);
    } else {
        data_ = initial;
        ++revision_;
    }

    next_update_ms_ = 0;
    refresh_requested_ = true;
}

void NewsService::request_refresh()
{
    refresh_requested_ = true;
    set_loading_state_();
}

void NewsService::restore_cached(const NewsData &cached)
{
    if (!cached.valid) {
        return;
    }

    NewsData restored = cached;
    restored.loading = false;
    restored.stale = true;
    service_copy_text(restored.status, sizeof(restored.status), "Cached");
    if (restored.error[0] == '\0') {
        service_copy_text(restored.error, sizeof(restored.error), "Restored from cache");
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_ = restored;
        ++revision_;
        xSemaphoreGive(mutex_);
    } else {
        data_ = restored;
        ++revision_;
    }

    refresh_requested_ = true;
    next_update_ms_ = 0;
}

void NewsService::snapshot(NewsData *out) const
{
    if (out == nullptr) {
        return;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        *out = data_;
        xSemaphoreGive(mutex_);
        return;
    }

    *out = data_;
}

uint32_t NewsService::revision() const
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        const uint32_t value = revision_;
        xSemaphoreGive(mutex_);
        return value;
    }

    return revision_;
}

bool NewsService::due(uint32_t now_ms) const
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        const bool value = refresh_requested_ || service_time_reached(now_ms, next_update_ms_);
        xSemaphoreGive(mutex_);
        return value;
    }

    return refresh_requested_ || service_time_reached(now_ms, next_update_ms_);
}

void NewsService::update()
{
    const uint32_t now = millis();
    if (!refresh_requested_ && !service_time_reached(now, next_update_ms_)) {
        return;
    }

    refresh_requested_ = false;
    set_loading_state_();

    if (!service_network_ready()) {
        apply_failure_("Waiting for Wi-Fi");
        next_update_ms_ = now + kNewsRetryMs;
        return;
    }

    NewsData fresh = {};
    if (fetch_(&fresh)) {
        apply_success_(fresh);
        next_update_ms_ = now + kNewsRefreshMs;
    } else {
        next_update_ms_ = now + kNewsRetryMs;
    }
}

bool NewsService::fetch_(NewsData *out)
{
    if (out == nullptr) {
        apply_failure_("News output missing");
        return false;
    }

    WiFiClientSecure client;
    if (!service_configure_tls_client(client, LONDONBRIEF_TLS_ROOT_CA, LONDONBRIEF_ALLOW_INSECURE_TLS != 0)) {
        apply_failure_("TLS root CA missing");
        return false;
    }

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);

    service_log_fetch_start(kServiceTag, "BBC RSS");
    if (!http.begin(client, LONDONBRIEF_NEWS_RSS_URL)) {
        service_report_network_fault(kServiceTag, -1001);
        service_log_parse_failure(kServiceTag, "HTTP begin failed");
        apply_failure_("News connection failed");
        return false;
    }
    http.addHeader("Accept", "application/rss+xml, application/xml, text/xml");
    http.addHeader("Accept-Encoding", "identity");

    const int http_code = http.GET();
    const int payload_size = http.getSize();
    service_log_http_result(kServiceTag, http_code, payload_size);
    if (http_code != HTTP_CODE_OK) {
        if (http_code < 0) {
            service_report_network_fault(kServiceTag, http_code);
        }
        String body;
        size_t body_bytes = 0;
        service_read_bounded_string(http.getStream(), 512U, payload_size, &body, &body_bytes);
        http.end();
        service_log_http_failure(kServiceTag, http_code, body);
        apply_failure_("News request failed");
        return false;
    }

    if (payload_size > 0 && static_cast<size_t>(payload_size) > kNewsMaxPayloadBytes) {
        http.end();
        apply_failure_("News payload too large");
        return false;
    }

    String payload;
    size_t payload_bytes = 0;
    if (!service_read_bounded_string(http.getStream(), kNewsMaxPayloadBytes, payload_size, &payload, &payload_bytes)) {
        http.end();
        service_log_payload_result(kServiceTag, payload_bytes);
        apply_failure_("News payload too large");
        return false;
    }
    http.end();
    service_log_payload_result(kServiceTag, payload_bytes);

    if (payload.length() == 0) {
        apply_failure_("News payload empty");
        return false;
    }

    NewsData fresh = {};
    fresh.valid = true;
    fresh.loading = false;
    fresh.stale = false;
    service_copy_text(fresh.status, sizeof(fresh.status), "Live");

    int search_from = 0;
    while (fresh.headline_count < 3) {
        String item_block;
        if (!extract_item_block(payload, &search_from, &item_block)) {
            break;
        }

        const String title = extract_tag_value(item_block, "title");
        String description = extract_tag_value(item_block, "description");
        if (description.length() == 0) {
            description = extract_tag_value(item_block, "summary");
        }
        const String link = extract_tag_value(item_block, "link");

        if (title.length() == 0) {
            continue;
        }

        const uint8_t index = fresh.headline_count;
        service_copy_text(fresh.headlines[index], sizeof(fresh.headlines[index]), title.c_str());
        service_copy_text(
            fresh.summaries[index],
            sizeof(fresh.summaries[index]),
            description.length() > 0 ? description.c_str() : "Tap for headline detail"
        );
        service_copy_text(fresh.links[index], sizeof(fresh.links[index]), link.c_str());
        fresh.headline_count++;
    }

    if (fresh.headline_count == 0) {
        apply_failure_("No headlines available");
        return false;
    }

    service_set_updated_now(fresh.updated, sizeof(fresh.updated));
    fresh.error[0] = '\0';
    *out = fresh;
    char success_text[64];
    snprintf(success_text, sizeof(success_text), "headlines=%u", static_cast<unsigned>(fresh.headline_count));
    service_log_success(kServiceTag, success_text);
    return true;
}

void NewsService::set_loading_state_()
{
    if (mutex_ == nullptr) {
        data_.loading = true;
        service_copy_text(data_.status, sizeof(data_.status), data_.valid ? "Refreshing" : "Loading");
        ++revision_;
        return;
    }

    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return;
    }

    data_.loading = true;
    service_copy_text(data_.status, sizeof(data_.status), data_.valid ? "Refreshing" : "Loading");
    ++revision_;
    xSemaphoreGive(mutex_);
}

void NewsService::apply_success_(const NewsData &fresh_data)
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_ = fresh_data;
        ++revision_;
        xSemaphoreGive(mutex_);
        return;
    }

    data_ = fresh_data;
    ++revision_;
}

void NewsService::apply_failure_(const char *message)
{
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        data_.loading = false;
        service_copy_text(data_.error, sizeof(data_.error), message);

        if (data_.valid) {
            data_.stale = true;
            service_copy_text(data_.status, sizeof(data_.status), "Stale");
        } else {
            data_.stale = false;
            data_.headline_count = 0;
            service_copy_text(data_.updated, sizeof(data_.updated), "--:--");
            service_copy_text(data_.status, sizeof(data_.status), "Offline");
        }

        ++revision_;
        xSemaphoreGive(mutex_);
        return;
    }

    service_copy_text(data_.error, sizeof(data_.error), message);
    data_.loading = false;
    ++revision_;
}
