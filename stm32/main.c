/* HBE-MCU-MULTI STM32F103RBT6. USART3 TX reports each angle and distance. */
#include "System_func.h"
#include "scan_config.h"
#if !CENTER_ONLY && !BENCH_CHECK
#include "servo_store.h"
#endif

static void pin_mode(GPIO_TypeDef *port, u16 pins, GPIOMode_TypeDef mode)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = pins;
    gpio.GPIO_Mode = mode;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(port, &gpio);
}

#if !CENTER_ONLY
static void timer_base(TIM_TypeDef *timer, u32 clock_hz, u16 period)
{
    TIM_TimeBaseInitTypeDef timer_config;
    TIM_TimeBaseStructInit(&timer_config);
    timer_config.TIM_Prescaler = (u16)(clock_hz / 1000000UL - 1);
    timer_config.TIM_Period = period;
    TIM_TimeBaseInit(timer, &timer_config);
    TIM_GenerateEvent(timer, TIM_EventSource_Update);
}
#endif

/* Index 0/1/2 holds the calibrated pulse for 0/90/180 commanded degrees. */
static u16 servo_us[] = { SERVO_0_US, SERVO_CENTER_US, SERVO_180_US };

static u16 pulse_for_angle(int angle)
{
    int half = angle > 90;
    int within_half = angle - 90 * half;
    return (u16)(servo_us[half] +
        ((long)within_half * (servo_us[half + 1] - servo_us[half]) + 45) / 90);
}

static void init_servo(u32 clock_hz)
{
    TIM1_TimeBaseInitTypeDef base;
    TIM1_OCInitTypeDef pwm;
    TIM1_TimeBaseStructInit(&base);
    base.TIM1_Prescaler = (u16)(clock_hz / 1000000UL - 1);
    base.TIM1_Period = SERVO_PERIOD_US - 1;
    TIM1_TimeBaseInit(&base);
    TIM1_OCStructInit(&pwm);
    pwm.TIM1_OCMode = TIM1_OCMode_PWM1;
    pwm.TIM1_OutputState = TIM1_OutputState_Enable;
    pwm.TIM1_Pulse = pulse_for_angle(SCAN_CENTER_DEG);
    TIM1_OC1Init(&pwm);
    TIM1_OC1PreloadConfig(TIM1_OCPreload_Enable);
    TIM1_GenerateEvent(TIM1_EventSource_Update);
    TIM1_CtrlPWMOutputs(ENABLE);
    TIM1_Cmd(ENABLE);
}

#if !CENTER_ONLY
static void init_capture(u32 clock_hz)
{
    TIM_ICInitTypeDef capture;
    timer_base(TIM2, clock_hz, 65535);
    TIM_ICStructInit(&capture);
    /* PA0 / TI1 drives both capture latches: CH1 rising, CH2 falling. */
    capture.TIM_Channel = TIM_Channel_1;
    capture.TIM_ICSelection = TIM_ICSelection_DirectTI;
    capture.TIM_ICPolarity = TIM_ICPolarity_Rising;
    capture.TIM_ICFilter = 3;
    TIM_ICInit(TIM2, &capture);
    capture.TIM_Channel = TIM_Channel_2;
    capture.TIM_ICSelection = TIM_ICSelection_IndirectTI;
    capture.TIM_ICPolarity = TIM_ICPolarity_Falling;
    TIM_ICInit(TIM2, &capture);
    TIM_Cmd(TIM2, ENABLE);
}

static void init_buzzer(u32 clock_hz)
{
    TIM_OCInitTypeDef pwm;
    timer_base(TIM3, clock_hz, (u16)(1000000UL / BUZZER_HZ - 1));
    TIM_OCStructInit(&pwm);
    pwm.TIM_Channel = TIM_Channel_1;
    pwm.TIM_OCMode = TIM_OCMode_PWM1;
    pwm.TIM_Pulse = 0;
    TIM_OCInit(TIM3, &pwm);
    TIM_Cmd(TIM3, ENABLE);
}

