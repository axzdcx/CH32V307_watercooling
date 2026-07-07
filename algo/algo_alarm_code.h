/**
 * @file    algo_alarm_code.h
 * @brief   诊断故障统一告警码映射（云端/WS共用）
 * @author  智能水冷项目
 * @date    2026-02-12
 */

#ifndef __ALGO_ALARM_CODE_H
#define __ALGO_ALARM_CODE_H

#include "algo_diagnosis.h"

/* 无告警时占位码 */
#define ALARM_CODE_NONE  "ALM_NONE"

/**
 * @brief  将诊断故障ID映射为统一告警码
 * @param  fault_id: 故障类型
 * @return 统一告警码字符串
 */
static inline const char* alarm_diag_fault_to_code(diag_fault_type_t fault_id)
{
    switch (fault_id) {
        case DIAG_FAULT_BLOCKAGE:       return "ALM_BLOCKAGE";
        case DIAG_FAULT_BUBBLE:         return "ALM_BUBBLE";
        case DIAG_FAULT_PUMP_AGING:     return "ALM_PUMP_AGING";
        case DIAG_FAULT_LEAKAGE:        return "ALM_LEAKAGE";
        case DIAG_FAULT_WATER_QUALITY:  return "ALM_WATER_QUAL";
        case DIAG_FAULT_LOW_EFFICIENCY: return "ALM_LOW_EFF";
        default:                        return ALARM_CODE_NONE;
    }
}

/**
 * @brief  依据置信度映射告警等级
 * @param  confidence: 置信度（0~1）
 * @return 告警等级字符串（low/medium/high）
 */
static inline const char* alarm_confidence_to_severity(float confidence)
{
    if (confidence >= 0.80f) return "high";
    if (confidence >= 0.60f) return "medium";
    return "low";
}

/**
 * @brief  获取当前最严重告警码
 * @param  diag: 诊断结果
 * @return 最严重告警码，无告警时返回ALARM_CODE_NONE
 */
static inline const char* alarm_get_most_severe_code(const diag_output_t *diag)
{
    if (diag == 0 || diag->active_count == 0 || diag->most_severe_id >= DIAG_FAULT_COUNT) {
        return ALARM_CODE_NONE;
    }
    return alarm_diag_fault_to_code((diag_fault_type_t)diag->most_severe_id);
}

#endif /* __ALGO_ALARM_CODE_H */
