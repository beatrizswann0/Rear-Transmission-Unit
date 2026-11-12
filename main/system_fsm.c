#include "system_fsm.h"
#include "esp_log.h"

static const char *TAG = "FSM";

static fsm_state_t g_state = FSM_BOOT;
static bool g_limp;
static int self_check_passes;

void fsm_init(void)
{
    g_state = FSM_BOOT;
    g_limp = false;
    self_check_passes = 0;
    ESP_LOGI(TAG, "BOOT");
}

fsm_state_t fsm_state(void)
{
    return g_state;
}

bool fsm_is_running(void)
{
    return g_state == FSM_RUNNING;
}

bool fsm_limp(void)
{
    return g_limp;
}

void fsm_tick(bool can_ok, bool e32_ok, bool sd_ok,
              bool fcu_down, bool rcu_down)
{
    (void)sd_ok; /* SD opcional: falha ⇒ limp de log, não ERROR global */

    switch (g_state) {
    case FSM_BOOT:
        g_state = FSM_SELF_CHECK;
        self_check_passes = 0;
        ESP_LOGI(TAG, "→ SELF_CHECK");
        break;

    case FSM_SELF_CHECK:
        /* CAN é obrigatório; E32 opcional (bancada SoftAP sem LoRa). */
        if (can_ok) {
            self_check_passes++;
            if (self_check_passes >= 3) {
                g_state = FSM_RUNNING;
                g_limp = fcu_down || rcu_down || !e32_ok;
                ESP_LOGI(TAG, "→ RUNNING%s%s",
                         g_limp ? " (limp)" : "",
                         e32_ok ? "" : " e32_down");
            }
        } else {
            self_check_passes = 0;
            static int fail_streak;
            fail_streak++;
            if (fail_streak > 50) {
                g_state = FSM_ERROR;
                ESP_LOGE(TAG, "→ ERROR (can=%d e32=%d)", (int)can_ok, (int)e32_ok);
                fail_streak = 0;
            }
        }
        break;

    case FSM_RUNNING:
        g_limp = fcu_down || rcu_down || !e32_ok;
        if (!can_ok) {
            g_state = FSM_ERROR;
            ESP_LOGE(TAG, "→ ERROR (CAN lost)");
        }
        break;

    case FSM_ERROR:
        if (can_ok) {
            g_state = FSM_SELF_CHECK;
            self_check_passes = 0;
            ESP_LOGW(TAG, "→ SELF_CHECK (recovery)");
        }
        break;
    }
}
