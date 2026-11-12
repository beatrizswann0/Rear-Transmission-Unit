#ifndef TELEMETRY_AGG_H
#define TELEMETRY_AGG_H

#include "can_mgmt.h"
#include "telemetry_packets.h"
#include <stdbool.h>

void telemetry_fill(telemetry_packet_t *pkt, const car_state_t *car, bool led_debug);

#endif
