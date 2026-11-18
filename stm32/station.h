#ifndef STATION_H
#define STATION_H
#include "stm32f10x_lib.h"

#define SENSOR_COUNT 5
#define SLOT_MS 60U
#define ECHO_TIMEOUT_MS 30U
#define LINE_CAPACITY 32
#define PC_PORT 0
#define DISPLAY_PORT 1

extern volatile u32 station_ms;
void station_hardware_init(void);
void gpio_mode(GPIO_TypeDef *port, u16 pins, GPIOMode_TypeDef mode);
void enable_irq(u8 channel, u8 priority);
void echo_start(u8 sensor);
int echo_result(u32 elapsed_ms, int *distance);
void echo_interrupt(TIM_TypeDef *timer);
void piezo_set(u8 on);
u8 servo_ready(void);
void servo_open(void);
char keypad_key(u32 now);
void leds_set(u8 a, u8 b);
void serial_init(void);
void serial_interrupt(u8 port);
int serial_line(u8 port, char *out);
int serial_send(u8 port, const char *text);
#endif
