/**
 * @file    bsp_i2c.h
 * @brief   I2C底层驱动 - CH32V307 (硬件I2C)
 * @author  智能水冷项目
 * @date    2025-01-22
 *
 * I2C分配:
 *   I2C1: PB6(SCL) / PB7(SDA) - ADXL345(0x53) + CST816(0x15) + CI1302(0x34)
 *   速度: 400kHz (Fast Mode)
 */

#ifndef __BSP_I2C_H
#define __BSP_I2C_H

#include "ch32v30x.h"

/*============================ 宏定义 ============================*/

/* I2C1 引脚定义 */
#define I2C1_SCL_PIN        GPIO_Pin_6
#define I2C1_SDA_PIN        GPIO_Pin_7
#define I2C1_GPIO_PORT      GPIOB
#define I2C1_GPIO_CLK       RCC_APB2Periph_GPIOB

/* I2C1 速度 */
#define I2C1_SPEED          400000  /* 400kHz Fast Mode */

/* I2C 超时时间 */
#define I2C_TIMEOUT         10000

/* I2C 设备地址 */
#define ADXL345_ADDR        0x53    /* ADXL345 7位地址 (SDO接GND) */
#define CST816_I2C_ADDR     0x15    /* CST816 触摸 7位地址 */
#define CI1302_ADDR         0x34    /* CI1302 语音模块 7位地址 */

/*============================ 函数声明 ============================*/

/**
 * @brief  I2C1初始化
 * @note   PB6(SCL) / PB7(SDA), 400kHz
 */
void i2c1_init(void);

/**
 * @brief  I2C1写一个字节
 * @param  dev_addr: 设备7位地址
 * @param  reg_addr: 寄存器地址
 * @param  data: 要写入的数据
 * @return 0-成功  其他-失败
 */
uint8_t i2c1_write_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);

/**
 * @brief  I2C1写多个字节
 * @param  dev_addr: 设备7位地址
 * @param  reg_addr: 起始寄存器地址
 * @param  data: 数据缓冲区
 * @param  len: 数据长度
 * @return 0-成功  其他-失败
 */
uint8_t i2c1_write_bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

/**
 * @brief  I2C1读一个字节
 * @param  dev_addr: 设备7位地址
 * @param  reg_addr: 寄存器地址
 * @param  data: 存放读取数据的指针
 * @return 0-成功  其他-失败
 */
uint8_t i2c1_read_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data);

/**
 * @brief  I2C1读多个字节
 * @param  dev_addr: 设备7位地址
 * @param  reg_addr: 起始寄存器地址
 * @param  data: 数据缓冲区
 * @param  len: 要读取的长度
 * @return 0-成功  其他-失败
 */
uint8_t i2c1_read_bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

/**
 * @brief  I2C1检测设备是否存在
 * @param  dev_addr: 设备7位地址
 * @return 0-存在  其他-不存在
 */
uint8_t i2c1_check_device(uint8_t dev_addr);

/**
 * @brief  I2C1总线复位（解决总线卡死）
 */
void i2c1_reset(void);

#endif /* __BSP_I2C_H */
