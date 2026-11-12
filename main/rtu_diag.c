#include "rtu_diag.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"

static const char *TAG = "RTU_DIAG";

#define LOC_MISS_MATURE   2
#define LOC_HEAL_DEMATURE 2
#define STARTUP_DELAY_MS  2000
#define BUSOFF_SILENCE_MS 150
#define AGE_FCU_MS        LINK_DOWN_TIMEOUT_MS
#define AGE_RCU_MS        LINK_DOWN_TIMEOUT_MS
#define AGE_SENSOR_MS     SENSOR_ALIVE_MS
#define AGE_GPS_MS        5000

typedef struct {
    uint8_t miss;
    uint8_t hit;
    rtu_dtc_st_t dtc;
    rtu_qf_t qf;
    uint32_t last_seen_ms;
    uint32_t age_limit_ms;
} ch_t;

static ch_t g_ch[RTU_CH_COUNT];
static uint32_t g_boot_ms;
static uint32_t g_busoff_until_ms;
static uint32_t g_last_tick_ms;
static uint32_t g_tick_n;
static bool g_inited;

static const char *const k_names[RTU_CH_COUNT] = {
    "FCU", "RCU", "SPEED", "CVT", "RPM", "ENG", "VOLT", "IMU", "GPS"
};

const char *rtu_diag_ch_name(rtu_ch_t ch)
{
    if (ch < 0 || ch >= RTU_CH_COUNT) {
        return "?";
    }
    return k_names[ch];
}

void rtu_diag_init(void)
{
    memset(g_ch, 0, sizeof(g_ch));
    g_ch[RTU_CH_FCU].age_limit_ms = AGE_FCU_MS;
    g_ch[RTU_CH_RCU].age_limit_ms = AGE_RCU_MS;
    g_ch[RTU_CH_SPEED].age_limit_ms = AGE_SENSOR_MS;
    g_ch[RTU_CH_CVT].age_limit_ms = AGE_SENSOR_MS;
    g_ch[RTU_CH_RPM].age_limit_ms = AGE_SENSOR_MS;
    g_ch[RTU_CH_ENG].age_limit_ms = AGE_SENSOR_MS;
    g_ch[RTU_CH_VOLT].age_limit_ms = AGE_SENSOR_MS;
    g_ch[RTU_CH_IMU].age_limit_ms = AGE_SENSOR_MS;
    g_ch[RTU_CH_GPS].age_limit_ms = AGE_GPS_MS;
    g_boot_ms = 0;
    g_busoff_until_ms = 0;
    g_tick_n = 0;
    g_inited = true;
    ESP_LOGI(TAG, "LOC mature=%d heal=%d startup=%dms",
             LOC_MISS_MATURE, LOC_HEAL_DEMATURE, STARTUP_DELAY_MS);
}

static void report_fail(ch_t *c)
{
    c->hit = 0;
    if (c->miss < 255) {
        c->miss++;
    }
    if (c->miss >= LOC_MISS_MATURE) {
        if (c->dtc == RTU_DTC_CLEAR) {
            c->dtc = RTU_DTC_PENDING;
        } else if (c->dtc == RTU_DTC_PENDING) {
            c->dtc = RTU_DTC_CONFIRMED;
        }
        c->qf = RTU_QF_FAULTED;
    } else if (c->dtc == RTU_DTC_CLEAR) {
        c->qf = RTU_QF_DEGRADED;
    }
}

static void report_pass(ch_t *c)
{
    c->miss = 0;
    if (c->hit < 255) {
        c->hit++;
    }
    if (c->dtc != RTU_DTC_CLEAR && c->hit >= LOC_HEAL_DEMATURE) {
        c->dtc = RTU_DTC_CLEAR;
        c->qf = RTU_QF_VALID;
        c->hit = 0;
    } else if (c->dtc == RTU_DTC_CLEAR) {
        c->qf = RTU_QF_VALID;
    }
}

static uint32_t age_of(uint32_t last_ms, uint32_t now)
{
    if (last_ms == 0) {
        return UINT32_MAX;
    }
    return now - last_ms;
}

