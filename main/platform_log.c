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
