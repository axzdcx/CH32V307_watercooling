/**
 * @file    websocket_client.c
 * @brief   WebSocket客户端 - 实现文件
 * @author  智能水冷项目
 * @date    2026-02-02
 */

#include "websocket_client.h"
#include "ws_protocol.h"
#include "ws_json.h"
#include "drv_air780e.h"
#include <string.h>
#include <stdio.h>

/*============================ 私有数据结构 ============================*/

typedef struct {
    ws_state_t state;           /* 连接状态 */
    ws_config_t config;         /* 配置信息 */
    uint32_t last_ping_time;    /* 上次ping时间 */
    uint32_t last_pong_time;    /* 上次pong时间 */
    uint8_t reconnect_count;    /* 重连计数 */
    uint8_t reconnect_blocked;  /* 是否处于重连冷却期 */
    uint32_t connect_start_time;/* 连接开始时间 */
    uint32_t last_reconnect_time;/* 上次重连尝试时间 */
    uint32_t reconnect_block_until;/* 重连冷却截止时间 */
    uint32_t rx_start_time;     /* 接收开始时间(用于超时检测) */
    
    /* 缓冲区 */
    uint8_t rx_buffer[WS_RX_BUFFER_SIZE];
    uint16_t rx_len;
    
    /* 健康状态 */
    health_status_t health_status;
    
    /* 回调函数 */
    void (*on_connected)(void);
    void (*on_disconnected)(void);
    void (*on_error)(int error_code);
    void (*on_mode_change)(const char *mode);
    void (*on_pid_update)(float kp, float ki, float kd);
    void (*on_reset)(void);
    void (*on_log)(const char *msg);
} ws_client_t;

/*============================ 私有变量 ============================*/

static ws_client_t g_ws_client;

/*============================ 私有函数声明 ============================*/

static void ws_log(const char *msg);
static uint32_t ws_get_tick_ms(void);
static uint32_t ws_calc_reconnect_wait_ms(void);
static void ws_reset_rx_window(void);
static int ws_extract_complete_json(const uint8_t *buffer, uint16_t len,
                                    uint16_t *start_idx, uint16_t *end_idx,
                                    uint8_t *found_json_start);
static void ws_handle_json_message(const char *json_buffer, uint16_t json_len);
static void ws_process_rx_buffer(void);
static void ws_handle_received_data(uint8_t *data, uint16_t len);
static void ws_handle_control_command(control_command_t *cmd);

/*============================ 私有函数实现 ============================*/

/**
 * @brief  日志输出
 */
static void ws_log(const char *msg)
{
    if (g_ws_client.on_log != NULL) {
        g_ws_client.on_log(msg);
    }
}

/**
 * @brief  获取毫秒计数
 */
static uint32_t ws_get_tick_ms(void)
{
    /* 使用BSP层的时钟函数 */
    extern uint32_t get_tick_ms(void);
    return get_tick_ms();
}

/**
 * @brief  计算当前重连等待时间（指数退避 + 抖动）
 * @return 等待时长（ms）
 */
static uint32_t ws_calc_reconnect_wait_ms(void)
{
    uint32_t base_interval;
    uint32_t wait_ms;
    uint32_t jitter_ms;
    uint8_t step;

    base_interval = (g_ws_client.config.reconnect_interval > 0U)
                  ? g_ws_client.config.reconnect_interval
                  : WS_RECONNECT_INTERVAL;

    if (base_interval > WS_RECONNECT_MAX_INTERVAL) {
        base_interval = WS_RECONNECT_MAX_INTERVAL;
    }

    wait_ms = base_interval;
    step = g_ws_client.reconnect_count;

    /* 指数退避：每次失败间隔翻倍，直到最大间隔 */
    while ((step > 0U) && (wait_ms < WS_RECONNECT_MAX_INTERVAL)) {
        if (wait_ms > (WS_RECONNECT_MAX_INTERVAL >> 1)) {
            wait_ms = WS_RECONNECT_MAX_INTERVAL;
        } else {
            wait_ms <<= 1;
        }
        step--;
    }

    /* 固定上限抖动，避免多设备同时重连 */
    jitter_ms = ((uint32_t)g_ws_client.reconnect_count * 137U
              + (g_ws_client.last_reconnect_time & 0xFFU))
              % (WS_RECONNECT_JITTER_MAX + 1U);

    if (wait_ms >= WS_RECONNECT_MAX_INTERVAL || (wait_ms + jitter_ms) > WS_RECONNECT_MAX_INTERVAL) {
        wait_ms = WS_RECONNECT_MAX_INTERVAL;
    } else {
        wait_ms += jitter_ms;
    }

    return wait_ms;
}

