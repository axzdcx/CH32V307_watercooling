/**
 * @file    app_cloud.c
 * @brief   4G云端通信应用层实现 — AIR780E MQTT数据上报
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 数据上报格式（JSON）：
 *   实时数据: {"cpu_temp":xx,"water_temp":xx,"flow":xx,"pump":xx,"fan":xx,"tds":xx}
 *   健康数据: {"health":xx,"level":"xx","faults":[...]}
 *
 * 云端下发命令格式：
 *   {"cmd":"set_mode","value":"auto"}
 *   {"cmd":"set_pump","value":80}
 *   {"cmd":"set_fan","value":60}
 */

#include "app_cloud.h"
#include "drv_air780e.h"
#include "app_temp.h"
#include "app_flow.h"
#include "app_pwm.h"
#include "app_tds.h"
#include "app_websocket.h"
#include "algo_health.h"
#include "algo_diagnosis.h"
#include "algo_alarm_code.h"
#include "algo_energy.h"
#include <stdio.h>
#include <string.h>

/*============================ 内部状态 ============================*/

static uint8_t s_initialized = 0;
static uint8_t s_online = 0;
static uint8_t s_bypass_ws_mode = 0;   /* 1=WebSocket已占用链路，云端模块旁路 */
static uint32_t s_report_count = 0;

/* 上报间隔计数器 */
#define CLOUD_REALTIME_INTERVAL     10  /* 实时数据每10秒上报一次 */
#define CLOUD_HEALTH_INTERVAL       30  /* 健康数据每30秒上报一次 */
#define CLOUD_JSON_SCHEMA_VERSION   "cloud_v2"

static uint16_t s_realtime_cnt = 0;
static uint16_t s_health_cnt = 0;

/*============================ 内部工具函数 ============================*/

/**
 * @brief  限幅并兜底浮点数
 * @param  value: 输入值
 * @param  min: 最小值
 * @param  max: 最大值
 * @param  fallback: 异常值兜底
 * @return 合法值
 */
