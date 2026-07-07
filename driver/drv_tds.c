/**
 * @file    drv_tds.c
 * @brief   TDS水质传感器驱动实现
 * @author  智能水冷项目
 * @date    2025-01-24
 */

#include "drv_tds.h"
#include "bsp_adc.h"
#include "debug.h"

/*============================ 私有变量 ============================*/

static tds_data_t g_tds_data;

/*============================ 公共函数实现 ============================*/

/**
 * @brief  初始化TDS传感器
 */
int8_t tds_init(void)
{
    printf("[TDS] 初始化TDS水质传感器...\r\n");
    
    /* ADC已在BSP层初始化 */
    
    /* 读取初始值 */
    tds_read_data(&g_tds_data, TDS_TEMP_REF);
    
    printf("[TDS] 初始TDS值: %d ppm\r\n", g_tds_data.tds_ppm);
    printf("[TDS] 水质等级: %s\r\n", tds_get_level_str(g_tds_data.level));
    printf("[TDS] 初始化完成！\r\n");
    
    return 0;
}

/**
 * @brief  读取TDS值（ppm）
 */
uint16_t tds_read_ppm(void)
{
    /* 多次采样取平均，减少噪声 */
    uint16_t adc_avg = adc_get_average(ADC_CH_TDS, TDS_FILTER_SAMPLES);
    
    /* ADC值转换为电压 (mV) */
    uint32_t voltage_mv = (uint32_t)adc_avg * ADC_VREF_MV / ADC_RESOLUTION;
    
    /* 电压转换为TDS值 (ppm) */
    /* 公式: TDS = (Voltage / 3300mV) * 1000ppm */
    uint16_t tds_ppm = (uint16_t)((voltage_mv * TDS_MAX_PPM) / TDS_VOLTAGE_MAX);
    
    return tds_ppm;
}

/**
 * @brief  读取TDS值（带温度补偿）
 */
uint16_t tds_read_compensated(float temperature)
{
    uint16_t tds_raw = tds_read_ppm();
    
    /* 温度补偿公式: TDS_compensated = TDS_raw / (1 + coef * (T - T_ref)) */
    float temp_factor = 1.0 + TDS_TEMP_COEF * (temperature - TDS_TEMP_REF);
    uint16_t tds_compensated = (uint16_t)(tds_raw / temp_factor);
    
    return tds_compensated;
}

/**
 * @brief  读取完整TDS数据
 */
int8_t tds_read_data(tds_data_t *data, float temperature)
{
    if (data == NULL) {
        return -1;
    }
    
    /* 保存温度 */
    data->temperature = temperature;
    
    /* 读取ADC原始值 */
    data->adc_value = adc_get_average(ADC_CH_TDS, TDS_FILTER_SAMPLES);
    
    /* 计算电压 */
    data->voltage_mv = (uint16_t)((uint32_t)data->adc_value * ADC_VREF_MV / ADC_RESOLUTION);
    
    /* 计算TDS值 (ppm) */
    data->tds_ppm = (uint16_t)((uint32_t)data->voltage_mv * TDS_MAX_PPM / TDS_VOLTAGE_MAX);
    
    /* 温度补偿 */
    float temp_factor = 1.0 + TDS_TEMP_COEF * (temperature - TDS_TEMP_REF);
    data->tds_compensated = (uint16_t)(data->tds_ppm / temp_factor);
    
    /* 获取水质等级 */
    data->level = tds_get_level(data->tds_compensated);
    
    /* 判断是否正常 */
    data->is_normal = (data->tds_compensated < TDS_WARNING_THRESHOLD) ? 1 : 0;
    
    /* 更新全局数据 */
    g_tds_data = *data;
    
    return 0;
}

/**
 * @brief  获取水质等级
 */
tds_level_t tds_get_level(uint16_t tds_ppm)
{
    if (tds_ppm <= TDS_EXCELLENT_MAX) {
        return TDS_LEVEL_EXCELLENT;
    } else if (tds_ppm <= TDS_GOOD_MAX) {
        return TDS_LEVEL_GOOD;
    } else if (tds_ppm <= TDS_NORMAL_MAX) {
        return TDS_LEVEL_NORMAL;
    } else if (tds_ppm <= TDS_ACCEPTABLE_MAX) {
        return TDS_LEVEL_ACCEPTABLE;
    } else if (tds_ppm <= 1000) {
        return TDS_LEVEL_POOR;
    } else {
        return TDS_LEVEL_UNUSABLE;
    }
}

/**
 * @brief  获取水质等级字符串
 */
const char* tds_get_level_str(tds_level_t level)
{
    switch (level) {
        case TDS_LEVEL_EXCELLENT:   return "纯净水";
        case TDS_LEVEL_GOOD:        return "优质水";
        case TDS_LEVEL_NORMAL:      return "良好水";
        case TDS_LEVEL_ACCEPTABLE:  return "一般水";
        case TDS_LEVEL_POOR:        return "差水";
        case TDS_LEVEL_UNUSABLE:    return "不可用";
        default:                    return "未知";
    }
}

/**
 * @brief  检查水质是否正常
 */
uint8_t tds_is_normal(void)
{
    tds_read_data(&g_tds_data, TDS_TEMP_REF);
    return g_tds_data.is_normal;
}

/**
 * @brief  TDS传感器自检
 */
int8_t tds_self_test(void)
{
    tds_data_t data;
    
    printf("[TDS] 开始自检...\r\n");
    
    /* 读取TDS数据 */
    if (tds_read_data(&data, TDS_TEMP_REF) != 0) {
        printf("[TDS] 自检失败：读取数据错误\r\n");
        return -1;
    }
    
    /* 检查ADC值是否合理 */
    if (data.adc_value == 0 || data.adc_value == 0xFFFF) {
        printf("[TDS] 自检失败：ADC值异常 (0x%04X)\r\n", data.adc_value);
        return -1;
    }
    
    /* 检查电压是否合理 */
    if (data.voltage_mv > TDS_VOLTAGE_MAX) {
        printf("[TDS] 自检失败：电压超出范围 (%d mV)\r\n", data.voltage_mv);
        return -1;
    }
    
    printf("[TDS] 自检通过\r\n");
    printf("  ADC值: %d\r\n", data.adc_value);
    printf("  电压: %d mV\r\n", data.voltage_mv);
    printf("  TDS值: %d ppm (补偿后: %d ppm)\r\n", data.tds_ppm, data.tds_compensated);
    printf("  水质等级: %s\r\n", tds_get_level_str(data.level));
    printf("  状态: %s\r\n", data.is_normal ? "正常" : "告警");
    
    return 0;
}
