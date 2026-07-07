/**
 * @file    algo_control.c
 * @brief   防震荡多层控制算法实现 — 5层防护 + 自适应PID
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 控制流程：
 *   1. 自适应PID参数更新（增益调度）
 *   2. PID计算原始输出
 *   3. AI预测前馈融合
 *   4. 滞环模式判断（带死区）
 *   5. 时间防抖（模式切换确认）
 *   6. 变化率限制（PWM平滑）
 *   7. 输出最终PWM
 *
 * RAM占用：~128B
 */

#include "algo_control.h"
#include <string.h>
#include <math.h>

/*============================ 内部状态 ============================*/

static ctrl_state_t s_ctrl;
static uint8_t s_initialized = 0;

/* 自适应PID增益调度表 */
typedef struct {
    float temp_thresh;  /* 温度上限 °C */
    float kp;
    float ki;
    float kd;
} pid_zone_t;

static const pid_zone_t s_pid_zones[CTRL_ADAPT_ZONES] = {
    { 40.0f,  2.0f, 0.05f, 0.5f },  /* 低温：平稳 */
    { 60.0f,  4.0f, 0.08f, 1.0f },  /* 中温：适中 */
    { 75.0f,  6.0f, 0.10f, 2.0f },  /* 高温：积极 */
    { 999.0f, 8.0f, 0.02f, 3.0f },  /* 危险：快速响应，减小Ki防超调 */
};

/* 模式切换温度阈值（含滞环） */
#define MODE_IDLE_UPPER         40.0f
#define MODE_IDLE_LOWER         (MODE_IDLE_UPPER - CTRL_HYSTERESIS_BAND)
#define MODE_NORMAL_UPPER       70.0f
#define MODE_NORMAL_LOWER       (MODE_NORMAL_UPPER - CTRL_HYSTERESIS_BAND)
#define MODE_AGGRESSIVE_UPPER   80.0f
#define MODE_AGGRESSIVE_LOWER   (MODE_AGGRESSIVE_UPPER - CTRL_HYSTERESIS_BAND)

static const char* s_mode_names[] = {
    "空闲", "正常", "积极", "紧急"
};

/*============================ 辅助函数 ============================*/

