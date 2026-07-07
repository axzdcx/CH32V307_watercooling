/**
 * @file    algo_diagnosis.c
 * @brief   故障诊断算法实现 — 6种故障模式检测
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 算法说明：
 *   使用滑动窗口统计（均值、标准差、变异系数、趋势斜率）
 *   每种故障独立判断，多条件融合提高置信度
 *   所有计算为浮点运算，总RAM占用约 512B + 窗口缓冲
 */

#include "algo_diagnosis.h"
#include <string.h>
#include <math.h>

/*============================ 内部数据结构 ============================*/

/**
 * @brief  环形缓冲区（用于滑动窗口统计）
 */
#define RING_BUF_SIZE   DIAG_WIN_MEDIUM  /* 30点，覆盖大部分窗口需求 */

typedef struct {
    float data[RING_BUF_SIZE];
    uint16_t head;
    uint16_t count;
} ring_buf_t;

/**
 * @brief  诊断系统内部状态
 */
typedef struct {
    /* 滑动窗口缓冲区 */
    ring_buf_t flow_buf;            /* 流量历史 */
    ring_buf_t vibration_buf;       /* 振动历史 */
    ring_buf_t noise_buf;           /* 噪音历史 */
    ring_buf_t pump_current_buf;    /* 水泵电流历史 */
    ring_buf_t pressure_buf;        /* 压力历史（复用30点，漏水用趋势） */
    ring_buf_t temp_diff_buf;       /* 温差历史 */

    /* 堵塞检测持续计数 */
    uint16_t blockage_hold_cnt;

    /* 水质持续计数 */
    uint16_t water_quality_hold_cnt;

    /* 诊断输出 */
    diag_output_t output;

    /* 初始化标志 */
    uint8_t initialized;
} diag_state_t;

static diag_state_t s_diag;

/* 诊断阈值（运行时可调，默认值来自方案常量） */
static diag_threshold_config_t s_diag_cfg = {
    DIAG_BLOCKAGE_FLOW_THRESH,
    DIAG_BLOCKAGE_TEMP_THRESH,
    DIAG_BLOCKAGE_HOLD_TIME,
    40,
    DIAG_BUBBLE_FLOW_CV_THRESH,
    DIAG_PUMP_CURRENT_CV_THRESH,
    DIAG_LEAK_PRESSURE_SLOPE,
    DIAG_LEAK_FLOW_DROP_PERCENT,
    DIAG_WATER_TDS_WARNING,
    DIAG_WATER_TDS_CRITICAL,
    DIAG_EFF_TEMP_DIFF_WARNING,
    DIAG_EFF_TEMP_DIFF_CRITICAL,
    DIAG_EFF_PUMP_PWM_THRESH
};

/*============================ 故障名称 ============================*/

static const char* s_fault_names[DIAG_FAULT_COUNT] = {
    "散热排堵塞",
    "系统气泡",
    "水泵老化",
    "系统漏水",
    "水质劣化",
    "散热效率低"
};

/*============================ 环形缓冲区操作 ============================*/

static void ring_init(ring_buf_t *rb)
{
    memset(rb, 0, sizeof(ring_buf_t));
}

static void ring_push(ring_buf_t *rb, float val)
{
    rb->data[rb->head] = val;
    rb->head = (rb->head + 1) % RING_BUF_SIZE;
    if (rb->count < RING_BUF_SIZE) {
        rb->count++;
    }
}

static float ring_mean(const ring_buf_t *rb, uint16_t window)
{
    if (rb->count == 0) return 0.0f;
    uint16_t n = (window < rb->count) ? window : rb->count;
    float sum = 0.0f;
    uint16_t idx = (rb->head + RING_BUF_SIZE - n) % RING_BUF_SIZE;
    for (uint16_t i = 0; i < n; i++) {
        sum += rb->data[(idx + i) % RING_BUF_SIZE];
    }
    return sum / (float)n;
}

static float ring_stddev(const ring_buf_t *rb, uint16_t window)
{
    if (rb->count < 2) return 0.0f;
    uint16_t n = (window < rb->count) ? window : rb->count;
    float mean = ring_mean(rb, window);
    float sum_sq = 0.0f;
    uint16_t idx = (rb->head + RING_BUF_SIZE - n) % RING_BUF_SIZE;
    for (uint16_t i = 0; i < n; i++) {
        float diff = rb->data[(idx + i) % RING_BUF_SIZE] - mean;
        sum_sq += diff * diff;
    }
    return sqrtf(sum_sq / (float)n);
}

