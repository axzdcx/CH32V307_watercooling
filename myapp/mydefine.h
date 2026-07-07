/**
 * @file    mydefine.h
 * @brief   全局头文件汇总
 * @author  智能水冷项目
 * @date    2025-01-22
 *
 * 在main.c中只需 #include "mydefine.h" 即可
 */

#ifndef __MYDEFINE_H
#define __MYDEFINE_H

/*============================ 系统头文件 ============================*/
#include "ch32v30x.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*============================ 组件库 ============================*/
#include "ringbuffer.h"
#include "websocket_client.h"   // WebSocket客户端

/*============================ BSP层 ============================*/
#include "bsp_gpio.h"
#include "bsp_uart.h"
#include "bsp_tim.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"
#include "bsp_adc.h"

/*============================ 驱动层 ============================*/
#include "drv_st7789.h"     // ST7789 240x240 IPS LCD驱动
#include "drv_cst816.h"     // CST816 电容触摸驱动（TK013F2261屏幕）
#include "drv_ds18b20.h"    // DS18B20 温度传感器（防水探头）
#include "drv_yfs201.h"     // YFS201 水流量传感器
#include "drv_adxl345.h"    // ADXL345 三轴加速度计（振动检测）
#include "drv_ci03.h"       // CI03(CI1302) 语音模块（I2C）
#include "drv_air780e.h"    // AIR780E 4G模块
#include "drv_tds.h"        // TDS水质传感器
#include "drv_current.h"    // ACS712电流传感器
// #include "drv_microphone.h" // MAX4466已移除，PA2改为TDS ADC

/*============================ 算法层 ============================*/
#include "algo_pid.h"       // PID控制器
#include "algo_diagnosis.h" // 故障诊断（6种故障模式）
#include "algo_health.h"    // 健康评分（6维加权）
#include "algo_predict.h"   // 温度预测（线性回归）
#include "algo_energy.h"    // 能效计算（功耗/碳排放）
#include "algo_control.h"   // 多层防震荡控制 + 自适应PID

/*============================ 应用层 ============================*/
#include "ui_framework.h"   // UI基础框架（无LVGL自绘）
#include "app_ui.h"         // 主界面应用
#include "app_temp.h"       // 温度传感器任务
#include "app_pwm.h"        // PWM风扇/水泵控制任务
#include "app_flow.h"       // 流量传感器任务
#include "app_tds.h"        // TDS水质传感器任务
#include "app_websocket.h"  // WebSocket云端通信任务
#include "app_voice.h"      // 语音交互任务（待重写为CI1302 I2C版本）
#include "app_cloud.h"      // 4G云端通信任务（AIR780E MQTT）

/*============================ 传感器模拟配置 ============================*/
/* 传感器未到位时启用模拟数据 */
#define USE_SIM_CURRENT    1
#define SIM_CURRENT_MA     1200
#define USE_SIM_PRESSURE   1
#define SIM_PRESSURE_MPA   0.20f
#define USE_SIM_VIBRATION  0
#define SIM_VIBRATION_G    0.20f
#define USE_SIM_FLOW       0
#define SIM_FLOW_LPM       3.0f
#define USE_SIM_TEMP       0
#define SIM_CPU_TEMP       45.0f
#define SIM_WATER_TEMP     35.0f

/*============================ 系统可靠性配置 ============================*/
/* 开机自检（POST）与看门狗 */
#define ENABLE_POST_CHECK  1
#define ENABLE_IWDG        1
#define IWDG_TIMEOUT_MS    2000

/*============================ 通信模式配置 ============================*/
/**
 * @brief  云端应用层开关
 * @note   0=仅启用WebSocket主链路（当前推荐，避免链路混用）
 *         1=启用app_cloud预留层（后续做独立云端时再开启）
 */
#define ENABLE_APP_CLOUD_LAYER  0

/*============================ 日志配置 ============================*/
/* 详细日志：0=精简输出（默认），1=输出高频调试日志 */
#define SYSTEM_VERBOSE_LOG_DEFAULT  0

#endif /* __MYDEFINE_H */
