/**
 * @file    scheduler.h
 * @brief   简单的时间片轮询调度器
 * @author  智能水冷项目
 * @date    2025-01-24
 */

#ifndef __SCHEDULER_H
#define __SCHEDULER_H

#include "ch32v30x.h"

/**
 * @brief  调度器任务结构体
 */
typedef struct {
    void (*task_func)(void);        /* 任务函数指针 */
    uint32_t rate_ms;               /* 任务执行周期（毫秒） */
    uint32_t last_run;              /* 任务上次运行时间 */
} task_t;

/**
 * @brief  TinyML状态快照
 */
typedef struct {
    uint8_t enabled;                /* 1=使能 */
    uint8_t ready;                  /* 1=已完成预热 */
    uint8_t anomaly_active;         /* 1=异常 */
    float score;                    /* 异常分数(0~1) */
    float threshold;                /* 异常阈值(0~1) */
    uint8_t backend;                /* 0=heuristic,1=stub,2=real */
    uint8_t model_ready;            /* 1=模型就绪 */
    uint8_t backend_fallback;       /* 1=当前已降级到heuristic */
    uint32_t fallback_count;        /* 累计降级次数 */
} scheduler_tinyml_status_t;

/**
 * @brief  调度器初始化
 */
void scheduler_init(void);

/**
 * @brief  调度器运行（在主循环中调用）
 */
void scheduler_run(void);

/**
 * @brief  应用方案默认标定参数
 * @note   包含ADC（压力/电流）、YFS201流量系数与诊断阈值默认项
 */
void scheduler_apply_calibration_defaults(void);

/**
 * @brief  打印当前标定参数
 */
void scheduler_print_calibration(void);

/**
 * @brief  执行电流零点校准并同步到ADC标定
 */
void scheduler_calibrate_current_zero(void);

/**
 * @brief  执行压力零点校准（无压状态）
 */
void scheduler_calibrate_pressure_zero(void);

/**
 * @brief  打印ADC原始快照（压力/电流/TDS）
 */
void scheduler_calibration_print_adc_snapshot(void);

/**
 * @brief  微调流量标定（每升脉冲数）
 * @param  delta: 增量（可正可负）
 */
void scheduler_adjust_flow_pulses_per_liter(int16_t delta);

/**
 * @brief  保存当前标定参数到Flash
 * @note   同步保存全量诊断阈值（V4记录）
 * @return 0=成功, -1=失败
 */
int8_t scheduler_calibration_save_to_flash(void);

/**
 * @brief  从Flash加载标定参数
 * @note   若存在V4记录，同步恢复全量诊断阈值；V1/V2/V3记录回退或部分恢复
 * @return 0=成功, -1=失败
 */
int8_t scheduler_calibration_load_from_flash(void);

/**
 * @brief  设置详细日志开关
 * @param  enable: 1=开启, 0=关闭
 */
void scheduler_set_verbose_log(uint8_t enable);

/**
 * @brief  查询详细日志开关状态
 * @return 1=开启, 0=关闭
 */
uint8_t scheduler_is_verbose_log_enabled(void);

/**
 * @brief  打印Flash中标定记录状态（简版）
 */
void scheduler_calibration_print_flash_status_brief(void);

/**
 * @brief  打印Flash中标定记录状态与原始字段（详细）
 */
void scheduler_calibration_print_flash_status(void);

/**
 * @brief  清空Flash中的标定记录（恢复未标定状态）
 * @return 0=成功, -1=失败
 */
int8_t scheduler_calibration_clear_flash(void);

/**
 * @brief  切换蜂鸣器静音状态
 */
void scheduler_alarm_toggle_mute(void);

/**
 * @brief  调整蜂鸣器CRIT阈值（诊断置信度）
 * @param  delta: 增量（建议±0.05）
 */
void scheduler_alarm_adjust_diag_crit_conf(float delta);

/**
 * @brief  打印蜂鸣器状态
 */
void scheduler_alarm_print_status(void);

/**
 * @brief  打印诊断快照（各故障置信度）
 */
void scheduler_diag_print_snapshot(void);

/**
 * @brief  切换TinyML温度异常检测开关
 */
void scheduler_tinyml_toggle_enable(void);

/**
 * @brief  切换TinyML推理后端
 * @note   heuristic -> tinymaix_stub -> tinymaix_real 循环
 */
void scheduler_tinyml_cycle_backend(void);

/**
 * @brief  微调TinyML异常阈值
 * @param  delta: 增量（建议±0.05）
 */
void scheduler_tinyml_adjust_threshold(float delta);

/**
 * @brief  打印TinyML状态
 */
void scheduler_tinyml_print_status(void);

/**
 * @brief  执行TinyMaix真实后端自检
 * @note   会尝试初始化模型并执行一次样例推理，便于P1联调快速验证链路
 */
void scheduler_tinyml_self_test(void);

/**
 * @brief  获取TinyML状态快照
 * @param  status: 输出结构体
 * @return 0=成功, -1=失败
 */
int8_t scheduler_tinyml_get_status(scheduler_tinyml_status_t *status);

#endif /* __SCHEDULER_H */
