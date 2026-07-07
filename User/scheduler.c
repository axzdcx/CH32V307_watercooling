/**
 * @file    scheduler.c
 * @brief   简单的时间片轮询调度器实现
 * @author  智能水冷项目
 * @date    2025-01-24
 */

#include "scheduler.h"
#include "mydefine.h"
#include "debug.h"
#include "ch32v30x.h"
#include "bsp_tim.h"    /* 使用BSP层的TIM2定时器 */
#include "algo_pid.h"   /* PID 控制器 */
#include "algo_control.h"   /* 多层防震荡控制 + 自适应PID */
#include "algo_predict.h"   /* 温度预测 */
#include "algo_diagnosis.h" /* 故障诊断 */
#include "algo_health.h"    /* 健康评分 */
#include "algo_energy.h"    /* 能效计算 */
#include "app_temp.h"   /* 温度传感器任务 */
#include "app_pwm.h"    /* PWM控制任务 */
#include "app_flow.h"   /* 流量传感器任务 */
#include "app_websocket.h"  /* WebSocket任务 */
#include "app_tds.h"        /* TDS水质传感器任务 */
#include "drv_adxl345.h"    /* ADXL345振动传感器 */
#include "drv_current.h"    /* ACS712电流传感器 */
#include "bsp_adc.h"        /* ADC压力/电流/TDS读取 */
#include "app_voice.h"      /* 语音交互任务 */
#include "app_cloud.h"      /* 4G云端通信任务 */
#include "app_ui.h"         /* UI多页面任务 */
#include <stdlib.h>     /* rand() 函数 */

/*============================ PID 温度控制 ============================*/

static pid_controller_t pid_temp;           /* PID 控制器 */
static float target_temp = 60.0f;           /* 目标温度 60℃ */
static uint8_t g_verbose_log_enabled = SYSTEM_VERBOSE_LOG_DEFAULT;  /* 详细日志开关 */

typedef enum {
    BUZZER_ALERT_OFF = 0,
    BUZZER_ALERT_WARN,
    BUZZER_ALERT_CRIT
} buzzer_alert_level_t;

typedef enum {
    TINYML_BACKEND_HEURISTIC = 0,
    TINYML_BACKEND_TINYMAIX_STUB = 1,
    TINYML_BACKEND_TINYMAIX_REAL = 2
} tinyml_backend_t;

typedef struct {
    float cpu_temp;
    float water_temp;
    float flow_rate;
    float last_cpu_temp;
    float window_mean;
    float window_mad;
} tinyml_temp_feature_t;

typedef struct {
    uint8_t enabled;
    uint8_t ready;
    uint8_t anomaly_active;
    uint8_t hold_count;
    uint8_t sample_count;
    uint8_t write_index;
    uint8_t backend;
    uint8_t model_ready;
    uint8_t backend_fallback;
    uint8_t placeholder_warned;
    float score;
    float threshold;
    float last_cpu_temp;
    uint32_t infer_count;
    uint32_t fallback_count;
    float cpu_hist[24];
} tinyml_temp_state_t;

static uint8_t s_buzzer_initialized = 0U;
static buzzer_alert_level_t s_buzzer_level = BUZZER_ALERT_OFF;
static uint8_t s_buzzer_muted = 0U;
static float s_alarm_diag_crit_conf = 0.80f;
static tinyml_temp_state_t s_tinyml;

/*============================ 标定参数默认配置 ============================*/

/* 方案默认值（硬件到位后可通过串口命令触发重标定） */
#define CALIB_DEFAULT_PRESSURE_OFFSET_MV      0
#define CALIB_DEFAULT_CURRENT_OFFSET_MV       0
#define CALIB_DEFAULT_CURRENT_SENSITIVITY_MV  CURRENT_SENSITIVITY_MV
#define CALIB_DEFAULT_FLOW_K_FACTOR           YFS201_K_FACTOR
#define CALIB_DEFAULT_FLOW_PULSES_PER_LITER   YFS201_PULSES_PER_LITER

/* IWDG参数（LSI约40kHz，预分频64） */
#define IWDG_LSI_FREQ_HZ                       40000U
#define IWDG_PRESCALER_DIV                     64U
#define IWDG_FLAG_WAIT_MAX                     1000000U

/* 蜂鸣器告警（有源蜂鸣器，PC11） */
#define ENABLE_BUZZER_ALARM                    1
#define BUZZER_GPIO_PORT                       GPIOC
#define BUZZER_GPIO_PIN                        GPIO_Pin_11
#define BUZZER_ACTIVE_LEVEL                    1U   /* 1=高电平响，0=低电平响 */
#define BUZZER_PATTERN_PERIOD_MS               2000U
#define BUZZER_WARN_ON_MS                      100U
#define BUZZER_CRIT_ON1_MS                     200U
#define BUZZER_CRIT_GAP1_MS                    200U
#define BUZZER_CRIT_ON2_MS                     200U
#define ALARM_DIAG_CRIT_CONF_DEFAULT           0.80f
#define ALARM_DIAG_CRIT_CONF_MIN               0.50f
#define ALARM_DIAG_CRIT_CONF_MAX               0.95f

/* TinyML温度异常检测（当前为可替换的占位推理骨架） */
#define ENABLE_TINYML_TEMP_ANOMALY             1
#define TINYML_TEMP_WINDOW_SIZE                24U
#define TINYML_TEMP_WARMUP_MIN                 8U
#define TINYML_TEMP_HOLD_ON                    3U
#define TINYML_TEMP_HOLD_OFF                   1U
#define TINYML_SCORE_THRESHOLD_DEFAULT         0.70f
#define TINYML_SCORE_THRESHOLD_MIN             0.40f
#define TINYML_SCORE_THRESHOLD_MAX             0.95f
#define TINYML_BACKEND_DEFAULT                 TINYML_BACKEND_HEURISTIC

/* 标定参数Flash存储（预留地址：224KB模式最后8KB，两页A/B冗余） */
#define CALIB_FLASH_PAGE_SIZE_BYTES            4096U
#define CALIB_FLASH_PAGE_A_ADDR                (FLASH_BASE + 0x00036000U)
#define CALIB_FLASH_PAGE_B_ADDR                (FLASH_BASE + 0x00037000U)
#define CALIB_FLASH_PAGE_A_END                 (CALIB_FLASH_PAGE_A_ADDR + CALIB_FLASH_PAGE_SIZE_BYTES)
#define CALIB_FLASH_PAGE_B_END                 (CALIB_FLASH_PAGE_B_ADDR + CALIB_FLASH_PAGE_SIZE_BYTES)
#define CALIB_FLASH_ERASED_WORD                0xFFFFFFFFU
#define CALIB_FLASH_MAGIC                      0x43414C42U  /* 'CALB' */
#define CALIB_FLASH_VERSION_V1                 0x00000001U
#define CALIB_FLASH_VERSION_V2                 0x00000002U
#define CALIB_FLASH_VERSION_V3                 0x00000003U
#define CALIB_FLASH_VERSION_V4                 0x00000004U

/* 标定写入节流阈值（小于阈值视为无效变化，不写Flash） */
#define CALIB_SAVE_DELTA_PRESSURE_MV           2U
#define CALIB_SAVE_DELTA_CURRENT_OFFSET_MV     2U
#define CALIB_SAVE_DELTA_CURRENT_SENS_MV       1U
#define CALIB_SAVE_DELTA_FLOW_PULSES           1U
#define CALIB_SAVE_DELTA_FLOW_K_Q1000          5U
#define CALIB_SAVE_DELTA_DIAG_BLOCK_FLOW_Q100  5U
#define CALIB_SAVE_DELTA_DIAG_BLOCK_PWM        1U

/* V3扩展：在不增加记录尺寸的前提下，复用高位保存诊断阈值 */
#define CALIB_PACK_BASE_MASK_16                0x0000FFFFU
#define CALIB_PACK_DIAG_SHIFT                  16U
#define CALIB_PACK_DIAG_FLOW_Q100_MASK         0x03FFU
#define CALIB_PACK_DIAG_PWM_MASK               0x007FU

#define CALIB_DEFAULT_DIAG_BLOCKAGE_FLOW_Q100  ((uint16_t)(DIAG_BLOCKAGE_FLOW_THRESH * 100.0f + 0.5f))
#define CALIB_DEFAULT_DIAG_BLOCKAGE_PUMP_PWM   40U

/* V4位流编码参数（总计160bit=5x32bit） */
#define CALIB_V4_BITS_PRESSURE_OFFSET          11U
#define CALIB_V4_BITS_CURRENT_OFFSET           11U
#define CALIB_V4_BITS_CURRENT_SENS             9U
#define CALIB_V4_BITS_FLOW_PULSES              10U
#define CALIB_V4_BITS_BLOCK_FLOW_Q100          10U
#define CALIB_V4_BITS_BLOCK_TEMP_Q10           10U
#define CALIB_V4_BITS_BLOCK_HOLD               7U
#define CALIB_V4_BITS_BLOCK_PWM                7U
#define CALIB_V4_BITS_BUBBLE_CV_Q1000          10U
#define CALIB_V4_BITS_PUMP_CV_Q1000            10U
#define CALIB_V4_BITS_LEAK_SLOPE_Q10000        9U
#define CALIB_V4_BITS_LEAK_DROP_Q10            10U
#define CALIB_V4_BITS_TDS_WARNING              10U
#define CALIB_V4_BITS_TDS_CRITICAL             11U
#define CALIB_V4_BITS_EFF_WARN_Q10             9U
#define CALIB_V4_BITS_EFF_CRIT_Q10             9U
#define CALIB_V4_BITS_EFF_PUMP_PWM             7U

#define CALIB_V4_PRESSURE_OFFSET_BIAS          1000
#define CALIB_V4_CURRENT_OFFSET_BIAS           1000
#define CALIB_V4_CURRENT_SENS_BASE             50U
#define CALIB_V4_FLOW_PULSES_BASE              200U
#define CALIB_V4_BLOCK_FLOW_Q100_BASE          20U
#define CALIB_V4_BLOCK_TEMP_Q10_BASE           400U
#define CALIB_V4_BLOCK_HOLD_BASE               3U
#define CALIB_V4_BUBBLE_CV_Q1000_BASE          20U
#define CALIB_V4_PUMP_CV_Q1000_BASE            20U
#define CALIB_V4_LEAK_SLOPE_Q10000_BASE        5U
#define CALIB_V4_LEAK_DROP_Q10_BASE            50U
#define CALIB_V4_TDS_WARNING_BASE              50U
#define CALIB_V4_TDS_CRITICAL_BASE             50U
#define CALIB_V4_EFF_WARN_Q10_BASE             80U
#define CALIB_V4_EFF_CRIT_Q10_BASE             80U

/**
 * @brief  传感器数据选择（离线时使用模拟值）
 */
static float select_sensor_value(float real_value, uint8_t online, float sim_value)
{
    return online ? real_value : sim_value;
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t pressure_offset_mv;
    int32_t current_offset_mv;
    uint32_t current_sensitivity_mv;
    uint32_t flow_pulses_per_liter;
    uint32_t flow_k_factor_q1000;
    uint32_t checksum;
} calib_flash_record_v1_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    int32_t pressure_offset_mv;
    int32_t current_offset_mv;
    uint32_t current_sensitivity_mv;
    uint32_t flow_pulses_per_liter;
    uint32_t flow_k_factor_q1000;
    uint32_t checksum;
} calib_flash_record_t;

/**
 * @brief  计算标定记录校验值（不含checksum字段）
 */
static uint32_t calib_flash_checksum_words(const uint32_t *word, uint32_t count_without_checksum)
{
    uint32_t checksum = 0x5A5AA5A5U;
    uint32_t index;

    for (index = 0U; index < count_without_checksum; index++) {
        checksum ^= (word[index] + 0x9E3779B9U + (checksum << 6U) + (checksum >> 2U));
    }

    return checksum;
}

/**
 * @brief  计算V2标定记录校验值（不含checksum字段）
 */
static uint32_t calib_flash_checksum_v2(const calib_flash_record_t *record)
{
    return calib_flash_checksum_words((const uint32_t *)record,
                                      (sizeof(calib_flash_record_t) / sizeof(uint32_t) - 1U));
}

/**
 * @brief  计算V1标定记录校验值（不含checksum字段）
 */
static uint32_t calib_flash_checksum_v1(const calib_flash_record_v1_t *record)
{
    return calib_flash_checksum_words((const uint32_t *)record,
                                      (sizeof(calib_flash_record_v1_t) / sizeof(uint32_t) - 1U));
}

/**
 * @brief  将流量阈值(float)转换为q100
 */
static uint16_t calib_diag_flow_to_q100(float flow_lpm)
{
    if (flow_lpm < 0.0f) {
        return 0U;
    }
    if (flow_lpm > 655.35f) {
        flow_lpm = 655.35f;
    }
    return (uint16_t)(flow_lpm * 100.0f + 0.5f);
}

/**
 * @brief  将流量阈值q100转换为float
 */
static float calib_diag_flow_from_q100(uint16_t flow_q100)
{
    return (float)flow_q100 / 100.0f;
}

/**
 * @brief  打包flow_k字段（低16位保留原值，高位保存堵塞流量阈值q100）
 */
static uint32_t calib_flash_pack_flow_k_q1000(uint32_t flow_k_q1000, uint16_t diag_flow_q100)
{
    uint32_t packed = flow_k_q1000 & CALIB_PACK_BASE_MASK_16;
    packed |= ((uint32_t)(diag_flow_q100 & CALIB_PACK_DIAG_FLOW_Q100_MASK) << CALIB_PACK_DIAG_SHIFT);
    return packed;
}

/**
 * @brief  打包flow_pulses字段（低16位保留原值，高位保存堵塞泵PWM门限）
 */
static uint32_t calib_flash_pack_flow_pulses(uint32_t flow_pulses, uint8_t diag_pump_pwm)
{
    uint32_t packed = flow_pulses & CALIB_PACK_BASE_MASK_16;
    packed |= ((uint32_t)(diag_pump_pwm & CALIB_PACK_DIAG_PWM_MASK) << CALIB_PACK_DIAG_SHIFT);
    return packed;
}

/**
 * @brief  解包flow_k原始值（q1000）
 */
static uint32_t calib_flash_unpack_flow_k_q1000(uint32_t packed_flow_k)
{
    return packed_flow_k & CALIB_PACK_BASE_MASK_16;
}

/**
 * @brief  解包flow_pulses原始值
 */
static uint32_t calib_flash_unpack_flow_pulses(uint32_t packed_flow_pulses)
{
    return packed_flow_pulses & CALIB_PACK_BASE_MASK_16;
}

/**
 * @brief  解包堵塞流量阈值q100（V2记录返回默认值）
 */
static uint16_t calib_flash_unpack_diag_block_flow_q100(uint32_t packed_flow_k, uint32_t version)
{
    if (version < CALIB_FLASH_VERSION_V3) {
        return CALIB_DEFAULT_DIAG_BLOCKAGE_FLOW_Q100;
    }
    return (uint16_t)((packed_flow_k >> CALIB_PACK_DIAG_SHIFT) & CALIB_PACK_DIAG_FLOW_Q100_MASK);
}

/**
 * @brief  解包堵塞泵PWM门限（V2记录返回默认值）
 */
static uint8_t calib_flash_unpack_diag_block_pwm(uint32_t packed_flow_pulses, uint32_t version)
{
    if (version < CALIB_FLASH_VERSION_V3) {
        return CALIB_DEFAULT_DIAG_BLOCKAGE_PUMP_PWM;
    }
    return (uint8_t)((packed_flow_pulses >> CALIB_PACK_DIAG_SHIFT) & CALIB_PACK_DIAG_PWM_MASK);
}

/**
 * @brief  将堵塞阈值写回运行时配置
 * @return 0=成功, -1=失败
 */
static int8_t calib_diag_apply_runtime_blockage(uint16_t flow_q100, uint8_t pump_pwm)
{
    diag_threshold_config_t cfg;

    if (diag_threshold_get(&cfg) != 0) {
        return -1;
    }

    cfg.blockage_flow_thresh = calib_diag_flow_from_q100(flow_q100);
    cfg.blockage_pump_pwm_min = pump_pwm;
    return diag_threshold_set(&cfg);
}

/* 前向声明：用于V4变化比较 */
static uint32_t calib_abs_diff_i32(int32_t left, int32_t right);
static uint32_t calib_abs_diff_u32(uint32_t left, uint32_t right);

/**
 * @brief  校验诊断阈值配置（与算法模块约束保持一致）
 */
static uint8_t calib_diag_validate_full(const diag_threshold_config_t *cfg)
{
    if (cfg == NULL) return 0;

    if (cfg->blockage_flow_thresh < 0.2f || cfg->blockage_flow_thresh > 10.0f) return 0;
    if (cfg->blockage_temp_thresh < 40.0f || cfg->blockage_temp_thresh > 95.0f) return 0;
    if (cfg->blockage_hold_time < 3 || cfg->blockage_hold_time > 120) return 0;
    if (cfg->blockage_pump_pwm_min > 100) return 0;

    if (cfg->bubble_flow_cv_thresh < 0.02f || cfg->bubble_flow_cv_thresh > 1.0f) return 0;
    if (cfg->pump_current_cv_thresh < 0.02f || cfg->pump_current_cv_thresh > 1.0f) return 0;

    if (cfg->leak_pressure_slope > -0.0005f || cfg->leak_pressure_slope < -0.05f) return 0;
    if (cfg->leak_flow_drop_percent < 5.0f || cfg->leak_flow_drop_percent > 95.0f) return 0;

    if (cfg->water_tds_warning < 50 || cfg->water_tds_warning > 1000) return 0;
    if (cfg->water_tds_critical < cfg->water_tds_warning || cfg->water_tds_critical > 2000) return 0;

    if (cfg->eff_temp_diff_warning < 8.0f || cfg->eff_temp_diff_warning > 40.0f) return 0;
    if (cfg->eff_temp_diff_critical < cfg->eff_temp_diff_warning || cfg->eff_temp_diff_critical > 50.0f) return 0;
    if (cfg->eff_pump_pwm_thresh > 100) return 0;

    return 1;
}

