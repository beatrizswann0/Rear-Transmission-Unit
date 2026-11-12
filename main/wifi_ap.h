#pragma once

#include "esp_err.h"

#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID       "Mangue-RTU"
#endif
#ifndef WIFI_AP_PASS
#define WIFI_AP_PASS       "manguebaja"
#endif
#ifndef WIFI_AP_CHANNEL
#define WIFI_AP_CHANNEL    1
#endif

esp_err_t wifi_ap_start(void);
