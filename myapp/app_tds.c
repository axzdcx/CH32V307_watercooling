/**
 * @file    app_tds.c
 * @brief   TDS水质传感器应用层模块实现
 * @author  智能水冷项目
 * @date    2026-02-09
 */

#include "app_tds.h"
#include "app_temp.h"   /* 获取水温用于温度补偿 */
#include "debug.h"

/*============================ 私有变量 ============================*/

static tds_data_t g_tds_data;           /* TDS数据 */
static uint8_t g_tds_initialized = 0;   /* 初始化标志 */

/*============================ 公共函数实现 ============================*/

/**
 * @brief  TDS应用层初始化
 */
int8_t app_tds_init(void)
{
    printf("[TDS] 应用层初始化...\r\n");

    /* 调用驱动层初始化 */
    if (tds_init() != 0) {
        printf("[TDS] 驱动初始化失败！\r\n");
        return -1;
    }

    /* 执行自检 */
    if (tds_self_test() != 0) {
        printf("[TDS] 自检失败！\r\n");
        return -1;
    }

    /* 读取初始数据 */
    tds_read_data(&g_tds_data, TDS_TEMP_REF);

    g_tds_initialized = 1;
    printf("[TDS] 应用层初始化完成！\r\n");

    return 0;
}

/**
 * @brief  TDS定时任务（在调度器中调用）
 */
void task_tds(void)
{
    if (!g_tds_initialized) {
        return;
    }

    /* 获取当前水温用于温度补偿 */
    float water_temp = TDS_TEMP_REF;  /* 默认25°C */
    const temp_data_t *temp = temp_get_data();
    if (temp != NULL) {
        if (temp->sensor_online[TEMP_SENSOR_WATER]) {
            water_temp = temp->water_temp;
        }
    }

    /* 读取TDS数据（带温度补偿） */
    tds_read_data(&g_tds_data, water_temp);

    /* 水质告警检测 */
    static uint8_t last_normal = 1;
    if (g_tds_data.is_normal != last_normal) {
        if (!g_tds_data.is_normal) {
            printf("[TDS] ⚠️ 水质告警！TDS=%d ppm，等级: %s\r\n",
                   g_tds_data.tds_compensated,
                   tds_get_level_str(g_tds_data.level));
        } else {
            printf("[TDS] ✅ 水质恢复正常，TDS=%d ppm\r\n",
                   g_tds_data.tds_compensated);
        }
        last_normal = g_tds_data.is_normal;
    }
}

/**
 * @brief  获取TDS数据指针
 */
const tds_data_t* app_tds_get_data(void)
{
    return &g_tds_data;
}

/**
 * @brief  获取当前TDS值 (ppm)
 */
uint16_t app_tds_get_ppm(void)
{
    return g_tds_data.tds_compensated;
}

/**
 * @brief  获取水质等级字符串
 */
const char* app_tds_get_level_str(void)
{
    return tds_get_level_str(g_tds_data.level);
}

/**
 * @brief  检查水质是否正常
 */
uint8_t app_tds_is_normal(void)
{
    return g_tds_data.is_normal;
}

/**
 * @brief  打印TDS详细信息（调试用）
 */
void app_tds_print_info(void)
{
    printf("\r\n========== TDS水质信息 ==========\r\n");
    printf("  ADC原始值: %d\r\n", g_tds_data.adc_value);
    printf("  电压: %d mV\r\n", g_tds_data.voltage_mv);
    printf("  TDS值: %d ppm\r\n", g_tds_data.tds_ppm);
    printf("  补偿后TDS: %d ppm (水温: %.1f°C)\r\n",
           g_tds_data.tds_compensated, g_tds_data.temperature);
    printf("  水质等级: %s\r\n", tds_get_level_str(g_tds_data.level));
    printf("  状态: %s\r\n", g_tds_data.is_normal ? "正常 ✅" : "告警 ⚠️");
    printf("==================================\r\n\r\n");
}
