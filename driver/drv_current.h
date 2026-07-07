/**
 * @file    drv_current.h
 * @brief   ACS712电流传感器驱动头文件
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * 硬件连接 (v3.0)：
 *   - VCC  → 5V
 *   - GND  → GND
 *   - OUT  → PB15 (OPA1_CHP0正输入) → 内置OPA1缓冲 → PA1 (OPA1_OUT0 → ADC1_IN1)
 *
 * 传感器参数：
 *   - 型号：ACS712-5A (或霍尔电流传感器)
 *   - 量程：0-5A
 *   - 输出：0-5V (2.5V为零点)
 *   - 灵敏度：185mV/A (5A型号)
 *   - 精度：±1.5%
 *
 * 电流等级说明：
 *   - 0-0.5A：待机/低功耗
 *   - 0.5-2A：正常运行
 *   - 2-4A：高负载
 *   - 4-5A：过载告警
 *   - >5A：危险（需立即断电）
 */

#ifndef __DRV_CURRENT_H
#define __DRV_CURRENT_H

#include "ch32v30x.h"

/*============================ 配置参数 ============================*/

/* 电流阈值 */
#define CURRENT_STANDBY_MAX     500     /* 待机上限 (mA) */
#define CURRENT_NORMAL_MAX      2000    /* 正常上限 (mA) */
#define CURRENT_HIGH_MAX        4000    /* 高负载上限 (mA) */
#define CURRENT_OVERLOAD_MAX    5000    /* 过载上限 (mA) */
#define CURRENT_WARNING_THRESHOLD 4000  /* 告警阈值 (mA) */

/* 滤波参数 */
#define CURRENT_FILTER_SAMPLES  20      /* 滤波采样次数 */

/* 传感器参数（与bsp_adc.h中定义一致，此处保留用于drv_current内部引用） */
#define CURRENT_ZERO_MV         2500    /* 零点电压 2.5V (mV) */
#define CURRENT_SENSITIVITY_MV  185     /* 灵敏度 185mV/A */

/*============================ 数据结构 ============================*/

/**
 * @brief  电流等级
 */
typedef enum {
    CURRENT_LEVEL_STANDBY = 0,  /* 待机 (0-0.5A) */
    CURRENT_LEVEL_NORMAL,       /* 正常 (0.5-2A) */
    CURRENT_LEVEL_HIGH,         /* 高负载 (2-4A) */
    CURRENT_LEVEL_OVERLOAD,     /* 过载 (4-5A) */
    CURRENT_LEVEL_DANGER        /* 危险 (>5A) */
} current_level_t;

/**
 * @brief  电流数据结构
 */
typedef struct {
    uint16_t current_ma;        /* 电流值 (mA) */
    uint16_t voltage_mv;        /* 电压值 (mV) */
    uint16_t adc_value;         /* ADC原始值 */
    current_level_t level;      /* 电流等级 */
    uint8_t  is_normal;         /* 是否正常 (1=正常, 0=异常) */
    uint8_t  is_overload;       /* 是否过载 (1=过载, 0=正常) */
} current_data_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化电流传感器
 * @return 0=成功, -1=失败
 */
int8_t current_init(void);

/**
 * @brief  读取电流值（mA）
 * @return 电流值 (mA)
 */
uint16_t current_read_ma(void);

/**
 * @brief  读取完整电流数据
 * @param  data: 电流数据结构指针
 * @return 0=成功, -1=失败
 */
int8_t current_read_data(current_data_t *data);

/**
 * @brief  获取电流等级
 * @param  current_ma: 电流值 (mA)
 * @return 电流等级
 */
current_level_t current_get_level(uint16_t current_ma);

/**
 * @brief  获取电流等级字符串
 * @param  level: 电流等级
 * @return 等级字符串
 */
const char* current_get_level_str(current_level_t level);

/**
 * @brief  检查电流是否正常
 * @return 1=正常, 0=异常
 */
uint8_t current_is_normal(void);

/**
 * @brief  电流传感器自检
 * @return 0=正常, -1=故障
 */
int8_t current_self_test(void);

/**
 * @brief  电流传感器校准（零点校准）
 * @note   在无电流时调用，校准零点偏移
 * @return 0=成功, -1=失败
 */
int8_t current_calibrate(void);

#endif /* __DRV_CURRENT_H */
