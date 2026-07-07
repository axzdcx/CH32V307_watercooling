/**
 * @file    app_temp.h
 * @brief   温度传感器应用层 - DS18B20温度采集任务
 * @author  智能水冷项目
 * @date    2025-01-27
 *
 * 功能说明:
 *   - 管理DS18B20温度传感器（支持多个）
 *   - 定期采集CPU温度和水温
 *   - 提供温度数据接口给其他模块
 *   - 异常温度检测和报警
 */

#ifndef __APP_TEMP_H
#define __APP_TEMP_H

#include "ch32v30x.h"

/*============================ 温度传感器配置 ============================*/

#define TEMP_SENSOR_COUNT       3       /* 温度传感器数量 */
#define TEMP_SAMPLE_INTERVAL    1000    /* 采样间隔（ms） */

/* 是否安装水温探头（0=未安装，仅CPU探头；1=已安装） */
#define TEMP_HAS_WATER_SENSOR   1
/* 是否安装环境温度（DHT11） */
#define TEMP_HAS_ENV_SENSOR     1

/* 水温DS18B20接线（第二路） */
#define TEMP_WATER_PORT         GPIOA
#define TEMP_WATER_PIN          GPIO_Pin_11
#define TEMP_WATER_RCC          RCC_APB2Periph_GPIOA

/* 温度报警阈值 */
#define TEMP_CPU_WARNING        75.0f   /* CPU温度警告阈值（℃） */
#define TEMP_CPU_CRITICAL       85.0f   /* CPU温度危险阈值（℃） */
#define TEMP_WATER_WARNING      45.0f   /* 水温警告阈值（℃） */
#define TEMP_WATER_CRITICAL     55.0f   /* 水温危险阈值（℃） */
#define TEMP_ENV_WARNING        40.0f   /* 环境温度警告阈值（℃） */
#define TEMP_ENV_CRITICAL       55.0f   /* 环境温度危险阈值（℃） */

/*============================ 温度传感器类型 ============================*/

typedef enum {
    TEMP_SENSOR_CPU = 0,        /* CPU温度传感器 */
    TEMP_SENSOR_WATER = 1,      /* 水温传感器 */
    TEMP_SENSOR_ENV = 2         /* 环境温度传感器（DHT11） */
} temp_sensor_type_t;

/*============================ 温度数据结构 ============================*/

typedef struct {
    float cpu_temp;             /* CPU温度（℃） */
    float water_temp;           /* 水温（℃） */
    float env_temp;             /* 环境温度（℃） */
    uint8_t cpu_status;         /* CPU温度状态: 0=正常, 1=警告, 2=危险 */
    uint8_t water_status;       /* 水温状态: 0=正常, 1=警告, 2=危险 */
    uint8_t env_status;         /* 环境温度状态 */
    uint8_t sensor_online[TEMP_SENSOR_COUNT];  /* 传感器在线状态 */
} temp_data_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  温度传感器初始化
 * @return 0=成功, -1=失败
 */
int8_t temp_init(void);

/**
 * @brief  温度采集任务（由调度器定时调用）
 *
 * 使用方法:
 *   在调度器任务列表中注册: {task_temp, 1000, 0}
 */
void task_temp(void);

/**
 * @brief  获取温度数据
 * @return 温度数据结构指针
 */
const temp_data_t* temp_get_data(void);

/**
 * @brief  获取CPU温度
 * @return CPU温度（℃），传感器离线返回-999.0
 */
float temp_get_cpu(void);

/**
 * @brief  获取水温
 * @return 水温（℃），传感器离线返回-999.0
 */
float temp_get_water(void);

/**
 * @brief  检查温度是否异常
 * @return 1=有异常, 0=正常
 */
uint8_t temp_check_alarm(void);

#endif /* __APP_TEMP_H */
