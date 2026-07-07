/**
 * @file    drv_ci03.c
 * @brief   CI03(CI1302) I2C语音模块驱动
 * @author  智能水冷项目
 * @date    2026-02-12
 */

#include "drv_ci03.h"
#include "bsp_i2c.h"
#include <stddef.h>

static uint8_t s_ci03_addr = CI1302_I2C_ADDR_DEFAULT;
static uint8_t s_initialized = 0;
static int8_t s_last_error = 0;
static uint32_t s_cmd_ok_count = 0;
static uint32_t s_cmd_fail_count = 0;
static uint32_t s_speak_ok_count = 0;
static uint32_t s_speak_fail_count = 0;

/**
 * @brief  记录最近错误码
 * @param  err: 错误码
 */
static void ci03_set_last_error(int8_t err)
{
    s_last_error = err;
}

/**
 * @brief  按指定地址探测设备
 * @param  addr: 7bit I2C地址
 * @return 0=在线, -1=离线
 */
static int8_t ci03_try_probe_addr(uint8_t addr)
{
    if (i2c1_check_device(addr) == 0) {
        s_ci03_addr = addr;
        s_initialized = 1U;
        ci03_set_last_error(0);
        return 0;
    }
    return -1;
}

/**
 * @brief  在线恢复（当前地址失败时自动回退另一地址）
 * @return 0=在线, -1=离线
 */
static int8_t ci03_try_recover_online(void)
{
    uint8_t alt_addr;

    if (ci03_try_probe_addr(s_ci03_addr) == 0) {
        return 0;
    }

    alt_addr = (s_ci03_addr == CI1302_I2C_ADDR_DEFAULT) ?
               CI1302_I2C_ADDR_FALLBACK : CI1302_I2C_ADDR_DEFAULT;
    if (ci03_try_probe_addr(alt_addr) == 0) {
        return 0;
    }

    s_initialized = 0U;
    ci03_set_last_error(-1);
    return -1;
}

/**
 * @brief  写播报寄存器（不做恢复重试）
 * @param  speak_id: 播报词ID
 * @return 0=成功, -1=失败
 */
static int8_t ci03_write_speak_once(uint8_t speak_id)
{
    uint8_t frame_data[2];

#if (CI03_PROTOCOL_PROFILE == CI03_PROTOCOL_PROFILE_F)
    frame_data[0] = CI03_FRAME_BROADCAST_TYPE;
    frame_data[1] = speak_id;
    if (i2c1_write_bytes(s_ci03_addr, CI1302_REG_SPEAK_FRAME, frame_data, 2) != 0) {
        return -1;
    }
#else
    if (i2c1_write_byte(s_ci03_addr, CI1302_REG_SPEAK, speak_id) != 0) {
        return -1;
    }
#endif

    return 0;
}

int8_t ci03_init(void)
{
    i2c1_init();

    if (ci03_try_recover_online() != 0) {
        s_initialized = 0;
        ci03_set_last_error(-1);
        return -1;
    }

    s_initialized = 1;
    ci03_set_last_error(0);
    (void)ci03_speak(CI03_BROADCAST_INIT);
    (void)ci03_speak(CI03_BROADCAST_INIT);
    return 0;
}

int8_t ci03_is_online(void)
{
    return ci03_try_recover_online();
}

int8_t ci03_read_cmd(uint8_t *cmd_id)
{
    if (cmd_id == NULL) {
        ci03_set_last_error(-2);
        return -1;
    }

    /* 先直接读，避免check_device瞬时NACK导致误判离线 */
    if (i2c1_read_byte(s_ci03_addr, CI1302_REG_RESULT, cmd_id) != 0) {
        /* 首次读失败后做一次在线恢复，并重读 */
        if (ci03_try_recover_online() != 0) {
            s_cmd_fail_count++;
            ci03_set_last_error(-3);
            return -1;
        }
        if (i2c1_read_byte(s_ci03_addr, CI1302_REG_RESULT, cmd_id) != 0) {
            s_initialized = 0;
            s_cmd_fail_count++;
            ci03_set_last_error(-4);
            return -1;
        }
    }

    s_initialized = 1;
    s_cmd_ok_count++;
    ci03_set_last_error(0);
    return 0;
}

int8_t ci03_speak(uint8_t speak_id)
{
    /* 先直接写，失败后再恢复重试 */
    if (ci03_write_speak_once(speak_id) != 0) {
        if (ci03_try_recover_online() != 0) {
            s_speak_fail_count++;
            ci03_set_last_error(-5);
            return -1;
        }
        if (ci03_write_speak_once(speak_id) != 0) {
            s_initialized = 0;
            s_speak_fail_count++;
            ci03_set_last_error(-6);
            return -1;
        }
    }

    s_initialized = 1;
    s_speak_ok_count++;
    ci03_set_last_error(0);
    return 0;
}

int8_t ci03_set_i2c_addr(uint8_t new_addr)
{
    if (new_addr < 0x08 || new_addr > 0x77) {
        ci03_set_last_error(-7);
        return -1;
    }

    s_ci03_addr = new_addr;
    return ci03_is_online();
}

uint8_t ci03_get_i2c_addr(void)
{
    return s_ci03_addr;
}

/**
 * @brief  获取CI03运行状态快照
 * @param  diag: 输出状态结构体
 * @return 0=成功, -1=失败
 */
int8_t ci03_get_diag(ci03_diag_t *diag)
{
    if (diag == NULL) {
        ci03_set_last_error(-8);
        return -1;
    }

    diag->i2c_addr = s_ci03_addr;
    diag->initialized = s_initialized;
    diag->protocol_profile = (uint8_t)CI03_PROTOCOL_PROFILE;
    diag->last_error = s_last_error;
    diag->cmd_ok_count = s_cmd_ok_count;
    diag->cmd_fail_count = s_cmd_fail_count;
    diag->speak_ok_count = s_speak_ok_count;
    diag->speak_fail_count = s_speak_fail_count;
    return 0;
}
