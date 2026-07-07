/**
 * @file    drv_ds18b20.c
 * @brief   DS18B20 数字温度传感器驱动实现
 * @author  智能水冷项目
 * @date    2025-01-23
 *
 * 移植自智能粮仓项目（已验证可用），使用开漏输出模式
 */

#include "drv_ds18b20.h"
#include "debug.h"

/*============================ GPIO初始化 ============================*/

/**
 * @brief  DS18B20 GPIO初始化（开漏输出模式）
 */
static void DS18B20_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(DS18B20_RCC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = DS18B20_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;  /* 开漏输出模式 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS18B20_PORT, &GPIO_InitStructure);

    DS18B20_SET;  /* 释放总线 */
}

/**
 * @brief  DS18B20 GPIO初始化（开漏输出模式）- 多引脚
 */
static void ds18b20_gpio_init_bus(const ds18b20_bus_t *bus)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(bus->rcc, ENABLE);

    GPIO_InitStructure.GPIO_Pin = bus->pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;  /* 开漏输出模式 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(bus->port, &GPIO_InitStructure);

    GPIO_SetBits(bus->port, bus->pin);  /* 释放总线 */
}

static void ds18b20_set_bus(const ds18b20_bus_t *bus)
{
    GPIO_SetBits(bus->port, bus->pin);
}

static void ds18b20_reset_bus(const ds18b20_bus_t *bus)
{
    GPIO_ResetBits(bus->port, bus->pin);
}

static uint8_t ds18b20_read_bus(const ds18b20_bus_t *bus)
{
    return GPIO_ReadInputDataBit(bus->port, bus->pin);
}

/**
 * @brief  DS18B20 复位（多引脚）
 * @return 1=设备存在, 0=无响应
 */
static uint8_t ds18b20_rst_bus(const ds18b20_bus_t *bus)
{
    uint8_t var;

    ds18b20_set_bus(bus);   /* 释放总线 */
    Delay_Us(5);

    ds18b20_reset_bus(bus); /* 拉低总线 */
    Delay_Ms(2);            /* 保持2ms发送复位脉冲 */

    ds18b20_set_bus(bus);   /* 释放总线 */
    Delay_Us(70);           /* 等待15-60us */

    var = !ds18b20_read_bus(bus);  /* 读取DS18B20应答（低电平=存在） */

    Delay_Us(500);          /* 等待480us */

    return var;
}

/**
 * @brief  读一个字节（多引脚）
 */
static uint8_t ds18b20_read_byte_bus(const ds18b20_bus_t *bus)
{
    uint8_t _data = 0x00, var;

    for (var = 0; var < 8; var++)
    {
        _data >>= 1;
        ds18b20_reset_bus(bus);
        Delay_Us(5);        /* 拉低1us以上 */
        ds18b20_set_bus(bus);        /* 释放总线 */
        Delay_Us(10);       /* 15us内开始采样 */
        if (ds18b20_read_bus(bus))
            _data |= 0x80;  /* 高位在前 */
        Delay_Us(60);       /* 完成读时隙 */
    }

    return _data;
}

/**
 * @brief  写一个字节（多引脚）
 */
static void ds18b20_write_byte_bus(const ds18b20_bus_t *bus, uint8_t _data)
{
    uint8_t var;

    for (var = 0; var < 8; var++)
    {
        if (_data & 0x01)
        {
            /* 写1 */
            ds18b20_reset_bus(bus);
            Delay_Us(5);    /* 15us内释放总线 */
            ds18b20_set_bus(bus);
            Delay_Us(70);   /* 在15-60us的采样窗口内为高电平 */
        }
        else
        {
            /* 写0 */
            ds18b20_reset_bus(bus);
            Delay_Us(70);   /* 保持低电平 */
            ds18b20_set_bus(bus);
            Delay_Us(5);
        }
        _data >>= 1;
    }
}

/*============================ 1-Wire 协议 ============================*/

/**
 * @brief  DS18B20 复位
 * @return 1=设备存在, 0=无响应
 */