/**
 * @brief  生成N位掩码
 */
static uint32_t calib_v4_mask(uint8_t bits)
{
    if (bits >= 32U) {
        return 0xFFFFFFFFU;
    }
    return (1UL << bits) - 1UL;
}

/**
 * @brief  向V4位流写入字段
 */
static void calib_v4_write_bits(uint32_t words[5], uint16_t *bit_pos, uint32_t value, uint8_t bits)
{
    uint16_t remaining = bits;
    uint32_t local_value = value;

    while (remaining > 0U) {
        uint16_t word_index = (uint16_t)(*bit_pos / 32U);
        uint16_t bit_offset = (uint16_t)(*bit_pos % 32U);
        uint8_t chunk = (uint8_t)(32U - bit_offset);
        uint32_t chunk_mask;
        uint32_t chunk_value;

        if (chunk > remaining) {
            chunk = (uint8_t)remaining;
        }

        chunk_mask = calib_v4_mask(chunk);
        chunk_value = local_value & chunk_mask;
        words[word_index] |= (chunk_value << bit_offset);

        local_value >>= chunk;
        *bit_pos = (uint16_t)(*bit_pos + chunk);
        remaining = (uint16_t)(remaining - chunk);
    }
}

/**
 * @brief  从V4位流读取字段
 */
static uint32_t calib_v4_read_bits(const uint32_t words[5], uint16_t *bit_pos, uint8_t bits)
{
    uint16_t remaining = bits;
    uint8_t dst_shift = 0U;
    uint32_t value = 0U;

    while (remaining > 0U) {
        uint16_t word_index = (uint16_t)(*bit_pos / 32U);
        uint16_t bit_offset = (uint16_t)(*bit_pos % 32U);
        uint8_t chunk = (uint8_t)(32U - bit_offset);
        uint32_t chunk_mask;
        uint32_t chunk_value;

        if (chunk > remaining) {
            chunk = (uint8_t)remaining;
        }

        chunk_mask = calib_v4_mask(chunk);
        chunk_value = (words[word_index] >> bit_offset) & chunk_mask;
        value |= (chunk_value << dst_shift);

        dst_shift = (uint8_t)(dst_shift + chunk);
        *bit_pos = (uint16_t)(*bit_pos + chunk);
        remaining = (uint16_t)(remaining - chunk);
    }

    return value;
}

/**
 * @brief  诊断温度阈值转换为q10
 */
static uint16_t calib_diag_temp_to_q10(float temp)
{
    if (temp < 0.0f) {
        return 0U;
    }
    return (uint16_t)(temp * 10.0f + 0.5f);
}

/**
 * @brief  诊断CV阈值转换为q1000
 */
static uint16_t calib_diag_cv_to_q1000(float cv)
{
    if (cv < 0.0f) {
        return 0U;
    }
    return (uint16_t)(cv * 1000.0f + 0.5f);
}

/**
 * @brief  漏水斜率阈值转换为abs(q10000)
 */
static uint16_t calib_diag_leak_slope_abs_to_q10000(float slope)
{
    float abs_slope = (slope < 0.0f) ? (-slope) : slope;
    return (uint16_t)(abs_slope * 10000.0f + 0.5f);
}

/**
 * @brief  百分比阈值转换为q10
 */
static uint16_t calib_diag_percent_to_q10(float percent)
{
    if (percent < 0.0f) {
        return 0U;
    }
    return (uint16_t)(percent * 10.0f + 0.5f);
}

/**
 * @brief  打包V4记录载荷（标定 + 全量诊断阈值）
 * @return 0=成功, -1=参数非法
 */
static int8_t calib_flash_pack_v4_payload(const calib_adc_t *adc_cfg,
                                          const yfs201_calib_t *flow_cfg,
                                          const diag_threshold_config_t *diag_cfg,
                                          uint32_t words[5])
{
    uint16_t bit_pos = 0U;
    uint32_t pressure_enc;
    uint32_t current_enc;
    uint32_t current_sens_enc;
    uint32_t flow_pulses_enc;
    uint32_t block_flow_enc;
    uint32_t block_temp_enc;
    uint32_t block_hold_enc;
    uint32_t block_pwm_enc;
    uint32_t bubble_cv_enc;
    uint32_t pump_cv_enc;
    uint32_t leak_slope_enc;
    uint32_t leak_drop_enc;
    uint32_t tds_warn_enc;
    uint32_t tds_crit_enc;
    uint32_t eff_warn_enc;
    uint32_t eff_crit_enc;
    uint32_t eff_pwm_enc;

    if ((adc_cfg == NULL) || (flow_cfg == NULL) || (diag_cfg == NULL) || (words == NULL)) {
        return -1;
    }

    if (!calib_diag_validate_full(diag_cfg)) {
        return -1;
    }

    if ((adc_cfg->pressure_offset_mv < -1000) || (adc_cfg->pressure_offset_mv > 1000)) return -1;
    if ((adc_cfg->current_offset_mv < -1000) || (adc_cfg->current_offset_mv > 1000)) return -1;
    if ((adc_cfg->current_sensitivity_mv < 50U) || (adc_cfg->current_sensitivity_mv > 500U)) return -1;
    if ((flow_cfg->pulses_per_liter < 200U) || (flow_cfg->pulses_per_liter > 1200U)) return -1;

    pressure_enc = (uint32_t)(adc_cfg->pressure_offset_mv + CALIB_V4_PRESSURE_OFFSET_BIAS);
    current_enc = (uint32_t)(adc_cfg->current_offset_mv + CALIB_V4_CURRENT_OFFSET_BIAS);
    current_sens_enc = (uint32_t)(adc_cfg->current_sensitivity_mv - CALIB_V4_CURRENT_SENS_BASE);
    flow_pulses_enc = (uint32_t)(flow_cfg->pulses_per_liter - CALIB_V4_FLOW_PULSES_BASE);
    block_flow_enc = (uint32_t)(calib_diag_flow_to_q100(diag_cfg->blockage_flow_thresh) - CALIB_V4_BLOCK_FLOW_Q100_BASE);
    block_temp_enc = (uint32_t)(calib_diag_temp_to_q10(diag_cfg->blockage_temp_thresh) - CALIB_V4_BLOCK_TEMP_Q10_BASE);
    block_hold_enc = (uint32_t)(diag_cfg->blockage_hold_time - CALIB_V4_BLOCK_HOLD_BASE);
    block_pwm_enc = diag_cfg->blockage_pump_pwm_min;
    bubble_cv_enc = (uint32_t)(calib_diag_cv_to_q1000(diag_cfg->bubble_flow_cv_thresh) - CALIB_V4_BUBBLE_CV_Q1000_BASE);
    pump_cv_enc = (uint32_t)(calib_diag_cv_to_q1000(diag_cfg->pump_current_cv_thresh) - CALIB_V4_PUMP_CV_Q1000_BASE);
    leak_slope_enc = (uint32_t)(calib_diag_leak_slope_abs_to_q10000(diag_cfg->leak_pressure_slope) - CALIB_V4_LEAK_SLOPE_Q10000_BASE);
    leak_drop_enc = (uint32_t)(calib_diag_percent_to_q10(diag_cfg->leak_flow_drop_percent) - CALIB_V4_LEAK_DROP_Q10_BASE);
    tds_warn_enc = (uint32_t)(diag_cfg->water_tds_warning - CALIB_V4_TDS_WARNING_BASE);
    tds_crit_enc = (uint32_t)(diag_cfg->water_tds_critical - CALIB_V4_TDS_CRITICAL_BASE);
    eff_warn_enc = (uint32_t)(calib_diag_temp_to_q10(diag_cfg->eff_temp_diff_warning) - CALIB_V4_EFF_WARN_Q10_BASE);
    eff_crit_enc = (uint32_t)(calib_diag_temp_to_q10(diag_cfg->eff_temp_diff_critical) - CALIB_V4_EFF_CRIT_Q10_BASE);
    eff_pwm_enc = diag_cfg->eff_pump_pwm_thresh;

    words[0] = 0U;
    words[1] = 0U;
    words[2] = 0U;
    words[3] = 0U;
    words[4] = 0U;

    calib_v4_write_bits(words, &bit_pos, pressure_enc, CALIB_V4_BITS_PRESSURE_OFFSET);
    calib_v4_write_bits(words, &bit_pos, current_enc, CALIB_V4_BITS_CURRENT_OFFSET);
    calib_v4_write_bits(words, &bit_pos, current_sens_enc, CALIB_V4_BITS_CURRENT_SENS);
    calib_v4_write_bits(words, &bit_pos, flow_pulses_enc, CALIB_V4_BITS_FLOW_PULSES);
    calib_v4_write_bits(words, &bit_pos, block_flow_enc, CALIB_V4_BITS_BLOCK_FLOW_Q100);
    calib_v4_write_bits(words, &bit_pos, block_temp_enc, CALIB_V4_BITS_BLOCK_TEMP_Q10);
    calib_v4_write_bits(words, &bit_pos, block_hold_enc, CALIB_V4_BITS_BLOCK_HOLD);
    calib_v4_write_bits(words, &bit_pos, block_pwm_enc, CALIB_V4_BITS_BLOCK_PWM);
    calib_v4_write_bits(words, &bit_pos, bubble_cv_enc, CALIB_V4_BITS_BUBBLE_CV_Q1000);
    calib_v4_write_bits(words, &bit_pos, pump_cv_enc, CALIB_V4_BITS_PUMP_CV_Q1000);
    calib_v4_write_bits(words, &bit_pos, leak_slope_enc, CALIB_V4_BITS_LEAK_SLOPE_Q10000);
    calib_v4_write_bits(words, &bit_pos, leak_drop_enc, CALIB_V4_BITS_LEAK_DROP_Q10);
    calib_v4_write_bits(words, &bit_pos, tds_warn_enc, CALIB_V4_BITS_TDS_WARNING);
    calib_v4_write_bits(words, &bit_pos, tds_crit_enc, CALIB_V4_BITS_TDS_CRITICAL);
    calib_v4_write_bits(words, &bit_pos, eff_warn_enc, CALIB_V4_BITS_EFF_WARN_Q10);
    calib_v4_write_bits(words, &bit_pos, eff_crit_enc, CALIB_V4_BITS_EFF_CRIT_Q10);
    calib_v4_write_bits(words, &bit_pos, eff_pwm_enc, CALIB_V4_BITS_EFF_PUMP_PWM);

    if (bit_pos != 160U) {
        return -1;
    }
    return 0;
}

/**
 * @brief  解包V4记录载荷（标定 + 全量诊断阈值）
 * @return 0=成功, -1=数据非法
 */
static int8_t calib_flash_unpack_v4_payload(const calib_flash_record_t *record,
                                            int32_t *pressure_offset_mv,
                                            int32_t *current_offset_mv,
                                            uint32_t *current_sensitivity_mv,
                                            uint32_t *flow_pulses_per_liter,
                                            uint32_t *flow_k_factor_q1000,
                                            diag_threshold_config_t *diag_cfg)
{
    uint32_t words[5];
    uint16_t bit_pos = 0U;
    uint32_t pressure_enc;
    uint32_t current_enc;
    uint32_t current_sens_enc;
    uint32_t flow_pulses_enc;
    uint32_t block_flow_enc;
    uint32_t block_temp_enc;
    uint32_t block_hold_enc;
    uint32_t block_pwm_enc;
    uint32_t bubble_cv_enc;
    uint32_t pump_cv_enc;
    uint32_t leak_slope_enc;
    uint32_t leak_drop_enc;
    uint32_t tds_warn_enc;
    uint32_t tds_crit_enc;
    uint32_t eff_warn_enc;
    uint32_t eff_crit_enc;
    uint32_t eff_pwm_enc;
    diag_threshold_config_t cfg;

    if ((record == NULL) || (pressure_offset_mv == NULL) || (current_offset_mv == NULL) ||
        (current_sensitivity_mv == NULL) || (flow_pulses_per_liter == NULL) ||
        (flow_k_factor_q1000 == NULL) || (diag_cfg == NULL)) {
        return -1;
    }

    words[0] = (uint32_t)record->pressure_offset_mv;
    words[1] = (uint32_t)record->current_offset_mv;
    words[2] = record->current_sensitivity_mv;
    words[3] = record->flow_pulses_per_liter;
    words[4] = record->flow_k_factor_q1000;

    pressure_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_PRESSURE_OFFSET);
    current_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_CURRENT_OFFSET);
    current_sens_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_CURRENT_SENS);
    flow_pulses_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_FLOW_PULSES);
    block_flow_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_BLOCK_FLOW_Q100);
    block_temp_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_BLOCK_TEMP_Q10);
    block_hold_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_BLOCK_HOLD);
    block_pwm_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_BLOCK_PWM);
    bubble_cv_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_BUBBLE_CV_Q1000);
    pump_cv_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_PUMP_CV_Q1000);
    leak_slope_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_LEAK_SLOPE_Q10000);
    leak_drop_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_LEAK_DROP_Q10);
    tds_warn_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_TDS_WARNING);
    tds_crit_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_TDS_CRITICAL);
    eff_warn_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_EFF_WARN_Q10);
    eff_crit_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_EFF_CRIT_Q10);
    eff_pwm_enc = calib_v4_read_bits(words, &bit_pos, CALIB_V4_BITS_EFF_PUMP_PWM);

    if (bit_pos != 160U) {
        return -1;
    }

    *pressure_offset_mv = (int32_t)pressure_enc - CALIB_V4_PRESSURE_OFFSET_BIAS;
    *current_offset_mv = (int32_t)current_enc - CALIB_V4_CURRENT_OFFSET_BIAS;
    *current_sensitivity_mv = current_sens_enc + CALIB_V4_CURRENT_SENS_BASE;
    *flow_pulses_per_liter = flow_pulses_enc + CALIB_V4_FLOW_PULSES_BASE;
    *flow_k_factor_q1000 = ((*flow_pulses_per_liter) * 1000U + 30U) / 60U;

    cfg.blockage_flow_thresh = (float)(block_flow_enc + CALIB_V4_BLOCK_FLOW_Q100_BASE) / 100.0f;
    cfg.blockage_temp_thresh = (float)(block_temp_enc + CALIB_V4_BLOCK_TEMP_Q10_BASE) / 10.0f;
    cfg.blockage_hold_time = (uint16_t)(block_hold_enc + CALIB_V4_BLOCK_HOLD_BASE);
    cfg.blockage_pump_pwm_min = (uint8_t)block_pwm_enc;
    cfg.bubble_flow_cv_thresh = (float)(bubble_cv_enc + CALIB_V4_BUBBLE_CV_Q1000_BASE) / 1000.0f;
    cfg.pump_current_cv_thresh = (float)(pump_cv_enc + CALIB_V4_PUMP_CV_Q1000_BASE) / 1000.0f;
    cfg.leak_pressure_slope = -(float)(leak_slope_enc + CALIB_V4_LEAK_SLOPE_Q10000_BASE) / 10000.0f;
    cfg.leak_flow_drop_percent = (float)(leak_drop_enc + CALIB_V4_LEAK_DROP_Q10_BASE) / 10.0f;
    cfg.water_tds_warning = (uint16_t)(tds_warn_enc + CALIB_V4_TDS_WARNING_BASE);
    cfg.water_tds_critical = (uint16_t)(tds_crit_enc + CALIB_V4_TDS_CRITICAL_BASE);
    cfg.eff_temp_diff_warning = (float)(eff_warn_enc + CALIB_V4_EFF_WARN_Q10_BASE) / 10.0f;
    cfg.eff_temp_diff_critical = (float)(eff_crit_enc + CALIB_V4_EFF_CRIT_Q10_BASE) / 10.0f;
    cfg.eff_pump_pwm_thresh = (uint8_t)eff_pwm_enc;

    if ((*pressure_offset_mv < -1000) || (*pressure_offset_mv > 1000)) return -1;
    if ((*current_offset_mv < -1000) || (*current_offset_mv > 1000)) return -1;
    if ((*current_sensitivity_mv < 50U) || (*current_sensitivity_mv > 500U)) return -1;
    if ((*flow_pulses_per_liter < 200U) || (*flow_pulses_per_liter > 1200U)) return -1;
    if (!calib_diag_validate_full(&cfg)) return -1;

    *diag_cfg = cfg;
    return 0;
}

/**
 * @brief  判断V4新旧记录是否有显著变化
 * @return 1=显著变化, 0=变化小于阈值
 */
