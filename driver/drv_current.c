/**
 * @file    drv_current.c
 * @brief   ACS712电流传感器驱动实现
 * @author  智能水冷项目
 * @date    2025-01-24
 */

#include "drv_current.h"
#include "bsp_adc.h"
#include "debug.h"

/*============================ 私有变量 ============================*/

static current_data_t g_current_data;
static int16_t g_zero_offset = 0;  /* 零点偏移校准值 (mV) */

/*============================ 公共函数实现 ============================*/

/**
 * @brief  初始化电流传感器
 */
int8_t current_init(void)
{
    printf("[Current] 初始化ACS712电流传感器...\r\n");
    
    /* ADC已在BSP层初始化 */
    
    /* 自动校准零点 */
    printf("[Current] 正在校准零点...\r\n");
    current_calibrate();
    
    /* 读取初始值 */
    current_read_data(&g_current_data);
    
    printf("[Current] 初始电流: %d mA (%.2f A)\r\n", 
           g_current_data.current_ma,
           g_current_data.current_ma / 1000.0);
    printf("[Current] 初始化完成！\r\n");
    
    return 0;
}

/**
 * @brief  读取电流值（mA）
 */
uint16_t current_read_ma(void)
{
    const calib_adc_t *adc_cfg = calib_adc_get();

    /* 多次采样取平均，减少噪声 */
    uint16_t adc_avg = adc_get_average(ADC_CH_CURRENT, CURRENT_FILTER_SAMPLES);
    
    /* ADC值转换为电压 (mV) */
    int32_t voltage_mv = (int32_t)adc_avg * ADC_VREF_MV / ADC_RESOLUTION;
    
    /* 应用零点校准 */
    voltage_mv -= adc_cfg->current_offset_mv;
    
    /* 电压转换为电流 (mA) */
    /* 公式: I = (V - V_zero) / Sensitivity */
    /* I(mA) = (V(mV) - 2500mV) / 0.185(mV/mA) */
    int32_t current_ma = ((voltage_mv - CURRENT_ZERO_MV) * 1000) / adc_cfg->current_sensitivity_mv;
    
    /* 防止负值 */
    if (current_ma < 0) {
        current_ma = 0;
    }
    
    return (uint16_t)current_ma;
}

/**
 * @brief  读取完整电流数据
 */
int8_t current_read_data(current_data_t *data)
{
    const calib_adc_t *adc_cfg = calib_adc_get();

    if (data == NULL) {
        return -1;
    }
    
    /* 读取ADC原始值 */
    data->adc_value = adc_get_average(ADC_CH_CURRENT, CURRENT_FILTER_SAMPLES);
    
    /* 计算电压 */
    data->voltage_mv = (uint16_t)((uint32_t)data->adc_value * ADC_VREF_MV / ADC_RESOLUTION);
    
    /* 计算电流 (mA) */
    int32_t voltage_offset = (int32_t)data->voltage_mv - adc_cfg->current_offset_mv - CURRENT_ZERO_MV;
    int32_t current_ma = (voltage_offset * 1000) / adc_cfg->current_sensitivity_mv;
    
    /* 防止负值 */
    if (current_ma < 0) {
        current_ma = 0;
    }
    data->current_ma = (uint16_t)current_ma;
    
    /* 获取电流等级 */
    data->level = current_get_level(data->current_ma);
    
    /* 判断是否正常 */
    data->is_normal = (data->current_ma < CURRENT_WARNING_THRESHOLD) ? 1 : 0;
    data->is_overload = (data->current_ma >= CURRENT_OVERLOAD_MAX) ? 1 : 0;
    
    /* 更新全局数据 */
    g_current_data = *data;
    
    return 0;
}

/**
 * @brief  获取电流等级
 */
