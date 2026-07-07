/**
 * @file    bsp_adc.h
 * @brief   ADC底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2025-01-23
 *
 * ADC通道分配 (v3.0):
 *   ADC1_IN0 (PA0) - HK1100C压力传感器 (经10K+6.8K分压, 0.2-1.82V)
 *   ADC1_IN1 (PA1) - ACS712电流 (经内置OPA1缓冲输出)
 *   ADC1_IN2 (PA2) - TDS水质传感器 0-3.3V → 0-1000ppm
 *   ADC1_CH16       - 片内温度传感器 (PCB板温监测)
 *
 * 采集方式: ADC1 + DMA连续采集
 */

#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include "ch32v30x.h"

/*============================ ADC通道定义 ============================*/

#define ADC_CHANNEL_NUM         4           /* ADC通道数量 */

/* 通道索引 */
#define ADC_CH_PRESSURE         0           /* 压力传感器通道索引 (PA0) */
#define ADC_CH_CURRENT          1           /* 电流检测通道索引 (PA1, 经OPA1) */
#define ADC_CH_TDS              2           /* TDS传感器通道索引 (PA2) */
#define ADC_CH_CHIP_TEMP        3           /* 片内温度通道索引 (CH16) */

/* ADC通道号 */
#define ADC_CHANNEL_PRESSURE    ADC_Channel_0   /* PA0 - HK1100C压力 */
#define ADC_CHANNEL_CURRENT     ADC_Channel_1   /* PA1 - ACS712(经OPA1输出) */
#define ADC_CHANNEL_TDS         ADC_Channel_2   /* PA2 - TDS水质 */
#define ADC_CHANNEL_CHIP_TEMP   ADC_Channel_16  /* 片内温度传感器 */

/* ADC参数 */
#define ADC_RESOLUTION          4096        /* 12位ADC分辨率 */
#define ADC_VREF_MV             3300        /* 参考电压 3.3V (mV) */

/*============================ 传感器参数 ============================*/

/* HK1100C压力传感器: 0.5-4.5V输出, 经10K+6.8K分压后0.2-1.82V, 0-1.2MPa */
#define PRESSURE_VOLTAGE_MIN    200         /* 分压后最小电压 0.2V (mV) */
#define PRESSURE_VOLTAGE_MAX    1820        /* 分压后最大电压 1.82V (mV) */
#define PRESSURE_MAX_KPA        1200        /* 最大压力 1.2MPa = 1200kPa */

/* TDS传感器: 0-3.3V → 0-1000ppm */
#define TDS_VOLTAGE_MAX         3300        /* 最大电压 3.3V (mV) */
#define TDS_MAX_PPM             1000        /* 最大TDS值 1000ppm */

/* ACS712-5A电流传感器: 经内置OPA1缓冲后输入ADC1_IN1 */
#define CURRENT_ZERO_MV         2500        /* ACS712零点电压 2.5V (mV) */
#define CURRENT_SENSITIVITY_MV  185         /* ACS712灵敏度 185mV/A */
#define CURRENT_MAX_MA          5000        /* 最大电流 5A = 5000mA */

/*============================ 标定参数 ============================*/

typedef struct {
    int16_t pressure_offset_mv;    /* 压力零点偏移 (mV) */
    int16_t current_offset_mv;     /* 电流零点偏移 (mV) */
    uint16_t current_sensitivity_mv; /* 电流灵敏度 (mV/A) */
} calib_adc_t;

void calib_adc_set(const calib_adc_t *cfg);
const calib_adc_t* calib_adc_get(void);

/*============================ 函数声明 ============================*/

/* ADC初始化 */
void adc_init(void);

/* 原始ADC值读取 */
uint16_t adc_get_value(uint8_t channel_index);

/* 电压值读取 (mV) */
uint16_t adc_get_voltage(uint8_t channel_index);

/* 传感器数据读取 */
uint16_t pressure_get_value(void);      /* 压力值 (kPa) */
uint16_t current_get_value(void);       /* 电流值 (mA) */
uint16_t tds_get_value(void);           /* TDS值 (ppm) */
int16_t  chip_temp_get_value(void);     /* 片内温度 (℃) */

/* 多次采样取平均 */
uint16_t adc_get_average(uint8_t channel_index, uint8_t times);

#endif /* __BSP_ADC_H */
