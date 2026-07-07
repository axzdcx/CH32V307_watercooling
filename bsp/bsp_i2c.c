/**
 * @file    bsp_i2c.c
 * @brief   I2C底层驱动 - CH32V307 (硬件I2C)
 * @author  智能水冷项目
 * @date    2025-01-22
 *
 * I2C分配:
 *   I2C1: PB6(SCL) / PB7(SDA) - ADXL345(0x53) + CST816(0x15) + CI1302(0x34)
 *   速度: 400kHz (Fast Mode)
 */

#include "bsp_i2c.h"

/*============================ 私有函数 ============================*/

/**
 * @brief  等待I2C事件，带超时
 */
static uint8_t i2c1_wait_event(uint32_t event)
{
    uint32_t timeout = I2C_TIMEOUT;

    while(!I2C_CheckEvent(I2C1, event))
    {
        if(--timeout == 0)
        {
            return 1;  /* 超时 */
        }
    }
    return 0;
}

/**
 * @brief  等待I2C标志位，带超时
 */
static uint8_t i2c1_wait_flag(uint32_t flag, FlagStatus status)
{
    uint32_t timeout = I2C_TIMEOUT;

    while(I2C_GetFlagStatus(I2C1, flag) != status)
    {
        if(--timeout == 0)
        {
            return 1;  /* 超时 */
        }
    }
    return 0;
}

/*============================ I2C1 初始化 ============================*/

/**
 * @brief  I2C1初始化
 * @note   PB6(SCL) / PB7(SDA), 400kHz
 */
void i2c1_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef I2C_InitStructure = {0};

    /* 使能时钟 */
    RCC_APB2PeriphClockCmd(I2C1_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* PB6(SCL) / PB7(SDA) 复用开漏输出 */
    GPIO_InitStructure.GPIO_Pin = I2C1_SCL_PIN | I2C1_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C1_GPIO_PORT, &GPIO_InitStructure);

    /* I2C1 配置 */
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;  /* 主机模式，地址随意 */
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed = I2C1_SPEED;
    I2C_Init(I2C1, &I2C_InitStructure);

    /* 使能I2C1 */
    I2C_Cmd(I2C1, ENABLE);
}

/*============================ I2C1 读写函数 ============================*/

/**
 * @brief  I2C1写一个字节（内部函数，不自动重试）
 */
