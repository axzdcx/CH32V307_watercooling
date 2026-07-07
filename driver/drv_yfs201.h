/**
 * @file    drv_yfs201.h
 * @brief   YFS201 水流量传感器驱动头文件
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * 工作原理：
 *   - 水流推动叶轮旋转，霍尔传感器输出脉冲
 *   - 脉冲频率与流量成正比：F(Hz) = K × Q(L/min)
 *   - 通过计数脉冲计算瞬时流量和累计流量
 *
 * 硬件连接：
 *   - 红线(VCC) → 5V
 *   - 黑线(GND) → GND
 *   - 黄线(OUT) → PB0 (外部中断)
 */

#ifndef __DRV_YFS201_H
#define __DRV_YFS201_H

#include "ch32v30x.h"

/*============================ 硬件配置 ============================*/

/* GPIO引脚定义 */
#define YFS201_GPIO_CLK         RCC_APB2Periph_GPIOB
#define YFS201_GPIO_PORT        GPIOB
#define YFS201_GPIO_PIN         GPIO_Pin_0
#define YFS201_EXTI_LINE        EXTI_Line0
#define YFS201_EXTI_IRQn        EXTI0_IRQn
#define YFS201_EXTI_PORT_SOURCE GPIO_PortSourceGPIOB
#define YFS201_EXTI_PIN_SOURCE  GPIO_PinSource0

/*============================ 传感器参数 ============================*/

/* 脉冲系数（根据实际传感器校准）*/
#define YFS201_K_FACTOR         7.5f    /* F(Hz) = 7.5 × Q(L/min) */
#define YFS201_PULSES_PER_LITER 450     /* 每升水的脉冲数（约数，需校准） */

/* 采样周期（毫秒） */
#define YFS201_SAMPLE_PERIOD_MS 1000    /* 1秒采样一次 */

/* 流量报警阈值 */
#define YFS201_FLOW_MIN         0.5f    /* 最小流量 L/min（低于此值报警） */
#define YFS201_FLOW_MAX         15.0f   /* 最大流量 L/min（高于此值报警） */

/*============================ 数据结构 ============================*/

/**
 * @brief  流量传感器数据结构
 */
typedef struct {
    uint32_t pulse_count;       /* 脉冲计数 */
    uint32_t pulse_count_last;  /* 上次采样的脉冲计数 */

    float    flow_rate;         /* 瞬时流量 (L/min) */
    float    flow_rate_avg;     /* 平均流量 (L/min) */
    float    total_volume;      /* 累计流量 (L) */

    uint32_t last_sample_time;  /* 上次采样时间戳 (ms) */
    uint8_t  valid;             /* 数据有效标志 */
    uint8_t  alarm_low;         /* 流量过低报警 */
    uint8_t  alarm_high;        /* 流量过高报警 */
} yfs201_data_t;

typedef struct {
    float k_factor;              /* 频率系数 Hz/(L/min) */
    uint16_t pulses_per_liter;   /* 每升脉冲数 */
} yfs201_calib_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化YFS201流量传感器
 */
void yfs201_init(void);

/**
 * @brief  更新流量数据（定时调用，建议1秒一次）
 * @param  data: 数据结构指针
 */
void yfs201_update(yfs201_data_t *data);

/**
 * @brief  获取瞬时流量
 * @return 流量值 (L/min)
 */
float yfs201_get_flow_rate(void);

/**
 * @brief  获取累计流量
 * @return 累计流量 (L)
 */
float yfs201_get_total_volume(void);

/**
 * @brief  复位累计流量
 */
void yfs201_reset_total_volume(void);

/**
 * @brief  获取脉冲计数
 * @return 脉冲计数值
 */
uint32_t yfs201_get_pulse_count(void);

/**
 * @brief  外部中断处理函数（在中断服务函数中调用）
 */
void yfs201_pulse_handler(void);

void yfs201_set_calib(const yfs201_calib_t *calib);
const yfs201_calib_t* yfs201_get_calib(void);

#endif /* __DRV_YFS201_H */
