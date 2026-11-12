#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "telemetry_packets.h"

#define TELEM_FRAME_MARK_LEN 4
#define TELEM_FRAME_LEN      (TELEM_FRAME_MARK_LEN + sizeof(telemetry_packet_t))

void telem_store_init(void);
void telem_store_update(const telemetry_packet_t *pkt);
bool telem_store_has_packet(void);
bool telem_store_copy(telemetry_packet_t *out);
bool telem_store_copy_frame(uint8_t out[TELEM_FRAME_LEN]);
uint32_t telem_store_seq(void);
uint32_t telem_store_age_ms(void);
