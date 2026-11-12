#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include "driver/twai.h"
#pragma GCC diagnostic pop

typedef struct {
    twai_message_t msg;
    uint16_t ack_flag;
    uint8_t ack_soc;
    bool is_box;
    bool is_coming_ack;
    bool is_led;
    bool led_on;
    bool handled;
} cmd_result_t;

/* Interpreta opcode + payload do protocolo binário (rtu_cmd_protocol.h). */
bool cmd_parse_opcode(uint8_t opcode, const uint8_t *payload, uint8_t plen,
                      cmd_result_t *result);

#endif
