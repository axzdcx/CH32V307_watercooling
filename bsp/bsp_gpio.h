/**
 * @file    bsp_gpio.h
 * @brief   GPIO底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2025-01-22
 *
 * LED引脚: PC2 (开发板板载LED，active low)
 */

#ifndef __BSP_GPIO_H
#define __BSP_GPIO_H

#include "ch32v30x.h"

/*============================ LED 定义 ============================*/

/* 板载LED - PC2 (低电平点亮) */
#define LED_PIN             GPIO_Pin_2
#define LED_GPIO_PORT       GPIOC
#define LED_GPIO_CLK        RCC_APB2Periph_GPIOC

/* LED 状态 */
#define LED_ON              0
#define LED_OFF             1

/*============================ GPIO 模式定义 ============================*/

typedef enum {
    GPIO_MODE_INPUT,            /* 浮空输入 */
    GPIO_MODE_INPUT_PU,         /* 上拉输入 */
    GPIO_MODE_INPUT_PD,         /* 下拉输入 */
    GPIO_MODE_OUTPUT_PP,        /* 推挽输出 */
    GPIO_MODE_OUTPUT_OD,        /* 开漏输出 */
    GPIO_MODE_AF_PP,            /* 复用推挽 */
    GPIO_MODE_AF_OD,            /* 复用开漏 */
    GPIO_MODE_ANALOG            /* 模拟输入 */
} gpio_mode_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  LED初始化
 * @note   PC2 推挽输出，默认熄灭
 */
void led_init(void);

/**
 * @brief  点亮LED
 */
void led_on(void);

/**
 * @brief  熄灭LED
 */
void led_off(void);

/**
 * @brief  翻转LED状态
 */
void led_toggle(void);

/**
 * @brief  设置LED状态
 * @param  state: LED_ON 或 LED_OFF
 */
void led_set(uint8_t state);

/**
 * @brief  通用GPIO初始化
 * @param  GPIOx: GPIO端口 (GPIOA, GPIOB, GPIOC...)
 * @param  pin: GPIO引脚 (GPIO_Pin_0 ~ GPIO_Pin_15)
 * @param  mode: GPIO模式 (gpio_mode_t)
 */
void gpio_init(GPIO_TypeDef *GPIOx, uint16_t pin, gpio_mode_t mode);

/**
 * @brief  设置GPIO引脚为高电平
 * @param  GPIOx: GPIO端口
 * @param  pin: GPIO引脚
 */
void gpio_set(GPIO_TypeDef *GPIOx, uint16_t pin);

/**
 * @brief  设置GPIO引脚为低电平
 * @param  GPIOx: GPIO端口
 * @param  pin: GPIO引脚
 */
void gpio_reset(GPIO_TypeDef *GPIOx, uint16_t pin);

/**
 * @brief  翻转GPIO引脚电平
 * @param  GPIOx: GPIO端口
 * @param  pin: GPIO引脚
 */
void gpio_toggle(GPIO_TypeDef *GPIOx, uint16_t pin);

/**
 * @brief  读取GPIO引脚电平
 * @param  GPIOx: GPIO端口
 * @param  pin: GPIO引脚
 * @return 0-低电平  1-高电平
 */
uint8_t gpio_read(GPIO_TypeDef *GPIOx, uint16_t pin);

/**
 * @brief  写GPIO引脚电平
 * @param  GPIOx: GPIO端口
 * @param  pin: GPIO引脚
 * @param  value: 0-低电平  1-高电平
 */
void gpio_write(GPIO_TypeDef *GPIOx, uint16_t pin, uint8_t value);

#endif /* __BSP_GPIO_H */
