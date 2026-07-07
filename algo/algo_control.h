/**
 * @file    algo_control.h
 * @brief   防震荡多层控制算法 — 5层防护
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 5层防震荡控制：
 *   1. PID控制器（已有 algo_pid.c）
 *   2. 滞环控制（死区）— 模式切换加死区防止频繁切换
 *   3. 时间防抖 — 确认持续N秒后再调整
 *   4. 变化率限制 — PWM每次最多变化 MAX_RATE%
 *   5. AI预测前馈 — 预测温度 + PID融合
 *
 * 自适应PID（增益调度）：
 *   根据温度区间动态调整 Kp/Ki/Kd
 *
 * 资源占用：
 *   RAM: ~128B
 *   计算时间: <0.2ms
 */

#ifndef __ALGO_CONTROL_H
#define __ALGO_CONTROL_H

#include <stdint.h>
#include "algo_pid.h"

/*============================ 配置 ============================*/

/* 滞环控制（死区） */
#define CTRL_HYSTERESIS_BAND    3.0f    /* 滞环宽度 °C（±1.5°C） */

/* 时间防抖 */
#define CTRL_DEBOUNCE_COUNT     5       /* 防抖确认次数（5次@1s = 5秒） */

/* 变化率限制 */
#define CTRL_MAX_RATE           5.0f    /* PWM每次最大变化量 %/次 */

/* 前馈融合 */
#define CTRL_PID_WEIGHT         0.6f    /* PID输出权重 */
#define CTRL_FF_WEIGHT          0.4f    /* 前馈输出权重 */

/* 自适应PID增益调度表 */
#define CTRL_ADAPT_ZONES        4       /* 温度区间数 */

/*============================ 数据结构 ============================*/

/**
 * @brief  控制模式
 */
typedef enum {
    CTRL_MODE_IDLE = 0,     /* 空闲（低温，最低转速） */
    CTRL_MODE_NORMAL,       /* 正常（PID控制） */
    CTRL_MODE_AGGRESSIVE,   /* 积极（高温，加大响应） */
    CTRL_MODE_EMERGENCY     /* 紧急（危险温度，全速） */
} ctrl_mode_t;

/**
 * @brief  控制器状态
 */
typedef struct {
    /* 当前模式 */
    ctrl_mode_t mode;
    ctrl_mode_t pending_mode;   /* 待切换模式（防抖用） */
    uint8_t debounce_cnt;       /* 防抖计数器 */

    /* 输出 */
    float raw_output;           /* PID原始输出 */
    float ff_output;            /* 前馈输出 */
    float blended_output;       /* 融合输出 */
    float rate_limited_output;  /* 限速后输出 */
    float final_output;         /* 最终输出（PWM %） */

    /* 上次输出（用于限速） */
    float last_output;

    /* 自适应PID当前参数 */
    float current_kp;
    float current_ki;
    float current_kd;
} ctrl_state_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化多层控制器
 * @param  pid: PID控制器指针（已初始化）
 */
void ctrl_init(pid_controller_t *pid);

/**
 * @brief  多层控制计算（由调度器100ms周期调用）
 * @param  pid: PID控制器指针
 * @param  current_temp: 当前CPU温度 °C
 * @param  predicted_temp: AI预测温度 °C（无预测时传入current_temp）
 * @param  temp_rate: 温度变化率 °C/s
 * @return 最终PWM输出 0~100%
 */
float ctrl_compute(pid_controller_t *pid, float current_temp,
                   float predicted_temp, float temp_rate);

/**
 * @brief  获取控制器状态
 * @return 控制器状态指针
 */
const ctrl_state_t* ctrl_get_state(void);

/**
 * @brief  获取当前控制模式名称
 * @return 模式名称字符串
 */
const char* ctrl_get_mode_name(void);

/**
 * @brief  自适应PID参数更新（根据温度区间）
 * @param  pid: PID控制器指针
 * @param  temp: 当前温度 °C
 * @param  dtemp_dt: 温度变化率 °C/s
 */
void ctrl_adaptive_pid_update(pid_controller_t *pid, float temp, float dtemp_dt);

#endif /* __ALGO_CONTROL_H */