static uint8_t i2c1_write_byte_internal(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    /* 等待总线空闲 */
    if(i2c1_wait_flag(I2C_FLAG_BUSY, RESET)) return 1;

    /* 发送起始信号 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(i2c1_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 2;

    /* 发送设备地址(写) */
    I2C_Send7bitAddress(I2C1, dev_addr << 1, I2C_Direction_Transmitter);
    if(i2c1_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 3;

    /* 发送寄存器地址 */
    I2C_SendData(I2C1, reg_addr);
    if(i2c1_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 4;

    /* 发送数据 */
    I2C_SendData(I2C1, data);
    if(i2c1_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 5;

    /* 发送停止信号 */
    I2C_GenerateSTOP(I2C1, ENABLE);

    return 0;
}

/**
 * @brief  I2C1写一个字节（失败时自动复位重试）
 */
uint8_t i2c1_write_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    uint8_t ret = i2c1_write_byte_internal(dev_addr, reg_addr, data);

    if(ret != 0)
    {
        /* 失败，尝试复位总线后重试一次 */
        i2c1_reset();
        ret = i2c1_write_byte_internal(dev_addr, reg_addr, data);
    }

    return ret;
}

/**
 * @brief  I2C1写多个字节（内部函数，不自动重试）
 */
static uint8_t i2c1_write_bytes_internal(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    /* 等待总线空闲 */
    if(i2c1_wait_flag(I2C_FLAG_BUSY, RESET)) return 1;

    /* 发送起始信号 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(i2c1_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 2;

    /* 发送设备地址(写) */
    I2C_Send7bitAddress(I2C1, dev_addr << 1, I2C_Direction_Transmitter);
    if(i2c1_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 3;

    /* 发送寄存器地址 */
    I2C_SendData(I2C1, reg_addr);
    if(i2c1_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 4;

    /* 发送数据 */
    for(uint16_t i = 0; i < len; i++)
    {
        I2C_SendData(I2C1, data[i]);
        if(i2c1_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 5;
    }

    /* 发送停止信号 */
    I2C_GenerateSTOP(I2C1, ENABLE);

    return 0;
}

/**
 * @brief  I2C1写多个字节（失败时自动复位重试）
 */
uint8_t i2c1_write_bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    uint8_t ret = i2c1_write_bytes_internal(dev_addr, reg_addr, data, len);

    if(ret != 0)
    {
        /* 失败，尝试复位总线后重试一次 */
        i2c1_reset();
        ret = i2c1_write_bytes_internal(dev_addr, reg_addr, data, len);
    }

    return ret;
}

/**
 * @brief  I2C1读一个字节（内部函数，不自动重试）
 */
static uint8_t i2c1_read_byte_internal(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data)
{
    /* 等待总线空闲 */
    if(i2c1_wait_flag(I2C_FLAG_BUSY, RESET)) return 1;

    /* 发送起始信号 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(i2c1_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 2;

    /* 发送设备地址(写) */
    I2C_Send7bitAddress(I2C1, dev_addr << 1, I2C_Direction_Transmitter);
    if(i2c1_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 3;

    /* 发送寄存器地址 */
    I2C_SendData(I2C1, reg_addr);
    if(i2c1_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 4;

    /* 重新发送起始信号 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(i2c1_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 5;

    /* 发送设备地址(读) */
    I2C_Send7bitAddress(I2C1, dev_addr << 1, I2C_Direction_Receiver);
    if(i2c1_wait_event(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) return 6;

    /* 禁用ACK（只读一个字节） */
    I2C_AcknowledgeConfig(I2C1, DISABLE);

    /* 发送停止信号 */
    I2C_GenerateSTOP(I2C1, ENABLE);

    /* 等待接收数据 */
    if(i2c1_wait_event(I2C_EVENT_MASTER_BYTE_RECEIVED)) return 7;

    /* 读取数据 */
    *data = I2C_ReceiveData(I2C1);

    /* 重新使能ACK */
    I2C_AcknowledgeConfig(I2C1, ENABLE);

    return 0;
}

/**
 * @brief  I2C1读一个字节（失败时自动复位重试）
 */
uint8_t i2c1_read_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data)
{
    uint8_t ret = i2c1_read_byte_internal(dev_addr, reg_addr, data);

    if(ret != 0)
    {
        /* 失败，尝试复位总线后重试一次 */
        i2c1_reset();
        ret = i2c1_read_byte_internal(dev_addr, reg_addr, data);
    }

    return ret;
}

/**
 * @brief  I2C1读多个字节（内部函数，不自动重试）
 */
static uint8_t i2c1_read_bytes_internal(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    if(len == 0) return 0;

    /* 等待总线空闲 */
    if(i2c1_wait_flag(I2C_FLAG_BUSY, RESET)) return 1;

    /* 发送起始信号 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(i2c1_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 2;

    /* 发送设备地址(写) */
    I2C_Send7bitAddress(I2C1, dev_addr << 1, I2C_Direction_Transmitter);
    if(i2c1_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 3;

    /* 发送寄存器地址 */
    I2C_SendData(I2C1, reg_addr);
    if(i2c1_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 4;

    /* 重新发送起始信号 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(i2c1_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 5;

    /* 发送设备地址(读) */
    I2C_Send7bitAddress(I2C1, dev_addr << 1, I2C_Direction_Receiver);
    if(i2c1_wait_event(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) return 6;

    /* 读取数据 */
    for(uint16_t i = 0; i < len; i++)
    {
        if(i == len - 1)
        {
            /* 最后一个字节，禁用ACK，发送STOP */
            I2C_AcknowledgeConfig(I2C1, DISABLE);
            I2C_GenerateSTOP(I2C1, ENABLE);
        }

        /* 等待接收数据 */
        if(i2c1_wait_event(I2C_EVENT_MASTER_BYTE_RECEIVED)) return 7;

        /* 读取数据 */
        data[i] = I2C_ReceiveData(I2C1);
    }

    /* 重新使能ACK */
    I2C_AcknowledgeConfig(I2C1, ENABLE);

    return 0;
}

/**
 * @brief  I2C1读多个字节（失败时自动复位重试）
 */
uint8_t i2c1_read_bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    uint8_t ret = i2c1_read_bytes_internal(dev_addr, reg_addr, data, len);

    if(ret != 0)
    {
        /* 失败，尝试复位总线后重试一次 */
        i2c1_reset();
        ret = i2c1_read_bytes_internal(dev_addr, reg_addr, data, len);
    }

    return ret;
}

/**
 * @brief  I2C1检测设备是否存在
 */
uint8_t i2c1_check_device(uint8_t dev_addr)
{
    uint8_t ret;

    /* 等待总线空闲 */
    if(i2c1_wait_flag(I2C_FLAG_BUSY, RESET)) return 1;

    /* 发送起始信号 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(i2c1_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 2;

    /* 发送设备地址(写) */
    I2C_Send7bitAddress(I2C1, dev_addr << 1, I2C_Direction_Transmitter);

    /* 等待ACK，超时则设备不存在 */
    ret = i2c1_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);

    /* 发送停止信号 */
    I2C_GenerateSTOP(I2C1, ENABLE);

    return ret;
}

/**
 * @brief  I2C1总线复位（解决总线卡死）
 */
void i2c1_reset(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* 禁用I2C1 */
    I2C_Cmd(I2C1, DISABLE);
    I2C_DeInit(I2C1);

    /* 将SDA/SCL设为普通GPIO输出 */
    GPIO_InitStructure.GPIO_Pin = I2C1_SCL_PIN | I2C1_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C1_GPIO_PORT, &GPIO_InitStructure);

    /* 发送9个时钟脉冲，释放总线 */
    for(uint8_t i = 0; i < 9; i++)
    {
        GPIO_SetBits(I2C1_GPIO_PORT, I2C1_SCL_PIN);
        for(volatile int j = 0; j < 100; j++);
        GPIO_ResetBits(I2C1_GPIO_PORT, I2C1_SCL_PIN);
        for(volatile int j = 0; j < 100; j++);
    }

    /* 发送停止条件 */
    GPIO_ResetBits(I2C1_GPIO_PORT, I2C1_SDA_PIN);
    for(volatile int j = 0; j < 100; j++);
    GPIO_SetBits(I2C1_GPIO_PORT, I2C1_SCL_PIN);
    for(volatile int j = 0; j < 100; j++);
    GPIO_SetBits(I2C1_GPIO_PORT, I2C1_SDA_PIN);
    for(volatile int j = 0; j < 100; j++);

    /* 重新初始化I2C1 */
    i2c1_init();
}