static void init_scan(u32 timer_apb1)
{
    USART_InitTypeDef serial;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 |
                           RCC_APB1Periph_USART3, ENABLE);
    pin_mode(GPIOA, GPIO_Pin_0, GPIO_Mode_IPD);
    pin_mode(GPIOA, GPIO_Pin_6, GPIO_Mode_AF_PP);
    pin_mode(GPIOB, GPIO_Pin_10, GPIO_Mode_AF_PP);
#if !BENCH_CHECK
    /* 5 V HC244 column outputs C0/C1/C2 need FT pins with pulls disabled.
       The keypad's R0 is tied to its own +5 V; other rows have board pulldowns. */
    pin_mode(GPIOB, GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14, GPIO_Mode_IN_FLOATING);
#endif
    USART_StructInit(&serial);
    serial.USART_BaudRate = 115200;
    serial.USART_Mode = USART_Mode_Tx; /* PB10 only, 115200 8N1. */
    USART_Init(USART3, &serial);
    USART_Cmd(USART3, ENABLE);
    init_capture(timer_apb1);
    init_buzzer(timer_apb1);
}
#endif

static void init_hardware(void)
{
    RCC_ClocksTypeDef clocks;
    u32 timer_apb2;
#if !CENTER_ONLY
    u32 timer_apb1;
#endif
    Init_STM32F103();
    RCC_GetClocksFreq(&clocks);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_AFIO | RCC_APB2Periph_TIM1, ENABLE);
    GPIO_ResetBits(GPIOC, GPIO_Pin_8);
    pin_mode(GPIOC, GPIO_Pin_8, GPIO_Mode_Out_PP); /* TRIG idle low */
    GPIO_ResetBits(GPIOA, GPIO_Pin_6);
    pin_mode(GPIOA, GPIO_Pin_6, GPIO_Mode_Out_PP); /* PIEZO quiet during alignment */
    pin_mode(GPIOA, GPIO_Pin_8, GPIO_Mode_AF_PP);
    timer_apb2 = clocks.PCLK2_Frequency;
    if (timer_apb2 != clocks.HCLK_Frequency) timer_apb2 *= 2;
    init_servo(timer_apb2);
#if !CENTER_ONLY
    timer_apb1 = clocks.PCLK1_Frequency;
    if (timer_apb1 != clocks.HCLK_Frequency) timer_apb1 *= 2;
    init_scan(timer_apb1);
#endif
}

#if !CENTER_ONLY
/* TIM2 runs continuously at 1 MHz. Unsigned subtraction handles one wrap. */
static void wait_us(u16 duration)
{
    u16 start = TIM_GetCounter(TIM2);
    while ((u16)(TIM_GetCounter(TIM2) - start) < duration) { }
}

static void wait_ms(unsigned duration)
{
    while (duration--) wait_us(1000);
}

static void set_buzzer(int on)
{
    /* V1 TIM_OCInit enables TIM3 CH1. CCR1=0 holds PA6 low in PWM1. */
    TIM_SetCompare1(TIM3, on ? (u16)(1000000UL / BUZZER_HZ / 2) : 0);
}

#if BENCH_CHECK
static void bench_startup(void)
{
    /* Keep the short wiring check independent of the normal scan limits. */
    static const u8 angles[] = { 60, 120, SCAN_CENTER_DEG };
    unsigned i;
    set_buzzer(1);
    wait_ms(150);
    set_buzzer(0);
    wait_ms(150);
    set_buzzer(1);
    wait_ms(150);
    set_buzzer(0);
    for (i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i) {
        TIM1_SetCompare1(pulse_for_angle(angles[i]));
        wait_ms(1000);
    }
}
#endif

