#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t uptime_ms;
    uint32_t heap_free;
    uint32_t heap_min;
    int reset_reason;   /* esp_reset_reason_t */
    uint32_t boot_count;
    bool sd_ok;
    bool e32_aux;
    bool limp;
    int fsm;
    uint32_t lora_tx_ok;
    uint32_t lora_tx_fail;
    uint32_t can_rx;
    char sd_path[32];
} rtu_health_t;

void rtu_health_init(void);
void rtu_health_note_lora(bool ok);
void rtu_health_snapshot(rtu_health_t *out);

/* JSON compacto → SoftAP /api/health. */
int rtu_health_json(char *buf, size_t cap);
