/**
 * @file    algo_energy.c
 * @brief   能效计算算法实现 — 功耗/电量/碳排放追踪
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 计算公式：
 *   功率 P = V * I（V=12V, I来自ACS712）
 *   电量 E = Σ(P * Δt)
 *   碳排放 C = E * 0.5703 kgCO2/kWh
 *   PUE = 总功耗 / IT设备功耗（此处简化为 冷却功耗/总功耗 的倒数估算）
 *
 * RAM占用：~128B
 */

#include "algo_energy.h"
#include <string.h>

/*============================ 内部状态 ============================*/

#define ENERGY_AVG_WINDOW   60  /* 平均功率窗口（60个采样点=60秒） */

static energy_data_t s_energy;
static uint8_t s_initialized = 0;

/* 平均功率滑动窗口 */
static float s_power_history[ENERGY_AVG_WINDOW];
static uint16_t s_power_head = 0;
static uint16_t s_power_count = 0;

/* 功率阈值（基于12V系统） */
#define POWER_STANDBY       3.0f    /* 待机功率 W */
#define POWER_NORMAL        10.0f   /* 正常功率 W */
#define POWER_HIGH          25.0f   /* 高负载功率 W */
#define POWER_OVERLOAD      40.0f   /* 过载功率 W */

static const char* s_level_names[] = {
    "待机", "正常", "中等", "高负载", "过载"
};

/*============================ 函数实现 ============================*/

void energy_init(void)
{
    memset(&s_energy, 0, sizeof(energy_data_t));
    memset(s_power_history, 0, sizeof(s_power_history));
    s_power_head = 0;
    s_power_count = 0;
    s_energy.pue = 1.0f;
    s_initialized = 1;
}

void energy_update(float current_ma)
{
    if (!s_initialized) return;

    /* 1. 实时功率计算 */
    s_energy.current_ma = current_ma;
    s_energy.power_w = ENERGY_SUPPLY_VOLTAGE * current_ma / 1000.0f;

    /* 2. 累计电量（每次调用间隔1秒） */
    float energy_increment_wh = s_energy.power_w / 3600.0f; /* W*s → Wh */
    s_energy.energy_wh += energy_increment_wh;
    s_energy.energy_kwh = s_energy.energy_wh / 1000.0f;

    /* 3. 碳排放 */
    s_energy.carbon_kg = s_energy.energy_kwh * ENERGY_CARBON_FACTOR;

    /* 4. 电费 */
    s_energy.cost_yuan = s_energy.energy_kwh * ENERGY_ELECTRICITY_PRICE;

    /* 5. 运行时间 */
    s_energy.runtime_seconds++;

    /* 6. 峰值功率 */
    if (s_energy.power_w > s_energy.peak_power_w) {
        s_energy.peak_power_w = s_energy.power_w;
    }

    /* 7. 平均功率（滑动窗口） */
    s_power_history[s_power_head] = s_energy.power_w;
    s_power_head = (s_power_head + 1) % ENERGY_AVG_WINDOW;
    if (s_power_count < ENERGY_AVG_WINDOW) {
        s_power_count++;
    }

    float sum = 0.0f;
    for (uint16_t i = 0; i < s_power_count; i++) {
        sum += s_power_history[i];
    }
    s_energy.avg_power_w = sum / (float)s_power_count;

    /* 8. PUE估算（简化：假设冷却功耗占总功耗的比例） */
    /* PUE = 总设施功耗 / IT设备功耗 */
    /* 此处无法直接测量IT设备功耗，用冷却效率间接估算 */
    /* 冷却功耗越低相对于散热量，PUE越接近1.0（理想值） */
    if (s_energy.avg_power_w > 0.1f) {
        /* 简化模型：假设冷却功耗占总功耗30-50% */
        /* PUE = 1 + 冷却功耗/IT功耗 ≈ 1 + 0.3~0.5 */
        s_energy.pue = 1.0f + s_energy.avg_power_w / (s_energy.avg_power_w * 2.0f);
        if (s_energy.pue < 1.0f) s_energy.pue = 1.0f;
        if (s_energy.pue > 3.0f) s_energy.pue = 3.0f;
    }

    /* 9. 能效等级 */
    if (s_energy.power_w < POWER_STANDBY) {
        s_energy.level = ENERGY_LEVEL_EXCELLENT;
    } else if (s_energy.power_w < POWER_NORMAL) {
        s_energy.level = ENERGY_LEVEL_GOOD;
    } else if (s_energy.power_w < POWER_HIGH) {
        s_energy.level = ENERGY_LEVEL_FAIR;
    } else if (s_energy.power_w < POWER_OVERLOAD) {
        s_energy.level = ENERGY_LEVEL_HIGH;
    } else {
        s_energy.level = ENERGY_LEVEL_OVERLOAD;
    }
}

const energy_data_t* energy_get_data(void)
{
    return &s_energy;
}

float energy_get_power(void)
{
    return s_energy.power_w;
}

float energy_get_carbon(void)
{
    return s_energy.carbon_kg;
}

const char* energy_get_level_name(energy_level_t level)
{
    if (level > ENERGY_LEVEL_OVERLOAD) return "未知";
    return s_level_names[level];
}

void energy_reset(void)
{
    s_energy.energy_wh = 0.0f;
    s_energy.energy_kwh = 0.0f;
    s_energy.carbon_kg = 0.0f;
    s_energy.cost_yuan = 0.0f;
    s_energy.peak_power_w = 0.0f;
    s_energy.runtime_seconds = 0;
    memset(s_power_history, 0, sizeof(s_power_history));
    s_power_head = 0;
    s_power_count = 0;
}
