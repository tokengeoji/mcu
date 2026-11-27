#ifndef SCAN_CONFIG_H
#define SCAN_CONFIG_H

/* Nominal angles, calibrated by centering the horn before fitting the sensor. */
#define SCAN_LEFT_DEG       60
#define SCAN_CENTER_DEG     90
#define SCAN_RIGHT_DEG      120
#define SCAN_STEP_DEG       5
#define SERVO_0_US          1000
#define SERVO_180_US        2000
#define SERVO_PERIOD_US     20000
/* Use 1 during assembly to hold the horn at its 90 degree center.
   Set back to 0 and rebuild/reflash for normal scanning. */
#define CENTER_ONLY         0
#define START_SETTLE_MS     3000
#define STEP_SETTLE_MS      80
#define ECHO_TIMEOUT_US     30000
#define MIN_VALID_MM        20
#define MAX_VALID_MM        4000
#define ALERT_ENTER_MM      300
#define ALERT_RELEASE_MM    350
#define BUZZER_HZ           1024

#if SCAN_LEFT_DEG < 0 || SCAN_RIGHT_DEG > 180 || SCAN_LEFT_DEG >= SCAN_CENTER_DEG || SCAN_CENTER_DEG >= SCAN_RIGHT_DEG || SCAN_STEP_DEG < 1 || SCAN_STEP_DEG > 180
#error Invalid_scan_limits
#endif
#if STEP_SETTLE_MS < 60 || ECHO_TIMEOUT_US >= 65535 || ECHO_TIMEOUT_US < 1
#error Invalid_ping_timing
#endif
#if MIN_VALID_MM < 1 || MIN_VALID_MM > ALERT_ENTER_MM || ALERT_RELEASE_MM <= ALERT_ENTER_MM || ALERT_RELEASE_MM > MAX_VALID_MM
#error Invalid_range_limits
#endif
#if SERVO_0_US <= 0 || SERVO_0_US >= SERVO_180_US || SERVO_180_US >= SERVO_PERIOD_US
#error Invalid_servo_pulse_range
#endif

#endif