static uint8_t DS18B20_RST(void)
{
    uint8_t var;

    DS18B20_SET;        /* 释放总线 */
    Delay_Us(5);

    DS18B20_RESET;      /* 拉低总线 */
    Delay_Ms(2);        /* 保持2ms发送复位脉冲 */

    DS18B20_SET;        /* 释放总线 */
    Delay_Us(70);       /* 等待15-60us */

    var = !DS18B20_READ;  /* 读取DS18B20应答（低电平=存在） */

    Delay_Us(500);      /* 等待480us */

    return var;
}

/**
 * @brief  读一个字节
 */
static uint8_t DS18B20_ReadByte(void)
{
    uint8_t _data = 0x00, var;

    for (var = 0; var < 8; var++)
    {
        _data >>= 1;
        DS18B20_RESET;
        Delay_Us(5);        /* 拉低1us以上 */
        DS18B20_SET;        /* 释放总线 */
        Delay_Us(10);       /* 15us内开始采样 */
        if (DS18B20_READ)
            _data |= 0x80;  /* 高位在前 */
        Delay_Us(60);       /* 完成读时隙 */
    }

    return _data;
}

/**
 * @brief  写一个字节
 */
static void DS18B20_WriteByte(uint8_t _data)
{
    uint8_t var;

    for (var = 0; var < 8; var++)
    {
        if (_data & 0x01)
        {
            /* 写1 */
            DS18B20_RESET;
            Delay_Us(5);    /* 15us内释放总线 */
            DS18B20_SET;
            Delay_Us(70);   /* 在15-60us的采样期内保持高电平 */
        }
        else
        {
            /* 写0 */
            DS18B20_RESET;
            Delay_Us(70);   /* 保持低电平60us以上 */
            DS18B20_SET;
            Delay_Us(5);
        }
        _data >>= 1;
    }
}

/*============================ 外部接口函数 ============================*/

/**
 * @brief  DS18B20 初始化
 */
int8_t ds18b20_init(void)
{
    printf("\r\n[DS18B20] ===== 开始初始化 =====\r\n");

    /* GPIO初始化（开漏输出） */
    DS18B20_GPIO_Init();

    Delay_Ms(10);

    /* 测试引脚 */
    uint8_t pin_state = DS18B20_READ;
    printf("[DS18B20] PE10读取测试: %d (应为1)\r\n", pin_state);

    /* 复位检测 */
    printf("[DS18B20] 正在检测设备 (PE10)...\r\n");
    if (DS18B20_RST())
    {
        printf("[DS18B20] ✅ 初始化成功\r\n");
        return 0;
    }
    else
    {
        printf("[DS18B20] ❌ 未找到设备！\r\n");
        printf("          DATA->PE10, VCC->3.3V/5V, GND->GND\r\n");
        return -1;
    }
}

/**
 * @brief  检测DS18B20
 */
int8_t ds18b20_check(void)
{
    return DS18B20_RST() ? 0 : -1;
}

/**
 * @brief  启动温度转换（非阻塞）
 */
void ds18b20_start_convert(void)
{
    if (DS18B20_RST())
    {
        DS18B20_WriteByte(DS18B20_CMD_Skip_ROM);
        DS18B20_WriteByte(DS18B20_CMD_Covert_Temp);
    }
}

/**
 * @brief  读取温度值
 */
float ds18b20_get_temperature(void)
{
    uint16_t temp = 0x00;

    if (DS18B20_RST())
    {
        DS18B20_WriteByte(DS18B20_CMD_Skip_ROM);
        DS18B20_WriteByte(DS18B20_CMD_Read_Scratchpad);

        temp |= DS18B20_ReadByte();
        temp |= DS18B20_ReadByte() << 8;

        printf("[DS18B20] 原始: 0x%04X\r\n", temp);

        if (temp & 0x8000)
        {
            /* 负温度 */
            return -((float)(~temp + 1) * 0.0625f);
        }
        else
        {
            return (float)temp * 0.0625f;
        }
    }

    return 999.0f;  /* 读取失败 */
}