static uint8_t calib_flash_is_significant_change_v4(const calib_flash_record_t *new_record,
                                                    const calib_flash_record_t *old_record)
{
    int32_t new_pressure;
    int32_t old_pressure;
    int32_t new_current_offset;
    int32_t old_current_offset;
    uint32_t new_current_sens;
    uint32_t old_current_sens;
    uint32_t new_flow_pulses;
    uint32_t old_flow_pulses;
    uint32_t new_flow_k_q1000;
    uint32_t old_flow_k_q1000;
    diag_threshold_config_t new_diag_cfg;
    diag_threshold_config_t old_diag_cfg;

    if ((new_record == NULL) || (old_record == NULL)) {
        return 1U;
    }
    if ((new_record->version != CALIB_FLASH_VERSION_V4) || (old_record->version != CALIB_FLASH_VERSION_V4)) {
        return 1U;
    }

    if (calib_flash_unpack_v4_payload(new_record, &new_pressure, &new_current_offset,
                                      &new_current_sens, &new_flow_pulses, &new_flow_k_q1000, &new_diag_cfg) != 0) {
        return 1U;
    }
    if (calib_flash_unpack_v4_payload(old_record, &old_pressure, &old_current_offset,
                                      &old_current_sens, &old_flow_pulses, &old_flow_k_q1000, &old_diag_cfg) != 0) {
        return 1U;
    }

    if (calib_abs_diff_i32(new_pressure, old_pressure) >= CALIB_SAVE_DELTA_PRESSURE_MV) return 1U;
    if (calib_abs_diff_i32(new_current_offset, old_current_offset) >= CALIB_SAVE_DELTA_CURRENT_OFFSET_MV) return 1U;
    if (calib_abs_diff_u32(new_current_sens, old_current_sens) >= CALIB_SAVE_DELTA_CURRENT_SENS_MV) return 1U;
    if (calib_abs_diff_u32(new_flow_pulses, old_flow_pulses) >= CALIB_SAVE_DELTA_FLOW_PULSES) return 1U;
    if (calib_abs_diff_u32(new_flow_k_q1000, old_flow_k_q1000) >= CALIB_SAVE_DELTA_FLOW_K_Q1000) return 1U;

    if (calib_diag_flow_to_q100(new_diag_cfg.blockage_flow_thresh) != calib_diag_flow_to_q100(old_diag_cfg.blockage_flow_thresh)) return 1U;
    if (calib_diag_temp_to_q10(new_diag_cfg.blockage_temp_thresh) != calib_diag_temp_to_q10(old_diag_cfg.blockage_temp_thresh)) return 1U;
    if (new_diag_cfg.blockage_hold_time != old_diag_cfg.blockage_hold_time) return 1U;
    if (new_diag_cfg.blockage_pump_pwm_min != old_diag_cfg.blockage_pump_pwm_min) return 1U;
    if (calib_diag_cv_to_q1000(new_diag_cfg.bubble_flow_cv_thresh) != calib_diag_cv_to_q1000(old_diag_cfg.bubble_flow_cv_thresh)) return 1U;
    if (calib_diag_cv_to_q1000(new_diag_cfg.pump_current_cv_thresh) != calib_diag_cv_to_q1000(old_diag_cfg.pump_current_cv_thresh)) return 1U;
    if (calib_diag_leak_slope_abs_to_q10000(new_diag_cfg.leak_pressure_slope) != calib_diag_leak_slope_abs_to_q10000(old_diag_cfg.leak_pressure_slope)) return 1U;
    if (calib_diag_percent_to_q10(new_diag_cfg.leak_flow_drop_percent) != calib_diag_percent_to_q10(old_diag_cfg.leak_flow_drop_percent)) return 1U;
    if (new_diag_cfg.water_tds_warning != old_diag_cfg.water_tds_warning) return 1U;
    if (new_diag_cfg.water_tds_critical != old_diag_cfg.water_tds_critical) return 1U;
    if (calib_diag_temp_to_q10(new_diag_cfg.eff_temp_diff_warning) != calib_diag_temp_to_q10(old_diag_cfg.eff_temp_diff_warning)) return 1U;
    if (calib_diag_temp_to_q10(new_diag_cfg.eff_temp_diff_critical) != calib_diag_temp_to_q10(old_diag_cfg.eff_temp_diff_critical)) return 1U;
    if (new_diag_cfg.eff_pump_pwm_thresh != old_diag_cfg.eff_pump_pwm_thresh) return 1U;

    return 0U;
}

/**
 * @brief  设置详细日志开关
 * @param  enable: 1=开启, 0=关闭
 */
void scheduler_set_verbose_log(uint8_t enable)
{
    g_verbose_log_enabled = enable ? 1U : 0U;
    printf("[LOG] 详细日志: %s\r\n", g_verbose_log_enabled ? "开启" : "关闭");
}

/**
 * @brief  查询详细日志开关状态
 * @return 1=开启, 0=关闭
 */
uint8_t scheduler_is_verbose_log_enabled(void)
{
    return g_verbose_log_enabled;
}

/**
 * @brief  浮点绝对值（避免引入math库依赖）
 */
static float tinyml_absf(float value)
{
    return (value >= 0.0f) ? value : -value;
}

/**
 * @brief  浮点限幅
 */
static float tinyml_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

/**
 * @brief  TinyML后端名称
 */
static const char *tinyml_backend_to_name(uint8_t backend)
{
    if (backend == TINYML_BACKEND_TINYMAIX_STUB) {
        return "tinymaix_stub";
    }
    if (backend == TINYML_BACKEND_TINYMAIX_REAL) {
        return "tinymaix_real";
    }
    return "heuristic";
}

/**
 * @brief  判断后端是否依赖模型
 */
static uint8_t tinyml_is_model_backend(uint8_t backend)
{
    return (uint8_t)((backend == TINYML_BACKEND_TINYMAIX_STUB) ||
                     (backend == TINYML_BACKEND_TINYMAIX_REAL));
}

/**
 * @brief  获取当前运行后端的模型就绪状态
 * @note   heuristic后端不依赖模型，统一返回0，避免状态语义歧义
 */
static uint8_t tinyml_get_effective_model_ready(void)
{
    return tinyml_is_model_backend(s_tinyml.backend) ? s_tinyml.model_ready : 0U;
}

/**
 * @brief  TinyMaix真实后端初始化弱符号
 * @note   用户可在其他源文件提供同名强符号完成真实模型接入
 */
__attribute__((weak)) int8_t tinymaix_real_init(void)
{
    return -1;
}

/**
 * @brief  TinyMaix真实后端推理弱符号
 * @param  cpu_temp: 当前CPU温度
 * @param  water_temp: 当前水温
 * @param  flow_rate: 当前流量
 * @param  last_cpu_temp: 上次CPU温度
 * @param  window_mean: 窗口均值
 * @param  window_mad: 窗口MAD
 * @param  score: 推理分数输出(0~1)
 * @return 0=成功, -1=失败
 */
__attribute__((weak)) int8_t tinymaix_real_infer(float cpu_temp,
                                                 float water_temp,
                                                 float flow_rate,
                                                 float last_cpu_temp,
                                                 float window_mean,
                                                 float window_mad,
                                                 float *score)
{
    (void)cpu_temp;
    (void)water_temp;
    (void)flow_rate;
    (void)last_cpu_temp;
    (void)window_mean;
    (void)window_mad;
    (void)score;
    return -1;
}

/**
 * @brief  TinyMaix模型初始化
 * @param  backend: 后端类型
 * @return 0=成功, -1=失败
 */
static int8_t tinyml_tinymaix_model_init(uint8_t backend)
{
    if (backend == TINYML_BACKEND_TINYMAIX_REAL) {
        return tinymaix_real_init();
    }
    return -1;
}

/**
 * @brief  计算窗口统计量（均值 + MAD）
 */
static void tinyml_calc_window_stats(float *mean, float *mad)
{
    float local_mean = 0.0f;
    float local_mad = 0.0f;
    uint8_t index;
    uint8_t count = s_tinyml.sample_count;

    if ((mean == NULL) || (mad == NULL) || (count == 0U)) {
        return;
    }

    for (index = 0U; index < count; index++) {
        local_mean += s_tinyml.cpu_hist[index];
    }
    local_mean /= (float)count;

    for (index = 0U; index < count; index++) {
        local_mad += tinyml_absf(s_tinyml.cpu_hist[index] - local_mean);
    }
    local_mad /= (float)count;

    *mean = local_mean;
    *mad = local_mad;
}

/**
 * @brief  组装TinyML特征输入
 */
static void tinyml_build_feature(tinyml_temp_feature_t *feature,
                                 float cpu_temp,
                                 float water_temp,
                                 float flow_rate,
                                 float mean,
                                 float mad)
{
    if (feature == NULL) {
        return;
    }

    feature->cpu_temp = cpu_temp;
    feature->water_temp = water_temp;
    feature->flow_rate = flow_rate;
    feature->last_cpu_temp = s_tinyml.last_cpu_temp;
    feature->window_mean = mean;
    feature->window_mad = mad;
}

/**
 * @brief  骨架推理后端（可运行）
 * @return 异常分数（0~1）
 */
static float tinyml_infer_heuristic(const tinyml_temp_feature_t *feature)
{
    float z_norm;
    float delta_temp;
    float rise;
    float flow_penalty;
    float score;

    if (feature == NULL) {
        return 0.0f;
    }

    z_norm = tinyml_absf(feature->cpu_temp - feature->window_mean) / (feature->window_mad + 0.20f);
    delta_temp = feature->cpu_temp - feature->water_temp;
    if (delta_temp < 0.0f) {
        delta_temp = 0.0f;
    }
    rise = feature->cpu_temp - feature->last_cpu_temp;
    if (rise < 0.0f) {
        rise = 0.0f;
    }
    flow_penalty = (feature->flow_rate < 1.0f) ? 0.12f : 0.0f;

    score = 0.55f * tinyml_clampf(z_norm / 3.0f, 0.0f, 1.0f)
          + 0.30f * tinyml_clampf(delta_temp / 20.0f, 0.0f, 1.0f)
          + 0.15f * tinyml_clampf(rise / 0.8f, 0.0f, 1.0f)
          + flow_penalty;
    return tinyml_clampf(score, 0.0f, 1.0f);
}

/**
 * @brief  TinyMaix推理占位接口
 * @param  feature: 特征输入
 * @param  score: 推理分数输出（0~1）
 * @return 0=成功, -1=失败
 */
static int8_t tinyml_infer_tinymaix_stub(const tinyml_temp_feature_t *feature, float *score)
{
    (void)feature;
    (void)score;
    return -1;
}

/**
 * @brief  TinyMaix真实推理接口（弱符号转发）
 * @param  feature: 特征输入
 * @param  score: 推理分数输出（0~1）
 * @return 0=成功, -1=失败
 */
static int8_t tinyml_infer_tinymaix_real(const tinyml_temp_feature_t *feature, float *score)
{
    if ((feature == NULL) || (score == NULL)) {
        return -1;
    }

    return tinymaix_real_infer(feature->cpu_temp,
                               feature->water_temp,
                               feature->flow_rate,
                               feature->last_cpu_temp,
                               feature->window_mean,
                               feature->window_mad,
                               score);
}

/**
 * @brief  根据后端执行推理并返回分数
 * @param  feature: 特征输入
 * @return 异常分数（0~1）
 */
static float tinyml_infer_score(const tinyml_temp_feature_t *feature)
{
    float score = 0.0f;
    int8_t infer_ret = -1;

    if ((s_tinyml.backend == TINYML_BACKEND_TINYMAIX_STUB) ||
        (s_tinyml.backend == TINYML_BACKEND_TINYMAIX_REAL)) {
        if ((s_tinyml.backend == TINYML_BACKEND_TINYMAIX_REAL) &&
            (s_tinyml.model_ready != 0U) &&
            tinymaix_real_is_placeholder_model()) {
            if (s_tinyml.placeholder_warned == 0U) {
                printf("[TINYML] 当前real后端使用占位模型，请替换真实INT8模型\r\n");
                s_tinyml.placeholder_warned = 1U;
            }
        }

        if (s_tinyml.backend == TINYML_BACKEND_TINYMAIX_STUB) {
            infer_ret = tinyml_infer_tinymaix_stub(feature, &score);
        } else {
            infer_ret = tinyml_infer_tinymaix_real(feature, &score);
        }

        if ((s_tinyml.model_ready == 0U) || (infer_ret != 0)) {
            if (s_tinyml.backend_fallback == 0U) {
                printf("[TINYML] 后端%s未就绪，已降级为骨架推理\r\n",
                       tinyml_backend_to_name(s_tinyml.backend));
            }
            s_tinyml.backend_fallback = 1U;
            s_tinyml.fallback_count++;
            return tinyml_infer_heuristic(feature);
        }
        s_tinyml.backend_fallback = 0U;
        return tinyml_clampf(score, 0.0f, 1.0f);
    }

    s_tinyml.backend_fallback = 0U;
    return tinyml_infer_heuristic(feature);
}

/**
 * @brief  TinyML温度异常检测初始化（占位推理骨架）
 */
static int8_t tinyml_temp_init(void)
{
#if ENABLE_TINYML_TEMP_ANOMALY
    memset(&s_tinyml, 0, sizeof(s_tinyml));
    s_tinyml.enabled = 1U;
    s_tinyml.threshold = TINYML_SCORE_THRESHOLD_DEFAULT;
    s_tinyml.backend = TINYML_BACKEND_DEFAULT;
    s_tinyml.model_ready = 0U;
    if (((s_tinyml.backend == TINYML_BACKEND_TINYMAIX_STUB) ||
         (s_tinyml.backend == TINYML_BACKEND_TINYMAIX_REAL)) &&
        (tinyml_tinymaix_model_init(s_tinyml.backend) == 0)) {
        s_tinyml.model_ready = 1U;
    }
    printf("[TINYML] 温度异常检测骨架已启用: window=%u, warmup=%u, threshold=%.2f\r\n",
           TINYML_TEMP_WINDOW_SIZE, TINYML_TEMP_WARMUP_MIN, s_tinyml.threshold);
    printf("[TINYML] 后端: %s, model_ready=%u\r\n",
           tinyml_backend_to_name(s_tinyml.backend), s_tinyml.model_ready);
    return 0;
#else
    memset(&s_tinyml, 0, sizeof(s_tinyml));
    printf("[TINYML] 温度异常检测已禁用（ENABLE_TINYML_TEMP_ANOMALY=0）\r\n");
    return -1;
#endif
}

/**
 * @brief  TinyML温度异常推理更新（1秒周期）
 * @note   当前阶段为可替换骨架，后续可无缝替换为TinyMaix模型推理
 */
static void tinyml_temp_update(void)
{
#if ENABLE_TINYML_TEMP_ANOMALY
    const temp_data_t *temp = temp_get_data();
    const flow_data_t *flow = flow_get_data();
    tinyml_temp_feature_t feature;
    float cpu_temp;
    float water_temp;
    float flow_rate;
    float mean = 0.0f;
    float mad = 0.0f;
    uint8_t next_anomaly;

    if (!s_tinyml.enabled) {
        return;
    }
    if ((temp == NULL) || (flow == NULL)) {
        return;
    }
    if (!temp->sensor_online[TEMP_SENSOR_CPU]) {
        s_tinyml.ready = 0U;
        s_tinyml.anomaly_active = 0U;
        s_tinyml.hold_count = 0U;
        s_tinyml.score = 0.0f;
        return;
    }

    cpu_temp = temp->cpu_temp;
    water_temp = temp->sensor_online[TEMP_SENSOR_WATER] ? temp->water_temp : cpu_temp;
    flow_rate = flow->sensor_online ? flow->flow_rate : SIM_FLOW_LPM;

    s_tinyml.cpu_hist[s_tinyml.write_index] = cpu_temp;
    s_tinyml.write_index = (uint8_t)((s_tinyml.write_index + 1U) % TINYML_TEMP_WINDOW_SIZE);
    if (s_tinyml.sample_count < TINYML_TEMP_WINDOW_SIZE) {
        s_tinyml.sample_count++;
    }
    s_tinyml.infer_count++;

    if (s_tinyml.sample_count < TINYML_TEMP_WARMUP_MIN) {
        s_tinyml.last_cpu_temp = cpu_temp;
        s_tinyml.ready = 0U;
        s_tinyml.score = 0.0f;
        return;
    }

    s_tinyml.ready = 1U;
    tinyml_calc_window_stats(&mean, &mad);
    tinyml_build_feature(&feature, cpu_temp, water_temp, flow_rate, mean, mad);
    s_tinyml.score = tinyml_infer_score(&feature);

    if (s_tinyml.score >= s_tinyml.threshold) {
        if (s_tinyml.hold_count < 255U) {
            s_tinyml.hold_count++;
        }
    } else {
        if (s_tinyml.hold_count > 0U) {
            s_tinyml.hold_count--;
        }
    }

    if (s_tinyml.hold_count >= TINYML_TEMP_HOLD_ON) {
        next_anomaly = 1U;
    } else if (s_tinyml.hold_count <= TINYML_TEMP_HOLD_OFF) {
        next_anomaly = 0U;
    } else {
        next_anomaly = s_tinyml.anomaly_active;
    }

    if (next_anomaly != s_tinyml.anomaly_active) {
        printf("[TINYML] 温度异常状态切换: %s (score=%.2f, thr=%.2f)\r\n",
               next_anomaly ? "异常" : "恢复", s_tinyml.score, s_tinyml.threshold);
    }
    s_tinyml.anomaly_active = next_anomaly;
    s_tinyml.last_cpu_temp = cpu_temp;

    if (scheduler_is_verbose_log_enabled() && ((s_tinyml.infer_count % 10U) == 0U)) {
        printf("[TINYML] score=%.2f, thr=%.2f, hold=%u, state=%s\r\n",
               s_tinyml.score, s_tinyml.threshold, s_tinyml.hold_count,
               s_tinyml.anomaly_active ? "ANOMALY" : "NORMAL");
    }
#endif
}

/**
 * @brief  蜂鸣器硬件输出控制
 * @param  on: 1=响，0=静音
 */
static void buzzer_set_output(uint8_t on)
{
#if ENABLE_BUZZER_ALARM
    uint8_t pin_level;

    if (!s_buzzer_initialized) {
        return;
    }

    pin_level = on ? BUZZER_ACTIVE_LEVEL : (uint8_t)(1U - BUZZER_ACTIVE_LEVEL);
    gpio_write(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, pin_level);
#else
    (void)on;
#endif
}

/**
 * @brief  蜂鸣器初始化
 * @return 0=成功, -1=失败
 */