/**
 * @brief  计算变异系数 CV = stddev / mean
 */
static float ring_cv(const ring_buf_t *rb, uint16_t window)
{
    float mean = ring_mean(rb, window);
    if (fabsf(mean) < 0.001f) return 0.0f;
    return ring_stddev(rb, window) / fabsf(mean);
}

/**
 * @brief  计算趋势斜率（最小二乘法线性回归）
 *         返回 slope，单位为 值/采样点
 */
static float ring_slope(const ring_buf_t *rb, uint16_t window)
{
    if (rb->count < 3) return 0.0f;
    uint16_t n = (window < rb->count) ? window : rb->count;

    float sum_x = 0.0f, sum_y = 0.0f, sum_xy = 0.0f, sum_xx = 0.0f;
    uint16_t idx = (rb->head + RING_BUF_SIZE - n) % RING_BUF_SIZE;

    for (uint16_t i = 0; i < n; i++) {
        float x = (float)i;
        float y = rb->data[(idx + i) % RING_BUF_SIZE];
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
    }

    float denom = (float)n * sum_xx - sum_x * sum_x;
    if (fabsf(denom) < 0.0001f) return 0.0f;

    return ((float)n * sum_xy - sum_x * sum_y) / denom;
}

/**
 * @brief  获取窗口内最早的均值（前1/3数据的均值）
 */
static float ring_early_mean(const ring_buf_t *rb, uint16_t window)
{
    if (rb->count < 3) return 0.0f;
    uint16_t n = (window < rb->count) ? window : rb->count;
    uint16_t early_n = n / 3;
    if (early_n == 0) early_n = 1;

    float sum = 0.0f;
    uint16_t idx = (rb->head + RING_BUF_SIZE - n) % RING_BUF_SIZE;
    for (uint16_t i = 0; i < early_n; i++) {
        sum += rb->data[(idx + i) % RING_BUF_SIZE];
    }
    return sum / (float)early_n;
}

/**
 * @brief  校验诊断阈值配置合法性
 * @param  cfg: 待校验配置
 * @return 1=合法, 0=非法
 */
static uint8_t diag_threshold_validate(const diag_threshold_config_t *cfg)
{
    if (cfg == NULL) return 0;

    if (cfg->blockage_flow_thresh < 0.2f || cfg->blockage_flow_thresh > 10.0f) return 0;
    if (cfg->blockage_temp_thresh < 40.0f || cfg->blockage_temp_thresh > 95.0f) return 0;
    if (cfg->blockage_hold_time < 3 || cfg->blockage_hold_time > 120) return 0;
    if (cfg->blockage_pump_pwm_min > 100) return 0;

    if (cfg->bubble_flow_cv_thresh < 0.02f || cfg->bubble_flow_cv_thresh > 1.0f) return 0;
    if (cfg->pump_current_cv_thresh < 0.02f || cfg->pump_current_cv_thresh > 1.0f) return 0;

    if (cfg->leak_pressure_slope > -0.0005f || cfg->leak_pressure_slope < -0.05f) return 0;
    if (cfg->leak_flow_drop_percent < 5.0f || cfg->leak_flow_drop_percent > 95.0f) return 0;

    if (cfg->water_tds_warning < 50 || cfg->water_tds_warning > 1000) return 0;
    if (cfg->water_tds_critical < cfg->water_tds_warning || cfg->water_tds_critical > 2000) return 0;

    if (cfg->eff_temp_diff_warning < 8.0f || cfg->eff_temp_diff_warning > 40.0f) return 0;
    if (cfg->eff_temp_diff_critical < cfg->eff_temp_diff_warning || cfg->eff_temp_diff_critical > 50.0f) return 0;
    if (cfg->eff_pump_pwm_thresh > 100) return 0;

    return 1;
}

/*============================ 各故障检测函数 ============================*/

/**
 * @brief  故障0：散热排堵塞
 *         条件: flow_rate < 1.5 L/min（持续>10s） AND cpu_temp > 70°C
 */