/**
 * @brief  清空接收窗口状态
 */
static void ws_reset_rx_window(void)
{
    g_ws_client.rx_len = 0;
    g_ws_client.rx_start_time = 0;
}

/**
 * @brief  在接收窗口中提取一个完整JSON对象范围
 * @param  buffer: 接收窗口数据
 * @param  len: 当前窗口长度
 * @param  start_idx: 输出JSON起始索引
 * @param  end_idx: 输出JSON结束索引（含）
 * @param  found_json_start: 输出是否已发现'{'起始
 * @return 1=找到完整JSON, 0=未找到
 * @note   支持字符串内转义字符，避免误把字符串中的'{'/'}'当结构符号
 */
static int ws_extract_complete_json(const uint8_t *buffer, uint16_t len,
                                    uint16_t *start_idx, uint16_t *end_idx,
                                    uint8_t *found_json_start)
{
    uint16_t i;
    uint16_t start = 0;
    int depth = 0;
    uint8_t started = 0;
    uint8_t in_string = 0;
    uint8_t escaped = 0;

    if (buffer == NULL || len == 0U || start_idx == NULL ||
        end_idx == NULL || found_json_start == NULL) {
        return 0;
    }

    *start_idx = 0U;
    *end_idx = 0U;
    *found_json_start = 0U;

    for (i = 0U; i < len; i++) {
        uint8_t ch = buffer[i];

        if (!started) {
            if (ch == (uint8_t)'{') {
                started = 1U;
                start = i;
                depth = 1;
                in_string = 0U;
                escaped = 0U;
            }
            continue;
        }

        if (in_string) {
            if (escaped) {
                escaped = 0U;
            } else if (ch == (uint8_t)'\\') {
                escaped = 1U;
            } else if (ch == (uint8_t)'"') {
                in_string = 0U;
            }
            continue;
        }

        if (ch == (uint8_t)'"') {
            in_string = 1U;
        } else if (ch == (uint8_t)'{') {
            depth++;
        } else if (ch == (uint8_t)'}') {
            depth--;
            if (depth == 0) {
                *start_idx = start;
                *end_idx = i;
                *found_json_start = 1U;
                return 1;
            }
        }
    }

    if (started) {
        *start_idx = start;
        *found_json_start = 1U;
    }

    return 0;
}

/**
 * @brief  处理单条完整JSON消息
 * @param  json_buffer: JSON字符串（以'\0'结尾）
 * @param  json_len: JSON长度（不含'\0'）
 */
