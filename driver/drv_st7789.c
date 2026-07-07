/**
 * @file    drv_st7789.c
 * @brief   ST7789 TFT LCD 驱动实现 - 240x240 IPS屏
 * @author  智能水冷项目
 * @date    2025-01-26
 */

#include "drv_st7789.h"
#include "bsp_spi.h"
#include "ch32v30x.h"
#include "debug.h"  /* 官方延时函数 */

/*============================ 硬件引脚定义 ============================*/
/* 使用与 ST7735S 相同的引脚 */

/*============================ 私有函数 ============================*/

/**
 * @brief  写命令
 */
static void st7789_write_cmd(uint8_t cmd)
{
    LCD_DC_CMD();   /* DC=0: 命令 */
    lcd_spi_write_byte(cmd);
}

/**
 * @brief  写数据（8位）
 */
static void st7789_write_data(uint8_t data)
{
    LCD_DC_DATA();  /* DC=1: 数据 */
    lcd_spi_write_byte(data);
}

/**
 * @brief  写数据（16位）
 */
static void st7789_write_data_16bit(uint16_t data)
{
    LCD_DC_DATA();
    lcd_write_data_16bit(data);
}

/*============================ 公共函数 ============================*/

/**
 * @brief  初始化 ST7789
 * @note   完整配置序列（参考商家TK013F2261示例代码）
 */
void st7789_init(void)
{
    /* 1. 初始化 SPI 和控制引脚 */
    lcd_spi_init();

    /* 2. 硬件复位LCD，确保控制器处于已知状态 */
    lcd_reset();

    /* 3. 打开背光 */
    LCD_BL_ON();

    /* 4. CS拉低并保持（商家做法：初始化期间一直保持CS低）*/
    LCD_CS_HIGH();
    LCD_CS_LOW();

    /* 5. 等待上电稳定（重要！商家代码强调需要延迟）*/
    Delay_Ms(120);

    /* 3. 退出睡眠模式 */
    st7789_write_cmd(0x11);  /* SLPOUT */
    Delay_Ms(120);

    /* 4. 帧率控制（Porch Setting）*/
    st7789_write_cmd(0xB2);
    st7789_write_data(0x0C);
    st7789_write_data(0x0C);
    st7789_write_data(0x00);
    st7789_write_data(0x33);
    st7789_write_data(0x33);

    /* 5. 栅极控制（Gate Control）*/
    st7789_write_cmd(0xB7);
    st7789_write_data(0x35);

    /* 6. VCOM设置 */
    st7789_write_cmd(0xBB);
    st7789_write_data(0x20);

    /* 7. LCM控制 */
    st7789_write_cmd(0xC0);
    st7789_write_data(0x2C);

    /* 8. VDV和VRH命令使能 */
    st7789_write_cmd(0xC2);
    st7789_write_data(0x01);

    /* 9. VRH设置 */
    st7789_write_cmd(0xC3);
    st7789_write_data(0x0B);

    /* 10. VDV设置 */
    st7789_write_cmd(0xC4);
    st7789_write_data(0x20);

    /* 11. 帧率控制（正常模式）*/
    st7789_write_cmd(0xC6);
    st7789_write_data(0x0F);

    /* 12. 电源控制2 */
    st7789_write_cmd(0xCA);
    st7789_write_data(0x0F);

    /* 13. 电源控制3 */
    st7789_write_cmd(0xC8);
    st7789_write_data(0x08);

    /* 14. 偏移设置 */
    st7789_write_cmd(0x55);
    st7789_write_data(0x90);

    /* 15. 电源控制1 */
    st7789_write_cmd(0xD0);
    st7789_write_data(0xA4);
    st7789_write_data(0xA1);

    /* 16. 伽马正校正（Positive Gamma Correction）*/
    st7789_write_cmd(0xE0);
    st7789_write_data(0xD0);
    st7789_write_data(0x00);
    st7789_write_data(0x03);
    st7789_write_data(0x08);
    st7789_write_data(0x0A);
    st7789_write_data(0x17);
    st7789_write_data(0x2E);
    st7789_write_data(0x44);
    st7789_write_data(0x3F);
    st7789_write_data(0x29);
    st7789_write_data(0x10);
    st7789_write_data(0x0E);
    st7789_write_data(0x14);
    st7789_write_data(0x18);

    /* 17. 伽马负校正（Negative Gamma Correction）*/
    st7789_write_cmd(0xE1);
    st7789_write_data(0xD0);
    st7789_write_data(0x00);
    st7789_write_data(0x03);
    st7789_write_data(0x08);
    st7789_write_data(0x07);
    st7789_write_data(0x27);
    st7789_write_data(0x2B);
    st7789_write_data(0x44);
    st7789_write_data(0x41);
    st7789_write_data(0x3C);
    st7789_write_data(0x1B);
    st7789_write_data(0x1D);
    st7789_write_data(0x14);
    st7789_write_data(0x18);

    /* 18. 内存访问控制（MADCTL）- 修复显示方向 */
    st7789_write_cmd(0x36);
    st7789_write_data(0x00); /* MY=1, MX=1, BGR=1 */

    /* 19. 像素格式：RGB565 (16bit) */
    st7789_write_cmd(0x3A);
    st7789_write_data(0x55);

    /* 20. 开启显示 */
    st7789_write_cmd(0x29);  /* DISPON */

    /* 21. 设置显示窗口为 240x240 */
    st7789_write_cmd(0x2A);  /* CASET */
    st7789_write_data(0x00);
    st7789_write_data(0x00);
    st7789_write_data(0x00);
    st7789_write_data(0xEF);  /* 239 */

    st7789_write_cmd(0x2B);  /* RASET */
    st7789_write_data(0x00);
    st7789_write_data(0x00);
    st7789_write_data(0x00);
    st7789_write_data(0xEF);  /* 239 */

    st7789_write_cmd(0x2C);  /* RAMWR */

    /* 22. 再次开启显示（商家代码中有两次）*/
    st7789_write_cmd(0x29);  /* DISPON */
    Delay_Ms(1);

    /* 23. 清屏测试（可选）*/
    // st7789_clear(ST7789_BLACK);
}

