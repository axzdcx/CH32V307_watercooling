/**
 * @file    app_flow.c
 * @brief   流量传感器应用层实现
 * @author  智能水冷项目
 * @date    2025-01-27
 */

#include "app_flow.h"
#include "drv_yfs201.h"
#include "scheduler.h"
#include "debug.h"

/*============================ 私有变量 ============================*/

/* 流量数据 */
static flow_data_t flow_data = {
    .flow_rate = 0.0f,
    .total_volume = 0.0f,
    .pulse_count = 0,
    .status = FLOW_STATUS_NORMAL,
    .sensor_online = 0,
    .last_pulse_time = 0
};

/* YFS201驱动数据 */
static yfs201_data_t yfs201_data;

/* 台架联调覆盖模式（无真实水路时可注入固定流量） */
static uint8_t s_flow_bench_override_enabled = 0U;
static uint8_t s_flow_bench_preset_index = 2U; /* 默认3.0 L/min */
static const float s_flow_bench_presets[] = {0.0f, 1.2f, 3.0f, 6.0f};

/*============================ 外部函数 ============================*/
extern uint32_t get_tick_ms(void);  /* 从scheduler.c获取系统时间 */

/*============================ 内部函数 ============================*/

/**
 * @brief  更新流量状态
 */
static void flow_update_status(void)
{
    uint32_t now = get_tick_ms();

    if (s_flow_bench_override_enabled) {
        flow_data.sensor_online = 1U;
    } else {
        /* 检查传感器在线状态 */
        if (yfs201_data.valid)
        {
            flow_data.sensor_online = 1;
        }
        else
        {
            flow_data.sensor_online = 0;
            flow_data.status = FLOW_STATUS_SENSOR_ERROR;
            return;
        }
    }

    /* 检查停流（超过5秒无脉冲） */
    if (!s_flow_bench_override_enabled) {
        if (now - flow_data.last_pulse_time > FLOW_STOP_TIMEOUT)
        {
            if (flow_data.flow_rate < 0.1f)
            {
                flow_data.status = FLOW_STATUS_STOPPED;
                return;
            }
        }
    }

    /* 检查流量异常 */
    if (flow_data.flow_rate < FLOW_CRITICAL_LOW)
    {
        flow_data.status = FLOW_STATUS_LOW;  /* 危险：流量过低 */
    }
    else if (flow_data.flow_rate < FLOW_WARNING_LOW)
    {
        flow_data.status = FLOW_STATUS_LOW;  /* 警告：流量偏低 */
    }
    else if (flow_data.flow_rate > FLOW_WARNING_HIGH)
    {
        flow_data.status = FLOW_STATUS_HIGH;  /* 警告：流量过高 */
    }
    else
    {
        flow_data.status = FLOW_STATUS_NORMAL;  /* 正常 */
    }
}

/**
 * @brief  立刻将台架覆盖状态同步到当前流量快照
 * @note   用于串口命令切换后的“下一帧即生效”，避免出现一拍旧值
 */
static void flow_sync_snapshot_immediately(void)
{
    if (s_flow_bench_override_enabled) {
        flow_data.flow_rate = s_flow_bench_presets[s_flow_bench_preset_index];
        flow_data.sensor_online = 1U;
        flow_data.last_pulse_time = get_tick_ms();
    } else {
        flow_data.flow_rate = yfs201_data.flow_rate;
        flow_data.total_volume = yfs201_data.total_volume;
        flow_data.pulse_count = yfs201_data.pulse_count;
    }

    flow_update_status();
}

/*============================ 公共接口函数 ============================*/

/**
 * @brief  流量传感器初始化
 */
int8_t flow_init(void)
{
    printf("\r\n[FLOW] 初始化流量传感器...\r\n");

    /* 初始化YFS201驱动 */
    yfs201_init();

    /* 初始化流量数据 */
    flow_data.flow_rate = 0.0f;
    flow_data.total_volume = 0.0f;
    flow_data.pulse_count = 0;
    flow_data.status = FLOW_STATUS_NORMAL;
    flow_data.sensor_online = 1;
    flow_data.last_pulse_time = get_tick_ms();

    printf("[FLOW] ✅ 流量传感器初始化成功\r\n");
    if (scheduler_is_verbose_log_enabled()) {
        printf("[FLOW] - 型号: YF-S201霍尔流量传感器\r\n");
        printf("[FLOW] - 接口: G1/2螺纹（4分管）\r\n");
        printf("[FLOW] - 测量范围: 1-30 L/min\r\n");
        printf("[FLOW] - 精度: ±3%%\r\n");
        printf("[FLOW] - 引脚: PB0 (EXTI0)\r\n");
        {
            const yfs201_calib_t *calib = yfs201_get_calib();
            printf("[FLOW] - 标定系数: K=%.3f, 每升脉冲=%u\r\n",
                   calib->k_factor, calib->pulses_per_liter);
        }
        printf("[FLOW] - 流量警告阈值: <%.1f L/min 或 >%.1f L/min\r\n",
               FLOW_WARNING_LOW, FLOW_WARNING_HIGH);
    }

    return 0;
}

/**
 * @brief  流量监测任务（1秒周期）
 */
