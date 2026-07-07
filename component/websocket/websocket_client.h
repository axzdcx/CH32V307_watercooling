/**
 * @file    websocket_client.h
 * @brief   WebSocket客户端 - 头文件
 * @author  智能水冷项目
 * @date    2026-02-02
 * 
 * 功能说明:
 *   - WebSocket客户端主接口
 *   - 连接管理、消息收发、回调分发
 *   - 与AIR780E驱动集成
 */

#ifndef __WEBSOCKET_CLIENT_H
#define __WEBSOCKET_CLIENT_H

#include <stdint.h>
#include "ws_json.h"

/*============================ 配置参数 ============================*/

#define WS_RX_BUFFER_SIZE       1024    /* 接收缓冲区大小 */
#define WS_TX_BUFFER_SIZE       512     /* 发送缓冲区大小 */
#define WS_JSON_BUFFER_SIZE     1024    /* JSON缓冲区大小 */

#define WS_HANDSHAKE_TIMEOUT    10000   /* 握手超时(ms) */
#define WS_PING_INTERVAL        30000   /* 心跳间隔(ms) */
#define WS_PONG_TIMEOUT         60000   /* 心跳超时(ms) */
#define WS_PARSE_TIMEOUT        5000    /* 解析超时(ms) */

#define WS_RECONNECT_INTERVAL   5000    /* 重连间隔(ms) */
#define WS_MAX_RECONNECT_ATTEMPTS 5     /* 最大重连次数 */
#define WS_RECONNECT_MAX_INTERVAL 30000 /* 重连最大退避间隔(ms) */
#define WS_RECONNECT_JITTER_MAX  500    /* 重连随机抖动上限(ms) */
#define WS_RECONNECT_COOLDOWN_MS 120000 /* 达到最大重连后冷却时长(ms) */

/*============================ 调试宏 ============================*/

#ifdef WS_DEBUG
#define WS_LOG(msg) do { \
    extern void ws_log_internal(const char *msg); \
    ws_log_internal(msg); \
} while(0)
#else
#define WS_LOG(msg) ((void)0)
#endif

/*============================ WebSocket连接状态 ============================*/

typedef enum {
    WS_STATE_DISCONNECTED = 0,  /* 未连接 */
    WS_STATE_CONNECTING,        /* 连接中 */
    WS_STATE_CONNECTED,         /* 已连接 */
    WS_STATE_CLOSING,           /* 关闭中 */
    WS_STATE_ERROR              /* 错误状态 */
} ws_state_t;

/*============================ WebSocket配置 ============================*/

typedef struct {
    char server_host[64];       /* 服务器地址 */
    uint16_t server_port;       /* 服务器端口 */
    char path[32];              /* WebSocket路径 */
    uint16_t reconnect_interval;/* 重连间隔(ms) */
    uint8_t max_reconnect_attempts; /* 最大重连次数 */
} ws_config_t;

/*============================ 重连状态 ============================*/

typedef struct {
    ws_state_t state;               /* 当前连接状态 */
    uint8_t reconnect_count;        /* 当前重连计数 */
    uint8_t reconnect_blocked;      /* 1=处于冷却期 */
    uint32_t cooldown_left_ms;      /* 冷却剩余时间(ms) */
    uint32_t next_retry_in_ms;      /* 距离下次自动重连剩余时间(ms) */
    uint32_t last_reconnect_time_ms;/* 上次重连尝试时间(ms) */
} ws_reconnect_status_t;

/*============================ 错误码 ============================*/

typedef enum {
    WS_OK = 0,                      /* 成功 */
    WS_ERR_INVALID_PARAM = -1,      /* 无效参数 */
    WS_ERR_NOT_CONNECTED = -2,      /* 未连接 */
    WS_ERR_BUFFER_FULL = -3,        /* 缓冲区满 */
    WS_ERR_PARSE_ERROR = -4,        /* 解析错误 */
    WS_ERR_SEND_FAILED = -5,        /* 发送失败 */
    WS_ERR_TIMEOUT = -6,            /* 超时 */
    WS_ERR_HANDSHAKE_FAILED = -7,   /* 握手失败 */
    WS_ERR_FRAME_ERROR = -8,        /* 帧错误 */
    WS_ERR_JSON_ERROR = -9,         /* JSON错误 */
    WS_ERR_UNKNOWN = -10            /* 未知错误 */
} ws_error_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化WebSocket客户端
 * @param  config: 配置参数（可为NULL使用默认配置）
 * @return 0=成功, <0=错误码
 * 
 * @example
 *   ws_config_t config = {
 *       .server_host = "192.168.1.100",
 *       .server_port = 8080,
 *       .path = "/",
 *       .reconnect_interval = 5000,
 *       .max_reconnect_attempts = 5
 *   };
 *   ws_client_init(&config);
 */
int ws_client_init(const ws_config_t *config);

/**
 * @brief  连接到WebSocket服务器
 * @return 0=成功, <0=错误码
 * 
 * @note   此函数会发送握手请求，但不会等待响应
 *         需要在ws_client_process()中处理握手响应
 */
int ws_client_connect(void);

/**
 * @brief  手动强制重连（解除自动重连冷却）
 * @return 0=成功, <0=错误码
 */
int ws_client_force_reconnect(void);

/**
 * @brief  获取重连状态快照
 * @param  status: 输出状态结构体
 * @return 0=成功, <0=错误码
 */
