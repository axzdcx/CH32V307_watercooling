/**
 * @file    drv_dht11.c
 * @brief   DHT11 温湿度传感器驱动实现
 * @author  智能水冷项目
 * @date    2026-02-12
 */

#include "drv_dht11.h"
#include "debug.h"

/*============================ GPIO配置 ============================*/

static void dht11_io_out(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin = DHT11_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;  /* 开漏输出 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_PORT, &GPIO_InitStructure);
}

static void dht11_io_in(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin = DHT11_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;     /* 上拉输入 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_PORT, &GPIO_InitStructure);
}

static uint8_t dht11_read_pin(void)
{
    return GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN);
}

/*============================ 公共函数 ============================*/

int8_t dht11_init(void)
{
    RCC_APB2PeriphClockCmd(DHT11_RCC, ENABLE);
    dht11_io_out();
    GPIO_SetBits(DHT11_PORT, DHT11_PIN);
    return 0;
}

/**
 * @brief  读取DHT11数据
 * @return 0=成功, -1=失败
 */
int8_t dht11_read(dht11_data_t *data)
{
    uint8_t i, j;
    uint8_t buf[5] = {0};

    if (data == NULL) {
        return -1;
    }

    /* 起始信号 */
    dht11_io_out();
    GPIO_ResetBits(DHT11_PORT, DHT11_PIN);
    Delay_Ms(20);
    GPIO_SetBits(DHT11_PORT, DHT11_PIN);
    Delay_Us(30);
    dht11_io_in();

    /* 等待响应：低80us + 高80us */
    for (i = 0; i < 100; i++) {
        if (!dht11_read_pin()) break;
        Delay_Us(1);
    }
    if (i >= 100) return -1;

    for (i = 0; i < 100; i++) {
        if (dht11_read_pin()) break;
        Delay_Us(1);
    }
    if (i >= 100) return -1;

    for (i = 0; i < 100; i++) {
        if (!dht11_read_pin()) break;
        Delay_Us(1);
    }
    if (i >= 100) return -1;

    /* 读取40位数据 */
    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 8; j++)
        {
            /* 等待低电平开始 */
            uint16_t timeout = 100;
            while (dht11_read_pin() && timeout--) Delay_Us(1);
            if (timeout == 0) return -1;

            timeout = 100;
            while (!dht11_read_pin() && timeout--) Delay_Us(1);
            if (timeout == 0) return -1;

            /* 高电平持续时间判断位值 */
            Delay_Us(35);
            buf[i] <<= 1;
            if (dht11_read_pin()) {
                buf[i] |= 0x01;
            }

            timeout = 100;
            while (dht11_read_pin() && timeout--) Delay_Us(1);
            if (timeout == 0) return -1;
        }
    }

    /* 校验 */
    if (((buf[0] + buf[1] + buf[2] + buf[3]) & 0xFF) != buf[4]) {
        return -1;
    }

    data->humidity = buf[0];
    data->temperature = buf[2];
    data->checksum = buf[4];

    return 0;
}
