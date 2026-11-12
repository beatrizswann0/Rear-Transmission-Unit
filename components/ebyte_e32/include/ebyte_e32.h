#ifndef EBYTE_E32_H
#define EBYTE_E32_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "board_pins.h"

#define E32_UART_NUM PIN_E32_UART_NUM

void e32_init(void);
bool e32_aux_ready(void);
/* Telemetria: marker AA BB CC DD + payload. */
bool e32_send_struct(void *data, size_t size);
/* Bytes crus (ex.: frame de comando A5 5A… da estação). */
bool e32_send_raw(const void *data, size_t size);

#endif
