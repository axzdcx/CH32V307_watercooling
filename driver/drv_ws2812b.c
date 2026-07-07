/**
 * @file    drv_ws2812b.c
 * @brief   WS2812B RGB LED灯带驱动实现（GPIO位操作）
 * @author  智能水冷项目
 * @date    2025-01-30
 */

#include "drv_ws2812b.h"
#include "debug.h"
#include <string.h>

/*============================ 私有变量 ============================*/

static uint8_t g_led_buffer[WS2812B_LED_NUM * 3];      /* RGB缓冲区 (GRB顺序) */
static uint8_t g_brightness = WS2812B_BRIGHTNESS_MAX;  /* 全局亮度 */

/*============================ 私有函数 ============================*/

/**
 * @brief  发送一位数据
 * @param  bit: 0 或 1
 */
static void ws2812b_send_bit(uint8_t bit)
{
    if (bit)
    {
        /* 发送1码：高电平0.8us，低电平0.45us */
        WS2812B_DIN_HIGH();
        Delay_Us(1);        /* 0.8us（实际约1us） */
        WS2812B_DIN_LOW();
        __NOP();
        __NOP();            /* 0.45us */
    }
    else
    {
        /* 发送0码：高电平0.4us，低电平0.85us */
        WS2812B_DIN_HIGH();
        __NOP();
        __NOP();            /* 0.4us */
        WS2812B_DIN_LOW();
        Delay_Us(1);        /* 0.85us（实际约1us） */
    }
}

/**
 * @brief  发送一个字节
 * @param  byte: 要发送的字节
 */
static void ws2812b_send_byte(uint8_t byte)
{
    /* 高位先发送 */
    for (int8_t i = 7; i >= 0; i--)
    {
        ws2812b_send_bit((byte >> i) & 0x01);
    }
}

/**
 * @brief  发送复位信号
 */
static void ws2812b_reset(void)
{
    WS2812B_DIN_LOW();
    Delay_Us(60);  /* 保持低电平>50us */
}

/*============================ 公共函数实现 ============================*/

/**
 * @brief  初始化WS2812B
 */
int8_t ws2812b_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    printf("[WS2812B] 初始化RGB LED灯带...\r\n");
    printf("[WS2812B] 引脚: PB5 (GPIO)\r\n");
    printf("[WS2812B] LED数量: %d\r\n", WS2812B_LED_NUM);

    /* 使能GPIO时钟 */
    RCC_APB2PeriphClockCmd(WS2812B_GPIO_CLK, ENABLE);

    /* 配置为推挽输出 */
    GPIO_InitStructure.GPIO_Pin = WS2812B_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(WS2812B_GPIO_PORT, &GPIO_InitStructure);

    /* 清空缓冲区 */
    memset(g_led_buffer, 0, sizeof(g_led_buffer));

    /* 发送复位信号 */
    ws2812b_clear();
    ws2812b_refresh();

    printf("[WS2812B] ✅ 初始化完成！\r\n");

    return 0;
}

/**
 * @brief  设置单个LED颜色
 */
int8_t ws2812b_set_led(uint16_t index, uint32_t color)
{
    if (index >= WS2812B_LED_NUM)
        return -1;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    return ws2812b_set_led_rgb(index, r, g, b);
}

/**
 * @brief  设置单个LED颜色（RGB分量）
 */
int8_t ws2812b_set_led_rgb(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= WS2812B_LED_NUM)
        return -1;

    /* 应用亮度调整 */
    r = (uint8_t)((uint16_t)r * g_brightness / 255);
    g = (uint8_t)((uint16_t)g * g_brightness / 255);
    b = (uint8_t)((uint16_t)b * g_brightness / 255);

    /* WS2812B使用GRB顺序 */
    uint16_t offset = index * 3;
    g_led_buffer[offset + 0] = g;
    g_led_buffer[offset + 1] = r;
    g_led_buffer[offset + 2] = b;

    return 0;
}

/**
 * @brief  设置所有LED为同一颜色
 */
int8_t ws2812b_set_all(uint32_t color)
{
    for (uint16_t i = 0; i < WS2812B_LED_NUM; i++)
    {
        ws2812b_set_led(i, color);
    }
    return 0;
}

