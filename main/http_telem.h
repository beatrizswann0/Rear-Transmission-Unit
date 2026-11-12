#pragma once

#include "esp_err.h"

/* Callback: comando ASCII (kill/start/…) → CAN. */
typedef bool (*http_cmd_fn)(const char *cmd);

esp_err_t http_telem_start(http_cmd_fn cmd_cb);
