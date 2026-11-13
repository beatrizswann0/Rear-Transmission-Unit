#include "lora_rate.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "LORA_RATE";

/* Números fechados: a UI e o rádio só aceitam estes. */
static const uint32_t k_opts[] = { 500u, 1000u, 2000u, 5000u, 10000u };

static volatile uint32_t s_period_ms = LORA_RATE_DEFAULT_MS;
static volatile uint32_t s_gap_ms;
static volatile uint32_t s_last_tx_ms;
static SemaphoreHandle_t s_kick;

static bool allowed(uint32_t ms)
{
    for (size_t i = 0; i < sizeof(k_opts) / sizeof(k_opts[0]); i++) {
        if (k_opts[i] == ms) {
            return true;
        }
    }
    return false;
}

static void persist(uint32_t ms)
{
    nvs_handle_t h;
    if (nvs_open("rtu", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_u32(h, "lora_ms", ms);
    (void)nvs_commit(h);
    nvs_close(h);
}

void lora_rate_init(void)
{
    nvs_handle_t h;
    if (nvs_open("rtu", NVS_READONLY, &h) == ESP_OK) {
        uint32_t ms = 0;
        if (nvs_get_u32(h, "lora_ms", &ms) == ESP_OK && allowed(ms)) {
            s_period_ms = ms;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "periodo=%lums", (unsigned long)s_period_ms);
}

void lora_rate_bind_kick(SemaphoreHandle_t kick)
{
    s_kick = kick;
}

uint32_t lora_rate_ms(void)
{
    uint32_t ms = s_period_ms;
    return allowed(ms) ? ms : LORA_RATE_DEFAULT_MS;
}

bool lora_rate_set_ms(uint32_t ms)
{
    if (!allowed(ms)) {
        return false;
    }
    if (s_period_ms == ms) {
        return true;
    }
    s_period_ms = ms;
    persist(ms);
    ESP_LOGI(TAG, "periodo → %lums", (unsigned long)ms);
    if (s_kick != NULL) {
        (void)xSemaphoreGive(s_kick);
    }
    return true;
}

void lora_rate_note_tx(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000LL);
    uint32_t prev = s_last_tx_ms;
    if (prev != 0 && now > prev) {
        s_gap_ms = now - prev;
    }
    s_last_tx_ms = now;
}

uint32_t lora_rate_gap_ms(void)
{
    return s_gap_ms;
}

int lora_rate_json(char *buf, size_t cap)
{
    if (buf == NULL || cap < 32) {
        return 0;
    }
    int n = snprintf(buf, cap, "{\"ms\":%lu,\"gap_ms\":%lu,\"opts\":[",
                     (unsigned long)lora_rate_ms(),
                     (unsigned long)lora_rate_gap_ms());
    if (n < 0 || (size_t)n >= cap) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(k_opts) / sizeof(k_opts[0]); i++) {
        int m = snprintf(buf + n, cap - (size_t)n, "%s%lu",
                         i ? "," : "", (unsigned long)k_opts[i]);
        if (m < 0 || (size_t)n + (size_t)m >= cap) {
            return 0;
        }
        n += m;
    }
    int m = snprintf(buf + n, cap - (size_t)n, "]}");
    if (m < 0 || (size_t)n + (size_t)m >= cap) {
        return 0;
    }
    return n + m;
}
