/**
 * @file    bsp_spi.c
 * @brief   SPI底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2025-01-23
 *
 * ST7796 LCD屏幕SPI接口:
 *   SPI1 - 高速模式, 时钟36MHz (144MHz/4)
 */

#include "bsp_spi.h"
#include "debug.h"  /* 使用Delay_Ms() */

/*============================ SPI初始化 ============================*/

/**
 * @brief  LCD SPI初始化
 * @note   SPI1, 36MHz, 8位数据, 主机模式
 */
void lcd_spi_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef SPI_InitStructure = {0};

    /* 使能时钟 */
    RCC_APB2PeriphClockCmd(LCD_SPI_CLK | LCD_SPI_GPIO_CLK |
                           LCD_CS_CLK | LCD_DC_CLK |
                           LCD_RST_CLK | LCD_BL_CLK, ENABLE);

    /* 配置SPI引脚 SCK + MOSI 为复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin = LCD_SCK_PIN | LCD_MOSI_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LCD_SCK_PORT, &GPIO_InitStructure);

    /* 配置控制引脚 CS + DC + RST + BL 为推挽输出 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;

    GPIO_InitStructure.GPIO_Pin = LCD_CS_PIN;
    GPIO_Init(LCD_CS_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = LCD_DC_PIN;
    GPIO_Init(LCD_DC_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = LCD_RST_PIN;
    GPIO_Init(LCD_RST_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = LCD_BL_PIN;
    GPIO_Init(LCD_BL_PORT, &GPIO_InitStructure);

    /* 默认电平 */
    LCD_CS_HIGH();      /* 片选拉高 */
    LCD_DC_DATA();      /* 默认数据模式 */
    LCD_RST_HIGH();     /* 复位拉高 */
    LCD_BL_OFF();       /* 背光关闭 */

    /* SPI1配置: 主机模式, SPI模式3（CPOL=1, CPHA=1）- 与商家示例一致 */
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;  /* 全双工模式 */
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;                   /* 主机模式 */
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;               /* 8位数据 */
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;                     /* 空闲高电平（模式3）*/
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;                    /* 第二个边沿采样（模式3）*/
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;                       /* 软件NSS */
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;  /* 144MHz/4=36MHz */
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;              /* MSB先行 */
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(LCD_SPI, &SPI_InitStructure);

    /* 使能SPI1 */
    SPI_Cmd(LCD_SPI, ENABLE);
}

/*============================ SPI发送 ============================*/

/**
 * @brief  SPI发送单字节
 * @param  data: 要发送的数据
 */
void lcd_spi_write_byte(uint8_t data)
{
    /* 等待发送缓冲区空 */
    while(SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_TXE) == RESET);

    /* 发送数据 */
    SPI_I2S_SendData(LCD_SPI, data);

    /* 等待发送完成 */
    while(SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_BSY) == SET);
}

/**
 * @brief  SPI连续发送数据
 * @param  data: 数据指针
 * @param  len: 数据长度
 */
void lcd_spi_write_data(uint8_t *data, uint16_t len)
{
    while(len--)
    {
        lcd_spi_write_byte(*data++);
    }
}

/*============================ LCD控制 ============================*/

/**
 * @brief  写LCD命令
 * @param  cmd: 命令字节
 */
void lcd_write_cmd(uint8_t cmd)
{
    LCD_CS_LOW();
    LCD_DC_CMD();
    lcd_spi_write_byte(cmd);
    LCD_CS_HIGH();
}

/**
 * @brief  写LCD 8位数据
 * @param  data: 数据字节
 */
void lcd_write_data_8bit(uint8_t data)
{
    LCD_CS_LOW();
    LCD_DC_DATA();
    lcd_spi_write_byte(data);
    LCD_CS_HIGH();
}

/**
 * @brief  写LCD 16位数据
 * @param  data: 16位数据
 */
void lcd_write_data_16bit(uint16_t data)
{
    LCD_CS_LOW();
    LCD_DC_DATA();
    lcd_spi_write_byte(data >> 8);      /* 高字节 */
    lcd_spi_write_byte(data & 0xFF);    /* 低字节 */
    LCD_CS_HIGH();
}

/**
 * @brief  连续写颜色数据 (用于填充屏幕)
 * @param  color: RGB565颜色值
 * @param  count: 像素数量
 */
void lcd_write_color(uint16_t color, uint32_t count)
{
    uint8_t color_h = color >> 8;
    uint8_t color_l = color & 0xFF;

    LCD_CS_LOW();
    LCD_DC_DATA();

    while(count--)
    {
        lcd_spi_write_byte(color_h);
        lcd_spi_write_byte(color_l);
    }

    LCD_CS_HIGH();
}

/*============================ LCD硬件控制 ============================*/

/**
 * @brief  LCD硬件复位
 */
void lcd_reset(void)
{
    LCD_RST_HIGH();
    Delay_Ms(5);

    LCD_RST_LOW();
    Delay_Ms(10);

    LCD_RST_HIGH();
    Delay_Ms(50);
}

/**
 * @brief  打开LCD背光
 */
void lcd_backlight_on(void)
{
    LCD_BL_ON();
}

/**
 * @brief  关闭LCD背光
 */
void lcd_backlight_off(void)
{
    LCD_BL_OFF();
}
