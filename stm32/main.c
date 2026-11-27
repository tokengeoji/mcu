/* HBE-MCU-MULTI STM32F103RBT6. USART3 TX reports each angle and distance. */
#include "System_func.h"
#include "scan_config.h"

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

static u16 pulse_for_angle(int angle)
{
    return (u16)(SERVO_0_US + ((long)angle * (SERVO_180_US - SERVO_0_US) + 90) / 180);
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
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET) { }
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
#endif

int main(void)
{
#if CENTER_ONLY
    init_hardware();
    for (;;) { } /* TIM1 holds 90 degrees until reset. */
#else
    u16 sequence = 0;
    int angle = SCAN_CENTER_DEG, step = SCAN_STEP_DEG, alert = 0;
    int distance;
    init_hardware();
    wait_ms(START_SETTLE_MS);
    for (;;) {
        distance = measure_mm();
        /* Invalid echoes clear the alert; hysteresis limits chatter near the boundary. */
        alert = distance >= 0 && (alert ? distance < ALERT_RELEASE_MM : distance <= ALERT_ENTER_MM);
        TIM_SetCompare1(TIM3, alert ? (u16)(1000000UL / BUZZER_HZ / 2) : 0);
        publish_scan(sequence++, alert, angle, distance);

        if (angle == SCAN_RIGHT_DEG) step = -SCAN_STEP_DEG;
        if (angle == SCAN_LEFT_DEG) step = SCAN_STEP_DEG;
        angle += step;
        if (angle > SCAN_RIGHT_DEG) angle = SCAN_RIGHT_DEG;
        if (angle < SCAN_LEFT_DEG) angle = SCAN_LEFT_DEG;
        TIM1_SetCompare1(pulse_for_angle(angle));
        wait_ms(STEP_SETTLE_MS);
    }
#endif
}
