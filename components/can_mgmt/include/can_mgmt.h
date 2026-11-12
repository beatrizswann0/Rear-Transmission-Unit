#pragma once

#include <stdint.h>
#include <stdbool.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include "driver/twai.h"
#pragma GCC diagnostic pop

#define LINK_DOWN_TIMEOUT_MS 1500
#define SENSOR_ALIVE_MS      1500

/* Estado agregado a partir da CAN (GPS, se houver, vem da FCU — não há GPS na RTU). */
typedef struct {
    uint16_t rpm;
    uint16_t speed;
    int16_t roll;
    int16_t pitch;
    int16_t cvt_temp;
    int16_t eng_temp;
    float voltage;
    int16_t acc_x;
    int16_t acc_y;
    int16_t acc_z;
    int16_t dps_x;
    int16_t dps_y;
    int16_t dps_z;
    float latitude;
    float longitude;
    float gps_speed;
    bool gps_from_can;
    uint32_t last_gps_ms;
    uint8_t fcu_state; // Front control unit
    uint8_t rcu_state;
    uint8_t fcu_boot_tries;
    uint8_t rcu_boot_tries;
    bool fcu_imu_ok;
    bool fcu_can_ok;
    bool fcu_rpm_warning;
    bool rcu_can_ok;
    bool rcu_mlx_ok;
    bool fcu_link_down;
    bool rcu_link_down;
    uint32_t last_fcu_ms;
    uint32_t last_rcu_ms;
    uint32_t last_imu_ms;
    uint32_t last_cvt_ms;
    uint32_t last_speed_ms;
    uint32_t last_rpm_ms;
    uint32_t last_eng_ms;
    uint32_t last_volt_ms;
    /*
     * Posição do pedal do acelerador
     * Posição do pedal do freio
     * Nível da gasolina
     * Pressão na linha de freio
     */
} car_state_t;

void can_init(void);
bool can_update_state(car_state_t *state);
void can_recover_if_needed(void);
void can_refresh_link_status(car_state_t *state, uint32_t now_ms);

bool can_enqueue_tx(const twai_message_t *msg);
bool can_post(uint32_t id, const uint8_t *data, uint8_t dlc);
bool can_post_u8(uint32_t id, uint8_t value);

bool can_is_running(void);
bool can_consume_led_debug(bool *on);
void can_pilot_box_clear(void);
bool can_pilot_box_latched(void);

typedef struct {
    uint32_t rx_frames;
    uint32_t last_rx_id;
    uint32_t tx_error_counter;
    uint32_t rx_error_counter;
    int state; /* twai_state_t */
} can_bus_stats_t;

void can_get_bus_stats(can_bus_stats_t *out);
