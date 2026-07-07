/**
 * @file    app_pwm.c
 * @brief   PWM控制应用层实现
 * @author  智能水冷项目
 * @date    2025-01-27
 */

#include "app_pwm.h"
#include "bsp_tim.h"
#include "app_temp.h"
#include "scheduler.h"
#include "debug.h"

/*============================ 私有变量 ============================*/

/* PWM数据 */
static pwm_data_t pwm_data = {
    .pump_speed = 50,
    .fan1_speed = 50,
    .fan2_speed = 50,
    .mode = PWM_MODE_AUTO
};

/*============================ 内部函数 ============================*/

/**
 * @brief  限制转速范围
 */
static uint8_t limit_pump_speed(uint8_t speed)
{
    if (speed < PWM_PUMP_MIN) return PWM_PUMP_MIN;
    if (speed > PWM_PUMP_MAX) return PWM_PUMP_MAX;
    return speed;
}

static uint8_t limit_fan_speed(uint8_t speed)
{
    if (speed < PWM_FAN_MIN) return PWM_FAN_MIN;
    if (speed > PWM_FAN_MAX) return PWM_FAN_MAX;
    return speed;
}

/**
 * @brief  自动控制逻辑（简单温度曲线，PID由scheduler中的task_temp_control负责）
 *
 * 这里实现一个简单的温度-转速映射作为备用策略
 */
static void auto_control(void)
{
    float cpu_temp = temp_get_cpu();

    /* 检查温度是否有效 */
    if (cpu_temp < -100.0f || cpu_temp > 150.0f)
    {
        /* 传感器离线，使用安全转速 */
        pwm_data.pump_speed = 70;
        pwm_data.fan1_speed = 70;
        pwm_data.fan2_speed = 70;
        return;
    }

    /* 简单温度曲线控制（备用策略，主要由PID控制）
     *
     * 温度范围    转速
     * < 40℃      30%
     * 40-60℃     线性 30%-70%
     * 60-80℃     线性 70%-100%
     * > 80℃      100%
     */
    uint8_t target_speed;

    if (cpu_temp < 40.0f)
    {
        target_speed = 30;
    }
    else if (cpu_temp < 60.0f)
    {
        /* 线性插值: 30% + (temp-40)/20 * 40% */
        target_speed = 30 + (uint8_t)((cpu_temp - 40.0f) / 20.0f * 40.0f);
    }
    else if (cpu_temp < 80.0f)
    {
        /* 线性插值: 70% + (temp-60)/20 * 30% */
        target_speed = 70 + (uint8_t)((cpu_temp - 60.0f) / 20.0f * 30.0f);
    }
    else
    {
        target_speed = 100;
    }

    /* 应用转速（自动限制范围） */
    pwm_data.pump_speed = limit_pump_speed(target_speed);
    pwm_data.fan1_speed = limit_fan_speed(target_speed);
    pwm_data.fan2_speed = limit_fan_speed(target_speed);
}

/*============================ 公共接口函数 ============================*/

/**
 * @brief  PWM控制初始化
 */
int8_t pwm_init(void)
{
    printf("\r\n[PWM] 初始化风扇/水泵控制...\r\n");

    /* 初始化PWM硬件（BSP层） */
    pump_pwm_init();
    fan_pwm_init();

    /* 设置初始转速（50%安全值） */
    pump_set_duty(50);
    fan_set_duty(50);

    printf("[PWM] ✅ PWM控制初始化成功\r\n");
    if (scheduler_is_verbose_log_enabled()) {
        printf("[PWM] - 水泵: PA8 (TIM1_CH1), 25kHz, 初始50%%\r\n");
        printf("[PWM] - 风扇1: PB8 (TIM4_CH3), 25kHz, 初始50%%\r\n");
        printf("[PWM] - 风扇2: PB9 (TIM4_CH4), 25kHz, 初始50%%\r\n");
        printf("[PWM] - 水泵范围: %d%% - %d%%\r\n", PWM_PUMP_MIN, PWM_PUMP_MAX);
        printf("[PWM] - 风扇范围: %d%% - %d%%\r\n", PWM_FAN_MIN, PWM_FAN_MAX);
        printf("[PWM] - 控制模式: 自动（温度曲线）\r\n");
    }

    return 0;
}

/**
 * @brief  PWM控制任务（100ms周期）
 */
void task_pwm(void)
{
    static uint8_t print_count = 0;

    /* 根据控制模式执行 */
    if (pwm_data.mode == PWM_MODE_AUTO)
    {
        /* 自动模式：根据温度调节 */
        auto_control();
    }
    /* 手动模式：不做任何操作，保持用户设置的转速 */

    /* 更新PWM输出 */
    pump_set_duty(pwm_data.pump_speed);
    fan1_set_duty(pwm_data.fan1_speed);
    fan2_set_duty(pwm_data.fan2_speed);

    /* 每10次打印一次（每1秒） */
    if (scheduler_is_verbose_log_enabled() && (++print_count >= 10))
    {
        print_count = 0;
        printf("[PWM] 水泵=%d%%, 风扇1=%d%%, 风扇2=%d%%, 模式=%s\r\n",
               pwm_data.pump_speed,
               pwm_data.fan1_speed,
               pwm_data.fan2_speed,
               pwm_data.mode == PWM_MODE_AUTO ? "自动" : "手动");
    }
}

/**
 * @brief  设置控制模式
 */
void pwm_set_mode(pwm_mode_t mode)
{
    pwm_data.mode = mode;
    printf("[PWM] 切换模式: %s\r\n", mode == PWM_MODE_AUTO ? "自动" : "手动");
}

/**
 * @brief  获取控制模式
 */
pwm_mode_t pwm_get_mode(void)
{
    return pwm_data.mode;
}

/**
 * @brief  手动设置水泵转速
 */
void pwm_set_pump(uint8_t speed)
{
    pwm_data.pump_speed = limit_pump_speed(speed);
    pump_set_duty(pwm_data.pump_speed);
    printf("[PWM] 手动设置水泵: %d%%\r\n", pwm_data.pump_speed);
}

/**
 * @brief  手动设置风扇转速
 */
void pwm_set_fan(uint8_t speed)
{
    pwm_data.fan1_speed = limit_fan_speed(speed);
    pwm_data.fan2_speed = limit_fan_speed(speed);
    fan_set_duty(pwm_data.fan1_speed);
    printf("[PWM] 手动设置风扇: %d%%\r\n", pwm_data.fan1_speed);
}

/**
 * @brief  手动设置单个风扇转速
 */
void pwm_set_fan_single(uint8_t fan_id, uint8_t speed)
{
    if (fan_id == 1)
    {
        pwm_data.fan1_speed = limit_fan_speed(speed);
        fan1_set_duty(pwm_data.fan1_speed);
        printf("[PWM] 手动设置风扇1: %d%%\r\n", pwm_data.fan1_speed);
    }
    else if (fan_id == 2)
    {
        pwm_data.fan2_speed = limit_fan_speed(speed);
        fan2_set_duty(pwm_data.fan2_speed);
        printf("[PWM] 手动设置风扇2: %d%%\r\n", pwm_data.fan2_speed);
    }
}

/**
 * @brief  获取PWM数据
 */
const pwm_data_t* pwm_get_data(void)
{
    return &pwm_data;
}

/**
 * @brief  获取水泵转速
 */
uint8_t pwm_get_pump(void)
{
    return pwm_data.pump_speed;
}

/**
 * @brief  获取风扇转速
 */
uint8_t pwm_get_fan(void)
{
    return pwm_data.fan1_speed;
}
