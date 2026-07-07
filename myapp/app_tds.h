/**
 * @file    app_tds.h
 * @brief   TDS水质传感器应用层模块
 * @author  智能水冷项目
 * @date    2026-02-09
 *
 * 功能：
 *   - TDS传感器初始化和周期性采集
 *   - 水质等级评估和告警
 *   - 温度补偿处理
 *   - 数据接口供其他模块调用
 */

#ifndef __APP_TDS_H
#define __APP_TDS_H

#include "ch32v30x.h"
#include "drv_tds.h"

/*============================ 函数声明 ============================*/

/**
 * @brief  TDS应用层初始化
 * @return 0=成功, -1=失败
 */
int8_t app_tds_init(void);

/**
 * @brief  TDS定时任务（在调度器中调用）
 */
void task_tds(void);

/**
 * @brief  获取TDS数据指针
 * @return TDS数据结构指针
 */
const tds_data_t* app_tds_get_data(void);

/**
 * @brief  获取当前TDS值 (ppm)
 * @return TDS值
 */
uint16_t app_tds_get_ppm(void);

/**
 * @brief  获取水质等级字符串
 * @return 等级字符串（如"优质水"）
 */
const char* app_tds_get_level_str(void);

/**
 * @brief  检查水质是否正常
 * @return 1=正常, 0=告警
 */
uint8_t app_tds_is_normal(void);

/**
 * @brief  打印TDS详细信息（调试用）
 */
void app_tds_print_info(void);

#endif /* __APP_TDS_H */
