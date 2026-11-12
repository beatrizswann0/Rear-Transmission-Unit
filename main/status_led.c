#include "status_led.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    uint16_t on_ms;
    uint16_t idle_ms; /* off no fim do ciclo (pulses==1) */
    uint8_t pulses;
    uint16_t gap_ms;
} led_pattern_t;

static const led_pattern_t PAT_SOLID = {0, 0, 0, 0};
static const led_pattern_t PAT_SLOW  = {400, 400, 1, 0};
static const led_pattern_t PAT_OK    = {80, 1920, 1, 0};
static const led_pattern_t PAT_FAIL  = {100, 100, 1, 0};

typedef struct {
    gpio_num_t pin;
    fsm_state_t last_state;
    bool last_ok;
    uint8_t pulse_i;
    bool led_on;
    TickType_t phase_start;
} led_chan_t;

static led_chan_t ch_can;
static led_chan_t ch_lora;

static const led_pattern_t *pattern_for(fsm_state_t state, bool link_ok)
{
    switch (state) {
    case FSM_BOOT:
        return &PAT_SOLID;
    case FSM_SELF_CHECK:
        return &PAT_SLOW;
    case FSM_RUNNING:
        return link_ok ? &PAT_OK : &PAT_FAIL;
    case FSM_ERROR:
    default:
        /* Em ERROR o CAN normalmente caiu; LoRa ainda pode estar OK. */
        return link_ok ? &PAT_OK : &PAT_FAIL;
    }
}

static void chan_init(led_chan_t *ch, gpio_num_t pin)
{
    ch->pin = pin;
    ch->last_state = (fsm_state_t)0xFF;
    ch->last_ok = false;
    ch->pulse_i = 0;
    ch->led_on = false;
    ch->phase_start = xTaskGetTickCount();
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

static void chan_tick(led_chan_t *ch, fsm_state_t state, bool link_ok)
{
    const led_pattern_t *pat = pattern_for(state, link_ok);
    TickType_t now = xTaskGetTickCount();

    if (state != ch->last_state || link_ok != ch->last_ok) {
        ch->last_state = state;
        ch->last_ok = link_ok;
        ch->pulse_i = 0;
        ch->led_on = true;
        ch->phase_start = now;
        if (state == FSM_BOOT || pat->pulses == 0) {
            gpio_set_level(ch->pin, 1);
            return;
        }
        gpio_set_level(ch->pin, 1);
    }

    if (state == FSM_BOOT || pat->pulses == 0) {
        gpio_set_level(ch->pin, 1);
        return;
    }

    uint32_t elapsed = (now - ch->phase_start) * portTICK_PERIOD_MS;

    if (ch->led_on) {
        if (elapsed >= pat->on_ms) {
            ch->led_on = false;
            ch->phase_start = now;
            gpio_set_level(ch->pin, 0);
        }
        return;
    }

    uint16_t wait = (ch->pulse_i + 1 < pat->pulses) ? pat->gap_ms : pat->idle_ms;
    if (elapsed >= wait) {
        ch->pulse_i++;
        if (ch->pulse_i >= pat->pulses) {
            ch->pulse_i = 0;
        }
        ch->led_on = true;
        ch->phase_start = now;
        gpio_set_level(ch->pin, 1);
    }
}

void status_led_init(void)
{
    chan_init(&ch_can, PIN_LED_STATUS);
    chan_init(&ch_lora, PIN_LED_SD);
}

void status_led_tick(fsm_state_t state, bool can_ok, bool e32_ok)
{
    /* ERROR sem CAN: LED CAN sempre fail; LoRa segue AUX. */
    bool can_vis = (state == FSM_ERROR) ? false : can_ok;
    chan_tick(&ch_can, state, can_vis);
    chan_tick(&ch_lora, state, e32_ok);
}
