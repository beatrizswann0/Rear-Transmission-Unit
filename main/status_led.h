#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "system_fsm.h"
#include <stdbool.h>

/*
 * Bancada sem PC — dois LEDs (não usa limp FCU/RCU):
 *   GPIO2  (DEBUG LED)    → CAN  (TWAI up)
 *   GPIO25 (DEBUG LED SD) → LoRa (E32 AUX ready)
 *
 * RUNNING:
 *   heartbeat ~2 s  = link OK
 *   estrobo ~100 ms = link FAIL
 * BOOT = ambos acesos; SELF_CHECK = ambos pisca lento; ERROR = CAN estrobo.
 */
void status_led_init(void);

/* Chamar ~a cada 20 ms (ex.: can_svc). */
void status_led_tick(fsm_state_t state, bool can_ok, bool e32_ok);

#endif