int ws_get_reconnect_status(ws_reconnect_status_t *status);

/**
 * @brief  断开WebSocket连接
 */
void ws_client_disconnect(void);

/**
 * @brief  处理WebSocket事件（定时调用）
 * @note   建议100ms调用一次
 *         此函数会处理接收数据、心跳、重连等
 */
void ws_client_process(void);

/**
 * @brief  发送实时数据
 * @param  cpu_temp: CPU温度
 * @param  water_temp: 水温
 * @param  flow_rate: 流速
 * @param  pump_speed: 水泵转速
 * @param  fan_speed: 风扇转速
 * @param  power: 功率
 * @return 0=成功, <0=错误码
 * 
 * @example
 *   ws_send_realtime_data(65.5, 35.2, 120.5, 2500, 1800, 25.3);
 */
int ws_send_realtime_data(float cpu_temp, float water_temp, float flow_rate,
                          uint16_t pump_speed, uint16_t fan_speed, float power);

/**
 * @brief  发送健康状态
 * @param  health_score: 健康分数
 * @param  pump_status: 水泵状态
 * @param  fan_status: 风扇状态
 * @param  sensor_status: 传感器状态
 * @param  cooling_status: 散热状态
 * @return 0=成功, <0=错误码
 * 
 * @example
 *   ws_send_health_status(85, "normal", "normal", "normal", "normal");
 */
int ws_send_health_status(uint8_t health_score,
                          const char *pump_status,
                          const char *fan_status,
                          const char *sensor_status,
                          const char *cooling_status);

/**
 * @brief  设置TinyML状态（用于健康上报附带AI信息）
 * @param  ready: TinyML预热状态
 * @param  anomaly: TinyML异常状态
 * @param  score: TinyML异常分数(0~1)
 * @param  backend: TinyML后端字符串
 * @param  model_ready: TinyML模型就绪状态
 * @param  fallback: TinyML是否已降级
 * @return 0=成功, <0=错误码
 */
int ws_set_tinyml_status(uint8_t ready,
                         uint8_t anomaly,
                         float score,
                         const char *backend,
                         uint8_t model_ready,
                         uint8_t fallback);

/**
 * @brief  添加故障记录到健康状态
 * @param  code: 故障代码
 * @param  description: 故障描述
 * @param  severity: 严重程度
 * @return 0=成功, <0=错误码
 * 
 * @example
 *   ws_add_fault("TEMP_HIGH", "CPU温度偏高", "medium");
 */
int ws_add_fault(const char *code, const char *description, const char *severity);

/**
 * @brief  清空健康状态中的故障列表
 * @note   每次构建新的健康上报前调用，避免旧故障残留
 */
void ws_clear_faults(void);

/**
 * @brief  获取连接状态
 * @return 连接状态
 */
ws_state_t ws_get_state(void);

/**
 * @brief  注册连接回调
 * @param  callback: 回调函数
 */
void ws_set_connected_callback(void (*callback)(void));

/**
 * @brief  注册断开回调
 * @param  callback: 回调函数
 */
void ws_set_disconnected_callback(void (*callback)(void));

/**
 * @brief  注册错误回调
 * @param  callback: 回调函数
 */
void ws_set_error_callback(void (*callback)(int error_code));

/**
 * @brief  注册模式切换回调
 * @param  callback: 回调函数
 * 
 * @example
 *   void on_mode_change(const char *mode) {
 *       printf("模式切换: %s\n", mode);
 *       // 执行模式切换逻辑
 *   }
 *   ws_set_mode_callback(on_mode_change);
 */
void ws_set_mode_callback(void (*callback)(const char *mode));

/**
 * @brief  注册PID参数回调
 * @param  callback: 回调函数
 * 
 * @example
 *   void on_pid_update(float kp, float ki, float kd) {
 *       printf("PID参数: kp=%.2f, ki=%.2f, kd=%.2f\n", kp, ki, kd);
 *       // 更新PID控制器参数
 *   }
 *   ws_set_pid_callback(on_pid_update);
 */
void ws_set_pid_callback(void (*callback)(float kp, float ki, float kd));

/**
 * @brief  注册重置回调
 * @param  callback: 回调函数
 * 
 * @example
 *   void on_reset(void) {
 *       printf("系统重置\n");
 *       // 执行系统重置逻辑
 *   }
 *   ws_set_reset_callback(on_reset);
 */
void ws_set_reset_callback(void (*callback)(void));

/**
 * @brief  设置日志回调函数
 * @param  callback: 日志回调函数
 * 
 * @example
 *   void log_callback(const char *msg) {
 *       printf("[WS] %s\r\n", msg);
 *   }
 *   ws_set_log_callback(log_callback);
 */
void ws_set_log_callback(void (*callback)(const char *msg));

/**
 * @brief  更新配置参数
 * @param  config: 新的配置参数
 * @return 0=成功, <0=错误码
 * @note   需要断开连接后才能更新配置
 */
int ws_update_config(const ws_config_t *config);

/**
 * @brief  获取当前配置
 * @param  config: 输出配置参数
 * @return 0=成功, <0=错误码
 */
int ws_get_config(ws_config_t *config);

/**
 * @brief  清空接收缓冲区
 * @note   用于错误恢复
 */
void ws_clear_rx_buffer(void);

#endif /* __WEBSOCKET_CLIENT_H */
