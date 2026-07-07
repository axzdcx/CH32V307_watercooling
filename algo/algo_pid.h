/**
 * @file    algo_pid.h
 * @brief   PID 控制器算法
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * PID 算法说明：
 *   - P（比例）：根据当前误差调整，响应快但可能震荡
 *   - I（积分）：消除稳态误差，但可能导致超调
 *   - D（微分）：预测趋势，减少超调和震荡
 *
 * 水冷系统特点：
 *   - 热惯性大，响应慢
 *   - 需要较小的 Kp 和 Kd
 *   - 积分限幅防止积分饱和
 */

#ifndef __ALGO_PID_H
#define __ALGO_PID_H

#include <stdint.h>

/*============================ 数据结构 ============================*/

/**
 * @brief  PID 控制器参数结构体
 */
typedef struct {
    /* PID 参数 */
    float kp;           /* 比例系数 */
    float ki;           /* 积分系数 */
    float kd;           /* 微分系数 */

    /* 控制变量 */
    float setpoint;     /* 目标值（期望温度） */
    float input;        /* 当前值（实际温度） */
    float output;       /* 输出值（PWM占空比 0-100） */

    /* 内部状态 */
    float integral;     /* 积分累积 */
    float last_error;   /* 上次误差 */
    float last_input;   /* 上次输入（用于微分） */

    /* 限幅参数 */
    float output_min;   /* 输出下限 */
    float output_max;   /* 输出上限 */
    float integral_min; /* 积分下限（防止积分饱和） */
    float integral_max; /* 积分上限 */

    /* 采样时间 */
    float sample_time_s; /* 采样周期（秒） */
} pid_controller_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化 PID 控制器
 * @param  pid: PID 控制器结构体指针
 * @param  kp: 比例系数
 * @param  ki: 积分系数
 * @param  kd: 微分系数
 * @param  sample_time_ms: 采样周期（毫秒）
 */
void pid_init(pid_controller_t *pid, float kp, float ki, float kd, uint32_t sample_time_ms);

/**
 * @brief  设置输出限幅
 * @param  pid: PID 控制器结构体指针
 * @param  min: 输出下限
 * @param  max: 输出上限
 */
void pid_set_output_limits(pid_controller_t *pid, float min, float max);

/**
 * @brief  设置积分限幅（防止积分饱和）
 * @param  pid: PID 控制器结构体指针
 * @param  min: 积分下限
 * @param  max: 积分上限
 */
void pid_set_integral_limits(pid_controller_t *pid, float min, float max);

/**
 * @brief  设置目标值
 * @param  pid: PID 控制器结构体指针
 * @param  setpoint: 目标值（期望温度）
 */
void pid_set_setpoint(pid_controller_t *pid, float setpoint);

/**
 * @brief  PID 计算（核心算法）
 * @param  pid: PID 控制器结构体指针
 * @param  input: 当前输入值（实际温度）
 * @return 控制输出（PWM占空比 0-100）
 */
float pid_compute(pid_controller_t *pid, float input);

/**
 * @brief  重置 PID 控制器
 * @param  pid: PID 控制器结构体指针
 */
void pid_reset(pid_controller_t *pid);

/**
 * @brief  获取当前输出
 * @param  pid: PID 控制器结构体指针
 * @return 当前输出值
 */
float pid_get_output(pid_controller_t *pid);

/**
 * @brief  调整 PID 参数（在线调参）
 * @param  pid: PID 控制器结构体指针
 * @param  kp: 比例系数
 * @param  ki: 积分系数
 * @param  kd: 微分系数
 */
void pid_tune(pid_controller_t *pid, float kp, float ki, float kd);

#endif /* __ALGO_PID_H */
