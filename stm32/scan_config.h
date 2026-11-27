#ifndef SCAN_CONFIG_H
#define SCAN_CONFIG_H

/* Nominal angles, calibrated by centering the horn before fitting the sensor. */
#define SCAN_LEFT_DEG       0
#define SCAN_CENTER_DEG     90
#define SCAN_RIGHT_DEG      180
#define SCAN_STEP_DEG       5
/* Initial calibration before a keypad adjustment is saved. These guards
   bound pulse widths in software; physical stops vary between servos. */
#define SERVO_0_US          950
#define SERVO_CENTER_US     1500
#define SERVO_180_US        2000
#define SERVO_PERIOD_US     20000
#define SERVO_MIN_US        500
#define SERVO_MAX_US        2500
#define TRIM_STEP_US        25
/* The IAR alignment build supplies CENTER_ONLY=1. */
#ifndef CENTER_ONLY
#define CENTER_ONLY         0
#endif
/* The IAR bench build exercises outputs once, then ranges straight ahead. */
#ifndef BENCH_CHECK
#define BENCH_CHECK         0
#endif
#define ENDPOINT_SETTLE_MS  400
#define STEP_SETTLE_MS      80
#define ECHO_TIMEOUT_US     30000
#define MIN_VALID_MM        20
#define MAX_VALID_MM        4000
#define ALERT_ENTER_MM      300
#define ALERT_RELEASE_MM    350
#define BUZZER_HZ           1024

#if CENTER_ONLY != 0 && CENTER_ONLY != 1
#error Invalid_center_only_mode
#endif
#if (BENCH_CHECK != 0 && BENCH_CHECK != 1) || (CENTER_ONLY && BENCH_CHECK)
#error Invalid_bench_check_mode
#endif

#if SCAN_LEFT_DEG != 0 || SCAN_CENTER_DEG != 90 || SCAN_RIGHT_DEG != 180 || SCAN_STEP_DEG != 5
#error PC_mapper_requires_0_to_180_degrees_in_5_degree_steps
#endif
#if STEP_SETTLE_MS < 60 || ECHO_TIMEOUT_US >= 65535 || ECHO_TIMEOUT_US < 1
#error Invalid_ping_timing
#endif
#if MIN_VALID_MM < 1 || MIN_VALID_MM > ALERT_ENTER_MM || ALERT_RELEASE_MM <= ALERT_ENTER_MM || ALERT_RELEASE_MM > MAX_VALID_MM
#error Invalid_range_limits
#endif
#if SERVO_MIN_US < 500 || SERVO_MAX_US > 2500 || SERVO_PERIOD_US != 20000 || TRIM_STEP_US < 1 || SERVO_0_US < SERVO_MIN_US || SERVO_0_US + TRIM_STEP_US > SERVO_CENTER_US || SERVO_CENTER_US + TRIM_STEP_US > SERVO_180_US || SERVO_180_US > SERVO_MAX_US
#error Invalid_servo_pulse_range
#endif

#endif