static void diag_check_blockage(const diag_sensor_input_t *input)
{
    diag_result_t *r = &s_diag.output.faults[DIAG_FAULT_BLOCKAGE];
    r->fault_id = DIAG_FAULT_BLOCKAGE;

    float flow_avg = ring_mean(&s_diag.flow_buf, DIAG_WIN_SHORT);
    uint8_t flow_low = (flow_avg < s_diag_cfg.blockage_flow_thresh) ? 1 : 0;
    uint8_t temp_high = (input->cpu_temp > s_diag_cfg.blockage_temp_thresh) ? 1 : 0;
    uint8_t pump_on = (input->pump_pwm >= s_diag_cfg.blockage_pump_pwm_min) ? 1 : 0;

    /* 持续计数 */
    if (flow_low && pump_on) {
        if (s_diag.blockage_hold_cnt < s_diag_cfg.blockage_hold_time)
            s_diag.blockage_hold_cnt++;
    } else {
        s_diag.blockage_hold_cnt = 0;
    }

    uint8_t flow_confirmed = (s_diag.blockage_hold_cnt >= s_diag_cfg.blockage_hold_time) ? 1 : 0;

    /* 置信度计算 */
    if (flow_confirmed && temp_high) {
        r->confidence = 0.9f;
    } else if (flow_confirmed) {
        r->confidence = 0.4f;  /* 仅低流量不直接激活，降低误报 */
    } else if (temp_high && flow_avg < (s_diag_cfg.blockage_flow_thresh + 0.5f)) {
        r->confidence = 0.3f;
    } else {
        r->confidence = 0.0f;
    }

    r->active = (r->confidence >= 0.5f) ? 1 : 0;
}

/**
 * @brief  故障1：系统气泡
 *         条件: flow CV > 0.15（30s窗口） AND vibration > 2.0g AND noise > 65dB
 */
static void diag_check_bubble(const diag_sensor_input_t *input)
{
    diag_result_t *r = &s_diag.output.faults[DIAG_FAULT_BUBBLE];
    r->fault_id = DIAG_FAULT_BUBBLE;

    float flow_cv = ring_cv(&s_diag.flow_buf, DIAG_WIN_MEDIUM);
    float vib_avg = ring_mean(&s_diag.vibration_buf, DIAG_WIN_MEDIUM);
    float noise_avg = ring_mean(&s_diag.noise_buf, DIAG_WIN_MEDIUM);

    uint8_t cond_count = 0;
    if (flow_cv > s_diag_cfg.bubble_flow_cv_thresh) cond_count++;
    if (vib_avg > DIAG_BUBBLE_VIBRATION_THRESH) cond_count++;
    if (noise_avg > DIAG_BUBBLE_NOISE_THRESH) cond_count++;

    /* 置信度 = 满足条件数 / 3 */
    r->confidence = (float)cond_count / 3.0f;
    r->active = (r->confidence >= 0.5f) ? 1 : 0;
}

/**
 * @brief  故障2：水泵老化（用电流波动+振动替代转速）
 *         条件: pump_current CV > 0.10（30s窗口） AND vibration > 1.5g
 */
static void diag_check_pump_aging(const diag_sensor_input_t *input)
{
    diag_result_t *r = &s_diag.output.faults[DIAG_FAULT_PUMP_AGING];
    r->fault_id = DIAG_FAULT_PUMP_AGING;

    float current_cv = ring_cv(&s_diag.pump_current_buf, DIAG_WIN_MEDIUM);
    float vib_avg = ring_mean(&s_diag.vibration_buf, DIAG_WIN_MEDIUM);

    uint8_t cond_current = (current_cv > s_diag_cfg.pump_current_cv_thresh) ? 1 : 0;
    uint8_t cond_vib = (vib_avg > DIAG_PUMP_VIBRATION_THRESH) ? 1 : 0;

    if (cond_current && cond_vib) {
        r->confidence = 0.85f;
    } else if (cond_current) {
        r->confidence = 0.5f;
    } else if (cond_vib) {
        r->confidence = 0.4f;
    } else {
        r->confidence = 0.0f;
    }

    r->active = (r->confidence >= 0.5f) ? 1 : 0;
}

/**
 * @brief  故障3：系统漏水
 *         条件: pressure斜率 < -0.005 MPa/min（30s窗口近似）
 *               AND flow下降 > 20%（相对窗口早期均值）
 *
 *         注意：压力斜率单位转换
 *         ring_slope返回 MPa/采样点，采样周期2s
 *         转换为 MPa/min: slope * (60/2) = slope * 30
 */
