/**
 * @file    drv_air780e.c
 * @brief   AIR780E 4G模块驱动实现（DTU透传模式）
 * @author  智能水冷项目
 * @date    2025-01-24
 */

#include "drv_air780e.h"
#include "bsp_uart.h"
#include "scheduler.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>

/*============================ 私有变量 ============================*/

static air780e_handle_t g_air780e;      /* 模块句柄 */
static uint8_t g_rx_buffer[AIR780E_RX_BUF_SIZE];  /* 接收缓冲区 */

/*============================ 私有函数 ============================*/

/**
 * @brief  获取系统时间戳（毫秒）
 */
static uint32_t get_tick(void)
{
    extern uint32_t get_tick_ms(void);
    return get_tick_ms();
}

/**
 * @brief  计算重连等待时间（指数退避，带上限）
 * @return 等待时长(ms)
 */
static uint32_t air780e_calc_reconnect_wait_ms(void)
{
    uint32_t wait_ms = AIR780E_RECONNECT_BASE_MS;
    uint8_t step = g_air780e.reconnect_count;

    if (wait_ms == 0U) {
        wait_ms = 1000U;
    }

    while ((step > 0U) && (wait_ms < AIR780E_RECONNECT_MAX_MS)) {
        if (wait_ms > (AIR780E_RECONNECT_MAX_MS >> 1)) {
            wait_ms = AIR780E_RECONNECT_MAX_MS;
        } else {
            wait_ms <<= 1;
        }
        step--;
    }

    if (wait_ms > AIR780E_RECONNECT_MAX_MS) {
        wait_ms = AIR780E_RECONNECT_MAX_MS;
    }

    return wait_ms;
}

/**
 * @brief  执行一次重连尝试（软重建UART3链路）
 * @param  force_now: 1=忽略退避立即尝试，0=按退避策略尝试
 * @return 0=已完成重连, -1=未执行或失败
 */
static int8_t air780e_try_reconnect(uint8_t force_now)
{
    uint32_t current_time = get_tick();
    uint32_t wait_ms;

    if (g_air780e.reconnect_blocked) {
        if (current_time >= g_air780e.reconnect_block_until) {
            g_air780e.reconnect_blocked = 0;
            g_air780e.reconnect_count = 0;
            if (scheduler_is_verbose_log_enabled()) {
                printf("[AIR780E] 重连冷却结束，恢复自动重连\r\n");
            }
        } else {
            return -1;
        }
    }

    if (!force_now) {
        if (g_air780e.reconnect_count >= AIR780E_MAX_RECONNECT_ATTEMPTS) {
            g_air780e.reconnect_blocked = 1;
            g_air780e.reconnect_block_until = current_time + AIR780E_RECONNECT_COOLDOWN_MS;
            printf("[AIR780E] 达到最大重连次数，进入冷却期\r\n");
            return -1;
        }

        wait_ms = air780e_calc_reconnect_wait_ms();
        if ((current_time - g_air780e.last_reconnect_time) < wait_ms) {
            return -1;
        }
    }

    g_air780e.last_reconnect_time = current_time;
    if (g_air780e.reconnect_count < 255U) {
        g_air780e.reconnect_count++;
    }

    if (scheduler_is_verbose_log_enabled()) {
        printf("[AIR780E] 尝试重连 #%u\r\n", g_air780e.reconnect_count);
    }

    uart3_init();
    uart3_clear();
    g_air780e.state = AIR780E_STATE_READY;
    g_air780e.reconnect_count = 0;
    g_air780e.reconnect_blocked = 0;
    g_air780e.reconnect_block_until = 0;
    printf("[AIR780E] 重连成功，链路恢复\r\n");

    return 0;
}

/*============================ 公共函数实现 ============================*/

/**
 * @brief  初始化AIR780E模块（DTU透传模式）
 */
int8_t air780e_init(void)
{
    printf("[AIR780E] 初始化（DTU透传模式）...\r\n");
    
    /* 初始化UART3硬件（115200, DMA+空闲中断） */
    printf("[AIR780E] 初始化UART3...\r\n");
    uart3_init();
    printf("[AIR780E] UART3初始化完成\r\n");
    
    /* 清空句柄 */
    memset(&g_air780e, 0, sizeof(air780e_handle_t));
    memset(g_rx_buffer, 0, AIR780E_RX_BUF_SIZE);
    
    /* DTU模式下，模块已通过Luatools配置好，上电自动连接云端 */
    
    g_air780e.state = AIR780E_STATE_READY;
    g_air780e.last_heartbeat_time = get_tick();
    g_air780e.last_report_time = get_tick();
    g_air780e.last_reconnect_time = 0;
    g_air780e.reconnect_block_until = 0;
    g_air780e.reconnect_count = 0;
    g_air780e.reconnect_blocked = 0;
    
    printf("[AIR780E] 初始化完成！\r\n");
    printf("[AIR780E] 提示：模块需通过Luatools配置WebSocket参数\r\n");
    
    return 0;
}