static void ws_handle_json_message(const char *json_buffer, uint16_t json_len)
{
    json_msg_type_t msg_type;

    if (json_buffer == NULL || json_len == 0U) {
        return;
    }

    if (strstr(json_buffer, "control_command") != NULL) {
        printf("[WS] *** 检测到control_command消息! ***\r\n");
    }
    if (strstr(json_buffer, "set_pid") != NULL) {
        printf("[WS] *** 检测到set_pid命令! ***\r\n");
    }

    msg_type = json_parse_type(json_buffer, json_len);
    g_ws_client.last_pong_time = ws_get_tick_ms(); /* 任意完整JSON都视作链路活跃 */

    if (msg_type == JSON_MSG_CONTROL_COMMAND) {
        control_command_t cmd;
        printf("[WS] 检测到控制命令\r\n");
        if (json_parse_control_command(json_buffer, json_len, &cmd) == 0) {
            printf("[WS] 控制命令解析成功\r\n");
            ws_handle_control_command(&cmd);
        } else {
            printf("[WS] 控制命令解析失败\r\n");
            ws_log("JSON解析失败");
            if (g_ws_client.on_error != NULL) {
                g_ws_client.on_error(WS_ERR_JSON_ERROR);
            }
        }
    } else if (msg_type == JSON_MSG_CONNECTION_ACK ||
               msg_type == JSON_MSG_REALTIME_DATA ||
               msg_type == JSON_MSG_HEALTH_STATUS) {
        if (msg_type == JSON_MSG_CONNECTION_ACK) {
            ws_log("收到连接确认");
        }
    } else if (msg_type == JSON_MSG_ERROR) {
        uint8_t was_connected = (g_ws_client.state == WS_STATE_CONNECTED);
        ws_log("收到服务端错误消息");
        if (strstr(json_buffer, "WS_MESSAGE_INVALID") != NULL) {
            ws_log("服务端判定报文无效，进入重连流程");
        }
        g_ws_client.state = WS_STATE_ERROR;
        g_ws_client.last_reconnect_time = ws_get_tick_ms();
        air780e_mark_error();
        if (g_ws_client.on_error != NULL) {
            g_ws_client.on_error(WS_ERR_JSON_ERROR);
        }
        if (was_connected && g_ws_client.on_disconnected != NULL) {
            g_ws_client.on_disconnected();
        }
    } else {
        printf("[WS] 未知消息类型\r\n");
    }
}

/**
 * @brief  处理接收窗口中的分片/粘包数据
 * @note   支持前导噪声、跨回调分片、一次回调多JSON拼接
 */
static void ws_process_rx_buffer(void)
{
    while (g_ws_client.rx_len > 0U) {
        uint16_t start_idx;
        uint16_t end_idx;
        uint8_t found_json_start;

        if (!ws_extract_complete_json(g_ws_client.rx_buffer,
                                      g_ws_client.rx_len,
                                      &start_idx,
                                      &end_idx,
                                      &found_json_start)) {
            if (!found_json_start) {
                ws_log("接收窗口无JSON起始符，丢弃噪声");
                ws_reset_rx_window();
            } else {
                if (start_idx > 0U) {
                    uint16_t remain = g_ws_client.rx_len - start_idx;
                    memmove(g_ws_client.rx_buffer, g_ws_client.rx_buffer + start_idx, remain);
                    g_ws_client.rx_len = remain;
                    if (g_ws_client.rx_len > 0U) {
                        g_ws_client.rx_start_time = ws_get_tick_ms();
                    }
                }

                if (g_ws_client.rx_len >= WS_RX_BUFFER_SIZE) {
                    ws_log("接收窗口已满且JSON未闭合，丢弃当前片段");
                    ws_reset_rx_window();
                    if (g_ws_client.on_error != NULL) {
                        g_ws_client.on_error(WS_ERR_PARSE_ERROR);
                    }
                }
            }
            break;
        }

        if (start_idx > 0U) {
            uint16_t remain = g_ws_client.rx_len - start_idx;
            memmove(g_ws_client.rx_buffer, g_ws_client.rx_buffer + start_idx, remain);
            g_ws_client.rx_len = remain;
            end_idx -= start_idx;
        }

        {
            uint16_t msg_len = end_idx + 1U;
            uint16_t tail_len = g_ws_client.rx_len - msg_len;

            if (msg_len >= WS_JSON_BUFFER_SIZE) {
                ws_log("JSON报文过长，丢弃");
                if (g_ws_client.on_error != NULL) {
                    g_ws_client.on_error(WS_ERR_BUFFER_FULL);
                }
            } else {
                char json_buffer[WS_JSON_BUFFER_SIZE];
                memcpy(json_buffer, g_ws_client.rx_buffer, msg_len);
                json_buffer[msg_len] = '\0';
                ws_handle_json_message(json_buffer, msg_len);
            }

            if (tail_len > 0U) {
                memmove(g_ws_client.rx_buffer, g_ws_client.rx_buffer + msg_len, tail_len);
                g_ws_client.rx_start_time = ws_get_tick_ms();
            } else {
                g_ws_client.rx_start_time = 0U;
            }
            g_ws_client.rx_len = tail_len;
        }
    }
}

