/**
 * @file    drv_cst816.c
 * @brief   CST816 电容触摸驱动实现 - 用于TK013F2261屏幕
 * @author  智能水冷项目
 * @date    2025-01-26
 *
 * @note    触摸芯片与屏幕集成，共用RST引脚(PC1)
 *          触摸中断引脚: PC10
 */

#include "drv_cst816.h"
#include "bsp_i2c.h"
#include "debug.h"

/*============================ 寄存器定义 ============================*/

#define CST816_REG_DATA         0x00    /* 触摸数据起始寄存器 */
#define CST816_REG_GESTURE_ID   0x01    /* 手势ID */
#define CST816_REG_TOUCH_NUM    0x02    /* 触摸点数 */
#define CST816_REG_XPOS_H       0x03    /* X坐标高字节 */
#define CST816_REG_XPOS_L       0x04    /* X坐标低字节 */
#define CST816_REG_YPOS_H       0x05    /* Y坐标高字节 */
#define CST816_REG_YPOS_L       0x06    /* Y坐标低字节 */
#define CST816_REG_CHIP_ID      0xA7    /* 芯片ID寄存器 */

/* RST引脚（与屏幕共用 PC1）*/
#define CST816_RST_PORT         GPIOC
#define CST816_RST_PIN          GPIO_Pin_1

/*============================ 私有函数 ============================*/

/**
 * @brief  简单延时
 */
static void cst816_delay_ms(uint16_t ms)
{
    for(uint16_t i = 0; i < ms; i++)
    {
        for(volatile uint32_t j = 0; j < 8000; j++);
    }
}

/**
 * @brief  复位触摸芯片（通过共用的RST引脚）
 */
static void cst816_reset(void)
{
    GPIO_ResetBits(CST816_RST_PORT, CST816_RST_PIN);
    cst816_delay_ms(20);
    GPIO_SetBits(CST816_RST_PORT, CST816_RST_PIN);
    cst816_delay_ms(100);
}

/*============================ 公共函数 ============================*/

/**
 * @brief  初始化CST816触摸芯片（纯I2C轮询模式，无中断引脚）
 * @return 0-成功  其他-失败
 */
uint8_t cst816_init(void)
{
    uint8_t ret;
    uint8_t chip_id = 0;

    /* 复位触摸芯片 */
    cst816_reset();

    /* 检测I2C设备 */
    ret = i2c1_check_device(CST816_ADDR);
    if(ret != 0)
    {
        printf("[CST816] 地址0x%02X检测失败，尝试0x5A...\r\n", CST816_ADDR);
        ret = i2c1_check_device(0x5A);
        if(ret != 0)
        {
            printf("[CST816] 触摸芯片检测失败！请检查I2C连线\r\n");
            return 1;
        }
    }

    /* 读取芯片ID验证通信 */
    i2c1_read_byte(CST816_ADDR, CST816_REG_CHIP_ID, &chip_id);
    printf("[CST816] 初始化成功(轮询模式)，ChipID: 0x%02X\r\n", chip_id);

    return 0;
}

/**
 * @brief  读取触摸数据
 * @param  touch: 触摸数据结构指针
 * @return 0-无触摸  1-有触摸
 */
uint8_t cst816_read_touch(cst816_touch_t *touch)
{
    uint8_t buf[8];
    uint8_t ret;
    uint16_t x_raw, y_raw;

    /* 读取8字节触摸数据（从寄存器0x00开始）*/
    ret = i2c1_read_bytes(CST816_ADDR, CST816_REG_DATA, buf, 8);

    if(ret != 0)
    {
        /* 读取失败，做一次快速重试 */
        ret = i2c1_read_bytes(CST816_ADDR, CST816_REG_DATA, buf, 8);
        if(ret != 0)
        {
            return 0;
        }
    }

    /* 检查触摸点数（buf[2]应该大于0表示有触摸）*/
    if(buf[2] == 0)
    {
        /* 无触摸 */
        return 0;
    }

    /* 提取坐标（12位分辨率）*/
    x_raw = ((buf[3] & 0x0F) << 8) | buf[4];  // 合并高4位和低8位
    y_raw = ((buf[5] & 0x0F) << 8) | buf[6];  // 合并高4位和低8位

    /* 边界检查：超出240则视为无效 */
    if(x_raw > 239 || y_raw > 239)
    {
        return 0;
    }

    /* 保存触摸数据 - 坐标直接映射（X/Y都不取反）*/
    touch->gesture_id = buf[1];
    touch->touch_num = buf[2];
    touch->x = x_raw;       /* 屏幕X = 触摸X */
    touch->y = y_raw;       /* 屏幕Y = 触摸Y */

    return 1;
}

/**
 * @brief  检测CST816是否存在
 * @return 0-存在  其他-不存在
 */
uint8_t cst816_check(void)
{
    return i2c1_check_device(CST816_ADDR);
}
