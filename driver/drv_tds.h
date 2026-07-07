/**
 * @file    drv_tds.h
 * @brief   TDS水质传感器驱动头文件
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * 硬件连接：
 *   - VCC  → 5V
 *   - GND  → GND
 *   - OUT  → PA1 (ADC1_IN1)
 *
 * 传感器参数：
 *   - 量程：0-1000 ppm
 *   - 输出：0-3.3V 线性输出
 *   - 精度：±10% FS
 *   - 温度补偿：需要温度传感器配合
 *
 * TDS值说明：
 *   - 0-50 ppm：纯净水
 *   - 50-150 ppm：优质水
 *   - 150-300 ppm：良好水
 *   - 300-600 ppm：一般水
 *   - 600-1000 ppm：差水
 *   - >1000 ppm：不可用
 */

#ifndef __DRV_TDS_H
#define __DRV_TDS_H

#include "ch32v30x.h"

/*============================ 配置参数 ============================*/

/* TDS阈值 */
#define TDS_EXCELLENT_MAX       50      /* 纯净水上限 (ppm) */
#define TDS_GOOD_MAX            150     /* 优质水上限 (ppm) */
#define TDS_NORMAL_MAX          300     /* 良好水上限 (ppm) */
#define TDS_ACCEPTABLE_MAX      600     /* 一般水上限 (ppm) */
#define TDS_WARNING_THRESHOLD   300     /* 告警阈值 (ppm) */

/* 滤波参数 */
#define TDS_FILTER_SAMPLES      10      /* 滤波采样次数 */

/* 温度补偿系数 */
#define TDS_TEMP_COEF           0.02    /* 温度补偿系数 (2%/°C) */
#define TDS_TEMP_REF            25.0    /* 参考温度 (°C) */

/*============================ 数据结构 ============================*/

/**
 * @brief  水质等级
 */
typedef enum {
    TDS_LEVEL_EXCELLENT = 0,    /* 纯净水 (0-50 ppm) */
    TDS_LEVEL_GOOD,             /* 优质水 (50-150 ppm) */
    TDS_LEVEL_NORMAL,           /* 良好水 (150-300 ppm) */
    TDS_LEVEL_ACCEPTABLE,       /* 一般水 (300-600 ppm) */
    TDS_LEVEL_POOR,             /* 差水 (600-1000 ppm) */
    TDS_LEVEL_UNUSABLE          /* 不可用 (>1000 ppm) */
} tds_level_t;

/**
 * @brief  TDS数据结构
 */
typedef struct {
    uint16_t tds_ppm;           /* TDS值 (ppm) */
    uint16_t tds_compensated;   /* 温度补偿后的TDS值 (ppm) */
    uint16_t voltage_mv;        /* 电压值 (mV) */
    uint16_t adc_value;         /* ADC原始值 */
    tds_level_t level;          /* 水质等级 */
    uint8_t  is_normal;         /* 是否正常 (1=正常, 0=异常) */
    float    temperature;       /* 当前水温 (°C, 用于温度补偿) */
} tds_data_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化TDS传感器
 * @return 0=成功, -1=失败
 */
int8_t tds_init(void);

/**
 * @brief  读取TDS值（ppm）
 * @return TDS值 (ppm)
 */
uint16_t tds_read_ppm(void);

/**
 * @brief  读取TDS值（带温度补偿）
 * @param  temperature: 当前水温 (°C)
 * @return 补偿后的TDS值 (ppm)
 */
uint16_t tds_read_compensated(float temperature);

/**
 * @brief  读取完整TDS数据
 * @param  data: TDS数据结构指针
 * @param  temperature: 当前水温 (°C, 用于温度补偿)
 * @return 0=成功, -1=失败
 */
int8_t tds_read_data(tds_data_t *data, float temperature);

/**
 * @brief  获取水质等级
 * @param  tds_ppm: TDS值 (ppm)
 * @return 水质等级
 */
tds_level_t tds_get_level(uint16_t tds_ppm);

/**
 * @brief  获取水质等级字符串
 * @param  level: 水质等级
 * @return 等级字符串
 */
const char* tds_get_level_str(tds_level_t level);

/**
 * @brief  检查水质是否正常
 * @return 1=正常, 0=异常
 */
uint8_t tds_is_normal(void);

/**
 * @brief  TDS传感器自检
 * @return 0=正常, -1=故障
 */
int8_t tds_self_test(void);

#endif /* __DRV_TDS_H */
