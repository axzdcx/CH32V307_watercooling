/**
 * @file    algo_diagnosis.h
 * @brief   故障诊断算法 — 6种故障模式检测
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 故障类型：
 *   0 - 散热排堵塞：flow↓ + cpu_temp↑
 *   1 - 系统气泡：flow波动大 + vibration↑ + noise↑
 *   2 - 水泵老化：电流波动 + vibration↑（无转速反馈，用电流替代）
 *   3 - 系统漏水：pressure下降趋势 + flow↓
 *   4 - 水质劣化：TDS>300ppm 或 运行时长>180天
 *   5 - 散热效率低：(cpu_temp - water_temp) > 25°C 且 pump_pwm > 80%
 *
 * 每种故障独立判断，输出置信度 0.0~1.0
 * 使用滑动窗口统计（均值、标准差、变异系数、趋势斜率）
 */

#ifndef __ALGO_DIAGNOSIS_H
#define __ALGO_DIAGNOSIS_H

#include <stdint.h>

/*============================ 故障类型定义 ============================*/

#define DIAG_FAULT_COUNT        6       /* 故障类型总数 */

typedef enum {
    DIAG_FAULT_BLOCKAGE = 0,    /* 散热排堵塞 */
    DIAG_FAULT_BUBBLE,          /* 系统气泡 */
    DIAG_FAULT_PUMP_AGING,      /* 水泵老化 */
    DIAG_FAULT_LEAKAGE,         /* 系统漏水 */
    DIAG_FAULT_WATER_QUALITY,   /* 水质劣化 */
    DIAG_FAULT_LOW_EFFICIENCY   /* 散热效率低 */
} diag_fault_type_t;

/*============================ 滑动窗口配置 ============================*/

#define DIAG_WIN_SHORT          10      /* 短窗口：10个采样点（10s @1s周期） */
#define DIAG_WIN_MEDIUM         30      /* 中窗口：30个采样点（30s） */
#define DIAG_WIN_LONG           60      /* 长窗口：60个采样点（60s） */
#define DIAG_WIN_PRESSURE       300     /* 压力窗口：300个采样点（5min） */

/*============================ 阈值定义 ============================*/

/* 故障1：散热排堵塞 */
#define DIAG_BLOCKAGE_FLOW_THRESH       1.5f    /* 流量阈值 L/min */
#define DIAG_BLOCKAGE_TEMP_THRESH       70.0f   /* CPU温度阈值 °C */
#define DIAG_BLOCKAGE_HOLD_TIME         10      /* 持续确认时间（采样点数） */

/* 故障2：系统气泡 */
#define DIAG_BUBBLE_FLOW_CV_THRESH      0.15f   /* 流量变异系数阈值 */
#define DIAG_BUBBLE_VIBRATION_THRESH    2.0f    /* 振动RMS阈值 g */
#define DIAG_BUBBLE_NOISE_THRESH        65.0f   /* 噪音RMS阈值 dB */

/* 故障3：水泵老化（用电流波动替代转速） */
#define DIAG_PUMP_CURRENT_CV_THRESH     0.10f   /* 电流变异系数阈值 */
#define DIAG_PUMP_VIBRATION_THRESH      1.5f    /* 振动RMS阈值 g */

/* 故障4：系统漏水 */
#define DIAG_LEAK_PRESSURE_SLOPE        (-0.005f) /* 压力下降斜率 MPa/min */
#define DIAG_LEAK_FLOW_DROP_PERCENT     20.0f   /* 流量下降百分比 % */

/* 故障5：水质劣化 */
#define DIAG_WATER_TDS_WARNING          300     /* TDS警告阈值 ppm */
#define DIAG_WATER_TDS_CRITICAL         500     /* TDS危险阈值 ppm */
#define DIAG_WATER_RUNTIME_DAYS         180     /* 运行时长阈值 天 */
#define DIAG_WATER_HOLD_TIME            60      /* 持续确认时间（采样点数） */

/* 故障6：散热效率低 */
#define DIAG_EFF_TEMP_DIFF_WARNING      25.0f   /* 温差警告阈值 °C */
#define DIAG_EFF_TEMP_DIFF_CRITICAL     30.0f   /* 温差危险阈值 °C */
#define DIAG_EFF_PUMP_PWM_THRESH        80      /* 水泵PWM阈值 % */