/**
 * @brief  处理接收到的数据
 */
static void ws_handle_received_data(uint8_t *data, uint16_t len)
{
    uint16_t input_len = len;

    if (data == NULL || len == 0U) {
        return;
    }

    printf("[WS] 收到数据，长度: %d\r\n", len);

    if (input_len > WS_RX_BUFFER_SIZE) {
        data += (input_len - WS_RX_BUFFER_SIZE);
        input_len = WS_RX_BUFFER_SIZE;
        ws_log("收到超长片段，仅保留尾部窗口");
    }

    /* 接收窗口满时，优先丢弃最旧数据以保留最新分片 */
    if ((uint32_t)g_ws_client.rx_len + input_len > WS_RX_BUFFER_SIZE) {
        uint16_t drop_len = (uint16_t)(g_ws_client.rx_len + input_len - WS_RX_BUFFER_SIZE);
        if (drop_len >= g_ws_client.rx_len) {
            ws_reset_rx_window();
        } else {
            uint16_t remain = g_ws_client.rx_len - drop_len;
            memmove(g_ws_client.rx_buffer, g_ws_client.rx_buffer + drop_len, remain);
            g_ws_client.rx_len = remain;
        }
        ws_log("接收窗口空间不足，已丢弃最旧数据");
        if (g_ws_client.on_error != NULL) {
            g_ws_client.on_error(WS_ERR_BUFFER_FULL);
        }
    }

    if (g_ws_client.rx_len == 0U) {
        g_ws_client.rx_start_time = ws_get_tick_ms();
    }

    memcpy(g_ws_client.rx_buffer + g_ws_client.rx_len, data, input_len);
    g_ws_client.rx_len += input_len;

    printf("[WS] 原始数据: %.*s\r\n", input_len, data);

    ws_process_rx_buffer();
}

/**
 * @brief  处理控制命令
 */
static void ws_handle_control_command(control_command_t *cmd)
{
    if (cmd == NULL) {
        return;
    }
    
    printf("[WS] 处理控制命令, action=%d\r\n", cmd->action);
    
    switch (cmd->action) {
        case CMD_ACTION_SET_MODE:
            ws_log("收到模式切换命令");
            printf("[WS] 模式: %s\r\n", cmd->params.mode);
            if (g_ws_client.on_mode_change != NULL) {
                g_ws_client.on_mode_change(cmd->params.mode);
            }
            break;
            
        case CMD_ACTION_SET_PID:
            ws_log("收到PID参数设置命令");
            printf("[WS] PID参数: kp=%.2f, ki=%.2f, kd=%.2f\r\n", 
                   cmd->params.pid.kp, cmd->params.pid.ki, cmd->params.pid.kd);
            if (g_ws_client.on_pid_update != NULL) {
                printf("[WS] 调用on_pid_update回调...\r\n");
                g_ws_client.on_pid_update(cmd->params.pid.kp, 
                                         cmd->params.pid.ki, 
                                         cmd->params.pid.kd);
            } else {
                printf("[WS] 警告: on_pid_update回调未注册!\r\n");
            }
            break;
            
        case CMD_ACTION_RESET:
            ws_log("收到系统重置命令");
            if (g_ws_client.on_reset != NULL) {
                g_ws_client.on_reset();
            }
            break;
            
        default:
            ws_log("未知命令");
            printf("[WS] 未知命令action=%d\r\n", cmd->action);
            break;
    }
}

/*============================ 公共函数实现 ============================*/

/**
 * @brief  初始化WebSocket客户端
 */
int ws_client_init(const ws_config_t *config)
{
    /* 清空客户端结构 */
    memset(&g_ws_client, 0, sizeof(ws_client_t));
    
    /* 设置配置 */
    if (config != NULL) {
        memcpy(&g_ws_client.config, config, sizeof(ws_config_t));
    }
    
    /* DTU模式下仍走统一连接入口，初始化后先置为断开态 */
    g_ws_client.state = WS_STATE_DISCONNECTED;
    g_ws_client.last_ping_time = ws_get_tick_ms();
    g_ws_client.last_pong_time = ws_get_tick_ms();
    g_ws_client.last_reconnect_time = 0;
    g_ws_client.reconnect_blocked = 0;
    g_ws_client.reconnect_block_until = 0;
    
    /* 初始化AIR780E驱动 */
    air780e_init();
    air780e_set_data_callback(ws_handle_received_data);
    
    ws_log("WebSocket客户端初始化完成(DTU模式)");
    
    return WS_OK;
}

