/**
 * @file    bsp_gpio.c
 * @brief   GPIO底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2025-01-22
 *
 * LED引脚: PC2 (开发板板载LED，active low)
 */

#include "bsp_gpio.h"

/*============================ LED 驱动 ============================*/

/**
 * @brief  LED初始化
 * @note   PC2 推挽输出，默认熄灭
 */
void led_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* 使能GPIOC时钟 */
    RCC_APB2PeriphClockCmd(LED_GPIO_CLK, ENABLE);

    /* PC2 推挽输出 */
    GPIO_InitStructure.GPIO_Pin = LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_GPIO_PORT, &GPIO_InitStructure);

    /* 默认熄灭 (高电平) */
    led_off();
}

/**
 * @brief  点亮LED (低电平点亮)
 */
void led_on(void)
{
    GPIO_ResetBits(LED_GPIO_PORT, LED_PIN);
}

/**
 * @brief  熄灭LED (高电平熄灭)
 */
void led_off(void)
{
    GPIO_SetBits(LED_GPIO_PORT, LED_PIN);
}

/**
 * @brief  翻转LED状态
 */
void led_toggle(void)
{
    if(GPIO_ReadOutputDataBit(LED_GPIO_PORT, LED_PIN))
    {
        GPIO_ResetBits(LED_GPIO_PORT, LED_PIN);
    }
    else
    {
        GPIO_SetBits(LED_GPIO_PORT, LED_PIN);
    }
}

/**
 * @brief  设置LED状态
 * @param  state: LED_ON 或 LED_OFF
 */
void led_set(uint8_t state)
{
    if(state == LED_ON)
    {
        led_on();
    }
    else
    {
        led_off();
    }
}

/*============================ 通用GPIO驱动 ============================*/

/**
 * @brief  使能GPIO端口时钟
 */
static void gpio_clk_enable(GPIO_TypeDef *GPIOx)
{
    if(GPIOx == GPIOA)
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    else if(GPIOx == GPIOB)
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    else if(GPIOx == GPIOC)
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    else if(GPIOx == GPIOD)
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    else if(GPIOx == GPIOE)
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
}

/**
 * @brief  通用GPIO初始化
 * @param  GPIOx: GPIO端口 (GPIOA, GPIOB, GPIOC...)
 * @param  pin: GPIO引脚 (GPIO_Pin_0 ~ GPIO_Pin_15)
 * @param  mode: GPIO模式 (gpio_mode_t)
 */
void gpio_init(GPIO_TypeDef *GPIOx, uint16_t pin, gpio_mode_t mode)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* 使能时钟 */
    gpio_clk_enable(GPIOx);

    GPIO_InitStructure.GPIO_Pin = pin;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    /* 模式转换 */
    switch(mode)
    {
        case GPIO_MODE_INPUT:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
            break;
        case GPIO_MODE_INPUT_PU:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
            break;
        case GPIO_MODE_INPUT_PD:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
            break;
        case GPIO_MODE_OUTPUT_PP:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
            break;
        case GPIO_MODE_OUTPUT_OD:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
            break;
        case GPIO_MODE_AF_PP:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
            break;
        case GPIO_MODE_AF_OD:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
            break;
        case GPIO_MODE_ANALOG:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
            break;
        default:
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
            break;
    }

    GPIO_Init(GPIOx, &GPIO_InitStructure);
}

/**
 * @brief  设置GPIO引脚为高电平
 */
void gpio_set(GPIO_TypeDef *GPIOx, uint16_t pin)
{
    GPIO_SetBits(GPIOx, pin);
}

/**
 * @brief  设置GPIO引脚为低电平
 */
void gpio_reset(GPIO_TypeDef *GPIOx, uint16_t pin)
{
    GPIO_ResetBits(GPIOx, pin);
}

/**
 * @brief  翻转GPIO引脚电平
 */
void gpio_toggle(GPIO_TypeDef *GPIOx, uint16_t pin)
{
    if(GPIO_ReadOutputDataBit(GPIOx, pin))
    {
        GPIO_ResetBits(GPIOx, pin);
    }
    else
    {
        GPIO_SetBits(GPIOx, pin);
    }
}

/**
 * @brief  读取GPIO引脚电平
 * @return 0-低电平  1-高电平
 */
uint8_t gpio_read(GPIO_TypeDef *GPIOx, uint16_t pin)
{
    return GPIO_ReadInputDataBit(GPIOx, pin);
}

/**
 * @brief  写GPIO引脚电平
 * @param  value: 0-低电平  1-高电平
 */
void gpio_write(GPIO_TypeDef *GPIOx, uint16_t pin, uint8_t value)
{
    if(value)
    {
        GPIO_SetBits(GPIOx, pin);
    }
    else
    {
        GPIO_ResetBits(GPIOx, pin);
    }
}