static int measure_mm(void)
{
    const u16 edges = TIM_FLAG_CC1 | TIM_FLAG_CC2;
    const u16 overflow = TIM_FLAG_CC1OF | TIM_FLAG_CC2OF;
    u16 start, rise, fall, width;
    int distance;
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0)) return -1;
    TIM_ClearFlag(TIM2, (u16)(edges | overflow));
    start = TIM_GetCounter(TIM2);
    GPIO_SetBits(GPIOC, GPIO_Pin_8);
    wait_us(12);
    GPIO_ResetBits(GPIOC, GPIO_Pin_8);

    while ((u16)(TIM_GetCounter(TIM2) - start) < ECHO_TIMEOUT_US) {
        if (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC1) == SET &&
            TIM_GetFlagStatus(TIM2, TIM_FLAG_CC2) == SET) {
            rise = TIM_GetCapture1(TIM2);
            fall = TIM_GetCapture2(TIM2);
            /* The V1 SPL accepts one flag per status query. Reading CCR1/2
               clears CC1IF/CC2IF; the overcapture flags remain latched. */
            if (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC1OF) == SET ||
                TIM_GetFlagStatus(TIM2, TIM_FLAG_CC2OF) == SET ||
                (u16)(fall - start) >= ECHO_TIMEOUT_US ||
                (u16)(rise - start) >= (u16)(fall - start)) return -1;
            width = (u16)(fall - rise);
            distance = (int)(((u32)width * 343UL + 1000UL) / 2000UL);
            if (distance >= MIN_VALID_MM && distance <= MAX_VALID_MM) return distance;
            return -1;
        }
    }
    return -1;
}

static void serial_char(char ch)
{
    u16 start = TIM_GetCounter(TIM2);
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET) {
        if ((u16)(TIM_GetCounter(TIM2) - start) >= 1000) return;
    }
    USART_SendData(USART3, (u16)(unsigned char)ch);
}

static void serial_text(const char *text)
{
    while (*text) serial_char(*text++);
}

