/**
 * @file    app_cloud.h
 * @brief   4G云端通信预留应用层（默认旁路）
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 功能（M2加分里程碑，离线时不影响核心功能）：
 *   - 定时上报传感器数据（JSON格式）
 *   - 定时上报健康评分和故障状态
 *   - 接收云端下发控制命令
 *   - 心跳保活
 *
 * 当前默认策略：
 *   - 项目默认仅启用 WebSocket 主链路。
 *   - 是否启用本预留层由 ENABLE_APP_CLOUD_LAYER 控制。
 */

#ifndef __APP_CLOUD_H
#define __APP_CLOUD_H

#include "ch32v30x.h"

/*============================ 函数声明 ============================*/

/**
 * @brief  云端通信初始化
 * @return 0=成功, -1=失败（不影响系统运行）
 */
int8_t cloud_app_init(void);

/**
 * @brief  云端通信任务（由调度器定时调用，建议1s周期）
 */
void task_cloud(void);

/**
 * @brief  立即上报实时数据
 * @return 0=成功, -1=失败
 */
int8_t cloud_report_realtime(void);

/**
 * @brief  立即上报健康数据
 * @return 0=成功, -1=失败
 */
int8_t cloud_report_health(void);

/**
 * @brief  云端是否在线
 * @return 1=在线, 0=离线
 */
uint8_t cloud_is_online(void);

#endif /* __APP_CLOUD_H */