static void diag_check_leakage(const diag_sensor_input_t *input)
{
    diag_result_t *r = &s_diag.output.faults[DIAG_FAULT_LEAKAGE];
    r->fault_id = DIAG_FAULT_LEAKAGE;

    /* 压力趋势（MPa/采样点 → MPa/min） */
    float p_slope_per_sample = ring_slope(&s_diag.pressure_buf, DIAG_WIN_MEDIUM);
    float p_slope_per_min = p_slope_per_sample * 30.0f; /* 30个采样点/分钟（@2s周期） */

    /* 流量下降百分比 */
    float flow_early = ring_early_mean(&s_diag.flow_buf, DIAG_WIN_MEDIUM);
    float flow_now = ring_mean(&s_diag.flow_buf, DIAG_WIN_SHORT);
    float flow_drop_pct = 0.0f;
    if (flow_early > 0.1f) {
        flow_drop_pct = (flow_early - flow_now) / flow_early * 100.0f;
    }

    uint8_t cond_pressure = (p_slope_per_min < s_diag_cfg.leak_pressure_slope) ? 1 : 0;
    uint8_t cond_flow = (flow_drop_pct > s_diag_cfg.leak_flow_drop_percent) ? 1 : 0;

    if (cond_pressure && cond_flow) {
        r->confidence = 0.9f;
    } else if (cond_pressure) {
        r->confidence = 0.5f;
    } else if (cond_flow) {
        r->confidence = 0.4f;
    } else {
        r->confidence = 0.0f;
    }

    r->active = (r->confidence >= 0.5f) ? 1 : 0;
}

/**
 * @brief  故障4：水质劣化
 *         条件: TDS > 300ppm（持续>60s） OR 运行时长 > 180天
 */
static void diag_check_water_quality(const diag_sensor_input_t *input)
{
    diag_result_t *r = &s_diag.output.faults[DIAG_FAULT_WATER_QUALITY];
    r->fault_id = DIAG_FAULT_WATER_QUALITY;

    /* TDS持续确认 */
    if (input->tds_ppm > s_diag_cfg.water_tds_warning) {
        if (s_diag.water_quality_hold_cnt < DIAG_WATER_HOLD_TIME)
            s_diag.water_quality_hold_cnt++;
    } else {
        if (s_diag.water_quality_hold_cnt > 0)
            s_diag.water_quality_hold_cnt--;
    }

    uint8_t tds_confirmed = (s_diag.water_quality_hold_cnt >= DIAG_WATER_HOLD_TIME) ? 1 : 0;

    /* 运行时长检查 */
    uint32_t runtime_days = input->runtime_seconds / 86400;
    uint8_t runtime_exceeded = (runtime_days > DIAG_WATER_RUNTIME_DAYS) ? 1 : 0;

    /* 置信度计算 */
    if (tds_confirmed && input->tds_ppm > s_diag_cfg.water_tds_critical) {
        r->confidence = 1.0f;
    } else if (tds_confirmed) {
        r->confidence = 0.7f;
    } else if (runtime_exceeded) {
        r->confidence = 0.5f;
    } else {
        r->confidence = 0.0f;
    }

    r->active = (r->confidence >= 0.5f) ? 1 : 0;
}

/**
 * @brief  故障5：散热效率低
 *         条件: (cpu_temp - water_temp) > 25°C AND pump_pwm > 80%
 *
 *         逻辑说明：差值越大 = 冷排/水块热阻高 = 散热能力不足
 *         （水温低但CPU温度高，说明热量无法有效传递到冷却液）
 */
static void diag_check_low_efficiency(const diag_sensor_input_t *input)
{
    diag_result_t *r = &s_diag.output.faults[DIAG_FAULT_LOW_EFFICIENCY];
    r->fault_id = DIAG_FAULT_LOW_EFFICIENCY;

    float temp_diff_avg = ring_mean(&s_diag.temp_diff_buf, DIAG_WIN_MEDIUM);
    uint8_t pump_high = (input->pump_pwm > s_diag_cfg.eff_pump_pwm_thresh) ? 1 : 0;

    if (temp_diff_avg > s_diag_cfg.eff_temp_diff_critical && pump_high) {
        r->confidence = 0.9f;
    } else if (temp_diff_avg > s_diag_cfg.eff_temp_diff_warning && pump_high) {
        r->confidence = 0.7f;
    } else if (temp_diff_avg > s_diag_cfg.eff_temp_diff_critical) {
        r->confidence = 0.5f;
    } else {
        r->confidence = 0.0f;
    }

    r->active = (r->confidence >= 0.5f) ? 1 : 0;
}

