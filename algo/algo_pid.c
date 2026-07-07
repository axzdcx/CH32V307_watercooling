/**
 * @file    algo_pid.c
 * @brief   PID 控制器算法实现
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * PID 算法公式：
 *   output = Kp * error + Ki * ∫error*dt + Kd * d(error)/dt
 *
 * 增量式 PID（避免积分饱和）：
 *   P 项：Kp * error
 *   I 项：Ki * error * dt
 *   D 项：Kd * (error - last_error) / dt
 */

#include "algo_pid.h"
#include <string.h>

/*============================ 函数实现 ============================*/

/**
 * @brief  初始化 PID 控制器
 */
void pid_init(pid_controller_t *pid, float kp, float ki, float kd, uint32_t sample_time_ms)
{
    if (pid == NULL) return;

    /* 清零结构体 */
    memset(pid, 0, sizeof(pid_controller_t));

    /* 设置 PID 参数 */
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;

    /* 设置采样时间（转换为秒） */
    pid->sample_time_s = (float)sample_time_ms / 1000.0f;

    /* 默认输出限幅：0-100（PWM占空比） */
    pid->output_min = 0.0f;
    pid->output_max = 100.0f;

    /* 默认积分限幅：防止积分饱和 */
    pid->integral_min = -50.0f;
    pid->integral_max = 50.0f;

    /* 初始状态 */
    pid->setpoint = 0.0f;
    pid->input = 0.0f;
    pid->output = 0.0f;
    pid->integral = 0.0f;
    pid->last_error = 0.0f;
    pid->last_input = 0.0f;
}

/**
 * @brief  设置输出限幅
 */
void pid_set_output_limits(pid_controller_t *pid, float min, float max)
{
    if (pid == NULL) return;

    pid->output_min = min;
    pid->output_max = max;

    /* 如果当前输出超出限幅，立即限制 */
    if (pid->output < min)
        pid->output = min;
    else if (pid->output > max)
        pid->output = max;
}

/**
 * @brief  设置积分限幅
 */
void pid_set_integral_limits(pid_controller_t *pid, float min, float max)
{
    if (pid == NULL) return;

    pid->integral_min = min;
    pid->integral_max = max;

    /* 如果当前积分超出限幅，立即限制 */
    if (pid->integral < min)
        pid->integral = min;
    else if (pid->integral > max)
        pid->integral = max;
}

/**
 * @brief  设置目标值
 */
void pid_set_setpoint(pid_controller_t *pid, float setpoint)
{
    if (pid == NULL) return;
    pid->setpoint = setpoint;
}

/**
 * @brief  PID 计算（核心算法）
 */
float pid_compute(pid_controller_t *pid, float input)
{
    if (pid == NULL) return 0.0f;

    /* 保存当前输入 */
    pid->input = input;

    /* 计算误差：实际值 - 目标值（冷却系统：温度越高，输出越大） */
    float error = input - pid->setpoint;

    /* ==================== P 项（比例） ==================== */
    float p_term = pid->kp * error;

    /* ==================== I 项（积分） ==================== */
    /* 积分累加：∫error*dt */
    pid->integral += error * pid->sample_time_s;

    /* 积分限幅（防止积分饱和） */
    if (pid->integral > pid->integral_max)
        pid->integral = pid->integral_max;
    else if (pid->integral < pid->integral_min)
        pid->integral = pid->integral_min;

    float i_term = pid->ki * pid->integral;

    /* ==================== D 项（微分） ==================== */
    /* 微分：d(input)/dt （用输入的变化率，避免设定值突变导致微分冲击） */
    float d_input = (input - pid->last_input) / pid->sample_time_s;
    float d_term = pid->kd * d_input;  /* 冷却系统：温度上升快 → 增大PWM */

    /* ==================== 计算总输出 ==================== */
    pid->output = p_term + i_term + d_term;

    /* 输出限幅 */
    if (pid->output > pid->output_max)
        pid->output = pid->output_max;
    else if (pid->output < pid->output_min)
        pid->output = pid->output_min;

    /* 保存本次状态 */
    pid->last_error = error;
    pid->last_input = input;

    return pid->output;
}

/**
 * @brief  重置 PID 控制器
 */
void pid_reset(pid_controller_t *pid)
{
    if (pid == NULL) return;

    pid->integral = 0.0f;
    pid->last_error = 0.0f;
    pid->last_input = 0.0f;
    pid->output = 0.0f;
}

/**
 * @brief  获取当前输出
 */
float pid_get_output(pid_controller_t *pid)
{
    if (pid == NULL) return 0.0f;
    return pid->output;
}

/**
 * @brief  调整 PID 参数（在线调参）
 */
void pid_tune(pid_controller_t *pid, float kp, float ki, float kd)
{
    if (pid == NULL) return;

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}
