/**
 * @file    drv_dht11.h
 * @brief   DHT11 温湿度传感器驱动
 * @author  智能水冷项目
 * @date    2026-02-12
 *
 * 硬件连接:
 *   DHT11_DATA (PC12) - DHT11 数据线
 *   VCC  - 3.3V/5V
 *   GND  - GND
 */

#ifndef __DRV_DHT11_H
#define __DRV_DHT11_H

#include "ch32v30x.h"

/*============================ 引脚定义 ============================*/

#define DHT11_PORT     GPIOC
#define DHT11_PIN      GPIO_Pin_12
#define DHT11_RCC      RCC_APB2Periph_GPIOC

/*============================ 数据结构 ============================*/

typedef struct {
    uint8_t humidity;       /* 湿度(%RH) */
    uint8_t temperature;    /* 温度(°C) */
    uint8_t checksum;       /* 校验 */
} dht11_data_t;

/*============================ 函数声明 ============================*/

int8_t dht11_init(void);
int8_t dht11_read(dht11_data_t *data);

#endif /* __DRV_DHT11_H */