/**
 * @brief  读取温度（阻塞式）
 */
float ds18b20_read_temperature(void)
{
    ds18b20_start_convert();
    Delay_Ms(750);
    return ds18b20_get_temperature();
}

/**
 * @brief  设置分辨率
 */
void ds18b20_set_resolution(ds18b20_resolution_t resolution)
{
    if (DS18B20_RST())
    {
        DS18B20_WriteByte(DS18B20_CMD_Skip_ROM);
        DS18B20_WriteByte(DS18B20_CMD_Write_Scratchpad);
        DS18B20_WriteByte(0);           /* TH */
        DS18B20_WriteByte(0);           /* TL */
        DS18B20_WriteByte(resolution);  /* 配置寄存器 */

        DS18B20_RST();
        DS18B20_WriteByte(DS18B20_CMD_Skip_ROM);
        DS18B20_WriteByte(DS18B20_CMD_Copy_Scratchpad);
        Delay_Ms(15);
    }
}

/**
 * @brief  读取ROM编码
 */
int8_t ds18b20_read_rom(uint8_t rom[8])
{
    uint8_t i;

    if (DS18B20_RST())
    {
        DS18B20_WriteByte(DS18B20_CMD_Read_ROM);
        for (i = 0; i < 8; i++)
        {
            rom[i] = DS18B20_ReadByte();
        }
        return 0;
    }

    return -1;
}

int8_t ds18b20_init_bus(const ds18b20_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    ds18b20_gpio_init_bus(bus);
    return ds18b20_rst_bus(bus) ? 0 : -1;
}

int8_t ds18b20_check_bus(const ds18b20_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    return ds18b20_rst_bus(bus) ? 0 : -1;
}

void ds18b20_start_convert_bus(const ds18b20_bus_t *bus)
{
    if (bus == NULL) return;
    if (ds18b20_rst_bus(bus))
    {
        ds18b20_write_byte_bus(bus, DS18B20_CMD_Skip_ROM);
        ds18b20_write_byte_bus(bus, DS18B20_CMD_Covert_Temp);
    }
}

float ds18b20_get_temperature_bus(const ds18b20_bus_t *bus)
{
    uint16_t temp = 0x00;

    if (bus == NULL) return 999.0f;

    if (ds18b20_rst_bus(bus))
    {
        ds18b20_write_byte_bus(bus, DS18B20_CMD_Skip_ROM);
        ds18b20_write_byte_bus(bus, DS18B20_CMD_Read_Scratchpad);

        temp |= ds18b20_read_byte_bus(bus);
        temp |= ds18b20_read_byte_bus(bus) << 8;

        if (temp & 0x8000)
        {
            return -((float)(~temp + 1) * 0.0625f);
        }
        else
        {
            return (float)temp * 0.0625f;
        }
    }

    return 999.0f;
}

float ds18b20_read_temperature_bus(const ds18b20_bus_t *bus)
{
    if (bus == NULL) return 999.0f;
    ds18b20_start_convert_bus(bus);
    Delay_Ms(750);
    return ds18b20_get_temperature_bus(bus);
}

void ds18b20_set_resolution_bus(const ds18b20_bus_t *bus, ds18b20_resolution_t resolution)
{
    if (bus == NULL) return;
    if (ds18b20_rst_bus(bus))
    {
        ds18b20_write_byte_bus(bus, DS18B20_CMD_Skip_ROM);
        ds18b20_write_byte_bus(bus, DS18B20_CMD_Write_Scratchpad);
        ds18b20_write_byte_bus(bus, 0);           /* TH */
        ds18b20_write_byte_bus(bus, 0);           /* TL */
        ds18b20_write_byte_bus(bus, resolution);  /* 配置寄存器 */
        ds18b20_rst_bus(bus);
        ds18b20_write_byte_bus(bus, DS18B20_CMD_Skip_ROM);
        ds18b20_write_byte_bus(bus, DS18B20_CMD_Copy_Scratchpad);
        Delay_Ms(15);
    }
}
