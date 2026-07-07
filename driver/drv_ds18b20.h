/**
 * @file    drv_ds18b20.h
 * @brief   DS18B20 数字温度传感器驱动
 * @author  智能水冷项目
 * @date    2025-01-23
 *
 * 硬件连接:
 *   DS18B20_DATA (PE10) - DS18B20 DQ引脚
 *   VCC  - 3.3V/5V
 *   GND  - GND
 *   模块自带上拉电阻，使用开漏输出模式
 *
 * 移植自智能粮仓项目，已验证可用
 */

#ifndef __DRV_DS18B20_H
#define __DRV_DS18B20_H

#include "ch32v30x.h"

/*============================ 引脚定义 ============================*/

/* 修改这里来更换引脚 */
#define DS18B20_PORT    GPIOE
#define DS18B20_PIN     GPIO_Pin_10
#define DS18B20_RCC     RCC_APB2Periph_GPIOE

/* 电平操作宏 */
#define DS18B20_SET     GPIO_SetBits(DS18B20_PORT, DS18B20_PIN)
#define DS18B20_RESET   GPIO_ResetBits(DS18B20_PORT, DS18B20_PIN)
#define DS18B20_READ    GPIO_ReadInputDataBit(DS18B20_PORT, DS18B20_PIN)

/*============================ 多引脚支持 ============================*/

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint32_t rcc;
} ds18b20_bus_t;

/*============================ DS18B20 命令 ============================*/

enum DS18B20_CMD
{
    DS18B20_CMD_Search_ROM = 0xF0,      /* 搜索ROM */
    DS18B20_CMD_Read_ROM = 0x33,        /* 读ROM */
    DS18B20_CMD_Match_ROM = 0x55,       /* 匹配ROM */
    DS18B20_CMD_Skip_ROM = 0xCC,        /* 跳过ROM */
    DS18B20_CMD_Alarm_Search = 0xEC,    /* 报警搜索 */
    DS18B20_CMD_Covert_Temp = 0x44,     /* 温度转换 */
    DS18B20_CMD_Write_Scratchpad = 0x4E,/* 写暂存器 */
    DS18B20_CMD_Read_Scratchpad = 0xBE, /* 读暂存器 */
    DS18B20_CMD_Copy_Scratchpad = 0x48, /* 复制暂存器到EEPROM */
    DS18B20_CMD_Recall_EE = 0xB8,       /* 从EEPROM恢复 */
    DS18B20_CMD_Read_Powersupply = 0xB4,/* 读电源 */
};

/*============================ 分辨率配置 ============================*/

typedef enum {
    DS18B20_RESOLUTION_9BIT  = 0x1F,
    DS18B20_RESOLUTION_10BIT = 0x3F,
    DS18B20_RESOLUTION_11BIT = 0x5F,
    DS18B20_RESOLUTION_12BIT = 0x7F
} ds18b20_resolution_t;

/*============================ 函数声明 ============================*/

int8_t ds18b20_init(void);
int8_t ds18b20_check(void);
float ds18b20_get_temperature(void);
float ds18b20_read_temperature(void);
void ds18b20_start_convert(void);
void ds18b20_set_resolution(ds18b20_resolution_t resolution);
int8_t ds18b20_read_rom(uint8_t rom[8]);

int8_t ds18b20_init_bus(const ds18b20_bus_t *bus);
int8_t ds18b20_check_bus(const ds18b20_bus_t *bus);
float ds18b20_get_temperature_bus(const ds18b20_bus_t *bus);
float ds18b20_read_temperature_bus(const ds18b20_bus_t *bus);
void ds18b20_start_convert_bus(const ds18b20_bus_t *bus);
void ds18b20_set_resolution_bus(const ds18b20_bus_t *bus, ds18b20_resolution_t resolution);

#endif /* __DRV_DS18B20_H */
