/**
 * @file    app_temp.c
 * @brief   温度传感器应用层实现
 * @author  智能水冷项目
 * @date    2025-01-27
 */

#include "app_temp.h"
#include "drv_ds18b20.h"
#include "drv_dht11.h"
#include "scheduler.h"
#include "debug.h"

/*============================ 私有变量 ============================*/

/* 温度数据 */
static temp_data_t temp_data = {
    .cpu_temp = 0.0f,
    .water_temp = 0.0f,
    .env_temp = 0.0f,
    .cpu_status = 0,
    .water_status = 0,
    .env_status = 0,
    .sensor_online = {0, 0, 0}
};

/* 转换状态机 */
static uint8_t convert_started = 0;
static uint32_t convert_start_time = 0;
static uint32_t last_dht_time = 0;

static const ds18b20_bus_t ds18b20_cpu_bus = {
    DS18B20_PORT, DS18B20_PIN, DS18B20_RCC
};

static const ds18b20_bus_t ds18b20_water_bus = {
    TEMP_WATER_PORT, TEMP_WATER_PIN, TEMP_WATER_RCC
};

/*============================ 外部函数 ============================*/
extern uint32_t get_tick_ms(void);  /* 从scheduler.c获取系统时间 */

/*============================ 内部函数 ============================*/

/**
 * @brief  更新温度状态
 */
static void temp_update_status(void)
{
    /* CPU温度状态 */
    if (temp_data.cpu_temp >= TEMP_CPU_CRITICAL)
    {
        temp_data.cpu_status = 2;  /* 危险 */
    }
    else if (temp_data.cpu_temp >= TEMP_CPU_WARNING)
    {
        temp_data.cpu_status = 1;  /* 警告 */
    }
    else
    {
        temp_data.cpu_status = 0;  /* 正常 */
    }

    /* 水温状态 */
    if (temp_data.water_temp >= TEMP_WATER_CRITICAL)
    {
        temp_data.water_status = 2;  /* 危险 */
    }
    else if (temp_data.water_temp >= TEMP_WATER_WARNING)
    {
        temp_data.water_status = 1;  /* 警告 */
    }
    else
    {
        temp_data.water_status = 0;  /* 正常 */
    }

    /* 环境温度状态 */
    if (temp_data.env_temp >= TEMP_ENV_CRITICAL)
    {
        temp_data.env_status = 2;  /* 危险 */
    }
    else if (temp_data.env_temp >= TEMP_ENV_WARNING)
    {
        temp_data.env_status = 1;  /* 警告 */
    }
    else
    {
        temp_data.env_status = 0;  /* 正常 */
    }
}

/*============================ 公共接口函数 ============================*/

/**
 * @brief  温度传感器初始化
 */
int8_t temp_init(void)
{
    printf("\r\n[TEMP] 初始化温度传感器...\r\n");

    /* 初始化DS18B20 */
    if (ds18b20_init_bus(&ds18b20_cpu_bus) != 0)
    {
        printf("[TEMP] ❌ DS18B20初始化失败！\r\n");
        return -1;
    }

    /* 设置为12位分辨率（最高精度） */
    ds18b20_set_resolution_bus(&ds18b20_cpu_bus, DS18B20_RESOLUTION_12BIT);

    if (TEMP_HAS_WATER_SENSOR) {
        if (ds18b20_init_bus(&ds18b20_water_bus) == 0) {
            ds18b20_set_resolution_bus(&ds18b20_water_bus, DS18B20_RESOLUTION_12BIT);
            temp_data.sensor_online[TEMP_SENSOR_WATER] = 1;
        } else {
            temp_data.sensor_online[TEMP_SENSOR_WATER] = 0;
        }
    }

    if (TEMP_HAS_ENV_SENSOR) {
        if (dht11_init() == 0) {
            temp_data.sensor_online[TEMP_SENSOR_ENV] = 1;
        } else {
            temp_data.sensor_online[TEMP_SENSOR_ENV] = 0;
        }
    }

    /* 启动第一次温度转换 */
    ds18b20_start_convert_bus(&ds18b20_cpu_bus);
    if (temp_data.sensor_online[TEMP_SENSOR_WATER]) {
        ds18b20_start_convert_bus(&ds18b20_water_bus);
    }
    convert_started = 1;
    convert_start_time = get_tick_ms();

    /* 初始化温度数据 */
    temp_data.cpu_temp = 25.0f;
    temp_data.water_temp = 25.0f;
    temp_data.env_temp = 25.0f;
    temp_data.sensor_online[TEMP_SENSOR_CPU] = 1;

    printf("[TEMP] ✅ 温度传感器初始化成功\r\n");
    if (scheduler_is_verbose_log_enabled()) {
        printf("[TEMP] - 分辨率: 12位 (±0.0625℃)\r\n");
        printf("[TEMP] - 采样周期: %d ms\r\n", TEMP_SAMPLE_INTERVAL);
        printf("[TEMP] - CPU警告阈值: %.1f℃\r\n", TEMP_CPU_WARNING);
        printf("[TEMP] - 水温警告阈值: %.1f℃\r\n", TEMP_WATER_WARNING);
    }

    return 0;
}

