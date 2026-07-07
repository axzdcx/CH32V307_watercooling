/**
 * @file    app_websocket.c
 * @brief   WebSocket云端通信任务实现
 * @author  智能水冷项目
 * @date    2026-02-02
 */

#include "app_websocket.h"
#include "websocket_client.h"
#include "ws_json.h"    /* JSON构建函数 */
#include "drv_air780e.h"
#include "app_temp.h"
#include "app_flow.h"
#include "app_pwm.h"
#include "app_tds.h"    /* TDS水质传感器 */
#include "algo_health.h"
#include "algo_diagnosis.h"
#include "algo_alarm_code.h"
#include "bsp_tim.h"
#include "scheduler.h"
#include "debug.h"
#include <string.h>

/*============================ 私有变量 ============================*/

static uint8_t g_ws_connected = 0;              /* WebSocket连接状态 */
static uint32_t g_last_realtime_time = 0;      /* 上次实时数据上报时间 */
static uint32_t g_last_health_time = 0;        /* 上次健康状态上报时间 */
static uint32_t g_last_tx_time = 0;            /* 上次发送时间（用于发送节流） */
static char g_current_mode[16] = "auto";       /* 当前工作模式 */
static uint8_t g_tx_fail_streak = 0;           /* 连续发送失败计数 */

/*============================ 私有工具函数 ============================*/

/**
 * @brief  限幅并兜底浮点数
 * @param  value: 输入值
 * @param  min: 最小值
 * @param  max: 最大值
 * @param  fallback: 异常值兜底
 * @return 合法值
 */
