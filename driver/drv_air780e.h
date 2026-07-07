/**
 * @file    drv_air780e.h
 * @brief   AIR780E 4G模块驱动头文件（DTU透传模式）
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * 工作模式：DTU透传模式
 *   - 模块已通过Luatools配置好MQTT参数
 *   - 上电自动连接阿里云IoT平台
 *   - MCU直接发送JSON数据，模块自动转发到云端
 *   - 云端下发的数据，模块自动转发给MCU
 *
 * 硬件连接：
 *   - VCC  → 5V（峰值电流2A，需大电容）
 *   - GND  → GND
 *   - TXD  → PB11 (UART3_RX)
 *   - RXD  → PB10 (UART3_TX)
 *
 * 通信参数：
 *   - 波特率：115200
 *   - 数据位：8
 *   - 停止位：1
 *   - 校验位：无
 *
 * 使用流程：
 *   1. air780e_init()           // 初始化
 *   2. air780e_send_json()      // 发送JSON数据到云端
 *   3. air780e_process()        // 定时调用，处理接收数据
 */

#ifndef __DRV_AIR780E_H
#define __DRV_AIR780E_H

#include "ch32v30x.h"

/*============================ 配置参数 ============================*/

/* 接收缓冲区大小 */
#define AIR780E_RX_BUF_SIZE     1024    /* 扩大接收窗口，降低长报文截断概率 */
#define AIR780E_TX_BUF_SIZE     512     /* 减小：1024→512 */

/* 心跳间隔（秒） */
#define AIR780E_HEARTBEAT_INTERVAL  60  /* 60秒发送一次心跳 */

/* 数据上报间隔（秒） */
#define AIR780E_REPORT_INTERVAL     10  /* 10秒上报一次数据 */

/* 自动重连参数（骨架） */
#define AIR780E_RECONNECT_BASE_MS      2000    /* 首次重连等待(ms) */
#define AIR780E_RECONNECT_MAX_MS       30000   /* 重连等待上限(ms) */
#define AIR780E_RECONNECT_COOLDOWN_MS  120000  /* 达到上限后冷却(ms) */
#define AIR780E_MAX_RECONNECT_ATTEMPTS 5       /* 最大连续重连次数 */

/*============================ 模块状态 ============================*/

typedef enum {
    AIR780E_STATE_IDLE = 0,         /* 空闲 */
    AIR780E_STATE_READY,            /* 就绪（可以发送数据） */
    AIR780E_STATE_SENDING,          /* 发送中 */
    AIR780E_STATE_ERROR             /* 错误状态 */
} air780e_state_t;

/*============================ 统计信息 ============================*/

typedef struct {
    uint32_t tx_count;              /* 发送次数 */
    uint32_t rx_count;              /* 接收次数 */
    uint32_t tx_bytes;              /* 发送字节数 */
    uint32_t rx_bytes;              /* 接收字节数 */
    uint32_t last_tx_time;          /* 上次发送时间 */
    uint32_t last_rx_time;          /* 上次接收时间 */
} air780e_stats_t;

/*============================ 数据结构 ============================*/

typedef struct {
    air780e_state_t state;              /* 模块状态 */
    air780e_stats_t stats;              /* 统计信息 */
    
    uint32_t last_heartbeat_time;       /* 上次心跳时间 */
    uint32_t last_report_time;          /* 上次上报时间 */
    uint32_t last_reconnect_time;       /* 上次重连尝试时间 */
    uint32_t reconnect_block_until;     /* 重连冷却截止时间 */
    uint8_t reconnect_count;            /* 当前重连计数 */
    uint8_t reconnect_blocked;          /* 1=重连冷却中 */
    
    /* 回调函数 */
    void (*on_data_received)(uint8_t *data, uint16_t len);  /* 数据接收回调 */
} air780e_handle_t;

/**
 * @brief  AIR780E重连状态快照
 */
typedef struct {
    air780e_state_t state;          /* 当前状态 */
    uint8_t reconnect_count;        /* 重连计数 */
    uint8_t reconnect_blocked;      /* 冷却标记 */
    uint32_t cooldown_left_ms;      /* 冷却剩余(ms) */
    uint32_t next_retry_in_ms;      /* 下次自动重连剩余(ms) */
    uint32_t last_reconnect_time_ms;/* 上次重连时间(ms) */
    uint32_t last_rx_time_ms;       /* 最近接收时间(ms) */
} air780e_reconnect_status_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化AIR780E模块（DTU透传模式）
 * @note   模块已通过Luatools配置好，这里只初始化UART
 * @return 0=成功, -1=失败
 */
int8_t air780e_init(void);

/**
 * @brief  发送JSON数据到云端
 * @param  json_str: JSON字符串
 * @return 0=成功, -1=失败
 * 
 * @example
 *   char json[256];
 *   sprintf(json, "{\"cpu_temp\":%.1f,\"water_temp\":%.1f}", cpu_temp, water_temp);
 *   air780e_send_json(json);
 */
int8_t air780e_send_json(const char *json_str);

/**
 * @brief  发送原始数据
 * @param  data: 数据缓冲区
 * @param  len: 数据长度
 * @return 0=成功, -1=失败
 */
int8_t air780e_send_data(uint8_t *data, uint16_t len);

/**
 * @brief  处理接收到的数据（定时调用，建议100ms一次）
 * @note   接收到云端下发的数据后，会调用回调函数
 */
void air780e_process(void);

/**
 * @brief  获取模块状态
 * @return 模块状态
 */
air780e_state_t air780e_get_state(void);

/**
 * @brief  获取统计信息
 * @return 统计信息结构体指针
 */
air780e_stats_t* air780e_get_stats(void);

/**
 * @brief  设置数据接收回调函数
 * @param  callback: 回调函数指针
 * 
 * @example
 *   void on_cloud_data(uint8_t *data, uint16_t len) {
 *       printf("收到云端数据: %.*s\r\n", len, data);
 *       // 解析JSON，执行控制命令
 *   }
 *   air780e_set_data_callback(on_cloud_data);
 */
void air780e_set_data_callback(void (*callback)(uint8_t *data, uint16_t len));

/**
 * @brief  标记链路异常（进入错误态，等待自动重连）
 */
void air780e_mark_error(void);

/**
 * @brief  手动强制重连（解除冷却并立即重连）
 * @return 0=成功, -1=失败
 */
int8_t air780e_force_reconnect(void);

/**
 * @brief  获取重连状态快照
 * @param  status: 输出状态结构体
 * @return 0=成功, -1=失败
 */
int8_t air780e_get_reconnect_status(air780e_reconnect_status_t *status);

/**
 * @brief  检查是否需要上报数据（根据时间间隔）
 * @return 1=需要上报, 0=不需要
 */
uint8_t air780e_should_report(void);

/**
 * @brief  打印统计信息（调试用）
 */
void air780e_print_stats(void);

#endif /* __DRV_AIR780E_H */