/**
 * @brief  设置显示窗口
 */
void st7789_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    /* 列地址设置 */
    st7789_write_cmd(ST7789_CASET);
    st7789_write_data(x1 >> 8);
    st7789_write_data(x1 & 0xFF);
    st7789_write_data(x2 >> 8);
    st7789_write_data(x2 & 0xFF);

    /* 行地址设置 */
    st7789_write_cmd(ST7789_RASET);
    st7789_write_data(y1 >> 8);
    st7789_write_data(y1 & 0xFF);
    st7789_write_data(y2 >> 8);
    st7789_write_data(y2 & 0xFF);

    /* 开始写入显存 */
    st7789_write_cmd(ST7789_RAMWR);
}

/**
 * @brief  填充颜色（优化版：保持CS低，连续写入）
 */
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint32_t i, total_pixels;
    uint8_t color_h = color >> 8;
    uint8_t color_l = color & 0xFF;

    /* 边界检查 */
    if(x2 > 239) x2 = 239;
    if(y2 > 239) y2 = 239;

    st7789_set_window(x1, y1, x2, y2);

    total_pixels = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);

    /* DC设为数据模式，然后连续发送所有像素 */
    LCD_DC_DATA();
    for (i = 0; i < total_pixels; i++)
    {
        lcd_spi_write_byte(color_h);
        lcd_spi_write_byte(color_l);
    }
}

/**
 * @brief  清屏
 */
void st7789_clear(uint16_t color)
{
    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, color);
}

/**
 * @brief  画点（优化版：不操作CS）
 */
void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    st7789_set_window(x, y, x, y);

    /* 直接发送像素数据，不操作CS */
    LCD_DC_DATA();
    lcd_spi_write_byte(color >> 8);
    lcd_spi_write_byte(color & 0xFF);
}