static float ws_sanitize_float(float value, float min, float max, float fallback)
{
    if (value != value) {
        return fallback;
    }
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief  限幅无符号8位整数
 * @param  value: 输入值
 * @param  min: 最小值
 * @param  max: 最大值
 * @return 合法值
 */
static uint8_t ws_clamp_u8(uint8_t value, uint8_t min, uint8_t max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief  TinyML后端枚举转字符串
 */
static const char* ws_tinyml_backend_to_str(uint8_t backend)
{
    switch (backend) {
        case 0: return "heuristic";
        case 1: return "tinymaix_stub";
        case 2: return "tinymaix_real";
        default: return "unknown";
    }
}

/**
 * @brief  判断是否满足最小发送间隔
 * @param  now_ms: 当前时间戳(ms)
 * @return 1=可发送, 0=需继续等待
 */
static uint8_t ws_tx_gap_ready(uint32_t now_ms)
{
    return (uint8_t)((now_ms - g_last_tx_time) >= WS_TX_MIN_GAP_MS);
}

/**
 * @brief  标记发送成功，清零失败计数
 */
static void ws_mark_send_success(void)
{
    g_tx_fail_streak = 0;
}

/**
 * @brief  标记发送失败，必要时触发重连
 * @param  tag: 日志标签
 * @param  err: 错误码
 */
static void ws_mark_send_failure(const char *tag, int err)
{
    if (g_tx_fail_streak < 255) {
        g_tx_fail_streak++;
    }

    printf("[%s] 发送失败: %d\r\n", tag, err);

    if (g_tx_fail_streak >= WS_TX_FAIL_RECONNECT_THRESHOLD) {
        printf("[WebSocket] 连续发送失败达到阈值，触发重连\r\n");
        air780e_mark_error();
        ws_client_disconnect();
        g_ws_connected = 0;
        g_tx_fail_streak = 0;
    }
}

/*============================ WebSocket回调函数 ============================*/

/**
 * @brief  WebSocket连接成功回调
 */
static void on_ws_connected(void)
{
    printf("[WebSocket] 连接成功\r\n");
    g_ws_connected = 1;
}

/**
 * @brief  WebSocket断开连接回调
 */
static void on_ws_disconnected(void)
{
    printf("[WebSocket] 连接断开\r\n");
    g_ws_connected = 0;
}

/**
 * @brief  WebSocket错误回调
 */
static void on_ws_error(int error_code)
{
    ws_reconnect_status_t status;
    if (ws_get_reconnect_status(&status) == 0) {
        printf("[WebSocket] 错误: %d (state=%d, retry=%u, blocked=%u)\r\n",
               error_code, status.state, status.reconnect_count, status.reconnect_blocked);
    } else {
        printf("[WebSocket] 错误: %d\r\n", error_code);
    }
}

/**
 * @brief  模式切换回调
 */
static void on_mode_change(const char *mode)
{
    printf("[WebSocket] 模式切换: %s\r\n", mode);
    
    /* 保存当前模式 */
    strncpy(g_current_mode, mode, sizeof(g_current_mode) - 1);
    g_current_mode[sizeof(g_current_mode) - 1] = '\0';
    
    /* 根据模式调整控制策略 */
    if (strcmp(mode, "auto") == 0) {
        printf("[模式] 切换到自动模式\r\n");
        /* 启动PID自动控制 */
    }
    else if (strcmp(mode, "manual") == 0) {
        printf("[模式] 切换到手动模式\r\n");
        /* 停止自动控制 */
    }
    else if (strcmp(mode, "silent") == 0) {
        printf("[模式] 切换到静音模式\r\n");
        /* 降低风扇转速 */
        extern void fan_set_duty(uint8_t duty);
        fan_set_duty(30);  /* 设置风扇转速为30% */
    }
}

/**
 * @brief  PID参数更新回调
 */
static void on_pid_update(float kp, float ki, float kd)
{
    printf("[WebSocket] PID参数更新: kp=%.2f, ki=%.2f, kd=%.2f\r\n", kp, ki, kd);
    
    /* 更新PID控制器参数 */
    extern void pid_tune_online(float kp, float ki, float kd);
    pid_tune_online(kp, ki, kd);
}

/**
 * @brief  系统重置回调
 */
static void on_reset(void)
{
    printf("[WebSocket] 系统重置\r\n");
    
    /* 恢复默认设置 */
    strcpy(g_current_mode, "auto");
    
    /* 重置PID参数 */
    extern void pid_tune_online(float kp, float ki, float kd);
    pid_tune_online(5.0f, 1.0f, 0.5f);
}

/**
 * @brief  日志回调
 */
static void on_ws_log(const char *msg)
{
    if (scheduler_is_verbose_log_enabled()) {
        printf("[WS] %s\r\n", msg);
    }
}

/*============================ 公共函数实现 ============================*/

/**
 * @brief  WebSocket任务初始化
 */
int8_t websocket_init(void)
{
    int ret;
    
    printf("[WebSocket] 初始化...\r\n");
    
    /* 配置WebSocket客户端 */
    ws_config_t config = {
        .server_host = WS_SERVER_HOST,
        .server_port = WS_SERVER_PORT,
        .path = WS_SERVER_PATH,
        .reconnect_interval = 5000,
        .max_reconnect_attempts = 5
    };
    
    /* 初始化WebSocket */
    ret = ws_client_init(&config);
    if (ret != 0) {
        printf("[WebSocket] 初始化失败: %d\r\n", ret);
        return -1;
    }
    
    /* 注册回调函数 */
    ws_set_connected_callback(on_ws_connected);
    ws_set_disconnected_callback(on_ws_disconnected);
    ws_set_error_callback(on_ws_error);
    ws_set_mode_callback(on_mode_change);
    ws_set_pid_callback(on_pid_update);
    ws_set_reset_callback(on_reset);
    ws_set_log_callback(on_ws_log);
    
    /* 连接到服务器(DTU模式下自动连接) */
    printf("[WebSocket] 服务器: %s:%d\r\n", WS_SERVER_HOST, WS_SERVER_PORT);
    ret = ws_client_connect();
    if (ret != 0) {
        printf("[WebSocket] 连接失败: %d\r\n", ret);
        return -1;
    }
    
    /* 初始化时间戳 */
    g_last_realtime_time = get_tick_ms();
    g_last_health_time = get_tick_ms();
    g_last_tx_time = get_tick_ms();
    g_tx_fail_streak = 0;
    
    printf("[WebSocket] 初始化完成\r\n");
    
    return 0;
}

/**
 * @brief  WebSocket任务处理
 */
void task_websocket(void)
{
    uint32_t current_time = get_tick_ms();
    
    /* 处理WebSocket事件 */
    ws_client_process();

    if (!g_ws_connected) {
        return;
    }

    if (!ws_tx_gap_ready(current_time)) {
        return;
    }

    /* 说明：同一周期最多发一包，避免连续发包被DTU拼接 */
    if (current_time - g_last_realtime_time >= WS_REALTIME_INTERVAL) {
        g_last_realtime_time = current_time;
        g_last_tx_time = current_time;
        websocket_send_realtime();
    } else if (current_time - g_last_health_time >= WS_HEALTH_INTERVAL) {
        g_last_health_time = current_time;
        g_last_tx_time = current_time;
        websocket_send_health();
    }
}

/**
 * @brief  获取WebSocket连接状态
 */
uint8_t websocket_is_connected(void)
{
    return g_ws_connected;
}

/**
 * @brief  手动发送实时数据
 */
void websocket_send_realtime(void)
{
    int ret;
    g_last_tx_time = get_tick_ms();

    /* 获取传感器数据 */
    const temp_data_t *temp = temp_get_data();
    const flow_data_t *flow = flow_get_data();
    const tds_data_t *tds = app_tds_get_data();
    scheduler_tinyml_status_t tinyml_status;
    float cpu_temp;
    float water_temp;
    float flow_rate;
    uint16_t tds_ppm;
    const char *tds_level_str;
    const char *tinyml_backend;

    /* 获取PWM占空比 */
    extern uint8_t pwm_get_pump(void);
    extern uint8_t pwm_get_fan(void);
    uint8_t pump_duty = ws_clamp_u8(pwm_get_pump(), 0, 100);
    uint8_t fan_duty = ws_clamp_u8(pwm_get_fan(), 0, 100);

    if (temp == NULL || flow == NULL || tds == NULL) {
        printf("[数据上报] 采集数据未就绪\r\n");
        return;
    }

    cpu_temp = ws_sanitize_float(temp->cpu_temp, -20.0f, 120.0f, 25.0f);
    water_temp = ws_sanitize_float(temp->water_temp, -20.0f, 120.0f, 25.0f);
    flow_rate = ws_sanitize_float(flow->flow_rate, 0.0f, 40.0f, 0.0f);
    tds_ppm = tds->tds_compensated > 2000 ? 2000 : tds->tds_compensated;
    tds_level_str = app_tds_get_level_str();
    if (scheduler_tinyml_get_status(&tinyml_status) != 0) {
        memset(&tinyml_status, 0, sizeof(tinyml_status));
    }
    tinyml_backend = ws_tinyml_backend_to_str(tinyml_status.backend);

    /* 计算转速（假设100%占空比对应3000RPM） */
    uint16_t pump_speed = (uint16_t)(pump_duty * 30);
    uint16_t fan_speed = (uint16_t)(fan_duty * 30);

    /* 计算功率（简单估算） */
    float power = ws_sanitize_float((pump_duty + fan_duty) * 0.25f, 0.0f, 60.0f, 0.0f);  /* 假设满载50W */

    /* 填充实时数据结构（包含TDS） */
    realtime_data_t data;
    data.cpu_temp = cpu_temp;
    data.water_temp = water_temp;
    data.flow_rate = flow_rate;
    data.pump_speed = pump_speed;
    data.fan_speed = fan_speed;
    data.power = power;
    data.tds_ppm = tds_ppm;
    strncpy(data.tds_level, tds_level_str, sizeof(data.tds_level) - 1);
    data.tds_level[sizeof(data.tds_level) - 1] = '\0';
    data.tinyml_ready = tinyml_status.ready ? 1U : 0U;
    data.tinyml_anomaly = tinyml_status.anomaly_active ? 1U : 0U;
    data.tinyml_score = ws_sanitize_float(tinyml_status.score, 0.0f, 1.0f, 0.0f);
    strncpy(data.tinyml_backend, tinyml_backend, sizeof(data.tinyml_backend) - 1);
    data.tinyml_backend[sizeof(data.tinyml_backend) - 1] = '\0';
    data.tinyml_model_ready = tinyml_status.model_ready ? 1U : 0U;
    data.tinyml_fallback = tinyml_status.backend_fallback ? 1U : 0U;

    /* 构造JSON并发送 */
    char json[WS_JSON_BUFFER_SIZE];
    int json_len = json_build_realtime_data(&data, json, sizeof(json));
    if (json_len < 0) {
        printf("[数据上报] JSON构建失败\r\n");
        return;
    }

    /* 发送数据 */
    ret = air780e_send_data((uint8_t*)json, json_len);

    if (ret == 0) {
        ws_mark_send_success();
        if (scheduler_is_verbose_log_enabled()) {
            printf("[数据上报] CPU=%.1f°C, 水温=%.1f°C, 流速=%.1fL/min, TDS=%dppm(%s)\r\n",
                   cpu_temp, water_temp, flow_rate, tds_ppm, tds_level_str);
        }
    } else {
        ws_mark_send_failure("数据上报", ret);
    }
}

/**
 * @brief  手动强制重连WebSocket服务器
 */
void websocket_connect(void)
{
    int ret = ws_client_force_reconnect();
    if (ret == 0) {
        printf("[WebSocket] 开始手动强制重连...\r\n");
    } else {
        printf("[WebSocket] 连接失败: %d\r\n", ret);
    }
}

/**
 * @brief  设置服务器地址
 */
void websocket_set_server(const char *host, uint16_t port)
{
    ws_config_t config;

    if (host == NULL) {
        printf("[WebSocket] 参数无效: host为空\r\n");
        return;
    }
    
    /* 获取当前配置 */
    if (ws_get_config(&config) != 0) {
        printf("[WebSocket] 获取配置失败\r\n");
        return;
    }
    
    /* 更新服务器地址 */
    strncpy(config.server_host, host, sizeof(config.server_host) - 1);
    config.server_host[sizeof(config.server_host) - 1] = '\0';
    config.server_port = port;
    
    /* 应用新配置 */
    if (ws_update_config(&config) == 0) {
        printf("[WebSocket] 服务器配置已更新: %s:%d\r\n", host, port);
    } else {
        printf("[WebSocket] 配置更新失败(需要先断开连接)\r\n");
    }
}

/**
 * @brief  获取当前服务器配置
 */
void websocket_get_server(char *host, uint16_t *port)
{
    ws_config_t config;
    
    if (ws_get_config(&config) == 0) {
        if (host != NULL) {
            strncpy(host, config.server_host, 64);
            host[63] = '\0';
        }
        if (port != NULL) {
            *port = config.server_port;
        }
    }
}

void websocket_send_health(void)
{
    int ret;
    g_last_tx_time = get_tick_ms();
    const health_output_t *health = health_get_result();
    const diag_output_t *diag = diag_get_result();
    scheduler_tinyml_status_t tinyml_status;
    uint8_t health_score;
    const char *pump_status = "normal";
    const char *fan_status = "normal";
    const char *sensor_status = "normal";
    const char *cooling_status = "normal";

    if (health == NULL || diag == NULL) {
        printf("[健康监控] 健康数据未就绪\r\n");
        return;
    }

    health_score = (uint8_t)(ws_sanitize_float(health->total_score, 0.0f, 100.0f, 100.0f) + 0.5f);

    if (health_score > 100) {
        health_score = 100;
    }

    if (scheduler_tinyml_get_status(&tinyml_status) != 0) {
        memset(&tinyml_status, 0, sizeof(tinyml_status));
    }
    ws_set_tinyml_status(tinyml_status.ready ? 1U : 0U,
                         tinyml_status.anomaly_active ? 1U : 0U,
                         ws_sanitize_float(tinyml_status.score, 0.0f, 1.0f, 0.0f),
                         ws_tinyml_backend_to_str(tinyml_status.backend),
                         tinyml_status.model_ready ? 1U : 0U,
                         tinyml_status.backend_fallback ? 1U : 0U);

    /* 每轮健康上报前先清空上轮故障 */
    ws_clear_faults();

    /* 将诊断结果映射为统一告警码并上报 */
    for (uint8_t i = 0; i < DIAG_FAULT_COUNT; i++) {
        if (diag->faults[i].active) {
            ws_add_fault(alarm_diag_fault_to_code((diag_fault_type_t)i),
                         diag_get_fault_name((diag_fault_type_t)i),
                         alarm_confidence_to_severity(diag->faults[i].confidence));
        }
    }

    if (diag->active_count > 0) {
        sensor_status = "warning";
        cooling_status = (diag->most_severe_confidence >= 0.80f) ? "critical" : "warning";
    }

    if (diag->faults[DIAG_FAULT_PUMP_AGING].active) {
        pump_status = "warning";
    }

    if (diag->faults[DIAG_FAULT_BLOCKAGE].active ||
        diag->faults[DIAG_FAULT_LOW_EFFICIENCY].active) {
        fan_status = "warning";
    }

    /* 发送健康状态 */
    ret = ws_send_health_status(
        health_score,
        pump_status,
        fan_status,
        sensor_status,
        cooling_status
    );
    
    if (ret == 0) {
        ws_mark_send_success();
        if (scheduler_is_verbose_log_enabled()) {
            printf("[健康监控] 健康分数: %d, TinyML(score=%.2f, backend=%s)\r\n",
                   health_score,
                   ws_sanitize_float(tinyml_status.score, 0.0f, 1.0f, 0.0f),
                   ws_tinyml_backend_to_str(tinyml_status.backend));
        }
    } else {
        ws_mark_send_failure("健康监控", ret);
    }
}
