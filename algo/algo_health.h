/**
 * @file    algo_health.h
 * @brief   健康评分算法 — 6维加权评分系统
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 评分维度与权重：
 *   温度(25%) + 流量(20%) + 压力(15%) + 振动(15%) + 水质(10%) + 效率(15%)
 *
 * 每个维度独立评分 0~100，加权求和得到总分 0~100
 * 总分含义：
 *   90-100: 优秀（绿色）
 *   70-89:  良好（蓝色）
 *   50-69:  一般（黄色）
 *   30-49:  较差（橙色）
 *   0-29:   危险（红色）
 */

#ifndef __ALGO_HEALTH_H
#define __ALGO_HEALTH_H

#include <stdint.h>

/*============================ 健康等级定义 ============================*/

typedef enum {
    HEALTH_LEVEL_EXCELLENT = 0, /* 90-100 优秀 */
    HEALTH_LEVEL_GOOD,          /* 70-89  良好 */
    HEALTH_LEVEL_FAIR,          /* 50-69  一般 */
    HEALTH_LEVEL_POOR,          /* 30-49  较差 */
    HEALTH_LEVEL_DANGER         /* 0-29   危险 */
} health_level_t;

/*============================ 权重配置 ============================*/

#define HEALTH_DIM_COUNT        6       /* 评分维度数量 */

#define HEALTH_WEIGHT_TEMP      25      /* 温度权重 % */
#define HEALTH_WEIGHT_FLOW      20      /* 流量权重 % */
#define HEALTH_WEIGHT_PRESSURE  15      /* 压力权重 % */
#define HEALTH_WEIGHT_VIBRATION 15      /* 振动权重 % */
#define HEALTH_WEIGHT_WATER     10      /* 水质权重 % */
#define HEALTH_WEIGHT_EFFICIENCY 15     /* 效率权重 % */

/*============================ 数据结构 ============================*/

/**
 * @brief  健康评分输入（传感器数据）
 */
typedef struct {
    float cpu_temp;             /* CPU温度 °C */
    float water_temp;           /* 水温 °C */
    float flow_rate;            /* 流量 L/min */
    float pressure;             /* 水压 MPa */
    float vibration_rms;        /* 振动RMS g */
    uint16_t tds_ppm;           /* TDS ppm */
    uint8_t pump_pwm;           /* 水泵PWM % */
} health_input_t;

/**
 * @brief  健康评分输出
 */
typedef struct {
    float total_score;          /* 总分 0~100 */
    float dim_scores[HEALTH_DIM_COUNT]; /* 各维度分数 0~100 */
    health_level_t level;       /* 健康等级 */
} health_output_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化健康评分系统
 */
void health_init(void);

/**
 * @brief  更新健康评分（建议2s周期调用）
 * @param  input: 传感器数据
 */
void health_update(const health_input_t *input);

/**
 * @brief  获取健康评分结果
 * @return 评分结果指针
 */
const health_output_t* health_get_result(void);

/**
 * @brief  获取总分
 * @return 总分 0~100
 */
float health_get_score(void);

/**
 * @brief  获取健康等级
 * @return 健康等级
 */
health_level_t health_get_level(void);

/**
 * @brief  获取健康等级名称
 * @param  level: 健康等级
 * @return 等级名称字符串
 */
const char* health_get_level_name(health_level_t level);

#endif /* __ALGO_HEALTH_H */
