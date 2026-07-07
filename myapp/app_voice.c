/**
 * @file    app_voice.c
 * @brief   语音交互应用层实现 — CI03(CI1302)离线语音集成
 * @author  智能水冷项目
 * @date    2026-02-12
 */

#include "app_voice.h"
#include "drv_ci03.h"
#include "app_temp.h"
#include "app_pwm.h"
#include "algo_diagnosis.h"
#include <stddef.h>
#include <stdio.h>

static uint8_t s_initialized = 0;
static uint8_t s_last_fault_announced = 0;
static uint8_t s_last_cmd_id = 0;
static uint32_t s_last_cmd_tick = 0;

extern uint32_t get_tick_ms(void);  /* 系统毫秒时间基准 */

#define VOICE_CMD_DEBOUNCE_MS   800U
#define VOICE_WAKEUP_DEBOUNCE_MS 2000U

/**
 * @brief  获取命令防抖窗口
 * @param  cmd_id: 标准命令ID（0x01~0x0C）
 * @return 防抖时间（ms）
 */
static uint32_t voice_get_cmd_debounce_ms(uint8_t cmd_id)
{
    if (cmd_id == CI03_CMD_WAKEUP) {
        return VOICE_WAKEUP_DEBOUNCE_MS;
    }
    if ((cmd_id == CI03_CMD_QUERY_TEMP) ||
        (cmd_id == CI03_CMD_QUERY_HEALTH) ||
        (cmd_id == CI03_CMD_QUERY_FAULT) ||
        (cmd_id == CI03_CMD_QUERY_ENERGY) ||
        (cmd_id == CI03_CMD_QUERY_SYSTEM)) {
        return VOICE_WAKEUP_DEBOUNCE_MS;
    }
    return VOICE_CMD_DEBOUNCE_MS;
}

/**
 * @brief  归一化命令ID（兼容网站语义标签101~112）
 * @param  raw_cmd_id: 模块原始命令ID
 * @param  cmd_id: 归一化后命令ID输出
 * @return 1=有效命令, 0=无效命令
 */
static uint8_t voice_normalize_cmd_id(uint8_t raw_cmd_id, uint8_t *cmd_id)
{
    if (cmd_id == NULL) {
        return 0U;
    }

    if ((raw_cmd_id >= CI03_CMD_WAKEUP) && (raw_cmd_id <= CI03_CMD_SLEEP)) {
        *cmd_id = raw_cmd_id;
        return 1U;
    }

    /* 兼容网站常见语义标签：101~112 -> 1~12 */
    if ((raw_cmd_id >= 101U) && (raw_cmd_id <= 112U)) {
        *cmd_id = (uint8_t)(raw_cmd_id - 100U);
        return 1U;
    }

    return 0U;
}

static void voice_cmd_handler(uint8_t cmd_id)
{
    switch (cmd_id) {
    case CI03_CMD_WAKEUP:
        ci03_speak(CI03_BROADCAST_RECOGNIZE_RED);
        break;

    case CI03_CMD_QUERY_TEMP:
        ci03_speak(CI03_BROADCAST_RECOGNIZE_GREEN);
        break;

    case CI03_CMD_QUERY_HEALTH:
        ci03_speak(CI03_BROADCAST_RECOGNIZE_BLUE);
        break;

    case CI03_CMD_MODE_SILENT:
        pwm_set_mode(PWM_MODE_MANUAL);
        pwm_set_pump(50);
        pwm_set_fan(30);
        ci03_speak(CI03_BROADCAST_THIS_YELLOW);
        break;

    case CI03_CMD_MODE_BALANCE:
        pwm_set_mode(PWM_MODE_MANUAL);
        pwm_set_pump(70);
        pwm_set_fan(50);
        ci03_speak(CI03_BROADCAST_THIS_GREEN);
        break;

    case CI03_CMD_MODE_PERFORMANCE:
        pwm_set_mode(PWM_MODE_MANUAL);
        pwm_set_pump(90);
        pwm_set_fan(80);
        ci03_speak(CI03_BROADCAST_THIS_RED);
        break;

    case CI03_CMD_MODE_AI:
        pwm_set_mode(PWM_MODE_AUTO);
        ci03_speak(CI03_BROADCAST_THIS_BLUE);
        break;

    case CI03_CMD_PUMP_FULL:
        pwm_set_mode(PWM_MODE_MANUAL);
        pwm_set_pump(100);
        ci03_speak(CI03_BROADCAST_THIS_RED);
        break;

    case CI03_CMD_QUERY_FAULT:
        ci03_speak(CI03_BROADCAST_RECOGNIZE_RED);
        break;

    case CI03_CMD_QUERY_ENERGY:
        ci03_speak(CI03_BROADCAST_RECOGNIZE_GREEN);
        break;

    case CI03_CMD_QUERY_SYSTEM:
        ci03_speak(CI03_BROADCAST_RECOGNIZE_BLUE);
        break;

    case CI03_CMD_SLEEP:
        ci03_speak(CI03_BROADCAST_THIS_YELLOW);
        break;

    default:
        break;
    }
}

int8_t voice_app_init(void)
{
    ci03_diag_t diag;
    const char *profile_name = "Profile-S";

    if (ci03_init() != 0) {
        s_initialized = 0;
        return -1;
    }

    if (ci03_get_diag(&diag) == 0) {
        if (diag.protocol_profile == CI03_PROTOCOL_PROFILE_F) {
            profile_name = "Profile-F";
        }
        printf("[VOICE] CI03在线 addr=0x%02X, profile=%s\r\n", diag.i2c_addr, profile_name);
    }

    s_initialized = 1;
    return 0;
}

void task_voice(void)
{
    uint8_t raw_cmd_id = 0;
    uint8_t cmd_id = 0;
    uint32_t now_tick;
    uint32_t debounce_window_ms;

    if (!s_initialized) {
        return;
    }

    if (ci03_read_cmd(&raw_cmd_id) == 0) {
        if (raw_cmd_id != 0x00U) {
            if (voice_normalize_cmd_id(raw_cmd_id, &cmd_id) != 0U) {
                now_tick = get_tick_ms();
                debounce_window_ms = voice_get_cmd_debounce_ms(cmd_id);
                if ((cmd_id == s_last_cmd_id) &&
                    ((now_tick - s_last_cmd_tick) < debounce_window_ms)) {
                    return;
                }

                voice_cmd_handler(cmd_id);
                s_last_cmd_id = cmd_id;
                s_last_cmd_tick = now_tick;
            }
        }
    }

    if (diag_has_active_fault() && !s_last_fault_announced) {
        voice_announce_fault();
    }
    s_last_fault_announced = diag_has_active_fault();
}

void voice_announce_temp(void)
{
    if (!s_initialized) {
        return;
    }

    if (temp_get_data()->cpu_status >= 2) {
        ci03_speak(CI03_BROADCAST_RECOGNIZE_GREEN);
    } else {
        ci03_speak(CI03_BROADCAST_RECOGNIZE_GREEN);
    }
}

void voice_announce_health(void)
{
    if (!s_initialized) {
        return;
    }

    ci03_speak(CI03_BROADCAST_RECOGNIZE_BLUE);
}

void voice_announce_fault(void)
{
    if (!s_initialized) {
        return;
    }

    ci03_speak(CI03_BROADCAST_RECOGNIZE_RED);
}
