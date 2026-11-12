#ifndef A7670_MQTT_H
#define A7670_MQTT_H

#include <stdbool.h>
#include "telemetry_packets.h"

/* Stub: bring-up AT/MQTT entra depois. UART1 = PIN_A7670_* (sem GPS). */

void a7670_init(void);
bool a7670_ready(void);
bool a7670_publish_telemetry(const telemetry_packet_t *pkt);

#endif