/**
 * @brief  连接到WebSocket服务器
 * @note   DTU模式下,AIR780E已自动连接,此函数仅用于兼容
 */
int ws_client_connect(void)
{
    uint8_t was_connected = (g_ws_client.state == WS_STATE_CONNECTED);
    uint32_t now_ms = ws_get_tick_ms();

    /* AIR780E异常时，先尝试恢复底层链路 */
    if (air780e_get_state() != AIR780E_STATE_READY) {
        ws_log("AIR780E未就绪，尝试重新初始化");
        if (air780e_init() != 0) {
            return WS_ERR_SEND_FAILED;
        }
        air780e_set_data_callback(ws_handle_received_data);
    }

    /* DTU模式下已经连接,直接返回成功 */
    ws_log("DTU模式:已连接");
    g_ws_client.state = WS_STATE_CONNECTED;
    g_ws_client.reconnect_count = 0;
    g_ws_client.reconnect_blocked = 0;
    g_ws_client.reconnect_block_until = 0;
    g_ws_client.last_reconnect_time = now_ms;
    g_ws_client.last_pong_time = now_ms;
    
    if (!was_connected && g_ws_client.on_connected != NULL) {
        g_ws_client.on_connected();
    }
    
    return WS_OK;
}

/**
 * @brief  断开WebSocket连接
 */
void ws_client_disconnect(void)
{
    uint8_t frame[16];
    int len;
    
    if (g_ws_client.state != WS_STATE_CONNECTED) {
        return;
    }
    
    /* 发送关闭帧 */
    len = ws_build_frame(WS_OPCODE_CLOSE, NULL, 0, frame, sizeof(frame));
    if (len > 0) {
        air780e_send_data(frame, len);
    }
    
    g_ws_client.state = WS_STATE_DISCONNECTED;
    g_ws_client.last_reconnect_time = ws_get_tick_ms();
    
    ws_log("断开连接");
    
    if (g_ws_client.on_disconnected != NULL) {
        g_ws_client.on_disconnected();
    }
}

/**
 * @brief  处理WebSocket事件
 */
void ws_client_process(void)
{
    uint32_t current_time;
    
    /* 处理AIR780E接收数据 */
    air780e_process();

    /* 注意：必须在处理收包后再取当前时间，避免与last_pong_time形成时间反转 */
    current_time = ws_get_tick_ms();
    
    /* 检查解析超时 */
    if (g_ws_client.rx_len > 0 && g_ws_client.rx_start_time > 0) {
        if (current_time - g_ws_client.rx_start_time > WS_PARSE_TIMEOUT) {
            ws_log("解析窗口超时，丢弃当前片段");
            ws_reset_rx_window();
        }
    }

    /* 心跳超时判定：长时间无下行数据，判定链路失活 */
    if (g_ws_client.state == WS_STATE_CONNECTED) {
        if ((current_time - g_ws_client.last_pong_time) > WS_PONG_TIMEOUT) {
            ws_log("心跳超时，链路失活，进入重连流程");
            g_ws_client.state = WS_STATE_ERROR;
            g_ws_client.last_reconnect_time = current_time;
            air780e_mark_error();

            if (g_ws_client.on_error != NULL) {
                g_ws_client.on_error(WS_ERR_TIMEOUT);
            }
            if (g_ws_client.on_disconnected != NULL) {
                g_ws_client.on_disconnected();
            }
        }
    }

    /* 链路异常时尝试自动重连（退避重试） */
    if (g_ws_client.state == WS_STATE_ERROR || g_ws_client.state == WS_STATE_DISCONNECTED) {
        uint32_t wait_ms;

        if (g_ws_client.reconnect_blocked) {
            if (current_time >= g_ws_client.reconnect_block_until) {
                g_ws_client.reconnect_blocked = 0;
                g_ws_client.reconnect_count = 0;
                g_ws_client.last_reconnect_time = current_time;
                ws_log("重连冷却结束，恢复自动重连");
            } else {
                return;
            }
        }

        if (g_ws_client.config.max_reconnect_attempts != 0 &&
            g_ws_client.reconnect_count >= g_ws_client.config.max_reconnect_attempts) {
            g_ws_client.reconnect_blocked = 1;
            g_ws_client.reconnect_block_until = current_time + WS_RECONNECT_COOLDOWN_MS;
            ws_log("达到最大重连次数，进入冷却期");
            return;
        }

        wait_ms = ws_calc_reconnect_wait_ms();
        if ((current_time - g_ws_client.last_reconnect_time) >= wait_ms) {
            char retry_log[64];
            g_ws_client.last_reconnect_time = current_time;
            g_ws_client.reconnect_count++;
            snprintf(retry_log, sizeof(retry_log), "开始自动重连 #%u", g_ws_client.reconnect_count);
            ws_log(retry_log);

            if (ws_client_connect() == WS_OK) {
                ws_log("自动重连成功");
                g_ws_client.reconnect_count = 0;
            } else if (g_ws_client.on_error != NULL) {
                g_ws_client.on_error(WS_ERR_TIMEOUT);
            }
        }
    }
}

