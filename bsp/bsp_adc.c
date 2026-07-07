/**
 * @file    bsp_adc.c
 * @brief   ADC底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2025-01-23
 *
 * ADC配置: 12位分辨率, DMA连续采集
 */

#include "bsp_adc.h"

/*============================ 私有变量 ============================*/

/* DMA缓冲区 - 存储4个通道的ADC值 */
static volatile uint16_t adc_dma_buffer[ADC_CHANNEL_NUM];

/* ADC标定配置（默认值：与方案一致） */
static calib_adc_t s_calib_adc = {
    .pressure_offset_mv = 0,
    .current_offset_mv = 0,
    .current_sensitivity_mv = CURRENT_SENSITIVITY_MV
};

/*============================ ADC初始化 ============================*/

/**
 * @brief  ADC + DMA初始化
 * @note   ADC1, 12位精度, DMA连续采集3个通道
 */
void adc_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    ADC_InitTypeDef ADC_InitStructure = {0};
    DMA_InitTypeDef DMA_InitStructure = {0};

    /* 使能时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* ADC时钟配置: 144MHz/6 = 24MHz */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    /* 配置ADC引脚为模拟输入: PA0(压力) PA1(OPA1输出/电流) PA2(TDS) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* DMA配置 */
    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)adc_dma_buffer;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = ADC_CHANNEL_NUM;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel1, ENABLE);

    /* ADC1配置 */
    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;                /* 扫描模式 */
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;          /* 连续转换 */
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = ADC_CHANNEL_NUM;
    ADC_Init(ADC1, &ADC_InitStructure);

    /* 配置ADC通道顺序和采样时间 */
    /* 采样时间: 239.5周期, 转换时间 = (239.5+12.5)/24MHz ≈ 10.5us */
    ADC_RegularChannelConfig(ADC1, ADC_CHANNEL_PRESSURE, 1, ADC_SampleTime_239Cycles5);   /* PA0 */
    ADC_RegularChannelConfig(ADC1, ADC_CHANNEL_CURRENT, 2, ADC_SampleTime_239Cycles5);    /* PA1(经OPA1) */
    ADC_RegularChannelConfig(ADC1, ADC_CHANNEL_TDS, 3, ADC_SampleTime_239Cycles5);        /* PA2 */
    ADC_RegularChannelConfig(ADC1, ADC_CHANNEL_CHIP_TEMP, 4, ADC_SampleTime_239Cycles5);  /* 片内温度 */

    /* 使能片内温度传感器 */
    ADC_TempSensorVrefintCmd(ENABLE);

    /* 使能DMA请求 */
    ADC_DMACmd(ADC1, ENABLE);

    /* 使能ADC1 */
    ADC_Cmd(ADC1, ENABLE);

    /* ADC校准 */
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));

    /* 启动ADC转换 */
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

/*============================ ADC数据读取 ============================*/

/**
 * @brief  获取ADC原始值
 * @param  channel_index: 通道索引 0-2
 * @return ADC原始值 0-4095
 */
uint16_t adc_get_value(uint8_t channel_index)
{
    if(channel_index >= ADC_CHANNEL_NUM)
        return 0;

    return adc_dma_buffer[channel_index];
}

/**
 * @brief  获取ADC电压值
 * @param  channel_index: 通道索引 0-2
 * @return 电压值 (mV)
 */
uint16_t adc_get_voltage(uint8_t channel_index)
{
    uint16_t adc_value = adc_get_value(channel_index);
    return (uint16_t)((uint32_t)adc_value * ADC_VREF_MV / ADC_RESOLUTION);
}

/**
 * @brief  多次采样取平均
 * @param  channel_index: 通道索引
 * @param  times: 采样次数 (建议8-16次)
 * @return 平均ADC值
 */
uint16_t adc_get_average(uint8_t channel_index, uint8_t times)
{
    uint32_t sum = 0;

    for(uint8_t i = 0; i < times; i++)
    {
        sum += adc_get_value(channel_index);
        /* 简单延时，等待新的转换结果 */
        for(volatile uint16_t j = 0; j < 1000; j++);
    }

    return (uint16_t)(sum / times);
}

void calib_adc_set(const calib_adc_t *cfg)
{
    if (cfg == NULL) return;
    s_calib_adc = *cfg;
    if (s_calib_adc.current_sensitivity_mv == 0) {
        s_calib_adc.current_sensitivity_mv = CURRENT_SENSITIVITY_MV;
    }
}

const calib_adc_t* calib_adc_get(void)
{
    return &s_calib_adc;
}

/*============================ 传感器数据读取 ============================*/

/**
 * @brief  获取压力值
 * @return 压力值 (kPa)
 * @note   HK1100C: 0.5-4.5V输出, 经10K+6.8K分压后0.2-1.82V, 对应0-1.2MPa
 */
uint16_t pressure_get_value(void)
{
    int32_t voltage = adc_get_voltage(ADC_CH_PRESSURE);

    /* HK1100C分压后: 0.2V(0kPa) ~ 1.82V(1200kPa) */
    voltage -= s_calib_adc.pressure_offset_mv;
    if(voltage <= PRESSURE_VOLTAGE_MIN)
        return 0;

    uint32_t pressure = (uint32_t)(voltage - PRESSURE_VOLTAGE_MIN) * PRESSURE_MAX_KPA
                        / (PRESSURE_VOLTAGE_MAX - PRESSURE_VOLTAGE_MIN);

    return (uint16_t)pressure;
}

/**
 * @brief  获取TDS水质值
 * @return TDS值 (ppm)
 * @note   TDS传感器: 0-3.3V → 0-1000ppm
 */
uint16_t tds_get_value(void)
{
    uint16_t voltage = adc_get_voltage(ADC_CH_TDS);

    /* 计算TDS值 */
    uint32_t tds = (uint32_t)voltage * TDS_MAX_PPM / TDS_VOLTAGE_MAX;

    return (uint16_t)tds;
}

/**
 * @brief  获取电流值
 * @return 电流值 (mA)
 * @note   ACS712-5A: 经内置OPA1缓冲后输入ADC1_IN1(PA1)
 *         零点电压2.5V, 灵敏度185mV/A
 */
uint16_t current_get_value(void)
{
    int32_t voltage = adc_get_voltage(ADC_CH_CURRENT);
    voltage -= s_calib_adc.current_offset_mv;

    /* ACS712: I = (Vout - 2500mV) / 185mV/A */
    int32_t current_ma;
    if(voltage >= CURRENT_ZERO_MV)
        current_ma = (int32_t)(voltage - CURRENT_ZERO_MV) * 1000 / s_calib_adc.current_sensitivity_mv;
    else
        current_ma = 0;  /* 负电流限制为0 */

    return (uint16_t)current_ma;
}

/**
 * @brief  获取片内温度值
 * @return 温度值 (℃)
 * @note   CH32V307片内温度传感器, 用于PCB板温监测
 *         公式: T = (V25 - Vsense) / Avg_Slope + 25
 *         V25 ≈ 1.34V, Avg_Slope ≈ 4.3mV/℃
 */
int16_t chip_temp_get_value(void)
{
    uint16_t voltage = adc_get_voltage(ADC_CH_CHIP_TEMP);

    /* 片内温度计算: T = (1340 - voltage) / 4.3 + 25 */
    int16_t temp = (int16_t)((1340 - (int32_t)voltage) * 10 / 43 + 25);

    return temp;
}
