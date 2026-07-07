/**
 * @file    algo_energy.h
 * @brief   能效计算算法 — 功耗/电量/碳排放/PUE追踪
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 功能：
 *   - 实时功耗计算（基于ACS712电流传感器）
 *   - 累计电量统计（Wh/kWh）
 *   - 碳排放量化（kg CO2，中国电网排放因子0.57 kgCO2/kWh）
 *   - PUE估算（Power Usage Effectiveness）
 *   - 能效等级评估
 *
 * 资源占用：
 *   RAM: ~128B
 *   计算时间: <0.1ms
 */

#ifndef __ALGO_ENERGY_H
#define __ALGO_ENERGY_H

#include <stdint.h>

/*============================ 配置 ============================*/

#define ENERGY_SUPPLY_VOLTAGE   12.0f   /* 系统供电电压 V */
#define ENERGY_CARBON_FACTOR    0.5703f /* 中国电网CO2排放因子 kgCO2/kWh (2022) */
#define ENERGY_ELECTRICITY_PRICE 0.56f  /* 电价 元/kWh（工业用电参考） */

/*============================ 能效等级 ============================*/

typedef enum {
    ENERGY_LEVEL_EXCELLENT = 0, /* 待机/低功耗 */
    ENERGY_LEVEL_GOOD,          /* 正常运行 */
    ENERGY_LEVEL_FAIR,          /* 中等负载 */
    ENERGY_LEVEL_HIGH,          /* 高负载 */
    ENERGY_LEVEL_OVERLOAD       /* 过载 */
} energy_level_t;

/*============================ 数据结构 ============================*/

/**
 * @brief  能效数据
 */
typedef struct {
    /* 实时数据 */
    float current_ma;           /* 当前电流 mA */
    float power_w;              /* 当前功率 W */

    /* 累计数据 */
    float energy_wh;            /* 累计电量 Wh */
    float energy_kwh;           /* 累计电量 kWh */
    float carbon_kg;            /* 累计碳排放 kg CO2 */
    float cost_yuan;            /* 累计电费 元 */

    /* 统计数据 */
    float avg_power_w;          /* 平均功率 W（滑动窗口） */
    float peak_power_w;         /* 峰值功率 W */
    float pue;                  /* PUE估算值 */

    /* 运行时间 */
    uint32_t runtime_seconds;   /* 累计运行时间 秒 */

    /* 等级 */
    energy_level_t level;       /* 能效等级 */
} energy_data_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化能效计算模块
 */
void energy_init(void);

/**
 * @brief  更新能效数据（建议1s周期调用）
 * @param  current_ma: 当前系统电流 mA（来自ACS712）
 */
void energy_update(float current_ma);

/**
 * @brief  获取能效数据
 * @return 能效数据指针
 */
const energy_data_t* energy_get_data(void);

/**
 * @brief  获取当前功率
 * @return 功率 W
 */
float energy_get_power(void);

/**
 * @brief  获取累计碳排放
 * @return 碳排放 kg CO2
 */
float energy_get_carbon(void);

/**
 * @brief  获取能效等级名称
 * @param  level: 能效等级
 * @return 等级名称
 */
const char* energy_get_level_name(energy_level_t level);

/**
 * @brief  复位累计数据
 */
void energy_reset(void);

#endif /* __ALGO_ENERGY_H */
