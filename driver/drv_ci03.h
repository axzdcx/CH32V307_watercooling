/**
 * @file    drv_ci03.h
 * @brief   CI03(CI1302) I2C语音模块驱动
 * @author  智能水冷项目
 * @date    2026-02-12
 *
 * 协议要点（当前固件口径）：
 *   - 设备地址: 默认0x2B（7位地址），初始化失败后回退探测0x34
 *   - 识别结果寄存器: 0x64（读1字节，0x00表示无新识别）
 *   - 播报寄存器: 0x03（Profile-S：写1字节播报词ID）
 *   - 兼容口径: Profile-F 使用0x6E写2字节（类型+ID），按需编译启用
 */

#ifndef __DRV_CI03_H
#define __DRV_CI03_H

#include "ch32v30x.h"

#define CI1302_I2C_ADDR_DEFAULT     0x2B
#define CI1302_I2C_ADDR_FALLBACK    0x34
#define CI1302_REG_RESULT            0x64
#define CI1302_REG_SPEAK             0x03
#define CI1302_REG_SPEAK_FRAME       0x6E

/* 协议Profile（默认Profile-S，后续如模块要求帧式可切到Profile-F） */
#define CI03_PROTOCOL_PROFILE_S      0U
#define CI03_PROTOCOL_PROFILE_F      1U
#ifndef CI03_PROTOCOL_PROFILE
#define CI03_PROTOCOL_PROFILE         CI03_PROTOCOL_PROFILE_S
#endif

#if ((CI03_PROTOCOL_PROFILE != CI03_PROTOCOL_PROFILE_S) && \
     (CI03_PROTOCOL_PROFILE != CI03_PROTOCOL_PROFILE_F))
#error "CI03_PROTOCOL_PROFILE must be CI03_PROTOCOL_PROFILE_S or CI03_PROTOCOL_PROFILE_F"
#endif

/* Profile-F下播报类型字节（默认0x00，可按模块配置调整） */
#ifndef CI03_FRAME_BROADCAST_TYPE
#define CI03_FRAME_BROADCAST_TYPE     0x00U
#endif

/* 语音命令ID（与词条配置保持一致） */
#define CI03_CMD_WAKEUP            0x01
#define CI03_CMD_QUERY_TEMP        0x02
#define CI03_CMD_QUERY_HEALTH      0x03
#define CI03_CMD_MODE_SILENT       0x04
#define CI03_CMD_MODE_BALANCE      0x05
#define CI03_CMD_MODE_PERFORMANCE  0x06
#define CI03_CMD_MODE_AI           0x07
#define CI03_CMD_PUMP_FULL         0x08
#define CI03_CMD_QUERY_FAULT       0x09
#define CI03_CMD_QUERY_ENERGY      0x0A
#define CI03_CMD_QUERY_SYSTEM      0x0B
#define CI03_CMD_SLEEP             0x0C

/* 官方示例中的播报词ID（如需独立播报可直接使用） */
#define CI03_BROADCAST_THIS_RED        0x5F
#define CI03_BROADCAST_THIS_BLUE       0x60
#define CI03_BROADCAST_THIS_GREEN      0x61
#define CI03_BROADCAST_THIS_YELLOW     0x62
#define CI03_BROADCAST_RECOGNIZE_YELLOW 0x63
#define CI03_BROADCAST_RECOGNIZE_GREEN  0x64
#define CI03_BROADCAST_RECOGNIZE_BLUE   0x65
#define CI03_BROADCAST_RECOGNIZE_RED    0x66
#define CI03_BROADCAST_INIT             0x67

/**
 * @brief  CI03运行状态快照
 */
typedef struct {
    uint8_t i2c_addr;                 /**< 当前I2C地址 */
    uint8_t initialized;              /**< 驱动是否已初始化 */
    uint8_t protocol_profile;         /**< 0=Profile-S, 1=Profile-F */
    int8_t last_error;                /**< 最近一次错误码（0=无错误） */
    uint32_t cmd_ok_count;            /**< 命令读取成功次数 */
    uint32_t cmd_fail_count;          /**< 命令读取失败次数 */
    uint32_t speak_ok_count;          /**< 播报写入成功次数 */
    uint32_t speak_fail_count;        /**< 播报写入失败次数 */
} ci03_diag_t;

int8_t ci03_init(void);
int8_t ci03_is_online(void);
int8_t ci03_read_cmd(uint8_t *cmd_id);
int8_t ci03_speak(uint8_t speak_id);
int8_t ci03_set_i2c_addr(uint8_t new_addr);
uint8_t ci03_get_i2c_addr(void);

/**
 * @brief  获取CI03运行状态快照
 * @param  diag: 输出状态结构体指针
 * @return 0=成功, -1=失败
 */
int8_t ci03_get_diag(ci03_diag_t *diag);

#endif /* __DRV_CI03_H */