static int8_t alarm_init(void)
{
#if ENABLE_BUZZER_ALARM
    gpio_init(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, GPIO_MODE_OUTPUT_PP);
    s_buzzer_initialized = 1U;
    s_buzzer_level = BUZZER_ALERT_OFF;
    s_buzzer_muted = 0U;
    s_alarm_diag_crit_conf = ALARM_DIAG_CRIT_CONF_DEFAULT;
    buzzer_set_output(0U);
    printf("[ALARM] 蜂鸣器初始化完成: PC11, 有源蜂鸣器, active=%u\r\n", BUZZER_ACTIVE_LEVEL);
    return 0;
#else
    s_buzzer_initialized = 0U;
    s_buzzer_level = BUZZER_ALERT_OFF;
    s_buzzer_muted = 0U;
    s_alarm_diag_crit_conf = ALARM_DIAG_CRIT_CONF_DEFAULT;
    printf("[ALARM] 蜂鸣器功能已禁用（ENABLE_BUZZER_ALARM=0）\r\n");
    return -1;
#endif
}

/**
 * @brief  切换蜂鸣器静音状态
 */
void scheduler_alarm_toggle_mute(void)
{
#if ENABLE_BUZZER_ALARM
    s_buzzer_muted = (uint8_t)!s_buzzer_muted;
    if (s_buzzer_muted) {
        buzzer_set_output(0U);
    }
    printf("[ALARM] 蜂鸣器静音: %s\r\n", s_buzzer_muted ? "开启" : "关闭");
#else
    printf("[ALARM] 蜂鸣器功能已禁用\r\n");
#endif
}

/**
 * @brief  调整蜂鸣器CRIT阈值（诊断置信度）
 * @param  delta: 增量
 */
void scheduler_alarm_adjust_diag_crit_conf(float delta)
{
    float next_value = s_alarm_diag_crit_conf + delta;
    s_alarm_diag_crit_conf = tinyml_clampf(next_value, ALARM_DIAG_CRIT_CONF_MIN, ALARM_DIAG_CRIT_CONF_MAX);
    printf("[ALARM] CRIT阈值已更新: %.2f\r\n", s_alarm_diag_crit_conf);
}

/**
 * @brief  打印蜂鸣器状态
 */
void scheduler_alarm_print_status(void)
{
    const char *level_name = "OFF";
    if (s_buzzer_level == BUZZER_ALERT_WARN) {
        level_name = "WARN";
    } else if (s_buzzer_level == BUZZER_ALERT_CRIT) {
        level_name = "CRIT";
    }

    printf("[ALARM] 状态: init=%u, mute=%u, level=%s, crit_conf=%.2f\r\n",
           s_buzzer_initialized, s_buzzer_muted, level_name, s_alarm_diag_crit_conf);
}

/**
 * @brief  打印诊断快照（各故障置信度）
 */
void scheduler_diag_print_snapshot(void)
{
    const diag_output_t *diag = diag_get_result();
    uint8_t i;

    if (diag == NULL) {
        printf("[DIAG] 诊断结果不可用\r\n");
        return;
    }

    if (diag->active_count == 0U) {
        printf("[DIAG] 快照: active=0, severe=none, conf=%.2f\r\n",
               diag->most_severe_confidence);
    } else if (diag->most_severe_id < DIAG_FAULT_COUNT) {
        printf("[DIAG] 快照: active=%u, severe=%s, conf=%.2f\r\n",
               diag->active_count,
               diag_get_fault_name((diag_fault_type_t)diag->most_severe_id),
               diag->most_severe_confidence);
    } else {
        printf("[DIAG] 快照: active=%u, severe=none, conf=%.2f\r\n",
               diag->active_count, diag->most_severe_confidence);
    }

    for (i = 0; i < DIAG_FAULT_COUNT; i++) {
        printf("[DIAG] %s: conf=%.2f, active=%u\r\n",
               diag_get_fault_name((diag_fault_type_t)i),
               diag->faults[i].confidence,
               diag->faults[i].active);
    }
}

/**
 * @brief  切换TinyML温度异常检测开关
 */
void scheduler_tinyml_toggle_enable(void)
{
#if ENABLE_TINYML_TEMP_ANOMALY
    s_tinyml.enabled = (uint8_t)!s_tinyml.enabled;
    s_tinyml.ready = 0U;
    s_tinyml.anomaly_active = 0U;
    s_tinyml.hold_count = 0U;
    s_tinyml.sample_count = 0U;
    s_tinyml.write_index = 0U;
    s_tinyml.score = 0.0f;
    s_tinyml.backend_fallback = 0U;
    s_tinyml.placeholder_warned = 0U;
    memset(s_tinyml.cpu_hist, 0, sizeof(s_tinyml.cpu_hist));

    if (!s_tinyml.enabled) {
        s_tinyml.model_ready = 0U;
    } else if ((s_tinyml.backend == TINYML_BACKEND_TINYMAIX_STUB) ||
               (s_tinyml.backend == TINYML_BACKEND_TINYMAIX_REAL)) {
        s_tinyml.model_ready = (tinyml_tinymaix_model_init(s_tinyml.backend) == 0) ? 1U : 0U;
    } else {
        s_tinyml.model_ready = 0U;
    }
    printf("[TINYML] 温度异常检测: %s\r\n", s_tinyml.enabled ? "开启" : "关闭");
#else
    printf("[TINYML] 温度异常检测已禁用\r\n");
#endif
}

/**
 * @brief  切换TinyML推理后端
 */
void scheduler_tinyml_cycle_backend(void)
{
#if ENABLE_TINYML_TEMP_ANOMALY
    if (s_tinyml.backend == TINYML_BACKEND_HEURISTIC) {
        s_tinyml.backend = TINYML_BACKEND_TINYMAIX_STUB;
    } else if (s_tinyml.backend == TINYML_BACKEND_TINYMAIX_STUB) {
        s_tinyml.backend = TINYML_BACKEND_TINYMAIX_REAL;
    } else {
        s_tinyml.backend = TINYML_BACKEND_HEURISTIC;
    }

    s_tinyml.model_ready = 0U;
    s_tinyml.backend_fallback = 0U;
    s_tinyml.fallback_count = 0U;
    s_tinyml.placeholder_warned = 0U;

    if ((s_tinyml.backend == TINYML_BACKEND_TINYMAIX_STUB) ||
        (s_tinyml.backend == TINYML_BACKEND_TINYMAIX_REAL)) {
        if (tinyml_tinymaix_model_init(s_tinyml.backend) == 0) {
            s_tinyml.model_ready = 1U;
        }
    }

    printf("[TINYML] 推理后端已切换: %s, model_ready=%u\r\n",
           tinyml_backend_to_name(s_tinyml.backend), s_tinyml.model_ready);
#else
    printf("[TINYML] 温度异常检测已禁用\r\n");
#endif
}

/**
 * @brief  微调TinyML异常阈值
 * @param  delta: 阈值增量
 */
void scheduler_tinyml_adjust_threshold(float delta)
{
#if ENABLE_TINYML_TEMP_ANOMALY
    float next = s_tinyml.threshold + delta;
    s_tinyml.threshold = tinyml_clampf(next, TINYML_SCORE_THRESHOLD_MIN, TINYML_SCORE_THRESHOLD_MAX);
    printf("[TINYML] 异常阈值已更新: %.2f\r\n", s_tinyml.threshold);
#else
    (void)delta;
    printf("[TINYML] 温度异常检测已禁用\r\n");
#endif
}

/**
 * @brief  打印TinyML状态
 */
void scheduler_tinyml_print_status(void)
{
    uint8_t model_ready = tinyml_get_effective_model_ready();

    printf("[TINYML] 状态: enable=%u, ready=%u, anomaly=%u, score=%.2f, thr=%.2f, samples=%u\r\n",
           s_tinyml.enabled, s_tinyml.ready, s_tinyml.anomaly_active,
           s_tinyml.score, s_tinyml.threshold, s_tinyml.sample_count);
    printf("[TINYML] 后端: %s, model_ready=%u, fallback=%u, fallback_count=%lu\r\n",
           tinyml_backend_to_name(s_tinyml.backend), model_ready,
           s_tinyml.backend_fallback, (unsigned long)s_tinyml.fallback_count);

    if (s_tinyml.backend == TINYML_BACKEND_TINYMAIX_REAL) {
        printf("[TINYML] 模型来源: %s\r\n",
               tinymaix_real_is_placeholder_model() ? "placeholder(占位)" : "custom(真实)");
        tinymaix_real_print_model_info();
    }
}

/**
 * @brief  执行TinyMaix真实后端自检
 * @note   使用固定样例特征执行一次推理，验证“初始化+推理”链路可用性
 */
void scheduler_tinyml_self_test(void)
{
#if ENABLE_TINYML_TEMP_ANOMALY
    tinyml_temp_feature_t feature;
    float score = 0.0f;
    int8_t init_ret;
    int8_t infer_ret;
    uint8_t real_model_ready;

    memset(&feature, 0, sizeof(feature));
    feature.cpu_temp = 72.0f;
    feature.water_temp = 38.0f;
    feature.flow_rate = 0.6f;
    feature.last_cpu_temp = 70.5f;
    feature.window_mean = 58.0f;
    feature.window_mad = 1.8f;

    printf("[TINYML] 开始TinyMaix真实后端自检...\r\n");
    init_ret = tinymaix_real_init();
    real_model_ready = (init_ret == 0) ? 1U : 0U;
    if (s_tinyml.backend == TINYML_BACKEND_TINYMAIX_REAL) {
        s_tinyml.model_ready = real_model_ready;
    }
    printf("[TINYML] real_init=%d, model_ready=%u\r\n", init_ret, real_model_ready);

    infer_ret = tinyml_infer_tinymaix_real(&feature, &score);
    if (infer_ret == 0) {
        score = tinyml_clampf(score, 0.0f, 1.0f);
        printf("[TINYML] real_infer=0, score=%.3f -> PASS\r\n", score);
        if (tinymaix_real_is_placeholder_model()) {
            printf("[TINYML] 注意：当前通过的是placeholder链路，请替换真实INT8模型\r\n");
        }
    } else {
        printf("[TINYML] real_infer=%d -> FAIL（将由运行时自动降级到heuristic）\r\n", infer_ret);
    }
    if (s_tinyml.backend != TINYML_BACKEND_TINYMAIX_REAL) {
        printf("[TINYML] 当前运行后端为%s，本次仅验证real链路，不改变运行后端\r\n",
               tinyml_backend_to_name(s_tinyml.backend));
    }
    tinymaix_real_print_model_info();
#else
    printf("[TINYML] 温度异常检测已禁用\r\n");
#endif
}

/**
 * @brief  获取TinyML状态快照
 * @param  status: 输出结构体
 * @return 0=成功, -1=失败
 */
int8_t scheduler_tinyml_get_status(scheduler_tinyml_status_t *status)
{
    if (status == NULL) {
        return -1;
    }

    status->enabled = s_tinyml.enabled;
    status->ready = s_tinyml.ready;
    status->anomaly_active = s_tinyml.anomaly_active;
    status->score = s_tinyml.score;
    status->threshold = s_tinyml.threshold;
    status->backend = s_tinyml.backend;
    status->model_ready = tinyml_get_effective_model_ready();
    status->backend_fallback = s_tinyml.backend_fallback;
    status->fallback_count = s_tinyml.fallback_count;
    return 0;
}

/**
 * @brief  评估当前告警等级（用于蜂鸣器状态机）
 * @return BUZZER_ALERT_OFF/BUZZER_ALERT_WARN/BUZZER_ALERT_CRIT
 */
static buzzer_alert_level_t alarm_eval_level(void)
{
    const diag_output_t *diag = diag_get_result();
    const temp_data_t *temp = temp_get_data();
    const flow_data_t *flow = flow_get_data();
    uint8_t critical = 0U;
    uint8_t warning = 0U;

    if (temp != NULL) {
        if ((temp->cpu_status >= 2U) || (temp->water_status >= 2U) || (temp->env_status >= 2U)) {
            critical = 1U;
        } else if ((temp->cpu_status == 1U) || (temp->water_status == 1U) || (temp->env_status == 1U)) {
            warning = 1U;
        }
    }

    if (flow != NULL) {
        if ((flow->status == FLOW_STATUS_STOPPED) || (flow->status == FLOW_STATUS_SENSOR_ERROR)) {
            critical = 1U;
        } else if (flow->status != FLOW_STATUS_NORMAL) {
            warning = 1U;
        }
    }

    if (app_tds_is_normal() == 0U) {
        warning = 1U;
    }

    if ((diag != NULL) && (diag->active_count > 0U)) {
        if (diag->most_severe_confidence >= s_alarm_diag_crit_conf) {
            critical = 1U;
        } else {
            warning = 1U;
        }
    }

#if ENABLE_TINYML_TEMP_ANOMALY
    if (s_tinyml.enabled && s_tinyml.ready && s_tinyml.anomaly_active) {
        float crit_score = tinyml_clampf(s_tinyml.threshold + 0.15f, TINYML_SCORE_THRESHOLD_MIN, 1.0f);
        if (s_tinyml.score >= crit_score) {
            critical = 1U;
        } else {
            warning = 1U;
        }
    }
#endif

    if (critical) {
        return BUZZER_ALERT_CRIT;
    }
    if (warning) {
        return BUZZER_ALERT_WARN;
    }
    return BUZZER_ALERT_OFF;
}

/**
 * @brief  蜂鸣器告警任务（100ms周期）
 * @note   WARN: 每2秒短鸣1次；CRIT: 每2秒双鸣
 */
static void task_alarm(void)
{
    buzzer_alert_level_t next_level;
    uint32_t phase;
    uint8_t buzzer_on = 0U;

#if !ENABLE_BUZZER_ALARM
    return;
#endif

    if (!s_buzzer_initialized) {
        return;
    }

    next_level = alarm_eval_level();
    if (next_level != s_buzzer_level) {
        if (next_level == BUZZER_ALERT_OFF) {
            printf("[ALARM] 蜂鸣器告警解除\r\n");
        } else if (next_level == BUZZER_ALERT_WARN) {
            printf("[ALARM] 蜂鸣器进入告警级别: WARN\r\n");
        } else {
            printf("[ALARM] 蜂鸣器进入告警级别: CRIT\r\n");
        }
        s_buzzer_level = next_level;
    }

    if (s_buzzer_level == BUZZER_ALERT_OFF) {
        buzzer_set_output(0U);
        return;
    }

    if (s_buzzer_muted) {
        buzzer_set_output(0U);
        return;
    }

    phase = get_tick_ms() % BUZZER_PATTERN_PERIOD_MS;
    if (s_buzzer_level == BUZZER_ALERT_WARN) {
        if (phase < BUZZER_WARN_ON_MS) {
            buzzer_on = 1U;
        }
    } else {
        uint32_t second_on_start = BUZZER_CRIT_ON1_MS + BUZZER_CRIT_GAP1_MS;
        uint32_t second_on_end = second_on_start + BUZZER_CRIT_ON2_MS;
        if ((phase < BUZZER_CRIT_ON1_MS) || ((phase >= second_on_start) && (phase < second_on_end))) {
            buzzer_on = 1U;
        }
    }

    buzzer_set_output(buzzer_on);
}

/**
 * @brief  V2标定记录范围校验
 */
static uint8_t calib_flash_validate_record_v2(const calib_flash_record_t *record, const char **reason)
{
    if (record->magic != CALIB_FLASH_MAGIC) {
        if (reason != NULL) {
            *reason = "magic不匹配";
        }
        return 0;
    }
    if (record->version != CALIB_FLASH_VERSION_V2) {
        if (reason != NULL) {
            *reason = "version不匹配";
        }
        return 0;
    }
    if (record->sequence == 0U) {
        if (reason != NULL) {
            *reason = "sequence非法";
        }
        return 0;
    }
    if ((record->pressure_offset_mv < -1000) || (record->pressure_offset_mv > 1000)) {
        if (reason != NULL) {
            *reason = "pressure_offset超范围";
        }
        return 0;
    }
    if ((record->current_offset_mv < -1000) || (record->current_offset_mv > 1000)) {
        if (reason != NULL) {
            *reason = "current_offset超范围";
        }
        return 0;
    }
    if ((record->current_sensitivity_mv < 50U) || (record->current_sensitivity_mv > 500U)) {
        if (reason != NULL) {
            *reason = "current_sensitivity超范围";
        }
        return 0;
    }
    if ((record->flow_pulses_per_liter < 200U) || (record->flow_pulses_per_liter > 1200U)) {
        if (reason != NULL) {
            *reason = "flow_pulses超范围";
        }
        return 0;
    }
    if ((record->flow_k_factor_q1000 < 1000U) || (record->flow_k_factor_q1000 > 30000U)) {
        if (reason != NULL) {
            *reason = "flow_k_factor超范围";
        }
        return 0;
    }
    if (record->checksum != calib_flash_checksum_v2(record)) {
        if (reason != NULL) {
            *reason = "checksum不匹配";
        }
        return 0;
    }

    if (reason != NULL) {
        *reason = "OK";
    }
    return 1;
}

/**
 * @brief  V3标定记录范围校验（含诊断阈值扩展字段）
 */
