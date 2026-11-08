#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "app.h"

static platform_event_t s_events[PRISMA_EVENT_LOG_CAPACITY];
static size_t s_count;
static size_t s_head;
static uint32_t s_total;
static SemaphoreHandle_t s_mutex;
/* Runtime-only log filter. This covers application events, not hardware
 * panic records or unconditional ESP-IDF console output. */
static platform_log_mode_t s_mode = PLATFORM_LOG_INFO;
static const char * const s_mode_names[] = {"OFF","ERROR","INFO","DEBUG"};
static uint32_t s_filtered;

platform_log_mode_t platform_log_mode_get(void)
{
    return s_mode;
}

const char *platform_log_mode_name(void)
{
    return s_mode_names[s_mode];
}

uint32_t platform_log_filtered_count(void)
{
    return s_filtered;
}

esp_err_t platform_log_mode_set(platform_log_mode_t mode)
{
    if (mode < PLATFORM_LOG_OFF || mode > PLATFORM_LOG_DEBUG)
        return ESP_ERR_INVALID_ARG;
    if (!s_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    s_mode = mode;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

static bool retained_at_mode(const char *level, platform_log_mode_t mode)
{
    if (mode == PLATFORM_LOG_OFF) return false;
    if (mode == PLATFORM_LOG_DEBUG) return true;
    if (level && (!strcmp(level,"ERROR") || !strcmp(level,"FATAL"))) return true;
    if (mode == PLATFORM_LOG_ERROR) return false;
    return !level || strcmp(level,"DEBUG") != 0;
}


esp_err_t platform_log_start(void)
{
    if (s_mutex) return ESP_OK;
    s_mutex = xSemaphoreCreateMutex();
    return s_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

void platform_log_event(const char *level, const char *subsystem, const char *message)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(25)) != pdTRUE) return;
    if (!retained_at_mode(level, s_mode)) {
        ++s_filtered;
        xSemaphoreGive(s_mutex);
        return;
    }

    platform_event_t *event = &s_events[s_head];
    memset(event, 0, sizeof(*event));
    event->sequence = ++s_total;
    event->uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
    strlcpy(event->level, level ? level : "INFO", sizeof(event->level));
    strlcpy(event->subsystem, subsystem ? subsystem : "system", sizeof(event->subsystem));
    strlcpy(event->message, message ? message : "", sizeof(event->message));

    s_head = (s_head + 1) % PRISMA_EVENT_LOG_CAPACITY;
    if (s_count < PRISMA_EVENT_LOG_CAPACITY) ++s_count;
    xSemaphoreGive(s_mutex);
}

size_t platform_log_copy(platform_event_t *out, size_t max_events)
{
    if (!out || max_events == 0 || !s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;

    size_t n = s_count < max_events ? s_count : max_events;
    size_t oldest = (s_head + PRISMA_EVENT_LOG_CAPACITY - s_count) % PRISMA_EVENT_LOG_CAPACITY;
    size_t skip = s_count - n;
    oldest = (oldest + skip) % PRISMA_EVENT_LOG_CAPACITY;
    for (size_t i = 0; i < n; ++i) {
        out[i] = s_events[(oldest + i) % PRISMA_EVENT_LOG_CAPACITY];
    }
    xSemaphoreGive(s_mutex);
    return n;
}

uint32_t platform_log_total_events(void)
{
    return s_total;
}
