#ifndef CAN_IDS_H
#define CAN_IDS_H

/* IDs legado (lockstep com FCU/RCU atuais). Migrar com as aquisições novas. */

/* RCU — Rear Control Unit */
#define CAN_SPEED_ID        0x300
#define CAN_VOLTAGE_ID      0x502
#define CAN_ENG_TEMP_ID     0x400
#define CAN_CVT_TEMP_ID     0x401
#define CAN_TCU_DEBUG_ID    0x600

/* FCU — Front Control Unit */
#define CAN_FCU_DEBUG_ID    0x102
#define CAN_IMU_ID          0x205
#define CAN_ACC_ID          0x206
#define CAN_DPS_ID          0x207
#define CAN_RPM_ID          0x304

/* GPS publicado pela FCU (RTU só consome — sem GPS local) */
#define CAN_GPS_ID          0x250
#define CAN_GPS_SPEED_ID    0x255

/* IMF — comandos */
#define HEADLIGHTS_CONTROL  0x700
#define KILL_SWITCH_CONTROL 0x701
#define DEBUG_LED_CONTROL   0x702
#define BOX_ALERT_CONTROL   0x703
#define PILOT_BOX_CONTROL   0x704

#endif