static uint8_t calib_flash_validate_record_v3(const calib_flash_record_t *record, const char **reason)
{
    uint32_t flow_pulses;
    uint32_t flow_k_q1000;
    uint16_t diag_block_flow_q100;
    uint8_t diag_block_pwm;

    if (record->magic != CALIB_FLASH_MAGIC) {
        if (reason != NULL) {
            *reason = "magic不匹配";
        }
        return 0;
    }
    if (record->version != CALIB_FLASH_VERSION_V3) {
        if (reason != NULL) {
            *reason = "version不匹配";
        }
        return 0;
    }
    if (record->sequence == 0U) {
        if (reason != NULL) {
            *reason = "sequence非法";
        }
        return 0;
    }
    if ((record->pressure_offset_mv < -1000) || (record->pressure_offset_mv > 1000)) {
        if (reason != NULL) {
            *reason = "pressure_offset超范围";
        }
        return 0;
    }
    if ((record->current_offset_mv < -1000) || (record->current_offset_mv > 1000)) {
        if (reason != NULL) {
            *reason = "current_offset超范围";
        }
        return 0;
    }
    if ((record->current_sensitivity_mv < 50U) || (record->current_sensitivity_mv > 500U)) {
        if (reason != NULL) {
            *reason = "current_sensitivity超范围";
        }
        return 0;
    }

    flow_pulses = calib_flash_unpack_flow_pulses(record->flow_pulses_per_liter);
    flow_k_q1000 = calib_flash_unpack_flow_k_q1000(record->flow_k_factor_q1000);
    if ((flow_pulses < 200U) || (flow_pulses > 1200U)) {
        if (reason != NULL) {
            *reason = "flow_pulses超范围";
        }
        return 0;
    }
    if ((flow_k_q1000 < 1000U) || (flow_k_q1000 > 30000U)) {
        if (reason != NULL) {
            *reason = "flow_k_factor超范围";
        }
        return 0;
    }

    diag_block_flow_q100 = calib_flash_unpack_diag_block_flow_q100(record->flow_k_factor_q1000, record->version);
    diag_block_pwm = calib_flash_unpack_diag_block_pwm(record->flow_pulses_per_liter, record->version);
    if ((diag_block_flow_q100 < 20U) || (diag_block_flow_q100 > 1000U)) {
        if (reason != NULL) {
            *reason = "diag_block_flow超范围";
        }
        return 0;
    }
    if (diag_block_pwm > 100U) {
        if (reason != NULL) {
            *reason = "diag_block_pwm超范围";
        }
        return 0;
    }

    if (record->checksum != calib_flash_checksum_v2(record)) {
        if (reason != NULL) {
            *reason = "checksum不匹配";
        }
        return 0;
    }

    if (reason != NULL) {
        *reason = "OK";
    }
    return 1;
}

/**
 * @brief  V4标定记录范围校验（全量诊断阈值持久化）
 */
static uint8_t calib_flash_validate_record_v4(const calib_flash_record_t *record, const char **reason)
{
    int32_t pressure_offset_mv;
    int32_t current_offset_mv;
    uint32_t current_sensitivity_mv;
    uint32_t flow_pulses_per_liter;
    uint32_t flow_k_factor_q1000;
    diag_threshold_config_t diag_cfg;

    if (record->magic != CALIB_FLASH_MAGIC) {
        if (reason != NULL) {
            *reason = "magic不匹配";
        }
        return 0;
    }
    if (record->version != CALIB_FLASH_VERSION_V4) {
        if (reason != NULL) {
            *reason = "version不匹配";
        }
        return 0;
    }
    if (record->sequence == 0U) {
        if (reason != NULL) {
            *reason = "sequence非法";
        }
        return 0;
    }
    if (record->checksum != calib_flash_checksum_v2(record)) {
        if (reason != NULL) {
            *reason = "checksum不匹配";
        }
        return 0;
    }

    if (calib_flash_unpack_v4_payload(record,
                                      &pressure_offset_mv,
                                      &current_offset_mv,
                                      &current_sensitivity_mv,
                                      &flow_pulses_per_liter,
                                      &flow_k_factor_q1000,
                                      &diag_cfg) != 0) {
        if (reason != NULL) {
            *reason = "payload解码失败";
        }
        return 0;
    }

    (void)flow_k_factor_q1000;
    if (reason != NULL) {
        *reason = "OK";
    }
    return 1;
}

/**
 * @brief  V1标定记录范围校验（兼容老格式）
 */
static uint8_t calib_flash_validate_record_v1(const calib_flash_record_v1_t *record, const char **reason)
{
    if (record->magic != CALIB_FLASH_MAGIC) {
        if (reason != NULL) {
            *reason = "magic不匹配";
        }
        return 0;
    }
    if (record->version != CALIB_FLASH_VERSION_V1) {
        if (reason != NULL) {
            *reason = "version不匹配";
        }
        return 0;
    }
    if ((record->pressure_offset_mv < -1000) || (record->pressure_offset_mv > 1000)) {
        if (reason != NULL) {
            *reason = "pressure_offset超范围";
        }
        return 0;
    }
    if ((record->current_offset_mv < -1000) || (record->current_offset_mv > 1000)) {
        if (reason != NULL) {
            *reason = "current_offset超范围";
        }
        return 0;
    }
    if ((record->current_sensitivity_mv < 50U) || (record->current_sensitivity_mv > 500U)) {
        if (reason != NULL) {
            *reason = "current_sensitivity超范围";
        }
        return 0;
    }
    if ((record->flow_pulses_per_liter < 200U) || (record->flow_pulses_per_liter > 1200U)) {
        if (reason != NULL) {
            *reason = "flow_pulses超范围";
        }
        return 0;
    }
    if ((record->flow_k_factor_q1000 < 1000U) || (record->flow_k_factor_q1000 > 30000U)) {
        if (reason != NULL) {
            *reason = "flow_k_factor超范围";
        }
        return 0;
    }
    if (record->checksum != calib_flash_checksum_v1(record)) {
        if (reason != NULL) {
            *reason = "checksum不匹配";
        }
        return 0;
    }

    if (reason != NULL) {
        *reason = "OK";
    }
    return 1;
}

/**
 * @brief  标定记录范围校验（V2/V3/V4）
 */
static uint8_t calib_flash_is_record_valid(const calib_flash_record_t *record)
{
    if (record->version == CALIB_FLASH_VERSION_V4) {
        return calib_flash_validate_record_v4(record, NULL);
    }
    if (record->version == CALIB_FLASH_VERSION_V3) {
        return calib_flash_validate_record_v3(record, NULL);
    }
    if (record->version == CALIB_FLASH_VERSION_V2) {
        return calib_flash_validate_record_v2(record, NULL);
    }
    return 0;
}

/**
 * @brief  判断指定槽位是否处于擦除态
 */
