#ifndef SYSTEM_FSM_H
#define SYSTEM_FSM_H

#include <stdbool.h>
#include "telemetry_packets.h"

typedef enum {
    FSM_BOOT = ECU_STATE_BOOT,
    FSM_SELF_CHECK = ECU_STATE_SELF_CHECK,
    FSM_RUNNING = ECU_STATE_RUNNING,
    FSM_ERROR = ECU_STATE_ERROR,
} fsm_state_t;

void fsm_init(void);
fsm_state_t fsm_state(void);
bool fsm_is_running(void);
bool fsm_limp(void);

/* Chamado periodicamente (ex.: can_svc). Atualiza política limp/error. */
void fsm_tick(bool can_ok, bool e32_ok, bool sd_ok,
              bool fcu_down, bool rcu_down);

#endif