/*============================ 数据结构 ============================*/

/**
 * @brief  单个故障诊断结果
 */
typedef struct {
    uint8_t fault_id;           /* 故障类型 (diag_fault_type_t) */
    float   confidence;         /* 置信度 0.0~1.0 */
    uint8_t active;             /* 是否激活（置信度>0.5时激活） */
} diag_result_t;

/**
 * @brief  传感器输入数据包（诊断算法的输入）
 */
typedef struct {
    float cpu_temp;             /* CPU温度 °C */
    float water_temp;           /* 水温 °C */
    float flow_rate;            /* 流量 L/min */
    float vibration_rms;        /* 振动RMS g */
    float noise_db;             /* 噪音 dB */
    float pressure;             /* 水压 MPa */
    float pump_current;         /* 水泵电流 mA */
    uint16_t tds_ppm;           /* TDS ppm */
    uint8_t pump_pwm;           /* 水泵PWM占空比 % */
    uint32_t runtime_seconds;   /* 累计运行时长 秒 */
} diag_sensor_input_t;

/**
 * @brief  诊断系统总结果
 */
typedef struct {
    diag_result_t faults[DIAG_FAULT_COUNT]; /* 6种故障结果 */
    uint8_t active_count;                    /* 激活的故障数量 */
    uint8_t most_severe_id;                  /* 最严重故障ID */
    float   most_severe_confidence;          /* 最严重故障置信度 */
} diag_output_t;

/**
 * @brief  诊断阈值配置（运行时可调）
 */
typedef struct {
    float blockage_flow_thresh;      /* 堵塞流量阈值 L/min */
    float blockage_temp_thresh;      /* 堵塞CPU温度阈值 °C */
    uint16_t blockage_hold_time;     /* 堵塞持续确认点数 */
    uint8_t blockage_pump_pwm_min;   /* 堵塞判定最小水泵PWM % */

    float bubble_flow_cv_thresh;     /* 气泡流量CV阈值 */
    float pump_current_cv_thresh;    /* 水泵老化电流CV阈值 */

    float leak_pressure_slope;       /* 漏水压力斜率阈值 MPa/min */
    float leak_flow_drop_percent;    /* 漏水流量下降百分比 % */

    uint16_t water_tds_warning;      /* 水质TDS警告阈值 ppm */
    uint16_t water_tds_critical;     /* 水质TDS危险阈值 ppm */

    float eff_temp_diff_warning;     /* 效率温差警告阈值 °C */
    float eff_temp_diff_critical;    /* 效率温差危险阈值 °C */
    uint8_t eff_pump_pwm_thresh;     /* 效率判定水泵PWM阈值 % */
} diag_threshold_config_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化诊断系统
 */
void diag_init(void);

/**
 * @brief  诊断系统更新（由调度器定时调用，建议2s周期）
 * @param  input: 传感器数据包
 */
void diag_update(const diag_sensor_input_t *input);

/**
 * @brief  获取诊断结果
 * @return 诊断结果指针
 */
const diag_output_t* diag_get_result(void);

/**
 * @brief  获取指定故障的置信度
 * @param  fault_id: 故障类型
 * @return 置信度 0.0~1.0
 */
float diag_get_confidence(diag_fault_type_t fault_id);

/**
 * @brief  是否有任何故障激活
 * @return 1=有故障, 0=无故障
 */
uint8_t diag_has_active_fault(void);

/**
 * @brief  获取故障名称字符串
 * @param  fault_id: 故障类型
 * @return 故障名称
 */
const char* diag_get_fault_name(diag_fault_type_t fault_id);

/**
 * @brief  重置诊断阈值为方案默认值
 */
void diag_threshold_reset_defaults(void);

/**
 * @brief  获取当前诊断阈值配置
 * @param  cfg: 输出配置
 * @return 0=成功, -1=参数无效
 */
int8_t diag_threshold_get(diag_threshold_config_t *cfg);

/**
 * @brief  设置诊断阈值配置
 * @param  cfg: 输入配置
 * @return 0=成功, -1=参数非法
 */
int8_t diag_threshold_set(const diag_threshold_config_t *cfg);

#endif /* __ALGO_DIAGNOSIS_H */