void rtu_diag_tick(const car_state_t *car, int twai_state, uint32_t now_ms)
{
    if (!g_inited || car == NULL) {
        return;
    }
    if (g_boot_ms == 0) {
        g_boot_ms = now_ms;
    }
    g_tick_n++;
    g_last_tick_ms = now_ms;

    /* TWAI bus-off → silenciar LOC por um curto período após recovery. */
    if (twai_state == 2 /* TWAI_STATE_BUS_OFF */) {
        g_busoff_until_ms = now_ms + BUSOFF_SILENCE_MS;
    }
    bool silence = (now_ms < g_busoff_until_ms) ||
                   ((now_ms - g_boot_ms) < STARTUP_DELAY_MS);

    const uint32_t lasts[RTU_CH_COUNT] = {
        car->last_fcu_ms,
        car->last_rcu_ms,
        car->last_speed_ms,
        car->last_cvt_ms,
        car->last_rpm_ms,
        car->last_eng_ms,
        car->last_volt_ms,
        car->last_imu_ms,
        car->last_gps_ms,
    };

    for (int i = 0; i < RTU_CH_COUNT; i++) {
        ch_t *c = &g_ch[i];
        uint32_t last = lasts[i];

        c->last_seen_ms = last;
        uint32_t age = age_of(last, now_ms);
        bool present = (age <= c->age_limit_ms);

        if (silence) {
            /* Startup / pós bus-off: não madura falha; QF temp se ausente. */
            if (!present && c->dtc == RTU_DTC_CLEAR) {
                c->qf = RTU_QF_TEMP;
            }
            continue;
        }

        if (present) {
            report_pass(c);
        } else {
            report_fail(c);
        }
    }
}

bool rtu_diag_fcu_down_confirmed(void)
{
    return g_ch[RTU_CH_FCU].dtc == RTU_DTC_CONFIRMED;
}

bool rtu_diag_rcu_down_confirmed(void)
{
    return g_ch[RTU_CH_RCU].dtc == RTU_DTC_CONFIRMED;
}

bool rtu_diag_any_limp(void)
{
    return rtu_diag_fcu_down_confirmed() || rtu_diag_rcu_down_confirmed();
}

rtu_qf_t rtu_diag_qf(rtu_ch_t ch)
{
    if (ch < 0 || ch >= RTU_CH_COUNT) {
        return RTU_QF_FAULTED;
    }
    return g_ch[ch].qf;
}

rtu_dtc_st_t rtu_diag_dtc(rtu_ch_t ch)
{
    if (ch < 0 || ch >= RTU_CH_COUNT) {
        return RTU_DTC_CLEAR;
    }
    return g_ch[ch].dtc;
}

static const char *qf_str(rtu_qf_t q)
{
    switch (q) {
    case RTU_QF_VALID: return "valid";
    case RTU_QF_DEGRADED: return "degraded";
    case RTU_QF_TEMP: return "temp";
    default: return "faulted";
    }
}

static const char *dtc_str(rtu_dtc_st_t s)
{
    switch (s) {
    case RTU_DTC_PENDING: return "pending";
    case RTU_DTC_CONFIRMED: return "confirmed";
    default: return "clear";
    }
}

int rtu_diag_json(char *buf, size_t cap)
{
    if (buf == NULL || cap < 32) {
        return 0;
    }
    int n = 0;
    const bool gate = (g_last_tick_ms < g_busoff_until_ms) ||
                      ((g_boot_ms != 0) && ((g_last_tick_ms - g_boot_ms) < STARTUP_DELAY_MS));
    int w = snprintf(buf + n, cap - (size_t)n,
                     "{\"tick\":%lu,\"limp\":%s,\"busoff_gate\":%s,\"ch\":[",
                     (unsigned long)g_tick_n,
                     rtu_diag_any_limp() ? "true" : "false",
                     gate ? "true" : "false");
    if (w < 0) {
        return 0;
    }
    n += w;

    for (int i = 0; i < RTU_CH_COUNT; i++) {
        const ch_t *c = &g_ch[i];
        w = snprintf(buf + n, cap - (size_t)n,
                     "%s{\"id\":\"%s\",\"dtc\":\"%s\",\"qf\":\"%s\","
                     "\"miss\":%u,\"hit\":%u,\"last_ms\":%lu}",
                     (i == 0) ? "" : ",",
                     k_names[i], dtc_str(c->dtc), qf_str(c->qf),
                     (unsigned)c->miss, (unsigned)c->hit,
                     (unsigned long)c->last_seen_ms);
        if (w < 0 || (size_t)(n + w) >= cap) {
            break;
        }
        n += w;
    }

    w = snprintf(buf + n, cap - (size_t)n, "]}");
    if (w > 0 && (size_t)(n + w) < cap) {
        n += w;
    }
    return n;
}
