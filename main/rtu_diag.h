#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "can_mgmt.h"

typedef enum {
    RTU_QF_VALID = 0,
    RTU_QF_DEGRADED,
    RTU_QF_TEMP,     /* temporário — não limp */
    RTU_QF_FAULTED
} rtu_qf_t;

typedef enum {
    RTU_DTC_CLEAR = 0,
    RTU_DTC_PENDING,
    RTU_DTC_CONFIRMED
} rtu_dtc_st_t;

typedef enum {
    RTU_CH_FCU = 0,
    RTU_CH_RCU,
    RTU_CH_SPEED,
    RTU_CH_CVT,
    RTU_CH_RPM,
    RTU_CH_ENG,
    RTU_CH_VOLT,
    RTU_CH_IMU,
    RTU_CH_GPS,
    RTU_CH_COUNT
} rtu_ch_t;

void rtu_diag_init(void);

/* Chamado ~50 Hz a partir de can_svc. twai_state = can_bus_stats.state. */
void rtu_diag_tick(const car_state_t *car, int twai_state, uint32_t now_ms);

bool rtu_diag_fcu_down_confirmed(void);
bool rtu_diag_rcu_down_confirmed(void);
bool rtu_diag_any_limp(void);

rtu_qf_t rtu_diag_qf(rtu_ch_t ch);
rtu_dtc_st_t rtu_diag_dtc(rtu_ch_t ch);
const char *rtu_diag_ch_name(rtu_ch_t ch);

/* JSON compacto para SoftAP /api/diag. Retorna bytes escritos (sem NUL final obrigatório no len). */
int rtu_diag_json(char *buf, size_t cap);
