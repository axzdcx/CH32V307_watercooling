/**
 * @file    drv_ws2812b.h
 * @brief   WS2812B RGB LED灯带驱动头文件
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * 硬件连接：
 *   - VCC  → 5V
 *   - GND  → GND
 *   - DIN  → PB5 (GPIO)
 *
 * 通信协议：
 *   - 单线通信，使用GPIO模拟WS2812B时序
 *   - 0码：高电平0.4us，低电平0.85us (总1.25us)
 *   - 1码：高电平0.8us，低电平0.45us (总1.25us)
 *   - 复位：低电平>50us
 *
 * LED数量：
 *   - 默认支持10颗LED（可修改WS2812B_LED_NUM）
 *   - 每颗LED 3字节 (GRB顺序)
 */

#ifndef __DRV_WS2812B_H
#define __DRV_WS2812B_H

#include "ch32v30x.h"

/*============================ 引脚定义 ============================*/

#define WS2812B_GPIO_CLK    RCC_APB2Periph_GPIOB
#define WS2812B_GPIO_PORT   GPIOB
#define WS2812B_GPIO_PIN    GPIO_Pin_5

/* 引脚操作宏 */
#define WS2812B_DIN_HIGH()  GPIO_SetBits(WS2812B_GPIO_PORT, WS2812B_GPIO_PIN)
#define WS2812B_DIN_LOW()   GPIO_ResetBits(WS2812B_GPIO_PORT, WS2812B_GPIO_PIN)

/*============================ 配置参数 ============================*/

/* LED数量 */
#define WS2812B_LED_NUM         10

/* 颜色定义 */
#define WS2812B_COLOR_OFF       0x000000    /* 关闭 */
#define WS2812B_COLOR_RED       0xFF0000    /* 红色 */
#define WS2812B_COLOR_GREEN     0x00FF00    /* 绿色 */
#define WS2812B_COLOR_BLUE      0x0000FF    /* 蓝色 */
#define WS2812B_COLOR_YELLOW    0xFFFF00    /* 黄色 */
#define WS2812B_COLOR_CYAN      0x00FFFF    /* 青色 */
#define WS2812B_COLOR_MAGENTA   0xFF00FF    /* 品红 */
#define WS2812B_COLOR_WHITE     0xFFFFFF    /* 白色 */
#define WS2812B_COLOR_ORANGE    0xFF8000    /* 橙色 */
#define WS2812B_COLOR_PURPLE    0x8000FF    /* 紫色 */

/* 亮度等级 */
#define WS2812B_BRIGHTNESS_OFF      0       /* 关闭 */
#define WS2812B_BRIGHTNESS_LOW      64      /* 低亮度 (25%) */
#define WS2812B_BRIGHTNESS_MEDIUM   128     /* 中亮度 (50%) */
#define WS2812B_BRIGHTNESS_HIGH     192     /* 高亮度 (75%) */
#define WS2812B_BRIGHTNESS_MAX      255     /* 最大亮度 (100%) */

/*============================ 数据结构 ============================*/

/**
 * @brief  RGB颜色结构
 */
typedef struct {
    uint8_t r;      /* 红色分量 0-255 */
    uint8_t g;      /* 绿色分量 0-255 */
    uint8_t b;      /* 蓝色分量 0-255 */
} ws2812b_rgb_t;

/**
 * @brief  HSV颜色结构
 */
typedef struct {
    uint16_t h;     /* 色调 0-360 */
    uint8_t s;      /* 饱和度 0-255 */
    uint8_t v;      /* 明度 0-255 */
} ws2812b_hsv_t;

/**
 * @brief  LED效果模式
 */
typedef enum {
    WS2812B_MODE_STATIC = 0,    /* 静态颜色 */
    WS2812B_MODE_BREATH,        /* 呼吸灯 */
    WS2812B_MODE_RAINBOW,       /* 彩虹流动 */
    WS2812B_MODE_CHASE,         /* 追逐效果 */
    WS2812B_MODE_TWINKLE,       /* 闪烁效果 */
    WS2812B_MODE_GRADIENT,      /* 渐变效果 */
    WS2812B_MODE_TEMPERATURE    /* 温度指示 */
} ws2812b_mode_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化WS2812B
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_init(void);

/**
 * @brief  设置单个LED颜色
 * @param  index: LED索引 (0 ~ WS2812B_LED_NUM-1)
 * @param  color: 24位RGB颜色值 (0xRRGGBB)
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_set_led(uint16_t index, uint32_t color);

/**
 * @brief  设置单个LED颜色（RGB分量）
 * @param  index: LED索引
 * @param  r: 红色 0-255
 * @param  g: 绿色 0-255
 * @param  b: 蓝色 0-255
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_set_led_rgb(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  设置所有LED为同一颜色
 * @param  color: 24位RGB颜色值
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_set_all(uint32_t color);

/**
 * @brief  设置所有LED为同一颜色（RGB分量）
 * @param  r: 红色 0-255
 * @param  g: 绿色 0-255
 * @param  b: 蓝色 0-255
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_set_all_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  清除所有LED（关闭）
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_clear(void);

/**
 * @brief  刷新显示（发送数据到LED）
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_refresh(void);

/**
 * @brief  设置全局亮度
 * @param  brightness: 亮度 0-255
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_set_brightness(uint8_t brightness);

/**
 * @brief  HSV转RGB
 * @param  hsv: HSV颜色
 * @param  rgb: RGB颜色输出
 */
void ws2812b_hsv_to_rgb(ws2812b_hsv_t *hsv, ws2812b_rgb_t *rgb);

/**
 * @brief  调整颜色亮度
 * @param  color: 原始颜色
 * @param  brightness: 亮度 0-255
 * @return 调整后的颜色
 */
uint32_t ws2812b_adjust_brightness(uint32_t color, uint8_t brightness);

/**
 * @brief  彩虹效果
 * @param  offset: 偏移量（用于动画）
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_rainbow(uint16_t offset);

/**
 * @brief  呼吸灯效果
 * @param  color: 颜色
 * @param  step: 呼吸步进（0-255）
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_breath(uint32_t color, uint8_t step);

/**
 * @brief  温度指示效果
 * @param  temperature: 温度值 (°C)
 * @param  min_temp: 最低温度
 * @param  max_temp: 最高温度
 * @return 0=成功, -1=失败
 * @note   蓝色(冷) → 绿色(适中) → 红色(热)
 */
int8_t ws2812b_temperature(float temperature, float min_temp, float max_temp);

/**
 * @brief  流水灯效果
 * @param  color: 颜色
 * @param  position: 位置（0 ~ WS2812B_LED_NUM-1）
 * @param  length: 长度
 * @return 0=成功, -1=失败
 */
int8_t ws2812b_chase(uint32_t color, uint16_t position, uint16_t length);

#endif /* __DRV_WS2812B_H */
