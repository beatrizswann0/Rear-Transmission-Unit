#include "cmd_parser.h"
#include "rtu_cmd_protocol.h"
#include "can_ids.h"
#include "headlight_cmd.h"
#include "telemetry_packets.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CMD";

bool cmd_parse_opcode(uint8_t opcode, const uint8_t *payload, uint8_t plen,
                      cmd_result_t *result)
{
    if (result == NULL) {
        return false;
    }
    if (plen != rtu_cmd_expected_plen(opcode)) {
        return false;
    }
    if (plen > 0 && payload == NULL) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->msg.data_length_code = 1;

    switch ((rtu_cmd_op_t)opcode) {
    case RTU_CMD_LIGHT_BLINK: {
        uint16_t n = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
        uint16_t ms = (uint16_t)payload[2] | ((uint16_t)payload[3] << 8);
        if (n < 1u) {
            return false;
        }
        if (ms < LIGHT_BLINK_MS_MIN || ms > LIGHT_BLINK_MS_MAX) {
            return false;
        }
        uint8_t buf[HEADLIGHT_BLINK_DLC];
        headlight_pack_blink(buf, n, ms);
        result->msg.identifier = HEADLIGHTS_CONTROL;
        result->msg.data_length_code = HEADLIGHT_BLINK_DLC;
        memcpy(result->msg.data, buf, HEADLIGHT_BLINK_DLC);
        result->ack_flag = FLAG_ACK_LIGHT_ON;
        result->handled = true;
        ESP_LOGI(TAG, "op LIGHT_BLINK n=%u ms=%u", (unsigned)n, (unsigned)ms);
        return true;
    }
    case RTU_CMD_COMING_ACK:
        result->is_coming_ack = true;
        result->msg.identifier = PILOT_BOX_CONTROL;
        result->msg.data[0] = 0;
        result->handled = true;
        ESP_LOGI(TAG, "op COMING_ACK");
        return true;
    case RTU_CMD_KILL:
        result->msg.identifier = KILL_SWITCH_CONTROL;
        result->msg.data[0] = 0x01;
        result->ack_flag = FLAG_ACK_KILL;
        result->handled = true;
        ESP_LOGW(TAG, "op KILL");
        return true;
    case RTU_CMD_START:
        result->msg.identifier = KILL_SWITCH_CONTROL;
        result->msg.data[0] = 0x00;
        result->ack_flag = FLAG_ACK_START;
        result->handled = true;
        ESP_LOGI(TAG, "op START");
        return true;
    case RTU_CMD_LIGHT_ON:
        result->msg.identifier = HEADLIGHTS_CONTROL;
        result->msg.data[0] = 0x01;
        result->ack_flag = FLAG_ACK_LIGHT_ON;
        result->handled = true;
        return true;
    case RTU_CMD_LIGHT_OFF:
        result->msg.identifier = HEADLIGHTS_CONTROL;
        result->msg.data[0] = 0x00;
        result->ack_flag = FLAG_ACK_LIGHT_OFF;
        result->handled = true;
        return true;
    case RTU_CMD_LED_ON:
        result->msg.identifier = DEBUG_LED_CONTROL;
        result->msg.data[0] = 0x01;
        result->ack_flag = FLAG_ACK_LED_ON;
        result->is_led = true;
        result->led_on = true;
        result->handled = true;
        return true;
    case RTU_CMD_LED_OFF:
        result->msg.identifier = DEBUG_LED_CONTROL;
        result->msg.data[0] = 0x00;
        result->ack_flag = FLAG_ACK_LED_OFF;
        result->is_led = true;
        result->led_on = false;
        result->handled = true;
        return true;
    case RTU_CMD_BOX_OFF:
        result->msg.identifier = BOX_ALERT_CONTROL;
        result->msg.data[0] = 0x00;
        result->is_box = true;
        result->ack_soc = TELEM_SOC_ACK_BOX;
        result->handled = true;
        return true;
    case RTU_CMD_BOX:
        result->msg.identifier = BOX_ALERT_CONTROL;
        result->msg.data[0] = 0x01;
        result->is_box = true;
        result->ack_soc = TELEM_SOC_ACK_BOX;
        result->handled = true;
        ESP_LOGW(TAG, "op BOX");
        return true;
    default:
        ESP_LOGW(TAG, "op desconhecido 0x%02X", opcode);
        return false;
    }
}
