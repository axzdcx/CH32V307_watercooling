/**
 * @file    app_pwm.h
 * @brief   PWM控制应用层 - 风扇/水泵智能调速
 * @author  智能水冷项目
 * @date    2025-01-27
 *
 * 硬件型号:
 *   - 水泵: DC 12V PWM调速水泵（PA8, TIM1_CH1, 25kHz）
 *   - 风扇: 12V 4Pin PC散热风扇 x2（PB8/PB9, TIM4_CH3/CH4, 25kHz）
 *
 * 功能说明:
 *   - 根据温度自动调节风扇/水泵转速（PID控制）
 *   - 手动模式：直接设置转速
 *   - 转速范围限制（防止水泵过低、风扇噪音）
 *   - 提供转速数据给UI显示
 */

#ifndef __APP_PWM_H
#define __APP_PWM_H

#include "ch32v30x.h"

/*============================ PWM控制配置 ============================*/

/* 转速限制（安全保护） */
#define PWM_PUMP_MIN        30      /* 水泵最低转速30%（防止水流过慢） */
#define PWM_PUMP_MAX        100     /* 水泵最高转速100% */
#define PWM_FAN_MIN         20      /* 风扇最低转速20%（防止启动失败） */
#define PWM_FAN_MAX         100     /* 风扇最高转速100% */

/* 控制模式 */
typedef enum {
    PWM_MODE_AUTO = 0,      /* 自动模式（PID温控） */
    PWM_MODE_MANUAL = 1     /* 手动模式（固定转速） */
} pwm_mode_t;

/*============================ PWM数据结构 ============================*/

typedef struct {
    uint8_t pump_speed;     /* 水泵转速（%） */
    uint8_t fan1_speed;     /* 风扇1转速（%） */
    uint8_t fan2_speed;     /* 风扇2转速（%） */
    pwm_mode_t mode;        /* 控制模式 */
} pwm_data_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  PWM控制初始化
 * @return 0=成功, -1=失败
 */
int8_t pwm_init(void);

/**
 * @brief  PWM控制任务（由调度器定时调用）
 *
 * 使用方法:
 *   在调度器任务列表中注册: {task_pwm, 100, 0}  // 100ms周期
 */
void task_pwm(void);

/**
 * @brief  设置控制模式
 * @param  mode: PWM_MODE_AUTO 或 PWM_MODE_MANUAL
 */
void pwm_set_mode(pwm_mode_t mode);

/**
 * @brief  获取控制模式
 * @return 当前控制模式
 */
pwm_mode_t pwm_get_mode(void);

/**
 * @brief  手动设置水泵转速
 * @param  speed: 转速 0-100%（会自动限制在 PWM_PUMP_MIN ~ PWM_PUMP_MAX）
 */
void pwm_set_pump(uint8_t speed);

/**
 * @brief  手动设置风扇转速（两个风扇同步）
 * @param  speed: 转速 0-100%（会自动限制在 PWM_FAN_MIN ~ PWM_FAN_MAX）
 */
void pwm_set_fan(uint8_t speed);

/**
 * @brief  手动设置单个风扇转速
 * @param  fan_id: 1或2
 * @param  speed: 转速 0-100%
 */
void pwm_set_fan_single(uint8_t fan_id, uint8_t speed);

/**
 * @brief  获取PWM数据
 * @return PWM数据结构指针
 */
const pwm_data_t* pwm_get_data(void);

/**
 * @brief  获取水泵转速
 * @return 水泵转速 0-100%
 */
uint8_t pwm_get_pump(void);

/**
 * @brief  获取风扇转速（返回风扇1的速度）
 * @return 风扇转速 0-100%
 */
uint8_t pwm_get_fan(void);

#endif /* __APP_PWM_H */
