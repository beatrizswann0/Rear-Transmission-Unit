#include "telem_store.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static SemaphoreHandle_t s_mu;
static telemetry_packet_t s_pkt;
static bool s_valid;
static int64_t s_updated_us;

void telem_store_init(void)
{
    s_mu = xSemaphoreCreateMutex();
    s_valid = false;
    s_updated_us = 0;
}

void telem_store_update(const telemetry_packet_t *pkt)
{
    if (pkt == NULL || s_mu == NULL) {
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_pkt = *pkt;
    s_valid = true;
    s_updated_us = esp_timer_get_time();
    xSemaphoreGive(s_mu);
}

bool telem_store_has_packet(void)
{
    return s_valid;
}

bool telem_store_copy(telemetry_packet_t *out)
{
    if (out == NULL || s_mu == NULL) {
        return false;
    }
    bool ok = false;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (s_valid) {
        *out = s_pkt;
        ok = true;
    }
    xSemaphoreGive(s_mu);
    return ok;
}

bool telem_store_copy_frame(uint8_t out[TELEM_FRAME_LEN])
{
    if (out == NULL) {
        return false;
    }
    telemetry_packet_t pkt;
    if (!telem_store_copy(&pkt)) {
        return false;
    }
    out[0] = 0xAA;
    out[1] = 0xBB;
    out[2] = 0xCC;
    out[3] = 0xDD;
    memcpy(out + TELEM_FRAME_MARK_LEN, &pkt, sizeof(pkt));
    return true;
}

uint32_t telem_store_seq(void)
{
    telemetry_packet_t pkt;
    if (!telem_store_copy(&pkt)) {
        return 0;
    }
    return pkt.seq;
}

uint32_t telem_store_age_ms(void)
{
    if (!s_valid || s_mu == NULL) {
        return UINT32_MAX;
    }
    int64_t now = esp_timer_get_time();
    int64_t age = now - s_updated_us;
    if (age < 0) {
        return 0;
    }
    return (uint32_t)(age / 1000);
}
