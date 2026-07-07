/**
 * @file    app_ui.h
 * @brief   UI应用层 - 多页面触摸交互界面
 * @author  智能水冷项目
 * @date    2025-01-26
 *
 * 页面设计（240x240屏幕）：
 *   PAGE_MAIN    - 主页：温度大卡片 + 健康度 + 通信/故障摘要
 *   PAGE_DETAIL  - 诊断页：关键传感器 + 健康与故障计数
 *   PAGE_CONTROL - 通信/模式页：链路状态 + 模式切换
 *   PAGE_SETTINGS- 设置页：目标温度与状态误差
 *
 * 触摸交互：
 *   左滑 → 下一页    右滑 → 上一页    点击 → 按钮操作
 */

#ifndef __APP_UI_H
#define __APP_UI_H

#include "ch32v30x.h"

/*============================ 页面枚举 ============================*/

typedef enum {
    PAGE_MAIN = 0,      /* 主页 */
    PAGE_DETAIL,        /* 详情页 */
    PAGE_CONTROL,       /* 控制页 */
    PAGE_SETTINGS,      /* 设置页 */
    PAGE_COUNT          /* 页面总数 */
} ui_page_t;

/*============================ 控制模式 ============================*/

typedef enum {
    MODE_AUTO = 0,      /* 智能模式（AI控制）*/
    MODE_SILENT,        /* 静音模式 */
    MODE_BALANCE,       /* 均衡模式 */
    MODE_PERFORMANCE,   /* 性能模式 */
    MODE_MANUAL,        /* 手动模式 */
    MODE_COUNT
} ui_ctrl_mode_t;

/*============================ 系统数据结构 ============================*/

typedef struct {
    /* 温度数据 */
    float cpu_temp;         /* CPU温度 (°C) */
    float water_temp;       /* 水温 (°C) */
    float mcu_temp;         /* MCU内部温度 (°C) */

    /* 转速数据 */
    uint8_t pump_speed;     /* 水泵转速 (0-100%) */
    uint8_t fan_speed;      /* 风扇转速 (0-100%) */

    /* 传感器数据 */
    float flow_rate;        /* 流量 (L/min) */
    float pressure;         /* 压力 (MPa) */
    uint16_t tds_ppm;       /* TDS水质 (ppm) */
    float current_ma;       /* 电流 (mA) */
    float vibration;        /* 振动 (g) */

    /* 功耗数据 */
    float power_w;          /* 功耗 (W) */
    float energy_kwh;       /* 累计电量 (kWh) */

    /* 健康度 */
    uint8_t health_score;   /* 健康评分 (0-100) */

    /* 控制状态 */
    ui_ctrl_mode_t mode;    /* 当前模式 */
    float target_temp;      /* 目标温度 */

    /* 状态标志 */
    uint8_t is_normal;      /* 1=正常, 0=异常 */
    uint8_t fault_count;    /* 故障数量 */

} system_data_t;

/*============================ 触摸事件 ============================*/

typedef enum {
    TOUCH_NONE = 0,
    TOUCH_TAP,          /* 点击 */
    TOUCH_SWIPE_LEFT,   /* 左滑 */
    TOUCH_SWIPE_RIGHT,  /* 右滑 */
    TOUCH_SWIPE_UP,     /* 上滑 */
    TOUCH_SWIPE_DOWN,   /* 下滑 */
    TOUCH_LONG_PRESS    /* 长按 */
} touch_event_t;

/*============================ 函数声明 ============================*/

/* 初始化 */
void ui_init(void);

/* 页面绘制 */
void ui_draw_current_page(system_data_t *data);
void ui_update_current_page(system_data_t *data);

/* 页面切换 */
void ui_set_page(ui_page_t page);
ui_page_t ui_get_page(void);
void ui_next_page(void);
void ui_prev_page(void);

/* 触摸处理 */
void ui_process_touch(void);
touch_event_t ui_get_touch_event(void);

/* 控制接口 */
void ui_set_mode(ui_ctrl_mode_t mode);
ui_ctrl_mode_t ui_get_mode(void);
void ui_adjust_target_temp(int8_t delta);

/* 兼容旧接口 */
void ui_draw_main_page(system_data_t *data);
void ui_update_main_page(system_data_t *data);

#endif /* __APP_UI_H */
