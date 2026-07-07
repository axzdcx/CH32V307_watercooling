/**
 * @file    bsp_spi.h
 * @brief   SPI底层驱动 - CH32V307
 * @author  智能水冷项目
 * @date    2025-01-23
 *
 * ST7789 LCD屏幕SPI接口:
 *   SPI1_SCK  (PA5) - 时钟
 *   SPI1_MOSI (PA7) - 数据输出
 *   LCD_CS    (PA4) - 片选
 *   LCD_DC    (PC0) - 数据/命令选择
 *   LCD_RST   (PC1) - 复位
 *   LCD_BL    (PC3) - 背光控制
 */

#ifndef __BSP_SPI_H
#define __BSP_SPI_H

#include "ch32v30x.h"

/*============================ 引脚定义 ============================*/

/* SPI1 硬件引脚 */
#define LCD_SPI                 SPI1
#define LCD_SPI_CLK             RCC_APB2Periph_SPI1
#define LCD_SPI_GPIO_CLK        RCC_APB2Periph_GPIOA

#define LCD_SCK_PORT            GPIOA
#define LCD_SCK_PIN             GPIO_Pin_5

#define LCD_MOSI_PORT           GPIOA
#define LCD_MOSI_PIN            GPIO_Pin_7

/* 控制引脚 */
#define LCD_CS_CLK              RCC_APB2Periph_GPIOA
#define LCD_CS_PORT             GPIOA
#define LCD_CS_PIN              GPIO_Pin_4

#define LCD_DC_CLK              RCC_APB2Periph_GPIOC
#define LCD_DC_PORT             GPIOC
#define LCD_DC_PIN              GPIO_Pin_0

#define LCD_RST_CLK             RCC_APB2Periph_GPIOC
#define LCD_RST_PORT            GPIOC
#define LCD_RST_PIN             GPIO_Pin_1

#define LCD_BL_CLK              RCC_APB2Periph_GPIOC
#define LCD_BL_PORT             GPIOC
#define LCD_BL_PIN              GPIO_Pin_3

/*============================ 控制宏 ============================*/

/* 片选控制 */
#define LCD_CS_LOW()            GPIO_ResetBits(LCD_CS_PORT, LCD_CS_PIN)
#define LCD_CS_HIGH()           GPIO_SetBits(LCD_CS_PORT, LCD_CS_PIN)

/* DC控制 (0=命令, 1=数据) */
#define LCD_DC_CMD()            GPIO_ResetBits(LCD_DC_PORT, LCD_DC_PIN)
#define LCD_DC_DATA()           GPIO_SetBits(LCD_DC_PORT, LCD_DC_PIN)

/* 复位控制 */
#define LCD_RST_LOW()           GPIO_ResetBits(LCD_RST_PORT, LCD_RST_PIN)
#define LCD_RST_HIGH()          GPIO_SetBits(LCD_RST_PORT, LCD_RST_PIN)

/* 背光控制 */
#define LCD_BL_ON()             GPIO_SetBits(LCD_BL_PORT, LCD_BL_PIN)
#define LCD_BL_OFF()            GPIO_ResetBits(LCD_BL_PORT, LCD_BL_PIN)

/*============================ 函数声明 ============================*/

/* SPI初始化 */
void lcd_spi_init(void);

/* SPI数据发送 */
void lcd_spi_write_byte(uint8_t data);
void lcd_spi_write_data(uint8_t *data, uint16_t len);

/* LCD控制函数 */
void lcd_write_cmd(uint8_t cmd);
void lcd_write_data_8bit(uint8_t data);
void lcd_write_data_16bit(uint16_t data);
void lcd_write_color(uint16_t color, uint32_t count);

/* LCD硬件控制 */
void lcd_reset(void);
void lcd_backlight_on(void);
void lcd_backlight_off(void);

#endif /* __BSP_SPI_H */