/**
 * @brief  发送实时数据
 */
int ws_send_realtime_data(float cpu_temp, float water_temp, float flow_rate,
                          uint16_t pump_speed, uint16_t fan_speed, float power)
{
    realtime_data_t data;
    char json[WS_JSON_BUFFER_SIZE];
    int json_len;
    
    if (g_ws_client.state != WS_STATE_CONNECTED) {
        return WS_ERR_NOT_CONNECTED;
    }
    
    /* 填充数据 */
    data.cpu_temp = cpu_temp;
    data.water_temp = water_temp;
    data.flow_rate = flow_rate;
    data.pump_speed = pump_speed;
    data.fan_speed = fan_speed;
    data.power = power;
    data.tds_ppm = 0U;
    strcpy(data.tds_level, "unknown");
    data.tinyml_ready = 0U;
    data.tinyml_anomaly = 0U;
    data.tinyml_score = 0.0f;
    strcpy(data.tinyml_backend, "unknown");
    data.tinyml_model_ready = 0U;
    data.tinyml_fallback = 0U;
    
    /* 构造JSON */
    json_len = json_build_realtime_data(&data, json, sizeof(json));
    if (json_len < 0) {
        return WS_ERR_JSON_ERROR;
    }
    
    /* DTU模式：直接发送纯JSON，AIR780E会自动封装成WebSocket帧 */
    printf("[WS] 发送JSON: %s\r\n", json);
    
    if (air780e_send_data((uint8_t*)json, json_len) != 0) {
        return WS_ERR_SEND_FAILED;
    }
    
    ws_log("发送实时数据");
    
    return WS_OK;
}

/**
 * @brief  设置TinyML状态（用于健康上报附带AI信息）
 */
int ws_set_tinyml_status(uint8_t ready,
                         uint8_t anomaly,
                         float score,
                         const char *backend,
                         uint8_t model_ready,
                         uint8_t fallback)
{
    if (backend == NULL) {
        return WS_ERR_INVALID_PARAM;
    }

    if (score != score) {
        score = 0.0f;
    }
    if (score < 0.0f) {
        score = 0.0f;
    } else if (score > 1.0f) {
        score = 1.0f;
    }

    g_ws_client.health_status.tinyml_ready = ready ? 1U : 0U;
    g_ws_client.health_status.tinyml_anomaly = anomaly ? 1U : 0U;
    g_ws_client.health_status.tinyml_score = score;
    strncpy(g_ws_client.health_status.tinyml_backend, backend, sizeof(g_ws_client.health_status.tinyml_backend) - 1U);
    g_ws_client.health_status.tinyml_backend[sizeof(g_ws_client.health_status.tinyml_backend) - 1U] = '\0';
    g_ws_client.health_status.tinyml_model_ready = model_ready ? 1U : 0U;
    g_ws_client.health_status.tinyml_fallback = fallback ? 1U : 0U;

    return WS_OK;
}