void task_flow(void)
{
    static uint32_t last_pulse_count = 0;
    static flow_status_t last_alarm_status = FLOW_STATUS_NORMAL;
    static uint32_t last_alarm_log_time = 0;
    uint32_t current_pulse_count;
    uint32_t now;

    /* 更新YFS201驱动数据 */
    yfs201_update(&yfs201_data);

    /* 同步数据到应用层 */
    flow_data.flow_rate = yfs201_data.flow_rate;
    flow_data.total_volume = yfs201_data.total_volume;
    flow_data.pulse_count = yfs201_data.pulse_count;

    /* 检测脉冲变化（用于停流检测） */
    current_pulse_count = yfs201_data.pulse_count;
    if (current_pulse_count != last_pulse_count)
    {
        flow_data.last_pulse_time = get_tick_ms();
        last_pulse_count = current_pulse_count;
    }

    /* 台架覆盖模式：使用预设流量并刷新活动时间，避免无脉冲触发停流 */
    if (s_flow_bench_override_enabled) {
        flow_data.flow_rate = s_flow_bench_presets[s_flow_bench_preset_index];
        flow_data.last_pulse_time = get_tick_ms();
    }

    /* 更新流量状态 */
    flow_update_status();

    /* 打印流量数据（调试用） */
    if (scheduler_is_verbose_log_enabled()) {
        printf("[FLOW] 流量=%.2f L/min, 累计=%.3f L, 脉冲=%lu, 状态=%d\r\n",
               flow_data.flow_rate,
               flow_data.total_volume,
               flow_data.pulse_count,
               flow_data.status);
    }

    /* 流量报警（状态变化立即打印，持续异常按周期提醒，避免刷屏） */
    now = get_tick_ms();
    if (flow_data.status != FLOW_STATUS_NORMAL) {
        uint8_t status_changed = (flow_data.status != last_alarm_status);
        uint8_t need_periodic_log = ((now - last_alarm_log_time) >= FLOW_ALARM_LOG_INTERVAL);

        if (status_changed || need_periodic_log) {
            if (flow_data.status == FLOW_STATUS_STOPPED)
            {
                printf("[FLOW] ⚠️ 停流警告！水泵可能故障或管路堵塞\r\n");
            }
            else if (flow_data.status == FLOW_STATUS_LOW)
            {
                printf("[FLOW] ⚡ 流量过低: %.2f L/min < %.1f L/min\r\n",
                       flow_data.flow_rate, FLOW_WARNING_LOW);
            }
            else if (flow_data.status == FLOW_STATUS_HIGH)
            {
                printf("[FLOW] ⚡ 流量过高: %.2f L/min > %.1f L/min\r\n",
                       flow_data.flow_rate, FLOW_WARNING_HIGH);
            }
            else if (flow_data.status == FLOW_STATUS_SENSOR_ERROR)
            {
                printf("[FLOW] ❌ 流量传感器异常，当前数据无效\r\n");
            }
            last_alarm_log_time = now;
        }
    } else if (last_alarm_status != FLOW_STATUS_NORMAL) {
        printf("[FLOW] ✅ 流量状态恢复正常\r\n");
        last_alarm_log_time = now;
    }

    last_alarm_status = flow_data.status;
}

/**
 * @brief  获取流量数据
 */
const flow_data_t* flow_get_data(void)
{
    return &flow_data;
}

/**
 * @brief  获取瞬时流量
 */
float flow_get_rate(void)
{
    if (flow_data.sensor_online)
        return flow_data.flow_rate;
    else
        return -1.0f;  /* 传感器离线 */
}

/**
 * @brief  获取累计流量
 */
float flow_get_total(void)
{
    return flow_data.total_volume;
}

/**
 * @brief  复位累计流量
 */
void flow_reset_total(void)
{
    yfs201_reset_total_volume();
    flow_data.total_volume = 0.0f;
    flow_data.pulse_count = 0;
    printf("[FLOW] 累计流量已复位\r\n");
}

/**
 * @brief  检查流量报警
 */
uint8_t flow_check_alarm(void)
{
    /* 任何异常状态都返回1 */
    return (flow_data.status != FLOW_STATUS_NORMAL);
}

/**
 * @brief  设置流量台架覆盖模式开关
 */
void flow_set_bench_override(uint8_t enable)
{
    s_flow_bench_override_enabled = enable ? 1U : 0U;
    flow_data.last_pulse_time = get_tick_ms();
    flow_sync_snapshot_immediately();
    printf("[FLOW] 台架覆盖模式: %s (%.1f L/min)\r\n",
           s_flow_bench_override_enabled ? "开启" : "关闭",
           s_flow_bench_presets[s_flow_bench_preset_index]);
}

/**
 * @brief  切换流量台架覆盖预设值
 */
void flow_cycle_bench_override_rate(void)
{
    s_flow_bench_preset_index = (uint8_t)((s_flow_bench_preset_index + 1U) %
                           (sizeof(s_flow_bench_presets) / sizeof(s_flow_bench_presets[0])));
    flow_data.last_pulse_time = get_tick_ms();
    flow_sync_snapshot_immediately();
    printf("[FLOW] 台架覆盖预设流量: %.1f L/min\r\n",
           s_flow_bench_presets[s_flow_bench_preset_index]);
}

/**
 * @brief  查询流量台架覆盖模式状态
 */
uint8_t flow_is_bench_override_enabled(void)
{
    return s_flow_bench_override_enabled;
}

/**
 * @brief  获取当前流量台架覆盖预设值
 */
float flow_get_bench_override_rate(void)
{
    return s_flow_bench_presets[s_flow_bench_preset_index];
}