static float cloud_sanitize_float(float value, float min, float max, float fallback)
{
    if (value != value) {
        return fallback;
    }
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief  构建激活告警码列表（使用'|'分隔）
 * @param  diag: 诊断结果
 * @param  buffer: 输出缓冲区
 * @param  len: 缓冲区长度
 */
static void cloud_build_alarm_codes(const diag_output_t *diag, char *buffer, uint16_t len)
{
    uint8_t first = 1;

    if (buffer == NULL || len == 0) return;
    buffer[0] = '\0';

    if (diag == NULL) return;

    for (uint8_t i = 0; i < DIAG_FAULT_COUNT; i++) {
        if (diag->faults[i].active) {
            const char *code = alarm_diag_fault_to_code((diag_fault_type_t)i);
            size_t used = strlen(buffer);

            if (!first && used < (size_t)(len - 1)) {
                strncat(buffer, "|", len - used - 1);
                used = strlen(buffer);
            }
            if (used < (size_t)(len - 1)) {
                strncat(buffer, code, len - used - 1);
            }
            first = 0;
        }
    }
}

/*============================ 云端下发命令处理 ============================*/

/**
 * @brief  解析并执行云端下发的命令
 *         简单的字符串匹配解析（不依赖JSON库）
 */
static void cloud_on_data_received(uint8_t *data, uint16_t len)
{
    char *str = (char *)data;
    printf("[云端] 收到下发: %.*s\r\n", len, str);

    /* 解析 "set_mode" 命令 */
    if (strstr(str, "set_mode") != NULL) {
        if (strstr(str, "auto") != NULL) {
            pwm_set_mode(PWM_MODE_AUTO);
            printf("[云端] 切换到自动模式\r\n");
        } else if (strstr(str, "manual") != NULL) {
            pwm_set_mode(PWM_MODE_MANUAL);
            printf("[云端] 切换到手动模式\r\n");
        }
    }

    /* 解析 "set_pump" 命令 */
    if (strstr(str, "set_pump") != NULL) {
        char *p = strstr(str, "value");
        if (p != NULL) {
            /* 简单提取数字 */
            p = strchr(p, ':');
            if (p != NULL) {
                int val = 0;
                p++;
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                if (val >= 0 && val <= 100) {
                    pwm_set_mode(PWM_MODE_MANUAL);
                    pwm_set_pump((uint8_t)val);
                    printf("[云端] 设置水泵转速: %d%%\r\n", val);
                }
            }
        }
    }

    /* 解析 "set_fan" 命令 */
    if (strstr(str, "set_fan") != NULL) {
        char *p = strstr(str, "value");
        if (p != NULL) {
            p = strchr(p, ':');
            if (p != NULL) {
                int val = 0;
                p++;
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                if (val >= 0 && val <= 100) {
                    pwm_set_mode(PWM_MODE_MANUAL);
                    pwm_set_fan((uint8_t)val);
                    printf("[云端] 设置风扇转速: %d%%\r\n", val);
                }
            }
        }
    }
}

/*============================ 公共函数实现 ============================*/

int8_t cloud_app_init(void)
{
    /* WebSocket模式下，AIR780E链路由app_websocket/websocket_client独占 */
    if (websocket_is_connected()) {
        s_initialized = 0;
        s_online = 1;
        s_bypass_ws_mode = 1;
        printf("[云端] 检测到WebSocket链路已启用，云端模块旁路（避免UART3冲突）\r\n");
        return 0;
    }

    int8_t ret = air780e_init();
    if (ret != 0) {
        printf("[云端] AIR780E初始化失败（离线模式运行）\r\n");
        s_online = 0;
        return -1;
    }

    air780e_set_data_callback(cloud_on_data_received);
    s_initialized = 1;
    s_online = 1;
    s_bypass_ws_mode = 0;
    printf("[云端] 4G云端通信初始化完成\r\n");
    return 0;
}

void task_cloud(void)
{
    if (s_bypass_ws_mode) return;
    if (!s_initialized) return;

    /* 处理接收数据 */
    air780e_process();

    /* 更新在线状态 */
    s_online = (air780e_get_state() == AIR780E_STATE_READY) ? 1 : 0;

    /* 定时上报实时数据 */
    s_realtime_cnt++;
    if (s_realtime_cnt >= CLOUD_REALTIME_INTERVAL) {
        s_realtime_cnt = 0;
        cloud_report_realtime();
    }

    /* 定时上报健康数据 */
    s_health_cnt++;
    if (s_health_cnt >= CLOUD_HEALTH_INTERVAL) {
        s_health_cnt = 0;
        cloud_report_health();
    }
}

int8_t cloud_report_realtime(void)
{
    if (s_bypass_ws_mode) return -1;
    if (!s_initialized || !s_online) return -1;

    const temp_data_t *temp = temp_get_data();
    const flow_data_t *flow = flow_get_data();
    const pwm_data_t *pwm = pwm_get_data();
    const energy_data_t *energy = energy_get_data();
    float cpu_temp;
    float water_temp;
    float flow_rate;
    uint8_t pump;
    uint8_t fan;
    uint16_t tds_ppm;
    float power_w;
    float carbon_kg;
    int len;

    if (temp == NULL || flow == NULL || pwm == NULL || energy == NULL) {
        return -1;
    }

    cpu_temp = cloud_sanitize_float(temp->cpu_temp, -20.0f, 120.0f, 25.0f);
    water_temp = cloud_sanitize_float(temp->water_temp, -20.0f, 120.0f, 25.0f);
    flow_rate = cloud_sanitize_float(flow->flow_rate, 0.0f, 40.0f, 0.0f);
    pump = (pwm->pump_speed > 100) ? 100 : pwm->pump_speed;
    fan = (pwm->fan1_speed > 100) ? 100 : pwm->fan1_speed;
    tds_ppm = app_tds_get_ppm();
    if (tds_ppm > 2000) tds_ppm = 2000;
    power_w = cloud_sanitize_float(energy->power_w, 0.0f, 500.0f, 0.0f);
    carbon_kg = cloud_sanitize_float(energy->carbon_kg, 0.0f, 100.0f, 0.0f);

    char json[256];
    len = snprintf(json, sizeof(json),
        "{\"schema\":\"%s\",\"cpu_temp\":%.1f,\"water_temp\":%.1f,"
        "\"flow\":%.1f,\"pump\":%d,\"fan\":%d,"
        "\"tds\":%d,\"power\":%.1f,\"carbon\":%.4f}",
        CLOUD_JSON_SCHEMA_VERSION,
        cpu_temp, water_temp,
        flow_rate, pump, fan,
        tds_ppm, power_w, carbon_kg);

    if (len < 0 || len >= (int)sizeof(json)) {
        return -1;
    }

    int8_t ret = air780e_send_json(json);
    if (ret == 0) {
        s_report_count++;
    }
    return ret;
}

int8_t cloud_report_health(void)
{
    if (s_bypass_ws_mode) return -1;
    if (!s_initialized || !s_online) return -1;

    const health_output_t *h = health_get_result();
    const diag_output_t *d = diag_get_result();
    float health_score;
    float severe_conf;
    int severe_id;
    const char *severe_code = alarm_get_most_severe_code(d);
    const char *severe_level;
    int len;

    if (h == NULL || d == NULL) {
        return -1;
    }

    health_score = cloud_sanitize_float(h->total_score, 0.0f, 100.0f, 100.0f);
    severe_conf = cloud_sanitize_float(d->most_severe_confidence, 0.0f, 1.0f, 0.0f);
    severe_id = (d->most_severe_id < DIAG_FAULT_COUNT) ? d->most_severe_id : -1;
    severe_level = (d->active_count > 0)
        ? alarm_confidence_to_severity(severe_conf)
        : "none";
    char alarm_codes[96];

    cloud_build_alarm_codes(d, alarm_codes, sizeof(alarm_codes));
    if (alarm_codes[0] == '\0') {
        strcpy(alarm_codes, ALARM_CODE_NONE);
    }

    char json[416];
    len = snprintf(json, sizeof(json),
        "{\"schema\":\"%s\",\"health\":%.0f,\"level\":\"%s\","
        "\"faults\":%d,\"severe_id\":%d,\"severe_code\":\"%s\","
        "\"severe_level\":\"%s\",\"severe_conf\":%.0f,\"alarm_codes\":\"%s\","
        "\"dim\":[%.0f,%.0f,%.0f,%.0f,%.0f,%.0f]}",
        CLOUD_JSON_SCHEMA_VERSION,
        health_score, health_get_level_name(h->level),
        d->active_count, severe_id,
        severe_code, severe_level,
        severe_conf * 100.0f, alarm_codes,
        cloud_sanitize_float(h->dim_scores[0], 0.0f, 100.0f, 0.0f),
        cloud_sanitize_float(h->dim_scores[1], 0.0f, 100.0f, 0.0f),
        cloud_sanitize_float(h->dim_scores[2], 0.0f, 100.0f, 0.0f),
        cloud_sanitize_float(h->dim_scores[3], 0.0f, 100.0f, 0.0f),
        cloud_sanitize_float(h->dim_scores[4], 0.0f, 100.0f, 0.0f),
        cloud_sanitize_float(h->dim_scores[5], 0.0f, 100.0f, 0.0f));

    if (len < 0 || len >= (int)sizeof(json)) {
        return -1;
    }

    return air780e_send_json(json);
}

uint8_t cloud_is_online(void)
{
    if (s_bypass_ws_mode) {
        return websocket_is_connected();
    }
    return s_online;
}
