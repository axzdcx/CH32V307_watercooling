/**
 * @file    drv_st7789.h
 * @brief   ST7789 TFT LCD 驱动 - 240x240 IPS屏
 * @author  智能水冷项目
 * @date    2025-01-26
 */

#ifndef DRV_ST7789_H
#define DRV_ST7789_H

#include <stdint.h>

/*============================ 宏定义 ============================*/

/* 屏幕尺寸 */
#define ST7789_WIDTH   240
#define ST7789_HEIGHT  240

/* ST7789 命令定义 */
#define ST7789_NOP     0x00
#define ST7789_SWRESET 0x01
#define ST7789_RDDID   0x04
#define ST7789_RDDST   0x09

#define ST7789_SLPIN   0x10   /* 进入睡眠模式 */
#define ST7789_SLPOUT  0x11   /* 退出睡眠模式 */
#define ST7789_PTLON   0x12
#define ST7789_NORON   0x13

#define ST7789_INVOFF  0x20   /* 关闭反色 */
#define ST7789_INVON   0x21   /* 开启反色 */
#define ST7789_DISPOFF 0x28   /* 关闭显示 */
#define ST7789_DISPON  0x29   /* 开启显示 */
#define ST7789_CASET   0x2A   /* 列地址设置 */
#define ST7789_RASET   0x2B   /* 行地址设置 */
#define ST7789_RAMWR   0x2C   /* 写入显存 */
#define ST7789_RAMRD   0x2E

#define ST7789_PTLAR   0x30
#define ST7789_COLMOD  0x3A   /* 颜色模式 */
#define ST7789_MADCTL  0x36   /* 内存访问控制 */

/* MADCTL 参数 */
#define ST7789_MADCTL_MY  0x80  /* 行地址顺序 */
#define ST7789_MADCTL_MX  0x40  /* 列地址顺序 */
#define ST7789_MADCTL_MV  0x20  /* 行列交换 */
#define ST7789_MADCTL_ML  0x10  /* 垂直刷新顺序 */
#define ST7789_MADCTL_RGB 0x00  /* RGB 顺序 */
#define ST7789_MADCTL_BGR 0x08  /* BGR 顺序 */
#define ST7789_MADCTL_MH  0x04  /* 水平刷新顺序 */

/* 颜色定义 (RGB565) */
#define ST7789_BLACK       0x0000
#define ST7789_WHITE       0xFFFF
#define ST7789_RED         0xF800
#define ST7789_GREEN       0x07E0
#define ST7789_BLUE        0x001F
#define ST7789_YELLOW      0xFFE0
#define ST7789_CYAN        0x07FF
#define ST7789_MAGENTA     0xF81F

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化 ST7789
 */
void st7789_init(void);

/**
 * @brief  设置显示窗口
 * @param  x1, y1: 起始坐标
 * @param  x2, y2: 结束坐标
 */
void st7789_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/**
 * @brief  填充颜色
 * @param  x1, y1: 起始坐标
 * @param  x2, y2: 结束坐标
 * @param  color: RGB565 颜色
 */
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/**
 * @brief  清屏
 * @param  color: RGB565 颜色
 */
void st7789_clear(uint16_t color);

/**
 * @brief  画点
 * @param  x, y: 坐标
 * @param  color: RGB565 颜色
 */
void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

#endif /* DRV_ST7789_H */