/**
 * @brief  设置所有LED为同一颜色（RGB分量）
 */
int8_t ws2812b_set_all_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t i = 0; i < WS2812B_LED_NUM; i++)
    {
        ws2812b_set_led_rgb(i, r, g, b);
    }
    return 0;
}

/**
 * @brief  清除所有LED
 */
int8_t ws2812b_clear(void)
{
    memset(g_led_buffer, 0, sizeof(g_led_buffer));
    return 0;
}

/**
 * @brief  刷新显示
 */
int8_t ws2812b_refresh(void)
{
    /* 发送所有LED数据 */
    for (uint16_t i = 0; i < WS2812B_LED_NUM * 3; i++)
    {
        ws2812b_send_byte(g_led_buffer[i]);
    }

    /* 发送复位信号 */
    ws2812b_reset();

    return 0;
}

/**
 * @brief  设置全局亮度
 */
int8_t ws2812b_set_brightness(uint8_t brightness)
{
    g_brightness = brightness;
    return 0;
}

/**
 * @brief  HSV转RGB
 */
void ws2812b_hsv_to_rgb(ws2812b_hsv_t *hsv, ws2812b_rgb_t *rgb)
{
    uint16_t h = hsv->h % 360;
    uint8_t s = hsv->s;
    uint8_t v = hsv->v;

    uint8_t region = h / 60;
    uint8_t remainder = (h % 60) * 255 / 60;

    uint8_t p = (v * (255 - s)) / 255;
    uint8_t q = (v * (255 - ((s * remainder) / 255))) / 255;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;

    switch (region)
    {
        case 0:  rgb->r = v; rgb->g = t; rgb->b = p; break;
        case 1:  rgb->r = q; rgb->g = v; rgb->b = p; break;
        case 2:  rgb->r = p; rgb->g = v; rgb->b = t; break;
        case 3:  rgb->r = p; rgb->g = q; rgb->b = v; break;
        case 4:  rgb->r = t; rgb->g = p; rgb->b = v; break;
        default: rgb->r = v; rgb->g = p; rgb->b = q; break;
    }
}

/**
 * @brief  彩虹效果
 */
int8_t ws2812b_rainbow(uint16_t offset)
{
    ws2812b_hsv_t hsv;
    ws2812b_rgb_t rgb;

    for (uint16_t i = 0; i < WS2812B_LED_NUM; i++)
    {
        hsv.h = (i * 360 / WS2812B_LED_NUM + offset) % 360;
        hsv.s = 255;
        hsv.v = 255;

        ws2812b_hsv_to_rgb(&hsv, &rgb);
        ws2812b_set_led_rgb(i, rgb.r, rgb.g, rgb.b);
    }

    return 0;
}

/**
 * @brief  呼吸灯效果
 */
int8_t ws2812b_breath(uint32_t color, uint8_t step)
{
    uint8_t brightness = (step < 128) ? (step * 2) : ((255 - step) * 2);
    ws2812b_set_brightness(brightness);
    ws2812b_set_all(color);
    return 0;
}

/**
 * @brief  温度指示效果
 */
int8_t ws2812b_temperature(float temperature, float min_temp, float max_temp)
{
    /* 将温度映射到色相（蓝色240° → 绿色120° → 红色0°） */
    float ratio = (temperature - min_temp) / (max_temp - min_temp);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    uint16_t hue = 240 - (uint16_t)(ratio * 240);

    ws2812b_hsv_t hsv = {hue, 255, 255};
    ws2812b_rgb_t rgb;
    ws2812b_hsv_to_rgb(&hsv, &rgb);

    ws2812b_set_all_rgb(rgb.r, rgb.g, rgb.b);

    return 0;
}

/**
 * @brief  流水灯效果
 */
int8_t ws2812b_chase(uint32_t color, uint16_t position, uint16_t length)
{
    ws2812b_clear();

    for (uint16_t i = 0; i < length; i++)
    {
        uint16_t index = (position + i) % WS2812B_LED_NUM;
        ws2812b_set_led(index, color);
    }

    return 0;
}