static uint8_t calib_flash_is_slot_erased(uint32_t addr, uint32_t word_count)
{
    uint32_t index;
    const uint32_t *word = (const uint32_t *)addr;

    for (index = 0U; index < word_count; index++) {
        if (word[index] != CALIB_FLASH_ERASED_WORD) {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief  应用标定参数到运行时配置
 */
static void calib_flash_apply_values(int32_t pressure_offset_mv,
                                     int32_t current_offset_mv,
                                     uint32_t current_sensitivity_mv,
                                     uint32_t flow_pulses_per_liter,
                                     uint32_t flow_k_factor_q1000)
{
    calib_adc_t adc_cfg = *calib_adc_get();
    yfs201_calib_t flow_cfg = *yfs201_get_calib();

    adc_cfg.pressure_offset_mv = (int16_t)pressure_offset_mv;
    adc_cfg.current_offset_mv = (int16_t)current_offset_mv;
    adc_cfg.current_sensitivity_mv = (uint16_t)current_sensitivity_mv;
    calib_adc_set(&adc_cfg);

    flow_cfg.pulses_per_liter = (uint16_t)flow_pulses_per_liter;
    flow_cfg.k_factor = (float)flow_k_factor_q1000 / 1000.0f;
    yfs201_set_calib(&flow_cfg);
}

/**
 * @brief  根据页地址返回页名
 */
static const char *calib_flash_get_page_name(uint32_t page_addr)
{
    if (page_addr == CALIB_FLASH_PAGE_A_ADDR) {
        return "A";
    }
    if (page_addr == CALIB_FLASH_PAGE_B_ADDR) {
        return "B";
    }
    return "?";
}

/**
 * @brief  获取另一页地址（A<->B）
 */
static uint32_t calib_flash_get_peer_page(uint32_t page_addr)
{
    return (page_addr == CALIB_FLASH_PAGE_A_ADDR) ? CALIB_FLASH_PAGE_B_ADDR : CALIB_FLASH_PAGE_A_ADDR;
}

/**
 * @brief  扫描指定页并返回该页最新有效记录（V2/V3/V4）
 * @return 0=找到, -1=未找到
 */
static int8_t calib_flash_scan_page_latest_v2(uint32_t page_addr,
                                              const calib_flash_record_t **latest_record,
                                              uint32_t *latest_addr,
                                              uint32_t *latest_seq,
                                              uint16_t *valid_count)
{
    uint32_t addr;
    const calib_flash_record_t *best = NULL;
    uint32_t best_addr = 0U;
    uint32_t best_seq = 0U;
    uint16_t count = 0U;
    uint32_t page_end = page_addr + CALIB_FLASH_PAGE_SIZE_BYTES;

    for (addr = page_addr;
         addr <= (page_end - sizeof(calib_flash_record_t));
         addr += sizeof(calib_flash_record_t)) {
        const calib_flash_record_t *record = (const calib_flash_record_t *)addr;
        if (calib_flash_is_record_valid(record)) {
            count++;
            if ((best == NULL) || (record->sequence > best_seq)) {
                best = record;
                best_addr = addr;
                best_seq = record->sequence;
            }
        }
    }

    if (valid_count != NULL) {
        *valid_count = count;
    }

    if (best == NULL) {
        if (latest_record != NULL) {
            *latest_record = NULL;
        }
        if (latest_addr != NULL) {
            *latest_addr = 0U;
        }
        if (latest_seq != NULL) {
            *latest_seq = 0U;
        }
        return -1;
    }

    if (latest_record != NULL) {
        *latest_record = best;
    }
    if (latest_addr != NULL) {
        *latest_addr = best_addr;
    }
    if (latest_seq != NULL) {
        *latest_seq = best_seq;
    }
    return 0;
}

/**
 * @brief  扫描A/B双页并返回全局最新有效记录（V2/V3/V4）
 * @return 0=找到, -1=未找到
 */
static int8_t calib_flash_find_latest_v2_record(const calib_flash_record_t **latest_record,
                                                uint32_t *latest_addr,
                                                uint32_t *latest_seq,
                                                uint16_t *valid_count,
                                                uint32_t *latest_page_addr)
{
    const calib_flash_record_t *record_a = NULL;
    const calib_flash_record_t *record_b = NULL;
    uint32_t addr_a = 0U;
    uint32_t addr_b = 0U;
    uint32_t seq_a = 0U;
    uint32_t seq_b = 0U;
    uint16_t count_a = 0U;
    uint16_t count_b = 0U;
    uint8_t has_a = (calib_flash_scan_page_latest_v2(CALIB_FLASH_PAGE_A_ADDR, &record_a, &addr_a, &seq_a, &count_a) == 0);
    uint8_t has_b = (calib_flash_scan_page_latest_v2(CALIB_FLASH_PAGE_B_ADDR, &record_b, &addr_b, &seq_b, &count_b) == 0);

    if (valid_count != NULL) {
        *valid_count = (uint16_t)(count_a + count_b);
    }

    if (!has_a && !has_b) {
        if (latest_record != NULL) {
            *latest_record = NULL;
        }
        if (latest_addr != NULL) {
            *latest_addr = 0U;
        }
        if (latest_seq != NULL) {
            *latest_seq = 0U;
        }
        if (latest_page_addr != NULL) {
            *latest_page_addr = 0U;
        }
        return -1;
    }

    if (has_a && (!has_b || (seq_a >= seq_b))) {
        if (latest_record != NULL) {
            *latest_record = record_a;
        }
        if (latest_addr != NULL) {
            *latest_addr = addr_a;
        }
        if (latest_seq != NULL) {
            *latest_seq = seq_a;
        }
        if (latest_page_addr != NULL) {
            *latest_page_addr = CALIB_FLASH_PAGE_A_ADDR;
        }
    } else {
        if (latest_record != NULL) {
            *latest_record = record_b;
        }
        if (latest_addr != NULL) {
            *latest_addr = addr_b;
        }
        if (latest_seq != NULL) {
            *latest_seq = seq_b;
        }
        if (latest_page_addr != NULL) {
            *latest_page_addr = CALIB_FLASH_PAGE_B_ADDR;
        }
    }

    return 0;
}

/**
 * @brief  在指定页查找首个空闲写入槽位
 * @return 0=找到, -1=未找到
 */
static int8_t calib_flash_find_first_empty_slot_in_page(uint32_t page_addr, uint32_t *slot_addr)
{
    uint32_t addr;
    const uint32_t words = sizeof(calib_flash_record_t) / sizeof(uint32_t);
    uint32_t page_end = page_addr + CALIB_FLASH_PAGE_SIZE_BYTES;

    for (addr = page_addr;
         addr <= (page_end - sizeof(calib_flash_record_t));
         addr += sizeof(calib_flash_record_t)) {
        if (calib_flash_is_slot_erased(addr, words)) {
            if (slot_addr != NULL) {
                *slot_addr = addr;
            }
            return 0;
        }
    }

    return -1;
}

/**
 * @brief  向指定地址写入标定记录（当前版本V4）
 * @return 0=成功, -1=失败
 */
static int8_t calib_flash_write_v2_record(uint32_t write_addr, const calib_flash_record_t *record)
{
    const uint32_t *word_ptr = (const uint32_t *)record;
    uint32_t index;
    FLASH_Status status;

    for (index = 0U; index < (sizeof(calib_flash_record_t) / sizeof(uint32_t)); index++) {
        status = FLASH_ProgramWord(write_addr + index * 4U, word_ptr[index]);
        if (status != FLASH_COMPLETE) {
            printf("[CAL] Flash保存失败：写入失败，index=%lu status=%d\r\n", index, status);
            return -1;
        }
    }

    return 0;
}

/**
 * @brief  计算有符号32位差值绝对值
 */
static uint32_t calib_abs_diff_i32(int32_t left, int32_t right)
{
    return (left >= right) ? (uint32_t)(left - right) : (uint32_t)(right - left);
}

/**
 * @brief  计算无符号32位差值绝对值
 */
static uint32_t calib_abs_diff_u32(uint32_t left, uint32_t right)
{
    return (left >= right) ? (left - right) : (right - left);
}

/**
 * @brief  判断新旧标定参数变化是否达到写入阈值
 * @return 1=变化显著(需要写入), 0=变化过小(跳过写入)
 */
static uint8_t calib_flash_is_significant_change(const calib_flash_record_t *new_record,
                                                 const calib_flash_record_t *old_record)
{
    uint32_t new_flow_pulses;
    uint32_t old_flow_pulses;
    uint32_t new_flow_k_q1000;
    uint32_t old_flow_k_q1000;
    uint16_t new_diag_block_flow_q100;
    uint16_t old_diag_block_flow_q100;
    uint8_t new_diag_block_pwm;
    uint8_t old_diag_block_pwm;
    uint32_t diff_pressure;
    uint32_t diff_current_offset;
    uint32_t diff_current_sens;
    uint32_t diff_flow_pulses;
    uint32_t diff_flow_k;
    uint32_t diff_diag_block_flow_q100;
    uint32_t diff_diag_block_pwm;

    if (old_record == NULL) {
        return 1U;
    }

    if (new_record->version == CALIB_FLASH_VERSION_V4) {
        return calib_flash_is_significant_change_v4(new_record, old_record);
    }

    new_flow_pulses = calib_flash_unpack_flow_pulses(new_record->flow_pulses_per_liter);
    old_flow_pulses = calib_flash_unpack_flow_pulses(old_record->flow_pulses_per_liter);
    new_flow_k_q1000 = calib_flash_unpack_flow_k_q1000(new_record->flow_k_factor_q1000);
    old_flow_k_q1000 = calib_flash_unpack_flow_k_q1000(old_record->flow_k_factor_q1000);
    new_diag_block_flow_q100 = calib_flash_unpack_diag_block_flow_q100(new_record->flow_k_factor_q1000, new_record->version);
    old_diag_block_flow_q100 = calib_flash_unpack_diag_block_flow_q100(old_record->flow_k_factor_q1000, old_record->version);
    new_diag_block_pwm = calib_flash_unpack_diag_block_pwm(new_record->flow_pulses_per_liter, new_record->version);
    old_diag_block_pwm = calib_flash_unpack_diag_block_pwm(old_record->flow_pulses_per_liter, old_record->version);

    diff_pressure = calib_abs_diff_i32(new_record->pressure_offset_mv, old_record->pressure_offset_mv);
    diff_current_offset = calib_abs_diff_i32(new_record->current_offset_mv, old_record->current_offset_mv);
    diff_current_sens = calib_abs_diff_u32(new_record->current_sensitivity_mv, old_record->current_sensitivity_mv);
    diff_flow_pulses = calib_abs_diff_u32(new_flow_pulses, old_flow_pulses);
    diff_flow_k = calib_abs_diff_u32(new_flow_k_q1000, old_flow_k_q1000);
    diff_diag_block_flow_q100 = calib_abs_diff_u32(new_diag_block_flow_q100, old_diag_block_flow_q100);
    diff_diag_block_pwm = calib_abs_diff_u32(new_diag_block_pwm, old_diag_block_pwm);

    if (diff_pressure >= CALIB_SAVE_DELTA_PRESSURE_MV) {
        return 1U;
    }
    if (diff_current_offset >= CALIB_SAVE_DELTA_CURRENT_OFFSET_MV) {
        return 1U;
    }
    if (diff_current_sens >= CALIB_SAVE_DELTA_CURRENT_SENS_MV) {
        return 1U;
    }
    if (diff_flow_pulses >= CALIB_SAVE_DELTA_FLOW_PULSES) {
        return 1U;
    }
    if (diff_flow_k >= CALIB_SAVE_DELTA_FLOW_K_Q1000) {
        return 1U;
    }
    if (diff_diag_block_flow_q100 >= CALIB_SAVE_DELTA_DIAG_BLOCK_FLOW_Q100) {
        return 1U;
    }
    if (diff_diag_block_pwm >= CALIB_SAVE_DELTA_DIAG_BLOCK_PWM) {
        return 1U;
    }

    if (scheduler_is_verbose_log_enabled()) {
        printf("[CAL] 写入节流: ΔP=%lumV ΔIoff=%lumV ΔIsens=%lu ΔFlowPulse=%lu ΔFlowKq=%lu ΔDiagFlowQ=%lu ΔDiagPWM=%lu\r\n",
               diff_pressure, diff_current_offset, diff_current_sens,
               diff_flow_pulses, diff_flow_k,
               diff_diag_block_flow_q100, diff_diag_block_pwm);
    }
    return 0U;
}

/**
 * @brief  在线调整 PID 参数（用于测试和调试）
 * @param  kp: 比例系数
 * @param  ki: 积分系数
 * @param  kd: 微分系数
 */
void pid_tune_online(float kp, float ki, float kd)
{
    pid_tune(&pid_temp, kp, ki, kd);
    pid_reset(&pid_temp);  /* 重置积分和微分项，避免旧数据影响 */

    printf("[PID] 参数已更新: Kp=%.2f, Ki=%.2f, Kd=%.2f\r\n", kp, ki, kd);
}

/**
 * @brief  应用方案默认标定参数
 */
void scheduler_apply_calibration_defaults(void)
{
    calib_adc_t adc_cfg;
    yfs201_calib_t flow_cfg;

    adc_cfg.pressure_offset_mv = CALIB_DEFAULT_PRESSURE_OFFSET_MV;
    adc_cfg.current_offset_mv = CALIB_DEFAULT_CURRENT_OFFSET_MV;
    adc_cfg.current_sensitivity_mv = CALIB_DEFAULT_CURRENT_SENSITIVITY_MV;
    calib_adc_set(&adc_cfg);

    flow_cfg.k_factor = CALIB_DEFAULT_FLOW_K_FACTOR;
    flow_cfg.pulses_per_liter = CALIB_DEFAULT_FLOW_PULSES_PER_LITER;
    yfs201_set_calib(&flow_cfg);
    diag_threshold_reset_defaults();

    printf("[CAL] 已加载方案默认标定参数\r\n");
}

/**
 * @brief  打印当前标定参数
 */
void scheduler_print_calibration(void)
{
    const calib_adc_t *adc_cfg = calib_adc_get();
    const yfs201_calib_t *flow_cfg = yfs201_get_calib();

    printf("[CAL] ADC: pressure_off=%d mV, current_off=%d mV, current_sens=%u mV/A\r\n",
           adc_cfg->pressure_offset_mv,
           adc_cfg->current_offset_mv,
           adc_cfg->current_sensitivity_mv);
    printf("[CAL] FLOW: k=%.3f Hz/(L/min), pulse_per_liter=%u\r\n",
           flow_cfg->k_factor,
           flow_cfg->pulses_per_liter);
}

/**
 * @brief  执行电流零点校准并打印结果
 */
void scheduler_calibrate_current_zero(void)
{
#if USE_SIM_CURRENT
    printf("[CAL] 当前启用电流模拟值（USE_SIM_CURRENT=1），跳过零点校准\r\n");
    printf("[CAL] 如需校准，请先关闭模拟并接入ACS712后再执行\r\n");
#else
    if (current_calibrate() == 0) {
        printf("[CAL] 电流零点校准完成\r\n");
    } else {
        printf("[CAL] 电流零点校准失败\r\n");
    }
#endif

    scheduler_print_calibration();
}

/**
 * @brief  执行压力零点校准（无压状态）
 * @note   逻辑：offset = V_zero_measured - PRESSURE_VOLTAGE_MIN
 */
void scheduler_calibrate_pressure_zero(void)
{
    calib_adc_t adc_cfg = *calib_adc_get();
    uint16_t pressure_adc;
    uint16_t pressure_mv;
    int16_t pressure_offset;

    pressure_adc = adc_get_average(ADC_CH_PRESSURE, 32);
    pressure_mv = (uint16_t)((uint32_t)pressure_adc * ADC_VREF_MV / ADC_RESOLUTION);

    /* 未接传感器或电压异常时，拒绝写入校准值，避免误伤配置 */
    if ((pressure_mv < 100U) || (pressure_mv > 600U)) {
        printf("[CAL] 压力零点校准失败：当前零压电压=%umV（期望约200mV）\r\n", pressure_mv);
        printf("[CAL] 请确认HK1100C接线/供电后重试\r\n");
        return;
    }

    pressure_offset = (int16_t)pressure_mv - (int16_t)PRESSURE_VOLTAGE_MIN;
    adc_cfg.pressure_offset_mv = pressure_offset;
    calib_adc_set(&adc_cfg);

    printf("[CAL] 压力零点校准完成：ADC=%u, Vzero=%umV, offset=%d mV\r\n",
           pressure_adc, pressure_mv, pressure_offset);

#if USE_SIM_PRESSURE
    printf("[CAL] 注意：当前启用压力模拟值（USE_SIM_PRESSURE=1），算法仍使用SIM_PRESSURE_MPA\r\n");
#endif

    scheduler_print_calibration();
}

/**
 * @brief  打印ADC原始快照（压力/电流/TDS）
 */
void scheduler_calibration_print_adc_snapshot(void)
{
    uint16_t adc_pressure = adc_get_average(ADC_CH_PRESSURE, 16);
    uint16_t adc_current = adc_get_average(ADC_CH_CURRENT, 16);
    uint16_t adc_tds = adc_get_average(ADC_CH_TDS, 16);

    uint16_t mv_pressure = (uint16_t)((uint32_t)adc_pressure * ADC_VREF_MV / ADC_RESOLUTION);
    uint16_t mv_current = (uint16_t)((uint32_t)adc_current * ADC_VREF_MV / ADC_RESOLUTION);
    uint16_t mv_tds = (uint16_t)((uint32_t)adc_tds * ADC_VREF_MV / ADC_RESOLUTION);

    printf("[CAL] ADC快照: PRESS=%u(%umV), CURRENT=%u(%umV), TDS=%u(%umV)\r\n",
           adc_pressure, mv_pressure, adc_current, mv_current, adc_tds, mv_tds);
}

/**
 * @brief  微调流量标定（每升脉冲数）
 */
void scheduler_adjust_flow_pulses_per_liter(int16_t delta)
{
    const yfs201_calib_t *flow_cfg = yfs201_get_calib();
    yfs201_calib_t new_cfg = *flow_cfg;
    int32_t new_pulses = (int32_t)new_cfg.pulses_per_liter + (int32_t)delta;

    if (new_pulses < 200) {
        new_pulses = 200;
    } else if (new_pulses > 1200) {
        new_pulses = 1200;
    }

    new_cfg.pulses_per_liter = (uint16_t)new_pulses;
    new_cfg.k_factor = (float)new_cfg.pulses_per_liter / 60.0f;
    yfs201_set_calib(&new_cfg);

    printf("[CAL] 流量系数微调完成：pulse_per_liter=%u, k=%.3f\r\n",
           new_cfg.pulses_per_liter, new_cfg.k_factor);
    scheduler_print_calibration();
}

/**
 * @brief  保存当前标定参数到Flash
 */
int8_t scheduler_calibration_save_to_flash(void)
{
    calib_flash_record_t record;
    const calib_flash_record_t *latest_record = NULL;
    const calib_adc_t *adc_cfg = calib_adc_get();
    const yfs201_calib_t *flow_cfg = yfs201_get_calib();
    diag_threshold_config_t diag_cfg;
    uint32_t packed_words[5];
    uint32_t latest_addr = 0U;
    uint32_t latest_page_addr = 0U;
    uint32_t latest_seq = 0U;
    uint32_t write_addr = 0U;
    uint32_t target_page_addr = CALIB_FLASH_PAGE_A_ADDR;
    uint16_t valid_count = 0U;
    uint16_t valid_count_after = 0U;
    uint32_t slot_index;
    FLASH_Status status;

    if (diag_threshold_get(&diag_cfg) != 0) {
        diag_threshold_reset_defaults();
        if (diag_threshold_get(&diag_cfg) != 0) {
            printf("[CAL] Flash保存失败：读取诊断阈值失败\r\n");
            return -1;
        }
    }
    if (calib_flash_pack_v4_payload(adc_cfg, flow_cfg, &diag_cfg, packed_words) != 0) {
        printf("[CAL] Flash保存失败：V4载荷打包失败\r\n");
        return -1;
    }

    record.magic = CALIB_FLASH_MAGIC;
    record.version = CALIB_FLASH_VERSION_V4;
    record.pressure_offset_mv = (int32_t)packed_words[0];
    record.current_offset_mv = (int32_t)packed_words[1];
    record.current_sensitivity_mv = packed_words[2];
    record.flow_pulses_per_liter = packed_words[3];
    record.flow_k_factor_q1000 = packed_words[4];
    record.sequence = 1U;

    if (calib_flash_find_latest_v2_record(&latest_record, &latest_addr, &latest_seq, &valid_count, &latest_page_addr) == 0) {
        (void)latest_addr;
        record.sequence = (latest_seq == CALIB_FLASH_ERASED_WORD) ? 1U : (latest_seq + 1U);
        if (!calib_flash_is_significant_change(&record, latest_record)) {
            printf("[CAL] 参数变化小于写入阈值，已跳过Flash保存\r\n");
            return 0;
        }
    }
    record.checksum = calib_flash_checksum_v2(&record);
    if (latest_page_addr != 0U) {
        target_page_addr = latest_page_addr;
    }

    FLASH_Unlock();
    if (calib_flash_find_first_empty_slot_in_page(target_page_addr, &write_addr) != 0) {
        /* 当前活动页已写满，切到对页擦除后再写入 */
        target_page_addr = calib_flash_get_peer_page(target_page_addr);
        status = FLASH_ErasePage(target_page_addr);
        if (status != FLASH_COMPLETE) {
            FLASH_Lock();
            printf("[CAL] Flash保存失败：擦除页失败，status=%d\r\n", status);
            return -1;
        }
        write_addr = target_page_addr;
    }

    if (calib_flash_write_v2_record(write_addr, &record) != 0) {
        FLASH_Lock();
        return -1;
    }

    FLASH_Lock();

    slot_index = (write_addr - target_page_addr) / sizeof(calib_flash_record_t);
    printf("[CAL] 已保存标定参数到Flash(V4): seq=%lu, page=%s, slot=%lu, addr=0x%08lX\r\n",
           record.sequence, calib_flash_get_page_name(target_page_addr), slot_index, write_addr);
    printf("[CAL] 已同步全量诊断阈值: blockage_flow=%.2f L/min, blockage_pump_pwm=%u%%\r\n",
           diag_cfg.blockage_flow_thresh, diag_cfg.blockage_pump_pwm_min);
    if (scheduler_is_verbose_log_enabled()) {
        (void)calib_flash_find_latest_v2_record(NULL, NULL, NULL, &valid_count_after, NULL);
        printf("[CAL] Flash双页有效记录总数: %u\r\n", valid_count_after);
    }
    return 0;
}

/**
 * @brief  从Flash加载标定参数
 */
int8_t scheduler_calibration_load_from_flash(void)
{
    const calib_flash_record_t *record_v2 = NULL;
    uint32_t record_addr = 0U;
    uint32_t record_page_addr = 0U;
    uint32_t latest_seq = 0U;
    uint16_t valid_count = 0U;
    uint32_t flow_pulses;
    uint32_t flow_k_q1000;
    uint16_t diag_block_flow_q100;
    uint8_t diag_block_pwm;

    if (calib_flash_find_latest_v2_record(&record_v2, &record_addr, &latest_seq, &valid_count, &record_page_addr) == 0) {
        if (record_v2->version == CALIB_FLASH_VERSION_V4) {
            diag_threshold_config_t diag_cfg_v4;
            int32_t pressure_offset_mv;
            int32_t current_offset_mv;
            uint32_t current_sensitivity_mv;

            if (calib_flash_unpack_v4_payload(record_v2,
                                              &pressure_offset_mv,
                                              &current_offset_mv,
                                              &current_sensitivity_mv,
                                              &flow_pulses,
                                              &flow_k_q1000,
                                              &diag_cfg_v4) != 0) {
                printf("[CAL] V4记录解码失败，回退默认标定参数\r\n");
                return -1;
            }

            calib_flash_apply_values(pressure_offset_mv,
                                     current_offset_mv,
                                     current_sensitivity_mv,
                                     flow_pulses,
                                     flow_k_q1000);

            if (diag_threshold_set(&diag_cfg_v4) == 0) {
                printf("[CAL] 已加载全量诊断阈值(V4): blockage_flow=%.2f L/min, blockage_pump_pwm=%u%%\r\n",
                       diag_cfg_v4.blockage_flow_thresh, diag_cfg_v4.blockage_pump_pwm_min);
            } else {
                diag_threshold_reset_defaults();
                printf("[CAL] V4诊断阈值加载失败，已回退默认阈值\r\n");
            }
        } else {
            flow_pulses = calib_flash_unpack_flow_pulses(record_v2->flow_pulses_per_liter);
            flow_k_q1000 = calib_flash_unpack_flow_k_q1000(record_v2->flow_k_factor_q1000);
            calib_flash_apply_values(record_v2->pressure_offset_mv,
                                     record_v2->current_offset_mv,
                                     record_v2->current_sensitivity_mv,
                                     flow_pulses,
                                     flow_k_q1000);

            if (record_v2->version == CALIB_FLASH_VERSION_V3) {
                diag_block_flow_q100 = calib_flash_unpack_diag_block_flow_q100(record_v2->flow_k_factor_q1000, record_v2->version);
                diag_block_pwm = calib_flash_unpack_diag_block_pwm(record_v2->flow_pulses_per_liter, record_v2->version);
                if (calib_diag_apply_runtime_blockage(diag_block_flow_q100, diag_block_pwm) == 0) {
                    printf("[CAL] 已加载诊断阈值(V3): blockage_flow=%.2f L/min, blockage_pump_pwm=%u%%\r\n",
                           calib_diag_flow_from_q100(diag_block_flow_q100), diag_block_pwm);
                } else {
                    diag_threshold_reset_defaults();
                    printf("[CAL] V3诊断阈值加载失败，已回退到默认值\r\n");
                }
            } else {
                diag_threshold_reset_defaults();
                printf("[CAL] 记录版本不含诊断阈值，已使用默认诊断阈值\r\n");
            }
        }

        printf("[CAL] 已从Flash加载标定参数(V%lu): seq=%lu, page=%s, slot=%lu\r\n",
               record_v2->version,
               latest_seq,
               calib_flash_get_page_name(record_page_addr),
               (record_addr - record_page_addr) / sizeof(calib_flash_record_t));
        if (scheduler_is_verbose_log_enabled()) {
            printf("[CAL] Flash双页有效记录总数: %u\r\n", valid_count);
        }
        return 0;
    } else {
        const calib_flash_record_v1_t *record_v1_a = (const calib_flash_record_v1_t *)CALIB_FLASH_PAGE_A_ADDR;
        const calib_flash_record_v1_t *record_v1_b = (const calib_flash_record_v1_t *)CALIB_FLASH_PAGE_B_ADDR;
        if (calib_flash_validate_record_v1(record_v1_b, NULL)) {
            calib_flash_apply_values(record_v1_b->pressure_offset_mv,
                                     record_v1_b->current_offset_mv,
                                     record_v1_b->current_sensitivity_mv,
                                     record_v1_b->flow_pulses_per_liter,
                                     record_v1_b->flow_k_factor_q1000);
            diag_threshold_reset_defaults();
            printf("[CAL] 已兼容加载V1标定参数(page=B)，建议执行`s`升级为V4格式\r\n");
            return 0;
        } else if (calib_flash_validate_record_v1(record_v1_a, NULL)) {
            calib_flash_apply_values(record_v1_a->pressure_offset_mv,
                                     record_v1_a->current_offset_mv,
                                     record_v1_a->current_sensitivity_mv,
                                     record_v1_a->flow_pulses_per_liter,
                                     record_v1_a->flow_k_factor_q1000);
            diag_threshold_reset_defaults();
            printf("[CAL] 已兼容加载V1标定参数(page=A)，建议执行`s`升级为V4格式\r\n");
            return 0;
        }
    }

    return -1;
}

/**
 * @brief  打印Flash标定记录状态（简版）
 */
void scheduler_calibration_print_flash_status_brief(void)
{
    const calib_flash_record_t *latest_global = NULL;
    uint32_t latest_addr_global = 0U;
    uint32_t latest_seq_global = 0U;
    uint32_t latest_page_global = 0U;
    uint16_t valid_count_total = 0U;
    uint16_t valid_count_a = 0U;
    uint16_t valid_count_b = 0U;
    uint32_t flow_pulses;
    uint32_t flow_k_q1000;
    int32_t pressure_offset_mv;
    int32_t current_offset_mv;
    uint32_t current_sensitivity_mv;
    uint16_t diag_block_flow_q100;
    uint8_t diag_block_pwm;
    diag_threshold_config_t diag_cfg_v4;

    (void)calib_flash_scan_page_latest_v2(CALIB_FLASH_PAGE_A_ADDR, NULL, NULL, NULL, &valid_count_a);
    (void)calib_flash_scan_page_latest_v2(CALIB_FLASH_PAGE_B_ADDR, NULL, NULL, NULL, &valid_count_b);

    printf("[CAL] Flash简报: pageA=%u, pageB=%u, total=%u\r\n",
           valid_count_a, valid_count_b, (uint16_t)(valid_count_a + valid_count_b));

    if (calib_flash_find_latest_v2_record(&latest_global,
                                          &latest_addr_global,
                                          &latest_seq_global,
                                          &valid_count_total,
                                          &latest_page_global) == 0) {
        printf("[CAL] 最新记录: page=%s, seq=%lu, slot=%lu\r\n",
               calib_flash_get_page_name(latest_page_global),
               latest_seq_global,
               (latest_addr_global - latest_page_global) / sizeof(calib_flash_record_t));
        if (latest_global->version == CALIB_FLASH_VERSION_V4) {
            if (calib_flash_unpack_v4_payload(latest_global,
                                              &pressure_offset_mv,
                                              &current_offset_mv,
                                              &current_sensitivity_mv,
                                              &flow_pulses,
                                              &flow_k_q1000,
                                              &diag_cfg_v4) == 0) {
                printf("[CAL] 参数: Poff=%ldmV Ioff=%ldmV Isens=%lumV/A FlowPulse=%lu FlowK=%.3f\r\n",
                       (long)pressure_offset_mv,
                       (long)current_offset_mv,
                       current_sensitivity_mv,
                       flow_pulses,
                       (float)flow_k_q1000 / 1000.0f);
                printf("[CAL] 诊断阈值: blockage_flow=%.2fL/min blockage_pump_pwm=%u%% (ver=%lu)\r\n",
                       diag_cfg_v4.blockage_flow_thresh,
                       diag_cfg_v4.blockage_pump_pwm_min,
                       latest_global->version);
            } else {
                printf("[CAL] 最新记录解码失败（version=%lu）\r\n", latest_global->version);
            }
        } else {
            flow_pulses = calib_flash_unpack_flow_pulses(latest_global->flow_pulses_per_liter);
            flow_k_q1000 = calib_flash_unpack_flow_k_q1000(latest_global->flow_k_factor_q1000);
            diag_block_flow_q100 = calib_flash_unpack_diag_block_flow_q100(latest_global->flow_k_factor_q1000, latest_global->version);
            diag_block_pwm = calib_flash_unpack_diag_block_pwm(latest_global->flow_pulses_per_liter, latest_global->version);
            printf("[CAL] 参数: Poff=%ldmV Ioff=%ldmV Isens=%lumV/A FlowPulse=%lu FlowK=%.3f\r\n",
                   (long)latest_global->pressure_offset_mv,
                   (long)latest_global->current_offset_mv,
                   latest_global->current_sensitivity_mv,
                   flow_pulses,
                   (float)flow_k_q1000 / 1000.0f);
            printf("[CAL] 诊断阈值: blockage_flow=%.2fL/min blockage_pump_pwm=%u%% (ver=%lu)\r\n",
                   calib_diag_flow_from_q100(diag_block_flow_q100),
                   diag_block_pwm,
                   latest_global->version);
        }
    } else {
        printf("[CAL] 最新V2/V3/V4记录: 无\r\n");
    }
}

/**
 * @brief  打印Flash标定记录状态（含原始字段）
 */
void scheduler_calibration_print_flash_status(void)
{
    const calib_flash_record_t *latest_global = NULL;
    const calib_flash_record_t *latest_a = NULL;
    const calib_flash_record_t *latest_b = NULL;
    const calib_flash_record_v1_t *record_v1_a = (const calib_flash_record_v1_t *)CALIB_FLASH_PAGE_A_ADDR;
    const calib_flash_record_v1_t *record_v1_b = (const calib_flash_record_v1_t *)CALIB_FLASH_PAGE_B_ADDR;
    const char *reason_a = NULL;
    const char *reason_b = NULL;
    uint32_t latest_addr_global = 0U;
    uint32_t latest_seq_global = 0U;
    uint32_t latest_page_global = 0U;
    uint32_t latest_addr_a = 0U;
    uint32_t latest_addr_b = 0U;
    uint32_t latest_seq_a = 0U;
    uint32_t latest_seq_b = 0U;
    uint16_t valid_count_total = 0U;
    uint16_t valid_count_a = 0U;
    uint16_t valid_count_b = 0U;
    uint32_t flow_pulses;
    uint32_t flow_k_q1000;
    int32_t pressure_offset_mv;
    int32_t current_offset_mv;
    uint32_t current_sensitivity_mv;
    uint16_t diag_block_flow_q100;
    uint8_t diag_block_pwm;
    diag_threshold_config_t diag_cfg_v4;

    (void)calib_flash_scan_page_latest_v2(CALIB_FLASH_PAGE_A_ADDR, &latest_a, &latest_addr_a, &latest_seq_a, &valid_count_a);
    (void)calib_flash_scan_page_latest_v2(CALIB_FLASH_PAGE_B_ADDR, &latest_b, &latest_addr_b, &latest_seq_b, &valid_count_b);

    printf("[CAL] Flash页A: 0x%08lX - 0x%08lX\r\n",
           CALIB_FLASH_PAGE_A_ADDR, CALIB_FLASH_PAGE_A_END - 1U);
    printf("[CAL] Flash页B: 0x%08lX - 0x%08lX\r\n",
           CALIB_FLASH_PAGE_B_ADDR, CALIB_FLASH_PAGE_B_END - 1U);
    printf("[CAL] 记录槽位大小=%uB, 每页槽位数=%u\r\n",
           (uint16_t)sizeof(calib_flash_record_t),
           (uint16_t)(CALIB_FLASH_PAGE_SIZE_BYTES / sizeof(calib_flash_record_t)));

    if (latest_a != NULL) {
        printf("[CAL] 页A: 有效记录=%u, 最新seq=%lu, slot=%lu\r\n",
               valid_count_a, latest_seq_a,
               (latest_addr_a - CALIB_FLASH_PAGE_A_ADDR) / sizeof(calib_flash_record_t));
    } else {
        printf("[CAL] 页A: 有效记录=0\r\n");
    }

    if (latest_b != NULL) {
        printf("[CAL] 页B: 有效记录=%u, 最新seq=%lu, slot=%lu\r\n",
               valid_count_b, latest_seq_b,
               (latest_addr_b - CALIB_FLASH_PAGE_B_ADDR) / sizeof(calib_flash_record_t));
    } else {
        printf("[CAL] 页B: 有效记录=0\r\n");
    }

    if (calib_flash_find_latest_v2_record(&latest_global,
                                          &latest_addr_global,
                                          &latest_seq_global,
                                          &valid_count_total,
                                          &latest_page_global) == 0) {
        printf("[CAL] 全局最新: page=%s, seq=%lu, slot=%lu, 总有效记录=%u\r\n",
               calib_flash_get_page_name(latest_page_global),
               latest_seq_global,
               (latest_addr_global - latest_page_global) / sizeof(calib_flash_record_t),
               valid_count_total);
        printf("[CAL] RAW(V2/V3/V4): magic=0x%08lX, version=%lu, seq=%lu, checksum=0x%08lX, calc=0x%08lX\r\n",
               latest_global->magic, latest_global->version, latest_global->sequence,
               latest_global->checksum, calib_flash_checksum_v2(latest_global));

        if (latest_global->version == CALIB_FLASH_VERSION_V4) {
            if (calib_flash_unpack_v4_payload(latest_global,
                                              &pressure_offset_mv,
                                              &current_offset_mv,
                                              &current_sensitivity_mv,
                                              &flow_pulses,
                                              &flow_k_q1000,
                                              &diag_cfg_v4) == 0) {
                printf("[CAL] RAW(V4): pressure_off=%ld mV, current_off=%ld mV, current_sens=%lu mV/A\r\n",
                       (long)pressure_offset_mv,
                       (long)current_offset_mv,
                       current_sensitivity_mv);
                printf("[CAL] RAW(V4): flow_pulses=%lu, flow_k=%.3f Hz/(L/min)\r\n",
                       flow_pulses,
                       (float)flow_k_q1000 / 1000.0f);
                printf("[CAL] RAW(V4): block_flow=%.2f block_temp=%.1f hold=%u block_pwm=%u%%\r\n",
                       diag_cfg_v4.blockage_flow_thresh,
                       diag_cfg_v4.blockage_temp_thresh,
                       diag_cfg_v4.blockage_hold_time,
                       diag_cfg_v4.blockage_pump_pwm_min);
                printf("[CAL] RAW(V4): bubble_cv=%.3f pump_cv=%.3f leak_slope=%.4f leak_drop=%.1f%%\r\n",
                       diag_cfg_v4.bubble_flow_cv_thresh,
                       diag_cfg_v4.pump_current_cv_thresh,
                       diag_cfg_v4.leak_pressure_slope,
                       diag_cfg_v4.leak_flow_drop_percent);
                printf("[CAL] RAW(V4): tds_warn=%u tds_crit=%u eff_warn=%.1f eff_crit=%.1f eff_pwm=%u%%\r\n",
                       diag_cfg_v4.water_tds_warning,
                       diag_cfg_v4.water_tds_critical,
                       diag_cfg_v4.eff_temp_diff_warning,
                       diag_cfg_v4.eff_temp_diff_critical,
                       diag_cfg_v4.eff_pump_pwm_thresh);
            } else {
                printf("[CAL] RAW(V4): payload解码失败\r\n");
            }
        } else {
            flow_pulses = calib_flash_unpack_flow_pulses(latest_global->flow_pulses_per_liter);
            flow_k_q1000 = calib_flash_unpack_flow_k_q1000(latest_global->flow_k_factor_q1000);
            printf("[CAL] RAW(V2/V3): pressure_off=%ld mV, current_off=%ld mV, current_sens=%lu mV/A\r\n",
                   (long)latest_global->pressure_offset_mv,
                   (long)latest_global->current_offset_mv,
                   latest_global->current_sensitivity_mv);
            printf("[CAL] RAW(V2/V3): flow_pulses=%lu, flow_k=%.3f Hz/(L/min)\r\n",
                   flow_pulses,
                   (float)flow_k_q1000 / 1000.0f);
            if (latest_global->version >= CALIB_FLASH_VERSION_V3) {
                diag_block_flow_q100 = calib_flash_unpack_diag_block_flow_q100(latest_global->flow_k_factor_q1000, latest_global->version);
                diag_block_pwm = calib_flash_unpack_diag_block_pwm(latest_global->flow_pulses_per_liter, latest_global->version);
                printf("[CAL] RAW(V3): blockage_flow=%.2f L/min, blockage_pump_pwm=%u%%\r\n",
                       calib_diag_flow_from_q100(diag_block_flow_q100), diag_block_pwm);
            } else {
                printf("[CAL] RAW(V2): 不含诊断阈值扩展字段\r\n");
            }
        }
    } else {
        printf("[CAL] 全局最新V2/V3/V4记录: 无\r\n");
    }

    if (calib_flash_validate_record_v1(record_v1_a, &reason_a)) {
        printf("[CAL] V1兼容(page=A): 有效, checksum=0x%08lX, calc=0x%08lX\r\n",
               record_v1_a->checksum, calib_flash_checksum_v1(record_v1_a));
    } else {
        printf("[CAL] V1兼容(page=A): 无效（%s）\r\n", reason_a);
    }

    if (calib_flash_validate_record_v1(record_v1_b, &reason_b)) {
        printf("[CAL] V1兼容(page=B): 有效, checksum=0x%08lX, calc=0x%08lX\r\n",
               record_v1_b->checksum, calib_flash_checksum_v1(record_v1_b));
    } else {
        printf("[CAL] V1兼容(page=B): 无效（%s）\r\n", reason_b);
    }
}

/**
 * @brief  清空Flash中的标定记录并恢复默认参数
 */
int8_t scheduler_calibration_clear_flash(void)
{
    FLASH_Status status_a;
    FLASH_Status status_b;

    FLASH_Unlock();
    status_a = FLASH_ErasePage(CALIB_FLASH_PAGE_A_ADDR);
    if (status_a == FLASH_COMPLETE) {
        status_b = FLASH_ErasePage(CALIB_FLASH_PAGE_B_ADDR);
    } else {
        status_b = FLASH_ERROR_PG;
    }
    FLASH_Lock();

    if (status_a != FLASH_COMPLETE) {
        printf("[CAL] 清空Flash失败：擦除页A失败，status=%d\r\n", status_a);
        return -1;
    }
    if (status_b != FLASH_COMPLETE) {
        printf("[CAL] 清空Flash失败：擦除页B失败，status=%d\r\n", status_b);
        return -1;
    }

    scheduler_apply_calibration_defaults();
    printf("[CAL] 已清空Flash标定记录（A/B双页），并恢复默认标定/诊断阈值参数\r\n");
    scheduler_print_calibration();
    return 0;
}

typedef struct {
    uint8_t pass_count;
    uint8_t warn_count;
    uint8_t fail_count;
} post_summary_t;

/**
 * @brief  POST条目打印
 */
static void post_log_item(post_summary_t *summary, const char *name, int8_t state, const char *detail)
{
    if (state > 0) {
        summary->pass_count++;
        printf("[POST] PASS %-12s %s\r\n", name, detail);
    } else if (state == 0) {
        summary->warn_count++;
        printf("[POST] WARN %-12s %s\r\n", name, detail);
    } else {
        summary->fail_count++;
        printf("[POST] FAIL %-12s %s\r\n", name, detail);
    }
}

/**
 * @brief  开机自检（POST）
 */
static void scheduler_post_check(int8_t temp_result,
                                 int8_t tds_result,
                                 int8_t ws_result,
                                 int8_t current_result,
                                 int8_t tinyml_result,
                                 int8_t alarm_result,
                                 int8_t voice_result,
                                 int8_t cloud_result)
{
#if ENABLE_POST_CHECK
    post_summary_t summary = {0, 0, 0};
    const temp_data_t *temp = temp_get_data();
    const flow_data_t *flow = flow_get_data();
    int16_t chip_temp = chip_temp_get_value();

    printf("[POST] 开始系统自检...\r\n");

    post_log_item(&summary, "TEMP_INIT", (temp_result == 0) ? 1 : -1,
                  (temp_result == 0) ? "温度任务初始化成功" : "温度任务初始化失败");
    post_log_item(&summary, "TEMP_CPU", temp->sensor_online[TEMP_SENSOR_CPU] ? 1 : -1,
                  temp->sensor_online[TEMP_SENSOR_CPU] ? "CPU探头在线" : "CPU探头离线");
    post_log_item(&summary, "TEMP_WATER",
                  temp->sensor_online[TEMP_SENSOR_WATER] ? 1 : 0,
                  temp->sensor_online[TEMP_SENSOR_WATER] ? "水温探头在线" : "水温探头离线/未安装");
    post_log_item(&summary, "TEMP_ENV",
                  temp->sensor_online[TEMP_SENSOR_ENV] ? 1 : 0,
                  temp->sensor_online[TEMP_SENSOR_ENV] ? "环境温度在线" : "环境温度离线/未安装");

    post_log_item(&summary, "FLOW_INIT", flow->sensor_online ? 1 : 0,
                  flow->sensor_online ? "流量任务初始化成功" : "流量状态待脉冲确认");
    post_log_item(&summary, "TDS_INIT", (tds_result == 0) ? 1 : -1,
                  (tds_result == 0) ? "TDS初始化成功" : "TDS初始化失败");
    post_log_item(&summary, "WEBSOCKET", (ws_result == 0) ? 1 : 0,
                  (ws_result == 0) ? "WebSocket初始化成功" : "WebSocket离线可运行");
    post_log_item(&summary, "VOICE", (voice_result == 0) ? 1 : 0,
                  (voice_result == 0) ? "CI03语音在线" : "CI03未就绪");
#if ENABLE_TINYML_TEMP_ANOMALY
    post_log_item(&summary, "TINYML", (tinyml_result == 0) ? 1 : 0,
                  (tinyml_result == 0) ? "温度异常检测已启用(预热中)" : "温度异常检测未就绪");
#else
    post_log_item(&summary, "TINYML", 0, "温度异常检测已禁用");
#endif
#if ENABLE_BUZZER_ALARM
    post_log_item(&summary, "ALARM", (alarm_result == 0) ? 1 : 0,
                  (alarm_result == 0) ? "蜂鸣器告警通道就绪" : "蜂鸣器未就绪");
#else
    post_log_item(&summary, "ALARM", 1, "蜂鸣器功能已禁用");
#endif
#if ENABLE_APP_CLOUD_LAYER
    post_log_item(&summary, "CLOUD", (cloud_result == 0) ? 1 : 0,
                  (cloud_result == 0) ? "4G云端在线" : "4G云端未就绪");
#else
    post_log_item(&summary, "CLOUD", 1, "云端应用层旁路（当前仅WebSocket主链路）");
#endif

#if USE_SIM_CURRENT
    post_log_item(&summary, "CURRENT", 0, "电流使用模拟值，等待ACS712");
#else
    post_log_item(&summary, "CURRENT", (current_result == 0) ? 1 : 0,
                  (current_result == 0) ? "电流传感器在线" : "电流传感器未就绪");
#endif

#if USE_SIM_PRESSURE
    post_log_item(&summary, "PRESSURE", 0, "压力使用模拟值，等待HK1100C");
#else
    post_log_item(&summary, "PRESSURE", 1, "压力通道启用真实值");
#endif

    post_log_item(&summary, "CHIP_TEMP",
                  ((chip_temp > -40) && (chip_temp < 125)) ? 1 : 0,
                  ((chip_temp > -40) && (chip_temp < 125)) ? "片内温度通道正常" : "片内温度值异常");

    printf("[POST] 自检汇总: PASS=%u WARN=%u FAIL=%u\r\n",
           summary.pass_count, summary.warn_count, summary.fail_count);

    if (summary.fail_count > 0) {
        printf("[POST] 结果: 存在关键失败项，建议先排查后再联调\r\n");
    } else {
        printf("[POST] 结果: 系统可继续运行，警告项待硬件到位后闭环\r\n");
    }
#else
    (void)temp_result;
    (void)tds_result;
    (void)ws_result;
    (void)current_result;
    (void)tinyml_result;
    (void)alarm_result;
    (void)voice_result;
    (void)cloud_result;
#endif
}

/**
 * @brief  初始化IWDG看门狗
 */
static void scheduler_iwdg_init(void)
{
#if ENABLE_IWDG
    uint32_t reload;
    uint32_t wait_cnt;

    reload = ((uint32_t)IWDG_TIMEOUT_MS * IWDG_LSI_FREQ_HZ) / (1000U * IWDG_PRESCALER_DIV);
    if (reload == 0U) {
        reload = 1U;
    } else if (reload > 4095U) {
        reload = 4095U;
    } else {
        reload -= 1U;
    }

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    wait_cnt = IWDG_FLAG_WAIT_MAX;
    while ((IWDG_GetFlagStatus(IWDG_FLAG_PVU) != RESET) && (wait_cnt > 0U)) {
        wait_cnt--;
    }
    if (wait_cnt == 0U) {
        printf("[IWDG] 警告: 等待PVU超时，继续执行\r\n");
    }

    IWDG_SetReload((uint16_t)reload);
    wait_cnt = IWDG_FLAG_WAIT_MAX;
    while ((IWDG_GetFlagStatus(IWDG_FLAG_RVU) != RESET) && (wait_cnt > 0U)) {
        wait_cnt--;
    }
    if (wait_cnt == 0U) {
        printf("[IWDG] 警告: 等待RVU超时，继续执行\r\n");
    }

    IWDG_ReloadCounter();
    IWDG_Enable();

    printf("[IWDG] 已使能: timeout=%dms, reload=%lu\r\n", IWDG_TIMEOUT_MS, reload);
#else
    printf("[IWDG] 未使能（ENABLE_IWDG=0）\r\n");
#endif
}

/**
 * @brief  喂狗
 */
static void scheduler_iwdg_feed(void)
{
#if ENABLE_IWDG
    IWDG_ReloadCounter();
#endif
}

/**
 * @brief  温度控制任务 - 使用多层防震荡控制（PID + 滞环 + 防抖 + 限速 + 前馈）
 */
static void task_temp_control(void)
{
    extern void pump_set_duty(uint8_t duty);
    extern void fan_set_duty(uint8_t duty);

    /* 获取真实温度数据 */
    const temp_data_t *temp = temp_get_data();
    float current_temp = temp->cpu_temp;

    /* 获取AI预测温度和变化率 */
    float predicted_temp = current_temp;
    float temp_rate = 0.0f;
    if (predict_is_valid()) {
        predicted_temp = predict_get_state()->predicted_temp;
        temp_rate = predict_get_rate();
    }

    /* ==================== 多层控制计算 ==================== */
    float pwm_duty = ctrl_compute(&pid_temp, current_temp, predicted_temp, temp_rate);

    /* ==================== 控制执行 ==================== */
    pump_set_duty((uint8_t)pwm_duty);   /* 设置水泵转速 */
    fan_set_duty((uint8_t)pwm_duty);    /* 设置风扇转速 */

    /* ==================== 串口输出（调试用） ==================== */
    static uint8_t print_count = 0;
    if (scheduler_is_verbose_log_enabled() && (++print_count >= 10))  /* 每 10 次打印一次（每秒） */
    {
        print_count = 0;
        printf("[温控] 目标=%.1f  当前=%.1f  预测=%.1f  模式=%s  PWM=%d%%\r\n",
               target_temp, current_temp, predicted_temp,
               ctrl_get_mode_name(), (uint8_t)pwm_duty);
    }
}

/*============================ AI算法任务 ============================*/

/**
 * @brief  温度预测任务 - 每1秒推送新采样点并计算预测
 */
static void task_predict(void)
{
    const temp_data_t *temp = temp_get_data();
    predict_push(temp->cpu_temp);
    predict_compute(PREDICT_DEFAULT_AHEAD);
}

/**
 * @brief  TinyML温度异常检测任务 - 每1秒更新一次
 */
static void task_tinyml(void)
{
    tinyml_temp_update();
}

/**
 * @brief  故障诊断任务 - 每2秒运行一次诊断
 */
static void task_diagnosis(void)
{
    const temp_data_t *temp = temp_get_data();
    const flow_data_t *flow = flow_get_data();
    const pwm_data_t *pwm = pwm_get_data();
    const energy_data_t *energy = energy_get_data();

    /* 构建诊断输入数据包 */
    diag_sensor_input_t input;
    input.cpu_temp = select_sensor_value(temp->cpu_temp, temp->sensor_online[TEMP_SENSOR_CPU], SIM_CPU_TEMP);
    input.water_temp = select_sensor_value(temp->water_temp, temp->sensor_online[TEMP_SENSOR_WATER], SIM_WATER_TEMP);
    input.flow_rate = select_sensor_value(flow->flow_rate, flow->sensor_online, SIM_FLOW_LPM);
    input.pump_pwm = pwm->pump_speed;
    input.pump_current = energy->current_ma;
    input.tds_ppm = app_tds_get_ppm();
    input.runtime_seconds = energy->runtime_seconds;

    /* 振动和噪音数据（如果ADXL345已初始化） */
    adxl345_data_t accel;
    if (adxl345_read_accel(&accel) == 0) {
        input.vibration_rms = accel.vibration;
    } else {
        input.vibration_rms = USE_SIM_VIBRATION ? SIM_VIBRATION_G : 0.0f;
    }

    /* 噪音数据（MAX4466已移除，置0不参与诊断） */
    input.noise_db = 0.0f;

    /* 压力数据（HK1100C ADC读取，未接传感器时返回0） */
    if (USE_SIM_PRESSURE) {
        input.pressure = SIM_PRESSURE_MPA;
    } else {
        input.pressure = (float)pressure_get_value() / 1000.0f;  /* kPa → MPa */
    }

    diag_update(&input);

    /* 打印激活的故障 */
    const diag_output_t *result = diag_get_result();
    if (result->active_count > 0) {
        printf("[诊断] 激活故障%d个: ", result->active_count);
        for (uint8_t i = 0; i < DIAG_FAULT_COUNT; i++) {
            if (result->faults[i].active) {
                printf("%s(%.0f%%) ", diag_get_fault_name(i),
                       result->faults[i].confidence * 100.0f);
            }
        }
        printf("\r\n");
    }
}

/**
 * @brief  健康评分任务 - 每2秒更新一次
 */
static void task_health(void)
{
    const temp_data_t *temp = temp_get_data();
    const flow_data_t *flow = flow_get_data();
    const pwm_data_t *pwm = pwm_get_data();

    health_input_t input;
    input.cpu_temp = select_sensor_value(temp->cpu_temp, temp->sensor_online[TEMP_SENSOR_CPU], SIM_CPU_TEMP);
    input.water_temp = select_sensor_value(temp->water_temp, temp->sensor_online[TEMP_SENSOR_WATER], SIM_WATER_TEMP);
    input.flow_rate = select_sensor_value(flow->flow_rate, flow->sensor_online, SIM_FLOW_LPM);
    if (USE_SIM_PRESSURE) {
        input.pressure = SIM_PRESSURE_MPA;
    } else {
        input.pressure = (float)pressure_get_value() / 1000.0f;  /* kPa → MPa */
    }
    input.pump_pwm = pwm->pump_speed;
    input.tds_ppm = app_tds_get_ppm();

    /* 振动数据 */
    adxl345_data_t accel;
    if (adxl345_read_accel(&accel) == 0) {
        input.vibration_rms = accel.vibration;
    } else {
        input.vibration_rms = USE_SIM_VIBRATION ? SIM_VIBRATION_G : 0.0f;
    }

    health_update(&input);

    /* 定期打印健康评分 */
    static uint8_t print_cnt = 0;
    if (scheduler_is_verbose_log_enabled() && (++print_cnt >= 5)) {  /* 每10秒打印一次 */
        print_cnt = 0;
        const health_output_t *h = health_get_result();
        printf("[健康] 总分=%.0f %s  温度=%.0f 流量=%.0f 压力=%.0f 振动=%.0f 水质=%.0f 效率=%.0f\r\n",
               h->total_score, health_get_level_name(h->level),
               h->dim_scores[0], h->dim_scores[1], h->dim_scores[2],
               h->dim_scores[3], h->dim_scores[4], h->dim_scores[5]);
    }
}

/**
 * @brief  能效计算任务 - 每1秒更新
 */
static void task_energy(void)
{
    /* 读取电流传感器 */
    float current_ma;
#if USE_SIM_CURRENT
    current_ma = SIM_CURRENT_MA;
#else
    current_ma = current_read_ma();  /* 来自ACS712驱动 */
#endif
    energy_update(current_ma);

    /* 定期打印能效数据 */
    static uint8_t print_cnt = 0;
    if (scheduler_is_verbose_log_enabled() && (++print_cnt >= 30)) {  /* 每30秒打印一次 */
        print_cnt = 0;
        const energy_data_t *e = energy_get_data();
        printf("[能效] 功率=%.1fW  电量=%.3fkWh  碳排放=%.4fkg  运行%lus\r\n",
               e->power_w, e->energy_kwh, e->carbon_kg, e->runtime_seconds);
    }
}

/*============================ 基础任务 ============================*/

/**
 * @brief  串口任务 - 每秒打印一次信息
 */
static void task_uart(void)
{
    static uint32_t count = 0;
    count++;
    if (scheduler_is_verbose_log_enabled() && (count % 10U == 0U)) {
        printf("[调度器] UART任务运行，Count=%lu\r\n", count);
    }
}

/**
 * @brief  LED任务 - LED闪烁
 */
static void task_led(void)
{
    extern void led_toggle(void);
    led_toggle();
}

/**
 * @brief  UI任务 - 触摸处理和界面更新
 */
static void task_ui(void)
{
    /* 处理触摸事件 */
    ui_process_touch();

    /* 构建系统数据 */
    static system_data_t ui_data;
    const temp_data_t *temp = temp_get_data();
    const flow_data_t *flow = flow_get_data();
    const pwm_data_t *pwm = pwm_get_data();
    const energy_data_t *energy = energy_get_data();
    const health_output_t *health = health_get_result();
    const diag_output_t *diag = diag_get_result();

    ui_data.cpu_temp = temp->cpu_temp;
    ui_data.water_temp = temp->water_temp;
    ui_data.mcu_temp = (float)chip_temp_get_value();
    ui_data.pump_speed = pwm->pump_speed;
    ui_data.fan_speed = pwm->fan1_speed;
    ui_data.flow_rate = flow->flow_rate;
    ui_data.pressure = (float)pressure_get_value() / 1000.0f;
    ui_data.tds_ppm = app_tds_get_ppm();
    ui_data.current_ma = energy->current_ma;
    ui_data.power_w = energy->power_w;
    ui_data.energy_kwh = energy->energy_kwh;
    ui_data.health_score = (uint8_t)health->total_score;
    ui_data.is_normal = (diag->active_count == 0) ? 1 : 0;
    ui_data.fault_count = diag->active_count;
    ui_data.mode = ui_get_mode();
    ui_data.target_temp = 60.0f;

    /* 振动数据 */
    adxl345_data_t accel;
    if (adxl345_read_accel(&accel) == 0) {
        ui_data.vibration = accel.vibration;
    } else {
        ui_data.vibration = 0.0f;
    }

    /* 更新界面 */
    ui_update_current_page(&ui_data);
}

/*============================ 调度器任务列表 ============================*/

static task_t scheduler_tasks[] = {
    /* 控制任务 */
    {task_temp_control, 100,  0},    /* 多层温控任务，每100ms执行一次（10Hz） */
    {task_pwm,          100,  0},    /* PWM控制任务，每100ms执行一次 */

    /* 传感器采集任务 */
    {task_temp,         1000, 0},    /* 温度采集任务，每1秒执行一次 */
    {task_flow,         1000, 0},    /* 流量监测任务，每1秒执行一次 */
    {task_tds,          2000, 0},    /* TDS水质任务，每2秒执行一次 */
    {task_energy,       1000, 0},    /* 能效计算任务，每1秒执行一次 */

    /* AI算法任务 */
    {task_predict,      1000, 0},    /* 温度预测任务，每1秒执行一次 */
    {task_tinyml,       1000, 0},    /* TinyML温度异常检测，每1秒执行一次 */
    {task_diagnosis,    2000, 0},    /* 故障诊断任务，每2秒执行一次 */
    {task_health,       2000, 0},    /* 健康评分任务，每2秒执行一次 */

    /* 通信任务 */
    {task_websocket,    100,  0},    /* WebSocket任务，每100ms执行一次 */
#if ENABLE_APP_CLOUD_LAYER
    {task_cloud,        1000, 0},    /* 4G云端任务，每1秒执行一次 */
#endif

    /* 交互任务 */
    {task_voice,        200,  0},    /* 语音任务，每200ms执行一次 */
    {task_ui,           30,   0},    /* UI任务，每30ms执行一次（触摸+刷新）*/
    {task_alarm,        100,  0},    /* 蜂鸣器告警任务，每100ms执行一次 */

    /* 系统任务 */
    {task_uart,         1000, 0},    /* 串口任务，每1000ms执行一次 */
    {task_led,          500,  0},    /* LED任务，每500ms执行一次 */
};

static uint8_t task_num = 0;   /* 任务数量 */

/*============================ 调度器函数实现 ============================*/

/**
 * @brief  调度器初始化
 */
void scheduler_init(void)
{
    extern void pump_pwm_init(void);
    extern void fan_pwm_init(void);
    int8_t current_result = 0;
    int8_t tinyml_result = -1;
    int8_t alarm_result = -1;
    int8_t voice_result = -1;
    int8_t cloud_result = -1;

    /* 初始化 TIM2 定时器（替代SysTick，避免与Delay_Us冲突） */
    tim2_tick_init();

    /* 初始化ADC（TDS传感器依赖ADC） */
    printf("[调度器] 准备初始化ADC...\r\n");
    extern void adc_init(void);
    adc_init();
    printf("[调度器] ADC初始化完成\r\n");

    /* 加载标定参数（方案默认） */
    scheduler_apply_calibration_defaults();
    if (scheduler_calibration_load_from_flash() != 0) {
        printf("[CAL] Flash无有效标定，使用默认参数\r\n");
    }
    scheduler_print_calibration();

    /* 初始化温度传感器 */
    printf("[调度器] 准备初始化温度传感器...\r\n");
    int8_t temp_result = temp_init();
    printf("[调度器] 温度传感器初始化返回: %d\r\n", temp_result);

    /* 初始化 PWM控制（水泵和风扇）*/
    pwm_init();

    /* 初始化流量传感器 */
    flow_init();

    /* 初始化TDS水质传感器 */
    printf("[调度器] 准备初始化TDS水质传感器...\r\n");
    int8_t tds_result = app_tds_init();
    printf("[调度器] TDS传感器初始化返回: %d\r\n", tds_result);

#if USE_SIM_CURRENT
    printf("[调度器] 电流通道使用模拟值（USE_SIM_CURRENT=1）\r\n");
#else
    printf("[调度器] 准备初始化电流传感器...\r\n");
    current_result = current_init();
    if (current_result == 0) {
        current_result = current_self_test();
    }
    printf("[调度器] 电流传感器初始化/自检返回: %d\r\n", current_result);
#endif

    /* 初始化WebSocket客户端 */
    printf("[调度器] 准备初始化WebSocket客户端...\r\n");
    int8_t ws_result = websocket_init();
    printf("[调度器] WebSocket客户端初始化返回: %d\r\n", ws_result);

    /* 初始化 PID 控制器 */
    pid_init(&pid_temp, 5.0f, 1.0f, 0.5f, 100);
    pid_set_setpoint(&pid_temp, target_temp);
    pid_set_output_limits(&pid_temp, 30.0f, 100.0f);
    pid_set_integral_limits(&pid_temp, -30.0f, 30.0f);

    /* 初始化多层控制器（5层防震荡 + 自适应PID） */
    ctrl_init(&pid_temp);
    printf("[控制] 多层防震荡控制器初始化完成\r\n");

    /* 初始化AI算法模块 */
    predict_init();
    diag_init();
    health_init();
    energy_init();
    printf("[AI] 预测/诊断/健康/能效算法初始化完成\r\n");

    /* 初始化TinyML温度异常检测骨架（后续可替换为TinyMaix模型） */
    tinyml_result = tinyml_temp_init();

    /* 初始化蜂鸣器告警状态机（失败不影响系统运行） */
    alarm_result = alarm_init();

    /* 初始化CI03(CI1302)语音模块（失败不影响系统运行） */
    voice_result = voice_app_init();
    if (voice_result != 0) {
        printf("[语音] CI03初始化失败，语音功能暂不可用\r\n");
    }

    /* 初始化云端应用层（当前默认旁路，避免与WebSocket链路混用） */
#if ENABLE_APP_CLOUD_LAYER
    printf("[调度器] 准备初始化云端应用层...\r\n");
    cloud_result = cloud_app_init();
#else
    cloud_result = 0;
    printf("[调度器] 云端应用层旁路（当前仅WebSocket主链路）\r\n");
#endif

    /* 初始化UI（屏幕+触摸） */
    printf("[调度器] 准备初始化UI...\r\n");
    ui_init();
    printf("[调度器] UI初始化完成\r\n");

    printf("[PID] 目标温度: %.1f  PID: Kp=%.1f Ki=%.1f Kd=%.1f\r\n",
           target_temp, pid_temp.kp, pid_temp.ki, pid_temp.kd);

    /* 开机POST自检 */
    scheduler_post_check(temp_result, tds_result, ws_result, current_result,
                         tinyml_result, alarm_result, voice_result, cloud_result);

    /* IWDG放在初始化末尾使能，避免初始化阶段误复位 */
    scheduler_iwdg_init();

    task_num = sizeof(scheduler_tasks) / sizeof(task_t);
    printf("[调度器] 初始化完成，任务数量: %d\r\n", task_num);
}

/**
 * @brief  调度器运行（在主循环中调用）
 */
void scheduler_run(void)
{
    uint8_t i;
    uint32_t now_time = get_tick_ms();  /* 获取当前时间 */

    for (i = 0; i < task_num; i++)
    {
        /* 检查任务是否到达执行时间 */
        if (now_time >= (scheduler_tasks[i].last_run + scheduler_tasks[i].rate_ms))
        {
            scheduler_tasks[i].last_run = now_time;     /* 更新任务上次运行时间 */
            scheduler_tasks[i].task_func();             /* 执行任务 */
        }
    }

    scheduler_iwdg_feed();

}