current_level_t current_get_level(uint16_t current_ma)
{
    if (current_ma <= CURRENT_STANDBY_MAX) {
        return CURRENT_LEVEL_STANDBY;
    } else if (current_ma <= CURRENT_NORMAL_MAX) {
        return CURRENT_LEVEL_NORMAL;
    } else if (current_ma <= CURRENT_HIGH_MAX) {
        return CURRENT_LEVEL_HIGH;
    } else if (current_ma <= CURRENT_OVERLOAD_MAX) {
        return CURRENT_LEVEL_OVERLOAD;
    } else {
        return CURRENT_LEVEL_DANGER;
    }
}

/**
 * @brief  获取电流等级字符串
 */
const char* current_get_level_str(current_level_t level)
{
    switch (level) {
        case CURRENT_LEVEL_STANDBY:     return "待机";
        case CURRENT_LEVEL_NORMAL:      return "正常";
        case CURRENT_LEVEL_HIGH:        return "高负载";
        case CURRENT_LEVEL_OVERLOAD:    return "过载";
        case CURRENT_LEVEL_DANGER:      return "危险";
        default:                        return "未知";
    }
}

/**
 * @brief  检查电流是否正常
 */
uint8_t current_is_normal(void)
{
    current_read_data(&g_current_data);
    return g_current_data.is_normal;
}

/**
 * @brief  电流传感器自检
 */
int8_t current_self_test(void)
{
    const calib_adc_t *adc_cfg = calib_adc_get();
    current_data_t data;
    
    printf("[Current] 开始自检...\r\n");
    
    /* 读取电流数据 */
    if (current_read_data(&data) != 0) {
        printf("[Current] 自检失败：读取数据错误\r\n");
        return -1;
    }
    
    /* 检查ADC值是否合理 */
    if (data.adc_value == 0 || data.adc_value == 0xFFFF) {
        printf("[Current] 自检失败：ADC值异常 (0x%04X)\r\n", data.adc_value);
        return -1;
    }
    
    /* 检查电压是否合理（应该在零点附近） */
    int32_t voltage_diff = (int32_t)data.voltage_mv - CURRENT_ZERO_MV;
    if (voltage_diff < -500 || voltage_diff > 500) {
        printf("[Current] 自检警告：零点偏移较大 (%d mV)\r\n", voltage_diff);
    }
    
    printf("[Current] 自检通过\r\n");
    printf("  ADC值: %d\r\n", data.adc_value);
    printf("  电压: %d mV\r\n", data.voltage_mv);
    printf("  零点偏移: %d mV\r\n", adc_cfg->current_offset_mv);
    printf("  电流: %d mA (%.2f A)\r\n", data.current_ma, data.current_ma / 1000.0);
    printf("  等级: %s\r\n", current_get_level_str(data.level));
    printf("  状态: %s\r\n", data.is_normal ? "正常" : "告警");
    
    return 0;
}

/**
 * @brief  电流传感器校准（零点校准）
 */
int8_t current_calibrate(void)
{
    printf("[Current] 开始零点校准（请确保无电流通过）...\r\n");
    
    /* 多次采样取平均 */
    uint32_t sum = 0;
    uint16_t samples = 50;
    
    for (uint16_t i = 0; i < samples; i++) {
        uint16_t adc_val = adc_get_value(ADC_CH_CURRENT);
        uint16_t voltage_mv = (uint16_t)((uint32_t)adc_val * ADC_VREF_MV / ADC_RESOLUTION);
        sum += voltage_mv;
        
        /* 简单延时 */
        for (volatile int j = 0; j < 1000; j++);
    }
    
    /* 计算平均电压 */
    uint16_t avg_voltage = (uint16_t)(sum / samples);

    /* 计算零点偏移 */
    g_zero_offset = (int16_t)avg_voltage - CURRENT_ZERO_MV;

    /* 同步到ADC标定 */
    calib_adc_t cfg = *calib_adc_get();
    cfg.current_offset_mv = g_zero_offset;
    calib_adc_set(&cfg);
    
    printf("[Current] 校准完成！零点偏移: %d mV\r\n", g_zero_offset);
    
    return 0;
}
