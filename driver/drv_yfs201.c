/**
 * @file    drv_yfs201.c
 * @brief   YFS201 水流量传感器驱动实现
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * 工作原理：
 *   1. 水流推动叶轮旋转
 *   2. 霍尔传感器输出方波脉冲
 *   3. 外部中断捕获脉冲计数
 *   4. 定时计算频率 → 流量
 */

#include "drv_yfs201.h"
#include "debug.h"

/*============================ 静态变量 ============================*/

static volatile uint32_t pulse_count = 0;      /* 脉冲总计数 */
static uint32_t pulse_count_last = 0;          /* 上次采样的计数 */
static uint32_t last_sample_tick = 0;          /* 上次采样时间戳 */

static float flow_rate_current = 0.0f;         /* 当前流量 */
static float total_volume = 0.0f;              /* 累计流量 */

static yfs201_calib_t s_yfs201_calib = {
    .k_factor = YFS201_K_FACTOR,
    .pulses_per_liter = YFS201_PULSES_PER_LITER
};

void yfs201_set_calib(const yfs201_calib_t *calib)
{
    if (calib == NULL) return;
    s_yfs201_calib = *calib;
    if (s_yfs201_calib.k_factor <= 0.1f) {
        s_yfs201_calib.k_factor = YFS201_K_FACTOR;
    }
    if (s_yfs201_calib.pulses_per_liter == 0) {
        s_yfs201_calib.pulses_per_liter = YFS201_PULSES_PER_LITER;
    }
}

const yfs201_calib_t* yfs201_get_calib(void)
{
    return &s_yfs201_calib;
}

/*============================ 内部函数 ============================*/

/**
 * @brief  配置GPIO为外部中断输入
 */
static void yfs201_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    EXTI_InitTypeDef EXTI_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    /* 1. 使能GPIO和AFIO时钟 */
    RCC_APB2PeriphClockCmd(YFS201_GPIO_CLK | RCC_APB2Periph_AFIO, ENABLE);

    /* 2. 配置GPIO为上拉输入 */
    GPIO_InitStructure.GPIO_Pin = YFS201_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  /* 上拉输入 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(YFS201_GPIO_PORT, &GPIO_InitStructure);

    /* 3. 配置外部中断线 */
    GPIO_EXTILineConfig(YFS201_EXTI_PORT_SOURCE, YFS201_EXTI_PIN_SOURCE);

    /* 4. 配置EXTI：下降沿触发（脉冲下降沿计数） */
    EXTI_InitStructure.EXTI_Line = YFS201_EXTI_LINE;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;  /* 下降沿触发 */
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    /* 5. 配置NVIC中断优先级 */
    NVIC_InitStructure.NVIC_IRQChannel = YFS201_EXTI_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;  /* 抢占优先级2 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;         /* 子优先级2 */
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/*============================ 外部接口函数 ============================*/

/**
 * @brief  初始化YFS201流量传感器
 */
void yfs201_init(void)
{
    /* 初始化GPIO和外部中断 */
    yfs201_gpio_init();

    /* 初始化变量 */
    pulse_count = 0;
    pulse_count_last = 0;
    last_sample_tick = 0;
    flow_rate_current = 0.0f;
    total_volume = 0.0f;

    printf("[YFS201] Flow sensor init on PB0 (EXTI0)\r\n");
    printf("[YFS201] K-factor: %.3f Hz/(L/min), pulses_per_liter=%u\r\n",
           s_yfs201_calib.k_factor,
           s_yfs201_calib.pulses_per_liter);
}

/**
 * @brief  更新流量数据（定时调用，建议1秒一次）
 * @param  data: 数据结构指针
 */
void yfs201_update(yfs201_data_t *data)
{
    extern uint32_t get_tick_ms(void);  /* 从scheduler.c获取 */

    uint32_t current_tick = get_tick_ms();  /* 获取当前时间戳 */
    uint32_t delta_time_ms;
    uint32_t delta_pulses;
    float frequency;

    /* 计算时间差（毫秒） */
    if (current_tick >= last_sample_tick)
    {
        delta_time_ms = current_tick - last_sample_tick;
    }
    else
    {
        /* 处理溢出 */
        delta_time_ms = (0xFFFFFFFF - last_sample_tick) + current_tick + 1;
    }

    /* 如果时间间隔太短，不更新 */
    if (delta_time_ms < 100)
    {
        return;
    }

    /* 计算脉冲差 */
    delta_pulses = pulse_count - pulse_count_last;

    /* 计算频率（Hz） */
    frequency = (float)delta_pulses * 1000.0f / (float)delta_time_ms;

    /* 根据脉冲系数计算流量 (L/min) */
    /* F(Hz) = K × Q(L/min) → Q = F / K */
    flow_rate_current = frequency / s_yfs201_calib.k_factor;

    /* 计算累计流量（升） */
    /* 方法：脉冲数 / 每升脉冲数 */
    total_volume = (float)pulse_count / (float)s_yfs201_calib.pulses_per_liter;

    /* 填充数据结构 */
    if (data != NULL)
    {
        data->pulse_count = pulse_count;
        data->pulse_count_last = pulse_count_last;
        data->flow_rate = flow_rate_current;
        data->total_volume = total_volume;
        data->last_sample_time = current_tick;
        data->valid = 1;

        /* 流量报警检测 */
        if (flow_rate_current < YFS201_FLOW_MIN && delta_pulses > 0)
        {
            data->alarm_low = 1;   /* 流量过低 */
        }
        else
        {
            data->alarm_low = 0;
        }

        if (flow_rate_current > YFS201_FLOW_MAX)
        {
            data->alarm_high = 1;  /* 流量过高 */
        }
        else
        {
            data->alarm_high = 0;
        }
    }

    /* 更新上次采样值 */
    pulse_count_last = pulse_count;
    last_sample_tick = current_tick;
}

/**
 * @brief  获取瞬时流量
 * @return 流量值 (L/min)
 */
float yfs201_get_flow_rate(void)
{
    return flow_rate_current;
}

/**
 * @brief  获取累计流量
 * @return 累计流量 (L)
 */
float yfs201_get_total_volume(void)
{
    return total_volume;
}

/**
 * @brief  复位累计流量
 */
void yfs201_reset_total_volume(void)
{
    pulse_count = 0;
    pulse_count_last = 0;
    total_volume = 0.0f;

    printf("[YFS201] Total volume reset\r\n");
}

/**
 * @brief  获取脉冲计数
 * @return 脉冲计数值
 */
uint32_t yfs201_get_pulse_count(void)
{
    return pulse_count;
}

/**
 * @brief  外部中断处理函数（在中断服务函数中调用）
 */
void yfs201_pulse_handler(void)
{
    /* 脉冲计数 +1 */
    pulse_count++;
}

/*============================ 中断服务函数 ============================*/

/**
 * @brief  EXTI0 中断服务函数
 */
void EXTI0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(YFS201_EXTI_LINE) != RESET)
    {
        /* 调用脉冲处理函数 */
        yfs201_pulse_handler();

        /* 清除中断标志 */
        EXTI_ClearITPendingBit(YFS201_EXTI_LINE);
    }
}
