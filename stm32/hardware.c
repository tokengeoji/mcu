/* Pin allocation: supplied wiring plan, sections 05 and 07.
   Timer/USART bindings use the supplied ST firmware library V1.0. */
#include "station.h"
#include "site_profile.h"

volatile u32 station_ms;
static TIM_TypeDef *capture_timer;
static u8 channel;
static u16 capture_it, overcapture_flag;
enum CaptureState { IDLE, RISING, FALLING, COMPLETE, BAD_ECHO };
static volatile enum CaptureState capture_state;
static volatile u16 rise, pulse;
static u16 piezo_half_period;

void gpio_mode(GPIO_TypeDef *port, u16 pins, GPIOMode_TypeDef mode)
{
    GPIO_InitTypeDef config;
    config.GPIO_Pin = pins;
    config.GPIO_Mode = mode;
    config.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &config);
}

void enable_irq(u8 channel_number, u8 priority)
{
    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = channel_number;
    nvic.NVIC_IRQChannelPreemptionPriority = priority;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

static void timer_base(TIM_TypeDef *timer, u16 prescaler, u16 reload)
{
    TIM_TimeBaseInitTypeDef base;
    TIM_TimeBaseStructInit(&base);
    base.TIM_Prescaler = prescaler;
    base.TIM_Period = reload;
    TIM_TimeBaseInit(timer, &base);
    TIM_GenerateEvent(timer, TIM_EventSource_Update); /* latch PSC immediately */
}

u8 servo_ready(void)
{
    return SITE_SERVO_CLOSED_US > 0 && SITE_SERVO_CLOSED_US < 20000
        && SITE_SERVO_OPEN_US > 0 && SITE_SERVO_OPEN_US < 20000
        && SITE_SERVO_CLOSED_US != SITE_SERVO_OPEN_US;
}

static void servo_init(u16 prescaler)
{
    TIM1_TimeBaseInitTypeDef base;
    TIM1_OCInitTypeDef oc;
    if (!servo_ready()) return; /* PA8 remains low until actual endpoints exist. */
    TIM1_TimeBaseStructInit(&base);
    base.TIM1_Prescaler = prescaler;
    base.TIM1_Period = 19999;
    TIM1_TimeBaseInit(&base);
    TIM1_OCStructInit(&oc);
    oc.TIM1_OCMode = TIM1_OCMode_PWM1;
    oc.TIM1_OutputState = TIM1_OutputState_Enable;
    oc.TIM1_Pulse = SITE_SERVO_CLOSED_US;
    TIM1_OC1Init(&oc);
    TIM1_OC1PreloadConfig(TIM1_OCPreload_Enable);
    TIM1_GenerateEvent(TIM1_EventSource_Update);
    gpio_mode(GPIOA, GPIO_Pin_8, GPIO_Mode_AF_PP);
    TIM1_CtrlPWMOutputs(ENABLE); /* advanced timer output enable (MOE) */
    TIM1_Cmd(ENABLE);
}

void servo_open(void)
{
    if (servo_ready()) TIM1_SetCompare1(SITE_SERVO_OPEN_US);
}

void station_hardware_init(void)
{
    RCC_ClocksTypeDef clock;
    TIM_OCInitTypeDef oc;
    u32 apb1_timer, apb2_timer;
    RCC_GetClocksFreq(&clock);
    apb1_timer = clock.PCLK1_Frequency;
    if (apb1_timer != clock.HCLK_Frequency) apb1_timer *= 2;
    apb2_timer = clock.PCLK2_Frequency;
    if (apb2_timer != clock.HCLK_Frequency) apb2_timer *= 2;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
        RCC_APB2Periph_GPIOC | RCC_APB2Periph_TIM1 | RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 |
        RCC_APB1Periph_TIM4 | RCC_APB1Periph_USART3, ENABLE);
    gpio_mode(GPIOA, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3, GPIO_Mode_IPD);
    gpio_mode(GPIOB, GPIO_Pin_6, GPIO_Mode_IPD);
    gpio_mode(GPIOA, GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_8, GPIO_Mode_Out_PP);
    gpio_mode(GPIOA, GPIO_Pin_6 | GPIO_Pin_9, GPIO_Mode_AF_PP);
    gpio_mode(GPIOB, GPIO_Pin_10, GPIO_Mode_AF_PP);
    gpio_mode(GPIOA, GPIO_Pin_10, GPIO_Mode_IN_FLOATING);
    gpio_mode(GPIOB, GPIO_Pin_11, GPIO_Mode_IN_FLOATING);
    gpio_mode(GPIOC, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2, GPIO_Mode_IPD);
    /* Inactive keypad rows float; active row drives high. This also avoids
       opposing push-pull outputs if multiple keys are pressed in one column. */
    gpio_mode(GPIOC, GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6, GPIO_Mode_Out_OD);
    GPIO_SetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6);
    gpio_mode(GPIOC, GPIO_Pin_3, GPIO_Mode_Out_PP);
    gpio_mode(GPIOC, GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12, GPIO_Mode_Out_PP);
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    timer_base(TIM2, (u16)(apb1_timer/1000000 - 1), 65535);
    timer_base(TIM4, (u16)(apb1_timer/1000000 - 1), 65535);
    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM4, ENABLE);
    enable_irq(TIM2_IRQChannel, 1);
    enable_irq(TIM4_IRQChannel, 1);
    /* The supplied PIEZO main.c calls Change_FREQ(1024). Use that test tone
       on the wiring plan's PA6/TIM3_CH1 with an independently running PWM. */
    timer_base(TIM3, (u16)(apb1_timer/1000000 - 1), (u16)(1000000UL/1024 - 1));
    piezo_half_period = (u16)(1000000UL/1024/2);
    TIM_OCStructInit(&oc);
    oc.TIM_Channel = TIM_Channel_1;
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_Pulse = 0;
    TIM_OCInit(TIM3, &oc);
    TIM_Cmd(TIM3, ENABLE);
    servo_init((u16)(apb2_timer/1000000 - 1));
    serial_init();
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK);
    SysTick_SetReload(clock.HCLK_Frequency/1000 - 1);
    SysTick_CounterCmd(SysTick_Counter_Clear);
    SysTick_ITConfig(ENABLE);
    SysTick_CounterCmd(SysTick_Counter_Enable);
}

