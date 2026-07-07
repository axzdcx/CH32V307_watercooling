/**
 * @file    algo_health.c
 * @brief   健康评分算法实现 — 6维加权评分
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 每个维度使用分段线性映射，将传感器值映射到 0~100 分
 * 总分 = Σ(维度分数 × 权重)
 * RAM占用：约 256B
 */

#include "algo_health.h"
#include <string.h>

/*============================ 内部状态 ============================*/

static health_output_t s_health_output;
static uint8_t s_initialized = 0;

/* 权重表（百分比，总和=100） */
static const uint8_t s_weights[HEALTH_DIM_COUNT] = {
    HEALTH_WEIGHT_TEMP,
    HEALTH_WEIGHT_FLOW,
    HEALTH_WEIGHT_PRESSURE,
    HEALTH_WEIGHT_VIBRATION,
    HEALTH_WEIGHT_WATER,
    HEALTH_WEIGHT_EFFICIENCY
};

static const char* s_level_names[] = {
    "优秀", "良好", "一般", "较差", "危险"
};

/*============================ 辅助函数 ============================*/

/**
 * @brief  限制浮点数范围
 */
static float clampf(float val, float min, float max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/**
 * @brief  分段线性映射
 *         value在[good, perfect]之间 → 分数在[80, 100]
 *         value在[warn, good]之间   → 分数在[50, 80]
 *         value在[bad, warn]之间    → 分数在[20, 50]
 *         value超出bad              → 分数在[0, 20]
 *
 * @param  value: 输入值
 * @param  perfect: 完美值（得100分）
 * @param  good: 良好值（得80分）
 * @param  warn: 警告值（得50分）
 * @param  bad: 危险值（得20分）
 * @param  invert: 1=值越小越好（如温度），0=值越大越好（如流量）
 */
static float score_piecewise(float value, float perfect, float good,
                             float warn, float bad, uint8_t invert)
{
    if (invert) {
        /* 值越小越好：perfect < good < warn < bad */
        if (value <= perfect) return 100.0f;
        if (value <= good) return 80.0f + 20.0f * (good - value) / (good - perfect);
        if (value <= warn) return 50.0f + 30.0f * (warn - value) / (warn - good);
        if (value <= bad)  return 20.0f + 30.0f * (bad - value) / (bad - warn);
        return clampf(20.0f * (1.0f - (value - bad) / bad), 0.0f, 20.0f);
    } else {
        /* 值越大越好：perfect > good > warn > bad */
        if (value >= perfect) return 100.0f;
        if (value >= good) return 80.0f + 20.0f * (value - good) / (perfect - good);
        if (value >= warn) return 50.0f + 30.0f * (value - warn) / (good - warn);
        if (value >= bad)  return 20.0f + 30.0f * (value - bad) / (warn - bad);
        return clampf(20.0f * value / bad, 0.0f, 20.0f);
    }
}

/*============================ 各维度评分 ============================*/

/**
 * @brief  温度评分（25%）
 *         CPU温度越低越好
 *         <45°C=100, 45-60=80, 60-75=50, 75-85=20, >85=0
 */
static float score_temperature(float cpu_temp)
{
    return score_piecewise(cpu_temp, 45.0f, 60.0f, 75.0f, 85.0f, 1);
}

/**
 * @brief  流量评分（20%）
 *         流量在正常范围内越稳定越好
 *         >4 L/min=100, 3-4=80, 1.5-3=50, 0.5-1.5=20, <0.5=0
 */
static float score_flow(float flow_rate)
{
    return score_piecewise(flow_rate, 4.0f, 3.0f, 1.5f, 0.5f, 0);
}

/**
 * @brief  压力评分（15%）
 *         压力在正常范围内
 *         0.1-0.3 MPa 为正常范围
 */
static float score_pressure(float pressure)
{
    /* 压力过低或过高都不好，取距离理想值0.2MPa的偏差 */
    float ideal = 0.2f;
    float deviation = (pressure - ideal) < 0 ? (ideal - pressure) : (pressure - ideal);
    /* 偏差越小越好 */
    return score_piecewise(deviation, 0.02f, 0.05f, 0.10f, 0.15f, 1);
}

/**
 * @brief  振动评分（15%）
 *         振动越小越好
 *         <0.3g=100, 0.3-0.5=80, 0.5-1.0=50, 1.0-2.0=20, >2.0=0
 */
static float score_vibration(float vibration_rms)
{
    return score_piecewise(vibration_rms, 0.3f, 0.5f, 1.0f, 2.0f, 1);
}

/**
 * @brief  水质评分（10%）
 *         TDS越低越好
 *         <50ppm=100, 50-100=80, 100-300=50, 300-500=20, >500=0
 */
static float score_water_quality(uint16_t tds_ppm)
{
    return score_piecewise((float)tds_ppm, 50.0f, 100.0f, 300.0f, 500.0f, 1);
}

/**
 * @brief  散热效率评分（15%）
 *         (cpu_temp - water_temp) 差值越小越好（散热越好）
 *         但需要在高PWM下评估才有意义
 *         <10°C=100, 10-15=80, 15-25=50, 25-35=20, >35=0
 */
static float score_efficiency(float cpu_temp, float water_temp, uint8_t pump_pwm)
{
    float temp_diff = cpu_temp - water_temp;
    if (temp_diff < 0.0f) temp_diff = 0.0f;

    /* 低PWM下温差大是正常的，给予折扣 */
    float base_score = score_piecewise(temp_diff, 10.0f, 15.0f, 25.0f, 35.0f, 1);

    /* 如果PWM < 50%，效率评分权重降低（温差大可能是因为负载低） */
    if (pump_pwm < 50) {
        /* 低负载时，效率评分向100靠拢 */
        float factor = (float)pump_pwm / 50.0f;
        base_score = base_score * factor + 100.0f * (1.0f - factor);
    }

    return base_score;
}

/*============================ 公共函数实现 ============================*/

void health_init(void)
{
    memset(&s_health_output, 0, sizeof(health_output_t));
    s_health_output.total_score = 100.0f;
    s_health_output.level = HEALTH_LEVEL_EXCELLENT;
    for (uint8_t i = 0; i < HEALTH_DIM_COUNT; i++) {
        s_health_output.dim_scores[i] = 100.0f;
    }
    s_initialized = 1;
}

void health_update(const health_input_t *input)
{
    if (!s_initialized || input == NULL) return;

    /* 计算各维度分数 */
    s_health_output.dim_scores[0] = score_temperature(input->cpu_temp);
    s_health_output.dim_scores[1] = score_flow(input->flow_rate);
    s_health_output.dim_scores[2] = score_pressure(input->pressure);
    s_health_output.dim_scores[3] = score_vibration(input->vibration_rms);
    s_health_output.dim_scores[4] = score_water_quality(input->tds_ppm);
    s_health_output.dim_scores[5] = score_efficiency(input->cpu_temp,
                                                      input->water_temp,
                                                      input->pump_pwm);

    /* 加权求和 */
    float total = 0.0f;
    for (uint8_t i = 0; i < HEALTH_DIM_COUNT; i++) {
        total += s_health_output.dim_scores[i] * (float)s_weights[i] / 100.0f;
    }
    s_health_output.total_score = clampf(total, 0.0f, 100.0f);

    /* 确定等级 */
    if (s_health_output.total_score >= 90.0f)
        s_health_output.level = HEALTH_LEVEL_EXCELLENT;
    else if (s_health_output.total_score >= 70.0f)
        s_health_output.level = HEALTH_LEVEL_GOOD;
    else if (s_health_output.total_score >= 50.0f)
        s_health_output.level = HEALTH_LEVEL_FAIR;
    else if (s_health_output.total_score >= 30.0f)
        s_health_output.level = HEALTH_LEVEL_POOR;
    else
        s_health_output.level = HEALTH_LEVEL_DANGER;
}

const health_output_t* health_get_result(void)
{
    return &s_health_output;
}

float health_get_score(void)
{
    return s_health_output.total_score;
}

health_level_t health_get_level(void)
{
    return s_health_output.level;
}

const char* health_get_level_name(health_level_t level)
{
    if (level > HEALTH_LEVEL_DANGER) return "未知";
    return s_level_names[level];
}