static void serial_unsigned(u16 value)
{
    char digits[5];
    unsigned length = 0;
    do {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (length) serial_char(digits[--length]);
}

static void publish_scan(u16 sequence, int alert, int angle, int distance)
{
    serial_text("SCAN,");
    serial_unsigned(sequence);
    serial_text(alert ? ",1," : ",0,");
    serial_unsigned((u16)angle);
    serial_char(',');
    if (distance < 0) serial_text("-1");
    else serial_unsigned((u16)distance);
    serial_text("\r\n");
}

static int sample_angle(int angle, int previous_alert)
{
    static u16 sequence;
    int distance = measure_mm();
    int alert = distance >= 0 &&
        (previous_alert ? distance < ALERT_RELEASE_MM : distance <= ALERT_ENTER_MM);
    set_buzzer(alert);
    publish_scan(sequence++, alert, angle, distance);
    return alert;
}

#if !BENCH_CHECK
#define KEY_POLL_MS 5
#define KEY_STABLE_POLLS 6
#define CALIBRATION_KEYS 5 /* K1 and K3, the first and third columns */

static u8 key_mask(void)
{
    return (u8)((GPIO_ReadInputData(GPIOB) >> 12) & 7);
}

/* After boot, a chord, a scan, or a long press, a stable release is required
   before any new single key can trigger. Poll every KEY_POLL_MS while idle. */
static char key_press(void)
{
    /* Index is the three-bit column mask. Multiple columns are ignored. */
    static const char digits[8] = { 0, '1', '2', 0, '3', 0, 0, 0 };
    static u8 previous, stable_polls, armed;
    u8 keys = key_mask();
    if (keys != previous) {
        previous = keys;
        stable_polls = 0;
    }
    if (stable_polls < KEY_STABLE_POLLS) ++stable_polls;
    if (stable_polls == KEY_STABLE_POLLS) {
        if (!keys) armed = 1;
        else if (armed) {
            armed = 0;
            return digits[keys];
        }
    }
    return 0;
}

static int calibration_requested(void)
{
    unsigned i;
    for (i = 0; i < 8; ++i) {
        if (key_mask() != CALIBRATION_KEYS) return 0;
        wait_ms(KEY_POLL_MS);
    }
    return 1;
}

static void report_calibration(u8 point)
{
    serial_text("CAL,");
    serial_unsigned((u16)(90 * point));
    serial_char(',');
    serial_unsigned(servo_us[point]);
    serial_text("\r\n");
}

/* Move between calibration positions in small steps, including the long
   transition from the low endpoint to the high endpoint. */
static void seek_calibration_pulse(int current, int target)
{
    while (current != target) {
        int difference = target - current;
        if (difference > TRIM_STEP_US) difference = TRIM_STEP_US;
        if (difference < -TRIM_STEP_US) difference = -TRIM_STEP_US;
        current += difference;
        TIM1_SetCompare1((u16)current);
        wait_ms(20);
    }
}

static void calibrate(void)
{
    static const u8 order[] = { 1, 0, 2 }; /* center, short pulse, long pulse */
    unsigned stage;
    int pulse, low, high, current = servo_us[1];
    u8 point;
    char key;
    for (stage = 0; stage < sizeof(order) / sizeof(order[0]); ++stage) {
        unsigned elapsed = 1000;
        point = order[stage];
        low = point == 0 ? SERVO_MIN_US : servo_us[point - 1] + TRIM_STEP_US;
        high = point == 2 ? SERVO_MAX_US : servo_us[point + 1] - TRIM_STEP_US;
        seek_calibration_pulse(current, servo_us[point]);
        for (;;) {
            if (elapsed >= 1000) { report_calibration(point); elapsed = 0; }
            wait_ms(KEY_POLL_MS);
            elapsed += KEY_POLL_MS;
            key = key_press();
            if (key == '2') break;
            if (key == '1' || key == '3') {
                pulse = servo_us[point] + (key == '1' ? -TRIM_STEP_US : TRIM_STEP_US);
                if (pulse < low) pulse = low;
                if (pulse > high) pulse = high;
                servo_us[point] = (u16)pulse;
                TIM1_SetCompare1(servo_us[point]);
                elapsed = 1000;
            }
        }
        current = servo_us[point];
    }
    seek_calibration_pulse(current, servo_us[1]);
    serial_text("CAL,DONE,");
    serial_unsigned(servo_us[0]);
    serial_char(',');
    serial_unsigned(servo_us[1]);
    serial_char(',');
    serial_unsigned(servo_us[2]);
    serial_text("\r\n");
    if (!servo_store_save(servo_us[0], servo_us[1], servo_us[2]))
        serial_text("CAL,VOLATILE\r\n");
    wait_ms(ENDPOINT_SETTLE_MS);
}

static void station_message(const char *prefix, char station)
{
    serial_text(prefix);
    serial_char(station);
    serial_text("\r\n");
}

static void scan_station(char station)
{
    int angle, alert = 0;
    station_message("BEGIN,K", station);
    for (angle = SCAN_LEFT_DEG; angle <= SCAN_RIGHT_DEG; angle += SCAN_STEP_DEG) {
        TIM1_SetCompare1(pulse_for_angle(angle));
        wait_ms(angle == SCAN_LEFT_DEG ? ENDPOINT_SETTLE_MS : STEP_SETTLE_MS);
        alert = sample_angle(angle, alert);
    }
    set_buzzer(0);
    TIM1_SetCompare1(pulse_for_angle(SCAN_CENTER_DEG));
    wait_ms(ENDPOINT_SETTLE_MS);
    station_message("END,K", station);
}

static void run_keypad(void)
{
    unsigned idle_ms = 1000;
    char key;
    if (calibration_requested()) calibrate();
    for (;;) {
        if (idle_ms >= 1000) { serial_text("READY\r\n"); idle_ms = 0; }
        key = key_press();
        if (key != 0) {
            scan_station(key);
            idle_ms = 1000;
        }
        wait_ms(KEY_POLL_MS);
        idle_ms += KEY_POLL_MS;
    }
}
#endif
#endif

int main(void)
{
#if BENCH_CHECK
    int alert = 0;
#elif !CENTER_ONLY
    servo_store_load(&servo_us[0], &servo_us[1], &servo_us[2]);
#endif
    init_hardware();
#if CENTER_ONLY
    for (;;) { } /* TIM1 holds 90 degrees until reset. */
#elif BENCH_CHECK
    bench_startup();
    for (;;) {
        alert = sample_angle(SCAN_CENTER_DEG, alert);
        wait_ms(STEP_SETTLE_MS);
    }
#else
    run_keypad();
    return 0;
#endif
}