void leds_set(u8 a, u8 b)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, a ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(GPIOA, GPIO_Pin_5, b ? Bit_SET : Bit_RESET);
}

void piezo_set(u8 on)
{
    TIM_SetCompare1(TIM3, on ? piezo_half_period : 0);
}

u8 keypad_zero_edge(u32 now)
{
    static u8 row, pressed, stable, count, other_key;
    static u32 last_tick;
    u16 cols, pin;
    u8 zero, edge = 0;
    if (now == last_tick) return 0;
    last_tick = now;
    cols = GPIO_ReadInputData(GPIOC) & 7;
    if (row < 3) {
        if (cols) other_key = 1;
    } else {
        zero = (cols == GPIO_Pin_1 && !other_key);
        other_key = 0;
        if (zero != pressed) { pressed = zero; count = 1; }
        else if (count < 5) ++count;
        if (count == 5 && stable != pressed) { stable = pressed; edge = stable; }
    }
    pin = (u16)(GPIO_Pin_3 << row);
    gpio_mode(GPIOC, pin, GPIO_Mode_Out_OD);
    row = (row + 1) & 3;
    gpio_mode(GPIOC, (u16)(GPIO_Pin_3 << row), GPIO_Mode_Out_PP);
    return edge;
}

static void set_capture_edge(u16 polarity)
{
    TIM_ICInitTypeDef input;
    TIM_ICStructInit(&input);
    input.TIM_ICMode = TIM_ICMode_ICAP;
    input.TIM_Channel = channel;
    input.TIM_ICPolarity = polarity;
    input.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInit(capture_timer, &input);
}

void echo_start(u8 sensor)
{
    u16 trigger = (u16)(GPIO_Pin_8 << sensor);
    u16 echo_pin, start;
    GPIO_TypeDef *echo_port;
    unsigned budget = 1024;
    capture_timer = sensor == 4 ? TIM4 : TIM2;
    channel = sensor == 4 ? 0 : sensor;
    capture_it = (u16)(TIM_IT_CC1 << channel);
    overcapture_flag = (u16)(TIM_FLAG_CC1OF << channel);
    echo_port = sensor == 4 ? GPIOB : GPIOA;
    echo_pin = sensor == 4 ? GPIO_Pin_6 : (u16)(GPIO_Pin_0 << channel);
    if (GPIO_ReadInputDataBit(echo_port, echo_pin)) { capture_state = BAD_ECHO; return; }
    set_capture_edge(TIM_ICPolarity_Rising);
    TIM_ClearFlag(capture_timer, capture_it | overcapture_flag);
    capture_state = RISING;
    TIM_ITConfig(capture_timer, capture_it, ENABLE);
    GPIO_SetBits(GPIOC, trigger);
    start = TIM_GetCounter(TIM2);
    /* Only the requested 10 us trigger uses a bounded foreground wait. */
    while ((u16)(TIM_GetCounter(TIM2) - start) < 10 && budget) --budget;
    GPIO_ResetBits(GPIOC, trigger);
    if (!budget) {
        TIM_ITConfig(capture_timer, capture_it, DISABLE);
        capture_state = BAD_ECHO;
    }
}

void echo_interrupt(TIM_TypeDef *timer)
{
    u16 stamp;
    if (timer != capture_timer || TIM_GetITStatus(timer, capture_it) == RESET) return;
    switch (channel) {
        case 0: stamp = TIM_GetCapture1(timer); break;
        case 1: stamp = TIM_GetCapture2(timer); break;
        case 2: stamp = TIM_GetCapture3(timer); break;
        default: stamp = TIM_GetCapture4(timer); break;
    }
    /* Reading CCRx clears CCxIF in input-capture mode. CCxOF marks lost data. */
    if (TIM_GetFlagStatus(timer, overcapture_flag) == SET) {
        TIM_ClearFlag(timer, overcapture_flag);
        capture_state = BAD_ECHO;
    } else if (capture_state == RISING) {
        rise = stamp;
        set_capture_edge(TIM_ICPolarity_Falling);
        capture_state = FALLING;
        return;
    } else if (capture_state == FALLING) {
        pulse = (u16)(stamp - rise);
        capture_state = COMPLETE;
    }
    TIM_ITConfig(timer, capture_it, DISABLE);
}

int echo_result(u32 elapsed_ms, int *distance)
{
    if (capture_state != COMPLETE && capture_state != BAD_ECHO &&
        elapsed_ms < ECHO_TIMEOUT_MS) return 0;
    TIM_ITConfig(capture_timer, capture_it, DISABLE);
    *distance = -1;
    /* At the slot deadline even a short, late pulse (or wrapped CCR) is invalid.
       The channel IRQ is masked above; the next slot clears CCxIF/CCxOF. */
    if (elapsed_ms < ECHO_TIMEOUT_MS && capture_state == COMPLETE &&
        pulse > 0 && pulse < ECHO_TIMEOUT_MS * 1000U) {
        *distance = (int)(((u32)pulse * 343 + 1000) / 2000);
        if (*distance == 0) *distance = -1;
    }
    capture_state = IDLE;
    return 1;
}