/*============================ 公共函数实现 ============================*/

void diag_init(void)
{
    memset(&s_diag, 0, sizeof(diag_state_t));

    ring_init(&s_diag.flow_buf);
    ring_init(&s_diag.vibration_buf);
    ring_init(&s_diag.noise_buf);
    ring_init(&s_diag.pump_current_buf);
    ring_init(&s_diag.pressure_buf);
    ring_init(&s_diag.temp_diff_buf);

    /* 初始化故障ID */
    for (uint8_t i = 0; i < DIAG_FAULT_COUNT; i++) {
        s_diag.output.faults[i].fault_id = i;
        s_diag.output.faults[i].confidence = 0.0f;
        s_diag.output.faults[i].active = 0;
    }

    s_diag.initialized = 1;
}

void diag_update(const diag_sensor_input_t *input)
{
    if (!s_diag.initialized || input == NULL) return;

    /* 1. 更新滑动窗口缓冲区 */
    ring_push(&s_diag.flow_buf, input->flow_rate);
    ring_push(&s_diag.vibration_buf, input->vibration_rms);
    ring_push(&s_diag.noise_buf, input->noise_db);
    ring_push(&s_diag.pump_current_buf, input->pump_current);
    ring_push(&s_diag.pressure_buf, input->pressure);
    ring_push(&s_diag.temp_diff_buf, input->cpu_temp - input->water_temp);

    /* 2. 运行各故障检测 */
    diag_check_blockage(input);
    diag_check_bubble(input);
    diag_check_pump_aging(input);
    diag_check_leakage(input);
    diag_check_water_quality(input);
    diag_check_low_efficiency(input);

    /* 3. 汇总结果 */
    s_diag.output.active_count = 0;
    s_diag.output.most_severe_confidence = 0.0f;
    s_diag.output.most_severe_id = 0;

    for (uint8_t i = 0; i < DIAG_FAULT_COUNT; i++) {
        if (s_diag.output.faults[i].active) {
            s_diag.output.active_count++;
        }
        if (s_diag.output.faults[i].confidence > s_diag.output.most_severe_confidence) {
            s_diag.output.most_severe_confidence = s_diag.output.faults[i].confidence;
            s_diag.output.most_severe_id = i;
        }
    }
}

const diag_output_t* diag_get_result(void)
{
    return &s_diag.output;
}

float diag_get_confidence(diag_fault_type_t fault_id)
{
    if (fault_id >= DIAG_FAULT_COUNT) return 0.0f;
    return s_diag.output.faults[fault_id].confidence;
}

uint8_t diag_has_active_fault(void)
{
    return (s_diag.output.active_count > 0) ? 1 : 0;
}

const char* diag_get_fault_name(diag_fault_type_t fault_id)
{
    if (fault_id >= DIAG_FAULT_COUNT) return "未知";
    return s_fault_names[fault_id];
}

void diag_threshold_reset_defaults(void)
{
    diag_threshold_config_t defaults = {
        DIAG_BLOCKAGE_FLOW_THRESH,
        DIAG_BLOCKAGE_TEMP_THRESH,
        DIAG_BLOCKAGE_HOLD_TIME,
        40,
        DIAG_BUBBLE_FLOW_CV_THRESH,
        DIAG_PUMP_CURRENT_CV_THRESH,
        DIAG_LEAK_PRESSURE_SLOPE,
        DIAG_LEAK_FLOW_DROP_PERCENT,
        DIAG_WATER_TDS_WARNING,
        DIAG_WATER_TDS_CRITICAL,
        DIAG_EFF_TEMP_DIFF_WARNING,
        DIAG_EFF_TEMP_DIFF_CRITICAL,
        DIAG_EFF_PUMP_PWM_THRESH
    };

    s_diag_cfg = defaults;
}

int8_t diag_threshold_get(diag_threshold_config_t *cfg)
{
    if (cfg == NULL) {
        return -1;
    }
    *cfg = s_diag_cfg;
    return 0;
}

int8_t diag_threshold_set(const diag_threshold_config_t *cfg)
{
    if (!diag_threshold_validate(cfg)) {
        return -1;
    }
    s_diag_cfg = *cfg;
    return 0;
}
