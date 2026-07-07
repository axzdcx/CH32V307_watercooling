/**
 * @file    bsp_uart.h
 * @brief   UART底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2026-02-12
 *
 * 串口分配:
 *   UART1: PA9(TX)/PA10(RX)  - 调试串口 115200bps（沁恒自带）
 *   UART3: PB10(TX)/PB11(RX) - AIR780E 4G模块 115200bps（DMA+空闲中断+ringbuffer）
 */

#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "ch32v30x.h"
#include "ringbuffer.h"
#include <stdio.h>

/* UART1 调试串口 */
#define UART1_TX_PIN        GPIO_Pin_9
#define UART1_RX_PIN        GPIO_Pin_10
#define UART1_GPIO_PORT     GPIOA
#define UART1_BAUDRATE      115200

/* UART3 AIR780E 4G模块 */
#define UART3_TX_PIN        GPIO_Pin_10
#define UART3_RX_PIN        GPIO_Pin_11
#define UART3_GPIO_PORT     GPIOB
#define UART3_BAUDRATE      115200
#define UART3_RX_BUF_SIZE   1024
#define UART3_DMA_BUF_SIZE  512

void uart3_init(void);
void uart3_send_byte(uint8_t data);
void uart3_send_string(const char *str);
void uart3_send_data(uint8_t *data, uint16_t len);
uint16_t uart3_read(uint8_t *buf, uint16_t len);
uint16_t uart3_available(void);
void uart3_clear(void);
struct rt_ringbuffer* uart3_get_ringbuffer(void);

#endif /* __BSP_UART_H */