/**
 * @brief  发送健康状态
 */
int ws_send_health_status(uint8_t health_score,
                          const char *pump_status,
                          const char *fan_status,
                          const char *sensor_status,
                          const char *cooling_status)
{
    char json[WS_JSON_BUFFER_SIZE];
    int json_len;

    if (pump_status == NULL || fan_status == NULL ||
        sensor_status == NULL || cooling_status == NULL) {
        return WS_ERR_INVALID_PARAM;
    }
    
    if (g_ws_client.state != WS_STATE_CONNECTED) {
        return WS_ERR_NOT_CONNECTED;
    }
    
    /* 更新健康状态 */
    g_ws_client.health_status.health_score = health_score;
    strncpy(g_ws_client.health_status.pump_status, pump_status, sizeof(g_ws_client.health_status.pump_status) - 1);
    g_ws_client.health_status.pump_status[sizeof(g_ws_client.health_status.pump_status) - 1] = '\0';
    strncpy(g_ws_client.health_status.fan_status, fan_status, sizeof(g_ws_client.health_status.fan_status) - 1);
    g_ws_client.health_status.fan_status[sizeof(g_ws_client.health_status.fan_status) - 1] = '\0';
    strncpy(g_ws_client.health_status.sensor_status, sensor_status, sizeof(g_ws_client.health_status.sensor_status) - 1);
    g_ws_client.health_status.sensor_status[sizeof(g_ws_client.health_status.sensor_status) - 1] = '\0';
    strncpy(g_ws_client.health_status.cooling_status, cooling_status, sizeof(g_ws_client.health_status.cooling_status) - 1);
    g_ws_client.health_status.cooling_status[sizeof(g_ws_client.health_status.cooling_status) - 1] = '\0';
    
    /* 构造JSON */
    json_len = json_build_health_status(&g_ws_client.health_status, json, sizeof(json));
    if (json_len < 0) {
        return WS_ERR_JSON_ERROR;
    }
    
    /* DTU模式：直接发送纯JSON，AIR780E会自动封装成WebSocket帧 */
    printf("[WS] 发送JSON: %s\r\n", json);
    
    if (air780e_send_data((uint8_t*)json, json_len) != 0) {
        return WS_ERR_SEND_FAILED;
    }
    
    ws_log("发送健康状态");
    
    return WS_OK;
}

/**
 * @brief  添加故障记录
 */
int ws_add_fault(const char *code, const char *description, const char *severity)
{
    if (code == NULL || description == NULL || severity == NULL) {
        return WS_ERR_INVALID_PARAM;
    }

    if (g_ws_client.health_status.fault_count >= WS_JSON_MAX_FAULTS) {
        return WS_ERR_BUFFER_FULL;
    }
    
    int idx = g_ws_client.health_status.fault_count;
    
    strncpy(g_ws_client.health_status.faults[idx].code, code, sizeof(g_ws_client.health_status.faults[idx].code) - 1);
    strncpy(g_ws_client.health_status.faults[idx].description, description, sizeof(g_ws_client.health_status.faults[idx].description) - 1);
    strncpy(g_ws_client.health_status.faults[idx].severity, severity, sizeof(g_ws_client.health_status.faults[idx].severity) - 1);
    g_ws_client.health_status.faults[idx].timestamp = json_get_timestamp();
    
    g_ws_client.health_status.fault_count++;
    
    return WS_OK;
}

/**
 * @brief  手动强制重连（解除自动重连冷却）
 */
int ws_client_force_reconnect(void)
{
    g_ws_client.reconnect_count = 0;
    g_ws_client.reconnect_blocked = 0;
    g_ws_client.reconnect_block_until = 0;
    g_ws_client.last_reconnect_time = 0;
    (void)air780e_force_reconnect();
    ws_log("手动强制重连：已解除冷却限制");
    return ws_client_connect();
}

/**
 * @brief  清空故障记录
 */