/**
 * @brief  发送JSON数据到云端
 */
int8_t air780e_send_json(const char *json_str)
{
    if (json_str == NULL) {
        return -1;
    }

    if (g_air780e.state == AIR780E_STATE_ERROR) {
        if (air780e_try_reconnect(0) != 0) {
            return -1;
        }
    }
    if (g_air780e.state != AIR780E_STATE_READY) {
        return -1;
    }
    
    uint16_t len = strlen(json_str);
    
    if (len == 0 || len > AIR780E_TX_BUF_SIZE) {
        printf("[AIR780E] JSON数据长度错误: %d\r\n", len);
        return -1;
    }
    
    /* 发送数据 */
    g_air780e.state = AIR780E_STATE_SENDING;
    
    if (scheduler_is_verbose_log_enabled()) {
        printf("[AIR780E] 准备发送 %d 字节数据...\r\n", len);
    }
    uart3_send_string(json_str);
    if (scheduler_is_verbose_log_enabled()) {
        printf("[AIR780E] UART3发送完成\r\n");
    }
    
    /* 更新统计信息 */
    g_air780e.stats.tx_count++;
    g_air780e.stats.tx_bytes += len;
    g_air780e.stats.last_tx_time = get_tick();
    
    g_air780e.state = AIR780E_STATE_READY;
    
    if (scheduler_is_verbose_log_enabled()) {
        printf("[AIR780E] 发送JSON: %s\r\n", json_str);
    }
    
    return 0;
}

/**
 * @brief  发送原始数据
 */
int8_t air780e_send_data(uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || len > AIR780E_TX_BUF_SIZE) {
        return -1;
    }

    if (g_air780e.state == AIR780E_STATE_ERROR) {
        if (air780e_try_reconnect(0) != 0) {
            return -1;
        }
    }
    if (g_air780e.state != AIR780E_STATE_READY) {
        return -1;
    }
    
    g_air780e.state = AIR780E_STATE_SENDING;
    
    /* 发送数据 */
    for (uint16_t i = 0; i < len; i++) {
        uart3_send_byte(data[i]);
    }
    
    /* 更新统计信息 */
    g_air780e.stats.tx_count++;
    g_air780e.stats.tx_bytes += len;
    g_air780e.stats.last_tx_time = get_tick();
    
    g_air780e.state = AIR780E_STATE_READY;
    
    return 0;
}

/**
 * @brief  处理接收到的数据
 */
void air780e_process(void)
{
    if (g_air780e.state == AIR780E_STATE_ERROR) {
        (void)air780e_try_reconnect(0);
        return;
    }

    uint16_t available = uart3_available();
    
    if (available > 0) {
        if (available > AIR780E_RX_BUF_SIZE) {
            available = AIR780E_RX_BUF_SIZE;
        }
        
        /* 读取数据 */
        uint16_t len = uart3_read(g_rx_buffer, available);
        
        if (len > 0) {
            /* 更新统计信息 */
            g_air780e.stats.rx_count++;
            g_air780e.stats.rx_bytes += len;
            g_air780e.stats.last_rx_time = get_tick();
            
            if (scheduler_is_verbose_log_enabled()) {
                /* 打印原始十六进制数据 */
                printf("[AIR780E] 收到 %d 字节，HEX: ", len);
                for (uint16_t i = 0; i < len; i++) {
                    printf("%02X ", g_rx_buffer[i]);
                }
                printf("\r\n");

                /* 打印ASCII数据（如果可打印） */
                printf("[AIR780E] ASCII: ");
                for (uint16_t i = 0; i < len; i++) {
                    if (g_rx_buffer[i] >= 0x20 && g_rx_buffer[i] <= 0x7E) {
                        printf("%c", g_rx_buffer[i]);
                    } else {
                        printf(".");
                    }
                }
                printf("\r\n");
            }
            
            /* 判断是否是心跳包 */
            if (len == 1 && g_rx_buffer[0] == 0x00) {
                if (scheduler_is_verbose_log_enabled()) {
                    printf("[AIR780E] 这是心跳包，忽略\r\n");
                }
                return;  /* 心跳包不传递给上层 */
            }
            
            /* 调用数据接收回调 */
            if (g_air780e.on_data_received != NULL) {
                g_air780e.on_data_received(g_rx_buffer, len);
            }
        }
    }
}

