#ifndef A7670_MQTT_H
#define A7670_MQTT_H

#include <stdbool.h>
#include "telemetry_packets.h"

/*
 * A7670SA em UART1 (PIN_A7670_TX/RX) com 3.8EN em PIN_A7670_EN.
 * Telemetria sai em JSON (mesmos nomes de campo do /api/json). LoRa/SD
 * continuam no pacote binário de 52 B.
 *
 * Sem APN e URL o modem sobe (EN + AT), mas não há sessão MQTT.
 * Override na compilação: -DA7670_APN=\"...\" -DA7670_MQTT_URL=\"tcp://host:1883\"
 */


void a7670_init(void);
bool a7670_ready(void);
bool a7670_publish_telemetry(const telemetry_packet_t *pkt);

#endif
