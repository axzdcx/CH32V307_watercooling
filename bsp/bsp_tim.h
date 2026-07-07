/**
 * @file    bsp_tim.h
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

#ifndef __BSP_TIM_H
#define __BSP_TIM_H

#include "ch32v30x.h"

/*============================ 宏定义 ============================*/

/* 水泵 PWM - TIM1_CH1 (PA8) */
#define PUMP_TIM            TIM1
#define PUMP_TIM_CLK        RCC_APB2Periph_TIM1
#define PUMP_GPIO_PORT      GPIOA
#define PUMP_GPIO_CLK       RCC_APB2Periph_GPIOA
#define PUMP_GPIO_PIN       GPIO_Pin_8

/* 风扇1 PWM - TIM4_CH3 (PB8) */
#define FAN1_TIM            TIM4
#define FAN1_TIM_CLK        RCC_APB1Periph_TIM4
#define FAN1_GPIO_PORT      GPIOB
#define FAN1_GPIO_CLK       RCC_APB2Periph_GPIOB
#define FAN1_GPIO_PIN       GPIO_Pin_8

/* 风扇2 PWM - TIM4_CH4 (PB9) */
#define FAN2_TIM            TIM4
#define FAN2_GPIO_PORT      GPIOB
#define FAN2_GPIO_CLK       RCC_APB2Periph_GPIOB
#define FAN2_GPIO_PIN       GPIO_Pin_9

/* PWM 参数 */
#define PWM_FREQ            25000   /* PWM频率 25kHz */
#define PWM_ARR             959     /* 自动重装值 (144MHz / 6 / 960 = 25kHz) */
#define PWM_PSC             5       /* 预分频值 */

/*============================ 函数声明 ============================*/

/*---------------- 系统定时器 ----------------*/

/**
 * @brief  系统1ms定时器初始化
 * @note   TIM2, 1kHz中断 (1ms)
 */
void tim2_tick_init(void);

/**
 * @brief  获取系统运行毫秒数
 * @return 系统运行毫秒数
 */
uint32_t get_tick_ms(void);

/*---------------- 水泵 PWM ----------------*/

/**
 * @brief  水泵PWM初始化
 * @note   TIM1_CH1 (PA8), 25kHz, 默认占空比0%
 */
void pump_pwm_init(void);

/**
 * @brief  设置水泵占空比
 * @param  duty: 占空比 0-100 (%)
 */
void pump_set_duty(uint8_t duty);

/**
 * @brief  获取水泵当前占空比
 * @return 占空比 0-100 (%)
 */
uint8_t pump_get_duty(void);

/*---------------- 风扇 PWM ----------------*/

/**
 * @brief  风扇PWM初始化
 * @note   TIM4_CH3 (PB8) + TIM4_CH4 (PB9), 25kHz, 默认占空比0%
 */
void fan_pwm_init(void);

/**
 * @brief  设置风扇1占空比
 * @param  duty: 占空比 0-100 (%)
 */
void fan1_set_duty(uint8_t duty);

/**
 * @brief  设置风扇2占空比
 * @param  duty: 占空比 0-100 (%)
 */
void fan2_set_duty(uint8_t duty);

/**
 * @brief  同时设置两个风扇占空比
 * @param  duty: 占空比 0-100 (%)
 */
void fan_set_duty(uint8_t duty);

/**
 * @brief  获取风扇1当前占空比
 * @return 占空比 0-100 (%)
 */
uint8_t fan1_get_duty(void);

/**
 * @brief  获取风扇2当前占空比
 * @return 占空比 0-100 (%)
 */
uint8_t fan2_get_duty(void);

#endif /* __BSP_TIM_H */