/**
 * @brief  获取模块状态
 */
air780e_state_t air780e_get_state(void)
{
    return g_air780e.state;
}

/**
 * @brief  获取统计信息
 */
air780e_stats_t* air780e_get_stats(void)
{
    return &g_air780e.stats;
}

/**
 * @brief  设置数据接收回调函数
 */
void air780e_set_data_callback(void (*callback)(uint8_t *data, uint16_t len))
{
    g_air780e.on_data_received = callback;
}

/**
 * @brief  标记链路异常（进入错误态）
 */
void air780e_mark_error(void)
{
    if (g_air780e.state != AIR780E_STATE_ERROR) {
        g_air780e.state = AIR780E_STATE_ERROR;
        g_air780e.last_reconnect_time = get_tick();
        if (scheduler_is_verbose_log_enabled()) {
            printf("[AIR780E] 标记链路异常，等待重连\r\n");
        }
    }
}

/**
 * @brief  手动强制重连（解除冷却并立即重连）
 */
int8_t air780e_force_reconnect(void)
{
    g_air780e.reconnect_count = 0;
    g_air780e.reconnect_blocked = 0;
    g_air780e.reconnect_block_until = 0;
    g_air780e.last_reconnect_time = 0;
    g_air780e.state = AIR780E_STATE_ERROR;
    return air780e_try_reconnect(1);
}

/**
 * @brief  获取重连状态快照
 */
int8_t air780e_get_reconnect_status(air780e_reconnect_status_t *status)
{
    uint32_t current_time;

    if (status == NULL) {
        return -1;
    }

    memset(status, 0, sizeof(air780e_reconnect_status_t));
    current_time = get_tick();

    status->state = g_air780e.state;
    status->reconnect_count = g_air780e.reconnect_count;
    status->reconnect_blocked = g_air780e.reconnect_blocked;
    status->last_reconnect_time_ms = g_air780e.last_reconnect_time;
    status->last_rx_time_ms = g_air780e.stats.last_rx_time;

    if (g_air780e.reconnect_blocked) {
        if (current_time < g_air780e.reconnect_block_until) {
            status->cooldown_left_ms = g_air780e.reconnect_block_until - current_time;
        } else {
            status->cooldown_left_ms = 0;
        }
    } else if (g_air780e.state == AIR780E_STATE_ERROR) {
        uint32_t wait_ms = air780e_calc_reconnect_wait_ms();
        uint32_t elapsed = current_time - g_air780e.last_reconnect_time;
        status->next_retry_in_ms = (elapsed >= wait_ms) ? 0 : (wait_ms - elapsed);
    }

    return 0;
}

/**
 * @brief  检查是否需要上报数据
 */
uint8_t air780e_should_report(void)
{
    uint32_t current_time = get_tick();
    
    if ((current_time - g_air780e.last_report_time) >= (AIR780E_REPORT_INTERVAL * 1000)) {
        g_air780e.last_report_time = current_time;
        return 1;
    }
    
    return 0;
}

/**
 * @brief  打印统计信息
 */
void air780e_print_stats(void)
{
    uint32_t current_time = get_tick();
    
    printf("\r\n========== AIR780E 统计信息 ==========\r\n");
    printf("状态: %s\r\n", 
           g_air780e.state == AIR780E_STATE_READY ? "就绪" :
           g_air780e.state == AIR780E_STATE_SENDING ? "发送中" : "错误");
    printf("发送次数: %lu\r\n", g_air780e.stats.tx_count);
    printf("接收次数: %lu\r\n", g_air780e.stats.rx_count);
    printf("发送字节: %lu\r\n", g_air780e.stats.tx_bytes);
    printf("接收字节: %lu\r\n", g_air780e.stats.rx_bytes);
    printf("上次发送: %lu ms前\r\n", current_time - g_air780e.stats.last_tx_time);
    printf("上次接收: %lu ms前\r\n", current_time - g_air780e.stats.last_rx_time);
    printf("重连计数: %u\r\n", g_air780e.reconnect_count);
    printf("重连冷却: %s\r\n", g_air780e.reconnect_blocked ? "是" : "否");
    printf("======================================\r\n\r\n");
}
