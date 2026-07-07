/**
 * @file    ui_framework.h
 * @brief   轻量级UI框架 - 无LVGL自绘UI
 * @author  智能水冷项目
 * @date    2025-01-26
 */

#ifndef __UI_FRAMEWORK_H
#define __UI_FRAMEWORK_H

#include "ch32v30x.h"
#include "drv_st7789.h"

/*============================ 颜色定义 ============================*/
/* 使用ST7789已定义的颜色 */
#define UI_COLOR_BG         ST7789_BLACK      /* 背景色 */
#define UI_COLOR_TEXT       ST7789_WHITE      /* 文字颜色 */
#define UI_COLOR_PRIMARY    ST7789_BLUE       /* 主题色 */
#define UI_COLOR_SUCCESS    ST7789_GREEN      /* 成功/正常 */
#define UI_COLOR_WARNING    0xFD20            /* 橙色(RGB565) */
#define UI_COLOR_DANGER     ST7789_RED        /* 危险/错误 */
#define UI_COLOR_GRAY       0x7BEF            /* 灰色(RGB565) */

/*============================ UI元素尺寸 ============================*/
#define UI_TITLE_HEIGHT     30                /* 标题栏高度 */
#define UI_PADDING          10                /* 边距 */
#define UI_LINE_HEIGHT      20                /* 行高 */

/*============================ 基础绘图函数 ============================*/

/**
 * @brief  绘制空心矩形
 */
void ui_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief  绘制圆角矩形（填充）
 */
void ui_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief  绘制进度条
 * @param  x, y: 左上角坐标
 * @param  w, h: 宽度和高度
 * @param  percent: 百分比 0-100
 * @param  color: 进度条颜色
 */
void ui_draw_progress_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint8_t percent, uint16_t color);

/**
 * @brief  绘制数字（简单8x16字体）
 * @param  x, y: 起始坐标
 * @param  num: 要显示的数字
 * @param  color: 颜色
 */
void ui_draw_number(uint16_t x, uint16_t y, int32_t num, uint16_t color);

/**
 * @brief  绘制字符串（仅ASCII，8x16字体）
 * @param  x, y: 起始坐标
 * @param  str: 字符串
 * @param  color: 颜色
 */
void ui_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color);

/**
 * @brief  绘制分隔线
 */
void ui_draw_line_h(uint16_t x, uint16_t y, uint16_t len, uint16_t color);

#endif /* __UI_FRAMEWORK_H */
