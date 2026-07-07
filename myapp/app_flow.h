/**
 * @file    app_flow.h
 * @brief   流量传感器应用层 - YF-S201水流量监测
 * @author  智能水冷项目
 * @date    2025-01-27
 *
 * 硬件型号:
 *   - 传感器: YF-S201霍尔水流量传感器
 *   - 接口: G1/2螺纹（4分管）
 *   - 测量范围: 1-30 L/min
 *   - 精度: ±3%
 *   - 接线: 红线5V, 黑线GND, 黄线→PB0（脉冲信号）
 *
 * 功能说明:
 *   - 实时监测水流速度（L/min）
 *   - 累计流量统计（L）
 *   - 异常检测（流量过低/过高/停流）
 *   - 提供数据给UI和健康评分算法
 */

#ifndef __APP_FLOW_H
#define __APP_FLOW_H

#include "ch32v30x.h"

/*============================ 流量监测配置 ============================*/

/* 流量报警阈值 */
#define FLOW_WARNING_LOW        1.0f    /* 流量过低警告（L/min） */
#define FLOW_CRITICAL_LOW       0.5f    /* 流量危险值（L/min，可能水泵堵塞） */
#define FLOW_WARNING_HIGH       12.0f   /* 流量过高警告（L/min） */

/* 停流检测时间 */
#define FLOW_STOP_TIMEOUT       5000    /* 无流量超过5秒报警（ms） */

/* 报警日志重复提醒周期（避免串口刷屏） */
#define FLOW_ALARM_LOG_INTERVAL 10000   /* 持续异常每10秒提醒一次（ms） */

/*============================ 流量状态 ============================*/

typedef enum {
    FLOW_STATUS_NORMAL = 0,     /* 正常 */
    FLOW_STATUS_LOW,            /* 流量过低 */
    FLOW_STATUS_HIGH,           /* 流量过高 */
    FLOW_STATUS_STOPPED,        /* 停流（可能水泵故障） */
    FLOW_STATUS_SENSOR_ERROR    /* 传感器异常 */
} flow_status_t;

/*============================ 流量数据结构 ============================*/

typedef struct {
    float flow_rate;            /* 瞬时流量（L/min） */
    float total_volume;         /* 累计流量（L） */
    uint32_t pulse_count;       /* 总脉冲计数 */
    flow_status_t status;       /* 流量状态 */
    uint8_t sensor_online;      /* 传感器在线状态 */
    uint32_t last_pulse_time;   /* 上次检测到脉冲的时间 */
} flow_data_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  流量传感器初始化
 * @return 0=成功, -1=失败
 */
int8_t flow_init(void);

/**
 * @brief  流量监测任务（由调度器定时调用）
 *
 * 使用方法:
 *   在调度器任务列表中注册: {task_flow, 1000, 0}  // 1秒周期
 */
void task_flow(void);

/**
 * @brief  获取流量数据
 * @return 流量数据结构指针
 */
const flow_data_t* flow_get_data(void);

/**
 * @brief  获取瞬时流量
 * @return 流量（L/min），传感器离线返回-1.0
 */
float flow_get_rate(void);

/**
 * @brief  获取累计流量
 * @return 累计流量（L）
 */
float flow_get_total(void);

/**
 * @brief  复位累计流量
 */
void flow_reset_total(void);

/**
 * @brief  检查流量是否异常
 * @return 1=有异常, 0=正常
 */
uint8_t flow_check_alarm(void);

/**
 * @brief  设置流量台架覆盖模式开关
 * @param  enable: 1=开启覆盖, 0=关闭覆盖
 * @note   开启后将使用预设流量值替代YFS201实测值，便于无水路条件下联调
 */
void flow_set_bench_override(uint8_t enable);

/**
 * @brief  切换流量台架覆盖预设值
 * @note   预设值循环：0.0 -> 1.2 -> 3.0 -> 6.0 L/min
 */
void flow_cycle_bench_override_rate(void);

/**
 * @brief  查询流量台架覆盖模式状态
 * @return 1=开启, 0=关闭
 */
uint8_t flow_is_bench_override_enabled(void);

/**
 * @brief  获取当前流量台架覆盖预设值
 * @return 预设流量值（L/min）
 */
float flow_get_bench_override_rate(void);

#endif /* __APP_FLOW_H */
