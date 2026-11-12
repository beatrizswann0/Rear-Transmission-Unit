#include "a7670_mqtt.h"
#include "board_pins.h"
#include "esp_log.h"

static const char *TAG = "A7670";

void a7670_init(void)
{
    ESP_LOGI(TAG, "stub — UART1 TX=%d RX=%d (EN=NC). Implementar AT/MQTT depois.",
             (int)PIN_A7670_TX, (int)PIN_A7670_RX);
    /* GPIO34–39 são input-only no ESP32-WROOM-32. */
    if ((int)PIN_A7670_TX >= 34) {
        ESP_LOGW(TAG,
                 "connections.md: A7670 RXD em GPIO%d — ESP não pode TX nesse pino; "
                 "HW precisa remapar (GPIO18=SD CS — usar outro, ex. 25/32/33)",
                 (int)PIN_A7670_TX);
    }
}

bool a7670_ready(void)
{
    return false;
}

bool a7670_publish_telemetry(const telemetry_packet_t *pkt)
{
    (void)pkt;
    return false;
}
