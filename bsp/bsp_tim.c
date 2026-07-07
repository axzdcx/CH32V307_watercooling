/**
 * @file    bsp_tim.c
 * @brief   定时器底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2025-01-22
 *
 * 定时器分配:
 *   TIM1_CH1 (PA8) - 水泵PWM   25kHz
 *   TIM2          - 系统1ms定时器（毫秒计时）
 *   TIM4_CH3 (PB8) - 风扇1 PWM 25kHz
 *   TIM4_CH4 (PB9) - 风扇2 PWM 25kHz
 */

#include "bsp_tim.h"

/*============================ 私有变量 ============================*/

static uint8_t pump_duty = 0;
static uint8_t fan1_duty = 0;
static uint8_t fan2_duty = 0;

/*============================ 全局变量 ============================*/

volatile uint32_t g_tick_ms = 0;        /* 系统毫秒计数器 */

/*============================ TIM2 系统1ms定时器 ============================*/

/**
 * @brief  系统1ms定时器初始化
 * @note   TIM2, 96MHz / 96 / 1000 = 1kHz (1ms)
 */
void tim2_tick_init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    /* 使能TIM2时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* TIM2配置：96MHz / 96 / 1000 = 1kHz (1ms) */
    TIM_TimeBaseStructure.TIM_Period = 1000 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = 96 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    /* 使能TIM2更新中断 */
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    /* 配置NVIC */
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能TIM2 */
    TIM_Cmd(TIM2, ENABLE);
}

/**
 * @brief  TIM2中断服务函数
 */
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        g_tick_ms++;
    }
}

/**
 * @brief  获取系统运行毫秒数
 */
uint32_t get_tick_ms(void)
{
    return g_tick_ms;
}

/*============================ 水泵 PWM ============================*/

/**
 * @brief  水泵PWM初始化
 * @note   TIM1_CH1 (PA8), 25kHz, 默认占空比0%
 *         TIM1是高级定时器，需要使能主输出
 */
void pump_pwm_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    /* 使能时钟 */
    RCC_APB2PeriphClockCmd(PUMP_TIM_CLK | PUMP_GPIO_CLK, ENABLE);

    /* PA8 复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin = PUMP_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PUMP_GPIO_PORT, &GPIO_InitStructure);

    /* TIM1 时基配置: 144MHz / 6 / 960 = 25kHz */
    TIM_TimeBaseStructure.TIM_Period = PWM_ARR;
    TIM_TimeBaseStructure.TIM_Prescaler = PWM_PSC;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(PUMP_TIM, &TIM_TimeBaseStructure);

    /* TIM1 CH1 PWM模式配置 */
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;  /* 默认占空比0% */
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(PUMP_TIM, &TIM_OCInitStructure);

    /* 使能预装载 */
    TIM_OC1PreloadConfig(PUMP_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(PUMP_TIM, ENABLE);

    /* TIM1是高级定时器，必须使能主输出 */
    TIM_CtrlPWMOutputs(PUMP_TIM, ENABLE);

    /* 使能TIM1 */
    TIM_Cmd(PUMP_TIM, ENABLE);

    pump_duty = 0;
}

/**
 * @brief  设置水泵占空比
 * @param  duty: 占空比 0-100 (%)
 */
void pump_set_duty(uint8_t duty)
{
    if(duty > 100) duty = 100;

    uint16_t compare = (uint16_t)((uint32_t)duty * (PWM_ARR + 1) / 100);
    TIM_SetCompare1(PUMP_TIM, compare);

    pump_duty = duty;
}

/**
 * @brief  获取水泵当前占空比
 */
uint8_t pump_get_duty(void)
{
    return pump_duty;
}

/*============================ 风扇 PWM ============================*/

/**
 * @brief  风扇PWM初始化
 * @note   TIM4_CH3 (PB8) + TIM4_CH4 (PB9), 25kHz, 默认占空比0%
 */
void fan_pwm_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    /* 使能时钟 */
    RCC_APB1PeriphClockCmd(FAN1_TIM_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(FAN1_GPIO_CLK, ENABLE);

    /* PB8 复用推挽输出 (风扇1) */
    GPIO_InitStructure.GPIO_Pin = FAN1_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(FAN1_GPIO_PORT, &GPIO_InitStructure);

    /* PB9 复用推挽输出 (风扇2) */
    GPIO_InitStructure.GPIO_Pin = FAN2_GPIO_PIN;
    GPIO_Init(FAN2_GPIO_PORT, &GPIO_InitStructure);

    /* TIM4 时基配置: 144MHz / 6 / 960 = 25kHz */
    TIM_TimeBaseStructure.TIM_Period = PWM_ARR;
    TIM_TimeBaseStructure.TIM_Prescaler = PWM_PSC;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(FAN1_TIM, &TIM_TimeBaseStructure);

    /* TIM4 CH3 PWM模式配置 (风扇1) */
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;  /* 默认占空比0% */
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC3Init(FAN1_TIM, &TIM_OCInitStructure);
    TIM_OC3PreloadConfig(FAN1_TIM, TIM_OCPreload_Enable);

    /* TIM4 CH4 PWM模式配置 (风扇2) */
    TIM_OC4Init(FAN1_TIM, &TIM_OCInitStructure);
    TIM_OC4PreloadConfig(FAN1_TIM, TIM_OCPreload_Enable);

    /* 使能预装载 */
    TIM_ARRPreloadConfig(FAN1_TIM, ENABLE);

    /* 使能TIM4 */
    TIM_Cmd(FAN1_TIM, ENABLE);

    fan1_duty = 0;
    fan2_duty = 0;
}

/**
 * @brief  设置风扇1占空比
 * @param  duty: 占空比 0-100 (%)
 */
void fan1_set_duty(uint8_t duty)
{
    if(duty > 100) duty = 100;

    uint16_t compare = (uint16_t)((uint32_t)duty * (PWM_ARR + 1) / 100);
    TIM_SetCompare3(FAN1_TIM, compare);

    fan1_duty = duty;
}

/**
 * @brief  设置风扇2占空比
 * @param  duty: 占空比 0-100 (%)
 */
void fan2_set_duty(uint8_t duty)
{
    if(duty > 100) duty = 100;

    uint16_t compare = (uint16_t)((uint32_t)duty * (PWM_ARR + 1) / 100);
    TIM_SetCompare4(FAN2_TIM, compare);

    fan2_duty = duty;
}

/**
 * @brief  同时设置两个风扇占空比
 * @param  duty: 占空比 0-100 (%)
 */
void fan_set_duty(uint8_t duty)
{
    fan1_set_duty(duty);
    fan2_set_duty(duty);
}

/**
 * @brief  获取风扇1当前占空比
 */
uint8_t fan1_get_duty(void)
{
    return fan1_duty;
}

/**
 * @brief  获取风扇2当前占空比
 */
uint8_t fan2_get_duty(void)
{
    return fan2_duty;
}