static float clampf(float val, float min, float max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/**
 * @brief  线性插值
 */
static float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

/*============================ 函数实现 ============================*/

void ctrl_init(pid_controller_t *pid)
{
    memset(&s_ctrl, 0, sizeof(ctrl_state_t));
    s_ctrl.mode = CTRL_MODE_IDLE;
    s_ctrl.pending_mode = CTRL_MODE_IDLE;
    s_ctrl.last_output = 0.0f;

    if (pid != NULL) {
        s_ctrl.current_kp = pid->kp;
        s_ctrl.current_ki = pid->ki;
        s_ctrl.current_kd = pid->kd;
    }

    s_initialized = 1;
}

void ctrl_adaptive_pid_update(pid_controller_t *pid, float temp, float dtemp_dt)
{
    if (pid == NULL) return;

    /* 查找温度所在区间 */
    uint8_t zone = CTRL_ADAPT_ZONES - 1;
    for (uint8_t i = 0; i < CTRL_ADAPT_ZONES; i++) {
        if (temp < s_pid_zones[i].temp_thresh) {
            zone = i;
            break;
        }
    }

    float kp = s_pid_zones[zone].kp;
    float ki = s_pid_zones[zone].ki;
    float kd = s_pid_zones[zone].kd;

    /* 区间内线性插值（平滑过渡） */
    if (zone > 0) {
        float lower = (zone > 0) ? s_pid_zones[zone - 1].temp_thresh : 0.0f;
        float upper = s_pid_zones[zone].temp_thresh;
        float range = upper - lower;
        if (range > 0.1f) {
            float t = (temp - lower) / range;
            t = clampf(t, 0.0f, 1.0f);
            kp = lerpf(s_pid_zones[zone - 1].kp, kp, t);
            ki = lerpf(s_pid_zones[zone - 1].ki, ki, t);
            kd = lerpf(s_pid_zones[zone - 1].kd, kd, t);
        }
    }

    /* 温度变化率修正：快速升温时增大Kd 50% */
    if (dtemp_dt > 2.0f) {
        kd *= 1.5f;
    } else if (dtemp_dt > 1.0f) {
        /* 线性过渡 */
        kd *= 1.0f + 0.5f * (dtemp_dt - 1.0f);
    }

    /* 应用参数 */
    pid_tune(pid, kp, ki, kd);
    s_ctrl.current_kp = kp;
    s_ctrl.current_ki = ki;
    s_ctrl.current_kd = kd;
}

float ctrl_compute(pid_controller_t *pid, float current_temp,
                   float predicted_temp, float temp_rate)
{
    if (!s_initialized || pid == NULL) return 0.0f;

    /* ============ 第1层：自适应PID参数更新 ============ */
    ctrl_adaptive_pid_update(pid, current_temp, temp_rate);

    /* ============ 第2层：PID计算 ============ */
    s_ctrl.raw_output = pid_compute(pid, current_temp);

    /* ============ 第3层：AI预测前馈融合 ============ */
    /* 前馈：基于预测温度与目标温度的差值 */
    float ff_error = predicted_temp - pid->setpoint;
    s_ctrl.ff_output = clampf(ff_error * s_ctrl.current_kp * 0.5f,
                              pid->output_min, pid->output_max);

    /* 融合：PID 60% + 前馈 40% */
    s_ctrl.blended_output = CTRL_PID_WEIGHT * s_ctrl.raw_output +
                            CTRL_FF_WEIGHT * s_ctrl.ff_output;
    s_ctrl.blended_output = clampf(s_ctrl.blended_output,
                                   pid->output_min, pid->output_max);

    /* ============ 第4层：滞环模式判断（带死区） ============ */
    ctrl_mode_t new_mode = s_ctrl.mode;

    if (current_temp >= MODE_AGGRESSIVE_UPPER) {
        new_mode = CTRL_MODE_EMERGENCY;
    } else if (current_temp >= MODE_NORMAL_UPPER) {
        if (s_ctrl.mode < CTRL_MODE_AGGRESSIVE)
            new_mode = CTRL_MODE_AGGRESSIVE;
    } else if (current_temp >= MODE_IDLE_UPPER) {
        if (s_ctrl.mode < CTRL_MODE_NORMAL)
            new_mode = CTRL_MODE_NORMAL;
    }

    /* 下降方向：使用滞环下限 */
    if (current_temp < MODE_IDLE_LOWER) {
        if (s_ctrl.mode > CTRL_MODE_IDLE)
            new_mode = CTRL_MODE_IDLE;
    } else if (current_temp < MODE_NORMAL_LOWER) {
        if (s_ctrl.mode > CTRL_MODE_NORMAL)
            new_mode = CTRL_MODE_NORMAL;
    } else if (current_temp < MODE_AGGRESSIVE_LOWER) {
        if (s_ctrl.mode > CTRL_MODE_AGGRESSIVE)
            new_mode = CTRL_MODE_AGGRESSIVE;
    }

    /* ============ 第5层：时间防抖 ============ */
    if (new_mode != s_ctrl.mode) {
        if (new_mode == s_ctrl.pending_mode) {
            s_ctrl.debounce_cnt++;
            if (s_ctrl.debounce_cnt >= CTRL_DEBOUNCE_COUNT ||
                new_mode == CTRL_MODE_EMERGENCY) {
                /* 紧急模式立即切换，其他模式需要防抖确认 */
                s_ctrl.mode = new_mode;
                s_ctrl.debounce_cnt = 0;
            }
        } else {
            s_ctrl.pending_mode = new_mode;
            s_ctrl.debounce_cnt = 1;
        }
    } else {
        s_ctrl.debounce_cnt = 0;
        s_ctrl.pending_mode = s_ctrl.mode;
    }

    /* 紧急模式覆盖：全速运行 */
    float output = s_ctrl.blended_output;
    if (s_ctrl.mode == CTRL_MODE_EMERGENCY) {
        output = 100.0f;
    } else if (s_ctrl.mode == CTRL_MODE_IDLE) {
        /* 空闲模式：最低转速 */
        output = clampf(output, 0.0f, 30.0f);
    }

    /* ============ 变化率限制（PWM平滑） ============ */
    float delta = output - s_ctrl.last_output;
    if (delta > CTRL_MAX_RATE) {
        output = s_ctrl.last_output + CTRL_MAX_RATE;
    } else if (delta < -CTRL_MAX_RATE) {
        output = s_ctrl.last_output - CTRL_MAX_RATE;
    }

    /* 紧急模式不限速 */
    if (s_ctrl.mode == CTRL_MODE_EMERGENCY) {
        output = 100.0f;
    }

    output = clampf(output, 0.0f, 100.0f);

    /* 保存状态 */
    s_ctrl.rate_limited_output = output;
    s_ctrl.final_output = output;
    s_ctrl.last_output = output;

    return output;
}

const ctrl_state_t* ctrl_get_state(void)
{
    return &s_ctrl;
}

const char* ctrl_get_mode_name(void)
{
    if (s_ctrl.mode > CTRL_MODE_EMERGENCY) return "未知";
    return s_mode_names[s_ctrl.mode];
}