void ws_clear_faults(void)
{
    g_ws_client.health_status.fault_count = 0;
    memset(g_ws_client.health_status.faults, 0, sizeof(g_ws_client.health_status.faults));
}

/**
 * @brief  获取连接状态
 */
ws_state_t ws_get_state(void)
{
    return g_ws_client.state;
}

/**
 * @brief  获取重连状态快照
 */
int ws_get_reconnect_status(ws_reconnect_status_t *status)
{
    uint32_t current_time;

    if (status == NULL) {
        return WS_ERR_INVALID_PARAM;
    }

    memset(status, 0, sizeof(ws_reconnect_status_t));
    current_time = ws_get_tick_ms();

    status->state = g_ws_client.state;
    status->reconnect_count = g_ws_client.reconnect_count;
    status->reconnect_blocked = g_ws_client.reconnect_blocked;
    status->last_reconnect_time_ms = g_ws_client.last_reconnect_time;

    if (g_ws_client.reconnect_blocked) {
        if (current_time < g_ws_client.reconnect_block_until) {
            status->cooldown_left_ms = g_ws_client.reconnect_block_until - current_time;
        } else {
            status->cooldown_left_ms = 0;
        }
    } else if (g_ws_client.state == WS_STATE_ERROR || g_ws_client.state == WS_STATE_DISCONNECTED) {
        uint32_t wait_ms = ws_calc_reconnect_wait_ms();
        uint32_t elapsed = current_time - g_ws_client.last_reconnect_time;
        status->next_retry_in_ms = (elapsed >= wait_ms) ? 0 : (wait_ms - elapsed);
    }

    return WS_OK;
}

/**
 * @brief  注册连接回调
 */
void ws_set_connected_callback(void (*callback)(void))
{
    g_ws_client.on_connected = callback;
}

/**
 * @brief  注册断开回调
 */
void ws_set_disconnected_callback(void (*callback)(void))
{
    g_ws_client.on_disconnected = callback;
}

/**
 * @brief  注册错误回调
 */
void ws_set_error_callback(void (*callback)(int error_code))
{
    g_ws_client.on_error = callback;
}

/**
 * @brief  注册模式切换回调
 */
void ws_set_mode_callback(void (*callback)(const char *mode))
{
    g_ws_client.on_mode_change = callback;
}

/**
 * @brief  注册PID参数回调
 */
void ws_set_pid_callback(void (*callback)(float kp, float ki, float kd))
{
    g_ws_client.on_pid_update = callback;
}

/**
 * @brief  注册重置回调
 */
void ws_set_reset_callback(void (*callback)(void))
{
    g_ws_client.on_reset = callback;
}

/**
 * @brief  设置日志回调函数
 */
void ws_set_log_callback(void (*callback)(const char *msg))
{
    g_ws_client.on_log = callback;
}

/**
 * @brief  更新配置参数
 */
int ws_update_config(const ws_config_t *config)
{
    if (config == NULL) {
        return WS_ERR_INVALID_PARAM;
    }
    
    /* 只有在断开状态才能更新配置 */
    if (g_ws_client.state != WS_STATE_DISCONNECTED) {
        ws_log("需要先断开连接才能更新配置");
        return WS_ERR_NOT_CONNECTED;
    }
    
    memcpy(&g_ws_client.config, config, sizeof(ws_config_t));
    ws_log("配置已更新");
    
    return WS_OK;
}

/**
 * @brief  获取当前配置
 */
int ws_get_config(ws_config_t *config)
{
    if (config == NULL) {
        return WS_ERR_INVALID_PARAM;
    }
    
    memcpy(config, &g_ws_client.config, sizeof(ws_config_t));
    
    return WS_OK;
}

/**
 * @brief  清空接收缓冲区
 */
void ws_clear_rx_buffer(void)
{
    g_ws_client.rx_len = 0;
    g_ws_client.rx_start_time = 0;
    memset(g_ws_client.rx_buffer, 0, sizeof(g_ws_client.rx_buffer));
    ws_log("接收缓冲区已清空");
}

/**
 * @brief  内部日志函数(供WS_LOG宏使用)
 */
void ws_log_internal(const char *msg)
{
    ws_log(msg);
}
