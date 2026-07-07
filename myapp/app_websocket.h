/**
 * @file    app_websocket.h
 * @brief   WebSocket云端通信任务
 * @author  智能水冷项目
 * @date    2026-02-02
 */

#ifndef __APP_WEBSOCKET_H
#define __APP_WEBSOCKET_H

#include "ch32v30x.h"

/*============================ 配置参数 ============================*/

/* WebSocket服务器配置 */
#define WS_SERVER_HOST      "43.139.170.206"    /* 服务器地址(银尔达测试服务器) */
#define WS_SERVER_PORT      28574               /* 服务器端口 */
#define WS_SERVER_PATH      "/"                 /* WebSocket路径 */

/* 数据上报间隔 */
#define WS_REALTIME_INTERVAL    5000    /* 实时数据上报间隔(ms) */
#define WS_HEALTH_INTERVAL      30000   /* 健康状态上报间隔(ms) */
#define WS_TX_FAIL_RECONNECT_THRESHOLD 3 /* 连续发送失败达到阈值后触发重连 */
#define WS_TX_MIN_GAP_MS        150     /* 连续两次上报最小间隔(ms)，防止粘连发送 */

/*============================ 函数声明 ============================*/

/**
 * @brief  WebSocket任务初始化
 * @return 0=成功, -1=失败
 */
int8_t websocket_init(void);

/**
 * @brief  WebSocket任务处理（定时调用）
 * @note   建议100ms调用一次
 */
void task_websocket(void);

/**
 * @brief  获取WebSocket连接状态
 * @return 1=已连接, 0=未连接
 */
uint8_t websocket_is_connected(void);

/**
 * @brief  手动连接WebSocket服务器
 */
void websocket_connect(void);

/**
 * @brief  设置服务器地址
 * @param  host: 服务器地址
 * @param  port: 服务器端口
 * @note   需要断开连接后才能更新配置
 */
void websocket_set_server(const char *host, uint16_t port);

/**
 * @brief  获取当前服务器配置
 * @param  host: 输出服务器地址(至少64字节)
 * @param  port: 输出服务器端口
 */
void websocket_get_server(char *host, uint16_t *port);

/**
 * @brief  手动发送实时数据
 */
void websocket_send_realtime(void);

/**
 * @brief  手动发送健康状态
 */
void websocket_send_health(void);

#endif /* __APP_WEBSOCKET_H */