/**
 * @brief  温度采集任务（定时调用，建议1秒）
 *
 * 工作流程:
 *   1. 检查上次转换是否完成（750ms）
 *   2. 读取温度值
 *   3. 启动新的转换
 *   4. 更新温度状态
 */
void task_temp(void)
{
    static uint32_t last_run = 0;
    uint32_t now = get_tick_ms();

    /* 采样间隔控制 */
    if (now - last_run < TEMP_SAMPLE_INTERVAL)
        return;

    last_run = now;

    /* 等待转换完成（12位需要750ms） */
    if (convert_started && (now - convert_start_time >= 750))
    {
        /* 读取温度值 */
        float temp = ds18b20_get_temperature_bus(&ds18b20_cpu_bus);

        /* 检查读取是否成功 */
        if (temp > -100.0f && temp < 150.0f)
        {
            /* 这里简化处理，假设只有一个DS18B20用于CPU温度
             * 如果有多个传感器，需要使用ROM匹配命令分别读取 */
            temp_data.cpu_temp = temp;
            temp_data.sensor_online[TEMP_SENSOR_CPU] = 1;

            if (TEMP_HAS_WATER_SENSOR && temp_data.sensor_online[TEMP_SENSOR_WATER]) {
                float water_temp = ds18b20_get_temperature_bus(&ds18b20_water_bus);
                if (water_temp > -100.0f && water_temp < 150.0f) {
                    temp_data.water_temp = water_temp;
                } else {
                    temp_data.sensor_online[TEMP_SENSOR_WATER] = 0;
                }
            } else {
                /* 只有一个DS18B20时，水温使用模拟值（待接第二个探头） */
                temp_data.water_temp = temp - 15.0f;  /* 水温通常比CPU低15度左右 */
            }

            /* 更新温度状态 */
            temp_update_status();

            /* 打印温度数据（可选，调试用） */
            if (scheduler_is_verbose_log_enabled()) {
                printf("[TEMP] CPU=%.2f℃ (状态:%d), 水温=%.2f℃ (状态:%d), 环境=%.2f℃ (状态:%d)\r\n",
                       temp_data.cpu_temp, temp_data.cpu_status,
                       temp_data.water_temp, temp_data.water_status,
                       temp_data.env_temp, temp_data.env_status);
            }

            /* 温度报警 */
            if (temp_data.cpu_status == 2)
            {
                printf("[TEMP] ⚠️ CPU温度过高！当前%.1f℃ > %.1f℃\r\n",
                       temp_data.cpu_temp, TEMP_CPU_CRITICAL);
            }
            else if (temp_data.cpu_status == 1)
            {
                printf("[TEMP] ⚡ CPU温度警告: %.1f℃\r\n", temp_data.cpu_temp);
            }
        }
        else
        {
            /* 读取失败，传感器可能离线 */
            temp_data.sensor_online[TEMP_SENSOR_CPU] = 0;
            printf("[TEMP] ❌ 温度读取失败！传感器离线？\r\n");
        }

        /* 启动新的温度转换 */
        ds18b20_start_convert_bus(&ds18b20_cpu_bus);
        if (temp_data.sensor_online[TEMP_SENSOR_WATER]) {
            ds18b20_start_convert_bus(&ds18b20_water_bus);
        }
        convert_started = 1;
        convert_start_time = now;
    }

    /* DHT11 环境温度读取（>=2s周期） */
    if (TEMP_HAS_ENV_SENSOR && (now - last_dht_time >= 2000)) {
        dht11_data_t dht;
        last_dht_time = now;
        if (dht11_read(&dht) == 0) {
            temp_data.env_temp = (float)dht.temperature;
            temp_data.sensor_online[TEMP_SENSOR_ENV] = 1;
        } else {
            temp_data.sensor_online[TEMP_SENSOR_ENV] = 0;
        }
    }
}

/**
 * @brief  获取温度数据
 */
const temp_data_t* temp_get_data(void)
{
    return &temp_data;
}

/**
 * @brief  获取CPU温度
 */
float temp_get_cpu(void)
{
    if (temp_data.sensor_online[TEMP_SENSOR_CPU])
        return temp_data.cpu_temp;
    else
        return -999.0f;  /* 传感器离线 */
}

/**
 * @brief  获取水温
 */
float temp_get_water(void)
{
    if (temp_data.sensor_online[TEMP_SENSOR_WATER])
        return temp_data.water_temp;
    else
        return -999.0f;  /* 传感器离线 */
}

/**
 * @brief  检查温度报警
 */
uint8_t temp_check_alarm(void)
{
    /* 任何一个传感器处于警告或危险状态 */
    return (temp_data.cpu_status > 0) || (temp_data.water_status > 0);
}
