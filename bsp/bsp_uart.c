/**
 * @file    bsp_uart.c
 * @brief   UART底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2026-02-12
 *
 * 串口分配:
 *   UART1: PA9(TX)/PA10(RX)  - 调试串口 (用沁恒自带USART_Printf_Init)
 *   UART3: PB10(TX)/PB11(RX) - AIR780E 4G模块 115200bps (DMA+空闲中断+ringbuffer)
 */

#include "bsp_uart.h"
#include "debug.h"
#include <string.h>

/*============================ UART3 变量 ============================*/

/* UART3 ringbuffer */
static struct rt_ringbuffer uart3_rb;
static uint8_t uart3_rb_pool[UART3_RX_BUF_SIZE];

/* UART3 DMA接收缓冲区 */
static uint8_t uart3_dma_buf[UART3_DMA_BUF_SIZE];

/*============================ 私有工具函数 ============================*/

/**
 * @brief  查询系统详细日志开关
 * @return 1=开启，0=关闭
 * @note   通过调度器统一控制，避免串口中断日志刷屏
 */
static uint8_t uart3_is_verbose_log_enabled(void)
{
    extern uint8_t scheduler_is_verbose_log_enabled(void);
    return scheduler_is_verbose_log_enabled();
}

/*============================ UART3 AIR780E ============================*/

/**
 * @brief  UART3初始化 - AIR780E 4G模块
 * @note   PB10(TX) / PB11(RX), 115200bps, DMA接收 + 空闲中断
 */
void uart3_init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure = {0};
    DMA_InitTypeDef   DMA_InitStructure = {0};

    /* 初始化ringbuffer */
    rt_ringbuffer_init(&uart3_rb, uart3_rb_pool, UART3_RX_BUF_SIZE);

    /* 使能时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PB10 - TX 复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin = UART3_TX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(UART3_GPIO_PORT, &GPIO_InitStructure);

    /* PB11 - RX 浮空输入 */
    GPIO_InitStructure.GPIO_Pin = UART3_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(UART3_GPIO_PORT, &GPIO_InitStructure);

    /* USART3 配置 */
    USART_InitStructure.USART_BaudRate = UART3_BAUDRATE;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &USART_InitStructure);

    /* DMA配置 - USART3_RX 使用 DMA1_Channel3 */
    DMA_DeInit(DMA1_Channel3);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)uart3_dma_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = UART3_DMA_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    /* 使能DMA通道 */
    DMA_Cmd(DMA1_Channel3, ENABLE);

    /* 使能USART3的DMA接收 */
    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);

    /* 使能空闲中断 */
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

    /* NVIC配置 */
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能USART3 */
    USART_Cmd(USART3, ENABLE);
}

/**
 * @brief  UART3发送一个字节
 */
void uart3_send_byte(uint8_t data)
{
    while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
    USART_SendData(USART3, data);
}

/**
 * @brief  UART3发送字符串
 */
void uart3_send_string(const char *str)
{
    while(*str)
    {
        uart3_send_byte(*str++);
    }
}

/**
 * @brief  UART3发送数据
 */
void uart3_send_data(uint8_t *data, uint16_t len)
{
    for(uint16_t i = 0; i < len; i++)
    {
        uart3_send_byte(data[i]);
    }
}

/**
 * @brief  从UART3 ringbuffer读取数据
 */
uint16_t uart3_read(uint8_t *buf, uint16_t len)
{
    return rt_ringbuffer_get(&uart3_rb, buf, len);
}

/**
 * @brief  获取UART3 ringbuffer中的数据长度
 */
uint16_t uart3_available(void)
{
    return rt_ringbuffer_data_len(&uart3_rb);
}

/**
 * @brief  清空UART3接收缓冲区
 */
void uart3_clear(void)
{
    rt_ringbuffer_reset(&uart3_rb);
}

/**
 * @brief  获取UART3的ringbuffer指针
 */
struct rt_ringbuffer* uart3_get_ringbuffer(void)
{
    return &uart3_rb;
}

/*============================ 中断处理函数 ============================*/

/**
 * @brief  USART3中断处理函数 - AIR780E (DMA + 空闲中断)
 */
void USART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART3_IRQHandler(void)
{
    if(USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
    {
        uint8_t verbose_log;

        /* 清除空闲中断标志：先读SR再读DR */
        volatile uint32_t temp;
        temp = USART3->STATR;
        temp = USART3->DATAR;
        (void)temp;

        /* 计算本次DMA接收的数据长度 */
        uint16_t recv_len = UART3_DMA_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);

        verbose_log = uart3_is_verbose_log_enabled();
        if (verbose_log) {
            /* 调试日志：仅详细日志开启时输出 */
            printf("[UART3 IRQ] 空闲中断触发，DMA接收长度: %d\r\n", recv_len);
        }

        if(recv_len > 0)
        {
            if (verbose_log) {
                uint16_t print_len = (recv_len > 48U) ? 48U : recv_len;
                /* 打印DMA缓冲区原始数据（截断显示，避免刷屏） */
                printf("[UART3 DMA] HEX(%dB): ", print_len);
                for(uint16_t i = 0; i < print_len; i++) {
                    printf("%02X ", uart3_dma_buf[i]);
                }
                if (recv_len > print_len) {
                    printf("...(%dB total)", recv_len);
                }
                printf("\r\n");
            }
            
            /* 将DMA缓冲区数据写入ringbuffer */
            rt_ringbuffer_put_force(&uart3_rb, uart3_dma_buf, recv_len);

            if (verbose_log) {
                printf("[UART3 DMA] 已写入ringbuffer，当前可用: %d 字节\r\n", rt_ringbuffer_data_len(&uart3_rb));
            }
        }

        /* 重启DMA接收 */
        DMA_Cmd(DMA1_Channel3, DISABLE);
        DMA_SetCurrDataCounter(DMA1_Channel3, UART3_DMA_BUF_SIZE);
        DMA_Cmd(DMA1_Channel3, ENABLE);
    }
}
