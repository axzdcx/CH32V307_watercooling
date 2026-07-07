/**
 * @file    app_ui.c
 * @brief   多页面触摸交互UI - 1.3寸屏幕（240x240）
 * @author  智能水冷项目
 * @date    2025-01-26
 */

#include "app_ui.h"
#include "ui_framework.h"
#include "drv_st7789.h"
#include "drv_cst816.h"
#include "app_pwm.h"
#include "app_websocket.h"
#include "ws_json.h"
#include "scheduler.h"
#include <stdio.h>

extern uint32_t get_tick_ms(void);  /* 系统毫秒时间基准 */

/*============================ 科技蓝水冷风格配色 ============================*/
/* 
 * 主题：CPU水冷散热科技风
 * 背景：深蓝色调，象征冷却
 * 高亮：青色/水蓝色，象征水流
 * 温度：蓝→绿→橙→红 渐变
 */
#define COOL_BG         0x0011      /* 深蓝背景 RGB(0,0,17) */
#define COOL_BG_DARK    0x0008      /* 更深蓝 */
#define COOL_CARD       0x1926      /* 深蓝卡片 RGB(24,36,48) */
#define COOL_CARD_LIGHT 0x2A4D      /* 浅蓝卡片（选中态）*/
#define COOL_TEXT       0xDFFF      /* 冷白文字 */
#define COOL_TEXT_DIM   0x8C71      /* 暗淡文字 */
#define COOL_CYAN       0x07FF      /* 水冷青（主色调）*/
#define COOL_BLUE       0x04BF      /* 深水蓝 */
#define COOL_AQUA       0x05FF      /* 浅水蓝 */
#define COOL_GREEN      0x07E0      /* 正常绿 */
#define COOL_YELLOW     0xFFE0      /* 警告黄 */
#define COOL_ORANGE     0xFC00      /* 高温橙 */
#define COOL_RED        0xF800      /* 危险红 */
#define COOL_PURPLE     0x781F      /* 紫色（性能模式）*/

/* 温度颜色映射 */
#define TEMP_COLOR_COLD     COOL_BLUE       /* <40°C 冷 */
#define TEMP_COLOR_NORMAL   COOL_CYAN       /* 40-60°C 正常 */
#define TEMP_COLOR_WARM     COOL_GREEN      /* 60-70°C 温暖 */
#define TEMP_COLOR_HOT      COOL_ORANGE     /* 70-80°C 热 */
#define TEMP_COLOR_DANGER   COOL_RED        /* >80°C 危险 */

/*============================ 触摸参数 ============================*/
#define SWIPE_THRESHOLD     30      /* 滑动阈值（像素）*/
#define TAP_THRESHOLD       10      /* 点击阈值（像素）*/

/* UI刷新节流，避免频繁重绘导致卡顿 */
#define UI_REFRESH_MS       200

/*============================ 8x8字体数据（外部引用）============================*/
extern const uint8_t font_8x8[128][8];

/*============================ 私有变量 ============================*/
static system_data_t last_data = {0};
static ui_page_t current_page = PAGE_MAIN;
static ui_ctrl_mode_t current_mode = MODE_AUTO;
static float target_temp = 60.0f;
static uint8_t touch_initialized = 0;

/* 触摸状态 */
static uint8_t touch_down = 0;
static uint16_t touch_start_x = 0;
static uint16_t touch_start_y = 0;
static touch_event_t last_event = TOUCH_NONE;
static uint32_t last_ui_refresh = 0;

/*============================ 放大字符绘制（正确的像素放大）============================*/

/**
 * @brief  绘制放大字符（像素级放大）
 * @param  x, y: 起始坐标
 * @param  ch: 字符
 * @param  color: 颜色
 * @param  scale: 放大倍数（1=8x8, 2=16x16, 3=24x24, 4=32x32）
 */
static void draw_char_scaled(uint16_t x, uint16_t y, char ch, uint16_t color, uint8_t scale)
{
    if(ch < 32 || ch > 127) return;

    /* 字体数组从索引0存储空格(ASCII 32)，所以要减32 */
    const uint8_t *font_data = font_8x8[(uint8_t)ch - 32];

    for(uint8_t row = 0; row < 8; row++)
    {
        uint8_t line = font_data[row];
        for(uint8_t col = 0; col < 8; col++)
        {
            if(line & (0x80 >> col))
            {
                /* 每个像素点放大成 scale x scale 的方块 */
                uint16_t px = x + col * scale;
                uint16_t py = y + row * scale;
                st7789_fill_color(px, py, px + scale - 1, py + scale - 1, color);
            }
        }
    }
}

/**
 * @brief  绘制放大字符串
 */
static void draw_string_scaled(uint16_t x, uint16_t y, const char *str, uint16_t color, uint8_t scale)
{
    uint16_t cur_x = x;
    while(*str)
    {
        draw_char_scaled(cur_x, y, *str, color, scale);
        cur_x += 8 * scale;  /* 字符间距 = 8 * 放大倍数 */
        str++;
    }
}

/**
 * @brief  绘制放大数字
 */
static void draw_number_scaled(uint16_t x, uint16_t y, int num, uint16_t color, uint8_t scale)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", num);
    draw_string_scaled(x, y, buf, color, scale);
}

/**
 * @brief  计算数字的像素宽度
 */
static uint16_t get_number_width(int num, uint8_t scale)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", num);
    uint16_t len = 0;
    const char *p = buf;
    while(*p) { len++; p++; }
    return len * 8 * scale;
}

/*============================ 主页绘制 ============================*/

/**
 * @brief  根据温度获取对应颜色
 */
static uint16_t get_temp_color(float temp)
{
    if(temp < 40.0f)      return TEMP_COLOR_COLD;     /* 冷 - 蓝色 */
    else if(temp < 55.0f) return TEMP_COLOR_NORMAL;   /* 正常 - 青色 */
    else if(temp < 70.0f) return TEMP_COLOR_WARM;     /* 温暖 - 绿色 */
    else if(temp < 80.0f) return TEMP_COLOR_HOT;      /* 热 - 橙色 */
    else                  return TEMP_COLOR_DANGER;   /* 危险 - 红色 */
}

/**
 * @brief  绘制主界面（科技蓝水冷风格）
 *
 * 布局设计（240x240屏幕）：
 * ┌────────────────────────────┐
 * │        CPU TEMP            │ <- 标签
 * │                            │
 * │          52°C              │ <- 超大温度（4倍=32x32）
 * │                            │
 * ├────────────────────────────┤
 * │   PUMP 70%  │  FAN 50%     │ <- 状态卡片
 * ├────────────────────────────┤
 * │  HEALTH 92     [=====] OK  │ <- 健康度
 * └────────────────────────────┘
 */
void ui_draw_main_page(system_data_t *data)
{
    uint8_t ws_online;
    uint8_t tinyml_ext;

    if(data == NULL) return;

    /* 清屏 */
    st7789_clear(COOL_BG);

    /*==================== 区域1：温度卡片（紧凑版） ====================*/
    /* 卡片背景 */
    st7789_fill_color(10, 10, 229, 100, COOL_CARD);

    /* 标签 "CPU TEMP"（1倍=8x8） */
    draw_string_scaled(20, 18, "CPU TEMP", COOL_TEXT_DIM, 1);

    /* 温度数字（3倍=24x24像素）- 颜色随温度变化 */
    int temp = (int)data->cpu_temp;
    uint16_t temp_color = get_temp_color(data->cpu_temp);
    uint16_t temp_width = get_number_width(temp, 3);
    uint16_t temp_x = (240 - temp_width - 24) / 2;  /* 居中，留出°C的位置 */
    draw_number_scaled(temp_x, 48, temp, temp_color, 3);

    /* 度数符号 "C"（2倍=16x16）*/
    draw_char_scaled(temp_x + temp_width + 4, 48, 'o', COOL_TEXT_DIM, 1);
    draw_char_scaled(temp_x + temp_width + 14, 52, 'C', COOL_TEXT_DIM, 2);

    /*==================== 区域2：状态卡片（两列，紧凑版）====================*/
    /* 左卡片：水泵 */
    st7789_fill_color(10, 112, 114, 160, COOL_CARD);
    draw_string_scaled(16, 118, "PUMP", COOL_TEXT_DIM, 1);
    draw_number_scaled(20, 132, data->pump_speed, COOL_BLUE, 2);
    draw_char_scaled(20 + get_number_width(data->pump_speed, 2) + 2, 132, '%', COOL_BLUE, 2);

    /* 右卡片：风扇 */
    st7789_fill_color(125, 112, 229, 160, COOL_CARD);
    draw_string_scaled(135, 118, "FAN", COOL_TEXT_DIM, 1);
    draw_number_scaled(135, 132, data->fan_speed, COOL_GREEN, 2);
    draw_char_scaled(135 + get_number_width(data->fan_speed, 2) + 2, 132, '%', COOL_GREEN, 2);

    /*==================== 区域3：健康度 ====================*/
    st7789_fill_color(10, 170, 229, 205, COOL_CARD);

    /* 标签 */
    draw_string_scaled(18, 178, "HP", COOL_TEXT_DIM, 1);

    /* 健康度数字 */
    draw_number_scaled(40, 176, data->health_score, COOL_GREEN, 2);

    /* 进度条 */
    ui_draw_progress_bar(90, 178, 90, 10, data->health_score, COOL_GREEN);

    /* 状态 */
    if(data->is_normal)
    {
        draw_string_scaled(190, 176, "OK", COOL_GREEN, 2);
    }
    else
    {
        draw_string_scaled(190, 176, "!!", COOL_RED, 2);
    }

    /*==================== 区域4：演示状态条（通信+故障） ====================*/
    ws_online = websocket_is_connected();
    tinyml_ext = json_is_tinyml_ext_enabled();

    st7789_fill_color(10, 208, 229, 220, COOL_CARD);
    draw_string_scaled(18, 211, "WS", COOL_TEXT_DIM, 1);
    draw_string_scaled(36, 211, ws_online ? "ON" : "OFF",
                       ws_online ? COOL_GREEN : COOL_YELLOW, 1);

    draw_string_scaled(86, 211, "AI", COOL_TEXT_DIM, 1);
    draw_string_scaled(104, 211, tinyml_ext ? "EXT" : "BASE",
                       tinyml_ext ? COOL_CYAN : COOL_TEXT_DIM, 1);

    draw_string_scaled(162, 211, "F", COOL_TEXT_DIM, 1);
    draw_number_scaled(174, 209, data->fault_count,
                       (data->fault_count > 0U) ? COOL_RED : COOL_GREEN, 1);
    draw_string_scaled(200, 211, data->is_normal ? "OK" : "NG",
                       data->is_normal ? COOL_GREEN : COOL_RED, 1);

    /* 保存当前数据 */
    last_data = *data;
}

/**
 * @brief  局部更新（避免全屏刷新闪烁）
 */
void ui_update_main_page(system_data_t *data)
{
    if(data == NULL) return;

    /* 温度变化 */
    if((int)data->cpu_temp != (int)last_data.cpu_temp)
    {
        /* 清除温度区域 */
        st7789_fill_color(30, 40, 200, 80, COOL_CARD);

        /* 重绘温度 - 动态颜色 */
        int temp = (int)data->cpu_temp;
        uint16_t temp_color = get_temp_color(data->cpu_temp);
        uint16_t temp_width = get_number_width(temp, 3);
        uint16_t temp_x = (240 - temp_width - 24) / 2;
        draw_number_scaled(temp_x, 48, temp, temp_color, 3);
        draw_char_scaled(temp_x + temp_width + 4, 48, 'o', COOL_TEXT_DIM, 1);
        draw_char_scaled(temp_x + temp_width + 14, 52, 'C', COOL_TEXT_DIM, 2);
    }

    /* 水泵转速变化 */
    if(data->pump_speed != last_data.pump_speed)
    {
        st7789_fill_color(20, 128, 105, 155, COOL_CARD);
        draw_number_scaled(20, 132, data->pump_speed, COOL_BLUE, 2);
        draw_char_scaled(20 + get_number_width(data->pump_speed, 2) + 2, 132, '%', COOL_BLUE, 2);
    }

    /* 风扇转速变化 */
    if(data->fan_speed != last_data.fan_speed)
    {
        st7789_fill_color(135, 128, 220, 155, COOL_CARD);
        draw_number_scaled(135, 132, data->fan_speed, COOL_GREEN, 2);
        draw_char_scaled(135 + get_number_width(data->fan_speed, 2) + 2, 132, '%', COOL_GREEN, 2);
    }

    /* 健康度变化 */
    if(data->health_score != last_data.health_score)
    {
        st7789_fill_color(40, 174, 90, 198, COOL_CARD);
        draw_number_scaled(40, 176, data->health_score, COOL_GREEN, 2);
        ui_draw_progress_bar(90, 178, 90, 10, data->health_score, COOL_GREEN);
    }

    /* 保存当前数据 */
    last_data = *data;
}

/*============================ 详情页 ============================*/

/**
 * @brief  绘制详情页 - 所有传感器数据
 */
static void draw_detail_page(system_data_t *data)
{
    st7789_clear(COOL_BG);

    /* 页面标题 */
    draw_string_scaled(78, 5, "DIAG", COOL_TEXT, 2);

    /* 页面指示器 */
    draw_string_scaled(108, 225, "2/4", COOL_TEXT_DIM, 2);

    /* 卡片1: 温度 */
    st7789_fill_color(10, 30, 115, 105, COOL_CARD);
    draw_string_scaled(20, 38, "TEMP", COOL_TEXT_DIM, 1);
    draw_string_scaled(20, 52, "CPU", COOL_TEXT, 1);
    draw_number_scaled(55, 50, (int)data->cpu_temp, COOL_ORANGE, 2);
    draw_string_scaled(20, 72, "H2O", COOL_TEXT, 1);
    draw_number_scaled(55, 70, (int)data->water_temp, COOL_CYAN, 2);
    draw_string_scaled(20, 92, "MCU", COOL_TEXT, 1);
    draw_number_scaled(55, 90, (int)data->mcu_temp, COOL_GREEN, 2);

    /* 卡片2: 流量压力 */
    st7789_fill_color(125, 30, 230, 105, COOL_CARD);
    draw_string_scaled(135, 38, "FLOW", COOL_TEXT_DIM, 1);
    draw_number_scaled(135, 55, (int)(data->flow_rate * 10), COOL_BLUE, 2);
    draw_string_scaled(180, 58, "L/m", COOL_TEXT_DIM, 1);
    draw_string_scaled(135, 78, "PRES", COOL_TEXT_DIM, 1);
    draw_number_scaled(135, 90, (int)(data->pressure * 1000), COOL_PURPLE, 2);
    draw_string_scaled(185, 93, "kPa", COOL_TEXT_DIM, 1);

    /* 卡片3: 水质电流 */
    st7789_fill_color(10, 115, 115, 175, COOL_CARD);
    draw_string_scaled(20, 123, "WATER", COOL_TEXT_DIM, 1);
    draw_string_scaled(20, 138, "TDS", COOL_TEXT, 1);
    draw_number_scaled(50, 136, data->tds_ppm, COOL_CYAN, 2);
    draw_string_scaled(20, 158, "mA", COOL_TEXT, 1);
    draw_number_scaled(50, 156, (int)data->current_ma, COOL_ORANGE, 2);

    /* 卡片4: 振动功耗 */
    st7789_fill_color(125, 115, 230, 175, COOL_CARD);
    draw_string_scaled(135, 123, "POWER", COOL_TEXT_DIM, 1);
    draw_number_scaled(135, 140, (int)data->power_w, COOL_RED, 2);
    draw_string_scaled(175, 143, "W", COOL_TEXT_DIM, 1);
    draw_string_scaled(135, 158, "VIB", COOL_TEXT_DIM, 1);
    draw_number_scaled(170, 156, (int)(data->vibration * 100), COOL_GREEN, 2);

    /* 底部状态栏 */
    st7789_fill_color(10, 185, 230, 215, COOL_CARD);
    draw_string_scaled(18, 193, "HEALTH", COOL_TEXT_DIM, 1);
    draw_number_scaled(74, 190, data->health_score, COOL_GREEN, 2);
    draw_string_scaled(120, 193, "FAULT", COOL_TEXT_DIM, 1);
    draw_number_scaled(166, 190, data->fault_count,
                       (data->fault_count > 0U) ? COOL_RED : COOL_GREEN, 2);
    draw_string_scaled(206, 193, data->is_normal ? "OK" : "NG",
                       data->is_normal ? COOL_GREEN : COOL_RED, 1);
}

/*============================ 控制页 ============================*/

static const char* mode_names[] = {"AUTO", "QUIET", "BAL", "PERF", "MAN"};
static const uint16_t mode_colors[] = {COOL_BLUE, COOL_GREEN, COOL_CYAN, COOL_RED, COOL_ORANGE};

/**
 * @brief  绘制控制页 - 模式切换
 */
static void draw_control_page(system_data_t *data)
{
    uint8_t ws_online = websocket_is_connected();
    uint8_t tinyml_ext = json_is_tinyml_ext_enabled();

    st7789_clear(COOL_BG);

    /* 页面标题 */
    draw_string_scaled(45, 5, "COMM/MODE", COOL_TEXT, 2);

    /* 页面指示器 */
    draw_string_scaled(108, 225, "3/4", COOL_TEXT_DIM, 2);

    /* 当前模式大显示 */
    st7789_fill_color(10, 30, 230, 90, COOL_CARD);
    draw_string_scaled(20, 40, "MODE", COOL_TEXT_DIM, 2);
    draw_string_scaled(60, 60, mode_names[current_mode], mode_colors[current_mode], 3);

    /* 模式按钮（5个）*/
    for(int i = 0; i < 5; i++)
    {
        uint16_t btn_x = 10 + (i % 3) * 75;
        uint16_t btn_y = 100 + (i / 3) * 45;
        uint16_t btn_color = (i == current_mode) ? mode_colors[i] : COOL_CARD;

        st7789_fill_color(btn_x, btn_y, btn_x + 65, btn_y + 38, btn_color);
        uint16_t text_color = (i == current_mode) ? COOL_BG : COOL_TEXT;
        draw_string_scaled(btn_x + 8, btn_y + 12, mode_names[i], text_color, 1);
    }

    /* 底部状态条：通信状态 + 手动模式提示 */
    st7789_fill_color(10, 195, 230, 220, COOL_CARD);
    draw_string_scaled(20, 201, "WS", COOL_TEXT_DIM, 1);
    draw_string_scaled(38, 201, ws_online ? "ON" : "OFF",
                       ws_online ? COOL_GREEN : COOL_YELLOW, 1);
    draw_string_scaled(82, 201, "AI", COOL_TEXT_DIM, 1);
    draw_string_scaled(100, 201, tinyml_ext ? "EXT" : "BASE",
                       tinyml_ext ? COOL_CYAN : COOL_TEXT_DIM, 1);
    if(current_mode == MODE_MANUAL)
    {
        draw_string_scaled(156, 201, "P", COOL_TEXT_DIM, 1);
        draw_number_scaled(168, 199, data->pump_speed, COOL_BLUE, 1);
        draw_string_scaled(194, 201, "F", COOL_TEXT_DIM, 1);
        draw_number_scaled(206, 199, data->fan_speed, COOL_GREEN, 1);
    }
    else
    {
        draw_string_scaled(156, 201, "TAP MODE", COOL_TEXT_DIM, 1);
    }
}

/*============================ 设置页 ============================*/

/**
 * @brief  绘制设置页 - 目标温度等
 */
static void draw_settings_page(system_data_t *data)
{
    uint8_t ws_online = websocket_is_connected();
    uint8_t tinyml_ext = json_is_tinyml_ext_enabled();

    st7789_clear(COOL_BG);

    /* 页面标题 */
    draw_string_scaled(55, 5, "SETTINGS", COOL_TEXT, 2);

    /* 页面指示器 */
    draw_string_scaled(108, 225, "4/4", COOL_TEXT_DIM, 2);

    /* 目标温度设置 */
    st7789_fill_color(10, 35, 230, 110, COOL_CARD);
    draw_string_scaled(20, 45, "TARGET TEMP", COOL_TEXT_DIM, 2);

    /* - 按钮 */
    st7789_fill_color(30, 70, 70, 100, COOL_BLUE);
    draw_string_scaled(45, 78, "-", COOL_TEXT, 2);

    /* 温度值 */
    draw_number_scaled(90, 70, (int)target_temp, COOL_TEXT, 3);
    draw_string_scaled(145, 78, "C", COOL_TEXT_DIM, 2);

    /* + 按钮 */
    st7789_fill_color(170, 70, 210, 100, COOL_RED);
    draw_string_scaled(183, 78, "+", COOL_TEXT, 2);

    /* 当前状态信息 */
    st7789_fill_color(10, 120, 230, 180, COOL_CARD);
    draw_string_scaled(20, 128, "STATUS", COOL_TEXT_DIM, 2);

    draw_string_scaled(20, 150, "CPU", COOL_TEXT, 1);
    draw_number_scaled(55, 148, (int)data->cpu_temp, COOL_ORANGE, 2);

    draw_string_scaled(110, 150, "ERR", COOL_TEXT, 1);
    int error = (int)(data->cpu_temp - target_temp);
    uint16_t err_color = (error > 5) ? COOL_RED : ((error < -5) ? COOL_BLUE : COOL_GREEN);
    draw_number_scaled(145, 148, error, err_color, 2);

    draw_string_scaled(20, 168, "FAULTS", COOL_TEXT, 1);
    draw_number_scaled(80, 166, data->fault_count, 
                       data->fault_count > 0 ? COOL_RED : COOL_GREEN, 2);

    draw_string_scaled(120, 168, "P", COOL_TEXT_DIM, 1);
    draw_number_scaled(132, 166, data->pump_speed, COOL_BLUE, 2);
    draw_string_scaled(166, 168, "F", COOL_TEXT_DIM, 1);
    draw_number_scaled(178, 166, data->fan_speed, COOL_GREEN, 2);

    /* 底部状态栏：模式 + 通信状态 */
    st7789_fill_color(10, 190, 230, 218, COOL_CARD);
    draw_string_scaled(18, 198, "MODE", COOL_TEXT_DIM, 1);
    draw_string_scaled(58, 198, mode_names[current_mode], mode_colors[current_mode], 1);
    draw_string_scaled(124, 198, "WS", COOL_TEXT_DIM, 1);
    draw_string_scaled(142, 198, ws_online ? "ON" : "OFF",
                       ws_online ? COOL_GREEN : COOL_YELLOW, 1);
    draw_string_scaled(178, 198, "AI", COOL_TEXT_DIM, 1);
    draw_string_scaled(196, 198, tinyml_ext ? "EXT" : "BASE",
                       tinyml_ext ? COOL_CYAN : COOL_TEXT_DIM, 1);
}

/*============================ 触摸处理 ============================*/

static void handle_tap(uint16_t x, uint16_t y);

/**
 * @brief  处理触摸事件
 */
void ui_process_touch(void)
{
    cst816_touch_t touch;
    last_event = TOUCH_NONE;

    if(!touch_initialized) return;

    if(cst816_read_touch(&touch))
    {
        if(!touch_down)
        {
            /* 触摸开始 */
            touch_down = 1;
            touch_start_x = touch.x;
            touch_start_y = touch.y;
        }
    }
    else
    {
        if(touch_down)
        {
            /* 触摸结束，计算手势 */
            touch_down = 0;
            int16_t dx = 0;
            int16_t dy = 0;

            /* 使用CST816的gesture_id（如果支持）或自己计算 */
            cst816_touch_t end_touch;
            if(cst816_read_touch(&end_touch))
            {
                dx = (int16_t)end_touch.x - (int16_t)touch_start_x;
                dy = (int16_t)end_touch.y - (int16_t)touch_start_y;
            }

            /* 判断手势 */
            int abs_dx = (dx > 0) ? dx : -dx;
            int abs_dy = (dy > 0) ? dy : -dy;

            if(abs_dx > SWIPE_THRESHOLD && abs_dx > abs_dy)
            {
                last_event = (dx < 0) ? TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT;
            }
            else if(abs_dy > SWIPE_THRESHOLD && abs_dy > abs_dx)
            {
                last_event = (dy < 0) ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
            }
            else if(abs_dx < TAP_THRESHOLD && abs_dy < TAP_THRESHOLD)
            {
                last_event = TOUCH_TAP;
            }

            /* 处理页面切换 */
            if(last_event == TOUCH_SWIPE_LEFT)
            {
                ui_next_page();
            }
            else if(last_event == TOUCH_SWIPE_RIGHT)
            {
                ui_prev_page();
            }
            else if(last_event == TOUCH_TAP)
            {
                /* 处理按钮点击 */
                handle_tap(touch_start_x, touch_start_y);
            }
        }
    }
}

/**
 * @brief  处理点击事件
 */
static void handle_tap(uint16_t x, uint16_t y)
{
    if(current_page == PAGE_CONTROL)
    {
        /* 模式按钮检测 */
        for(int i = 0; i < 5; i++)
        {
            uint16_t btn_x = 10 + (i % 3) * 75;
            uint16_t btn_y = 100 + (i / 3) * 45;

            if(x >= btn_x && x <= btn_x + 65 && y >= btn_y && y <= btn_y + 38)
            {
                ui_set_mode((ui_ctrl_mode_t)i);
                ui_draw_current_page(&last_data);
                return;
            }
        }
    }
    else if(current_page == PAGE_SETTINGS)
    {
        /* - 按钮 */
        if(x >= 30 && x <= 70 && y >= 70 && y <= 100)
        {
            ui_adjust_target_temp(-1);
            ui_draw_current_page(&last_data);
        }
        /* + 按钮 */
        else if(x >= 170 && x <= 210 && y >= 70 && y <= 100)
        {
            ui_adjust_target_temp(1);
            ui_draw_current_page(&last_data);
        }
    }
}

/*============================ 页面管理 ============================*/

void ui_init(void)
{
    /* 初始化LCD控制器与SPI总线 */
    st7789_init();

    st7789_clear(COOL_BG);

    /* 初始化触摸 */
    if(cst816_init() == 0)
    {
        touch_initialized = 1;
        printf("[UI] 触摸初始化成功\r\n");
    }
    else
    {
        touch_initialized = 0;
        printf("[UI] 触摸初始化失败，仅显示模式\r\n");
    }

    current_page = PAGE_MAIN;

    /* 上电后先绘制一次完整主页，避免仅做局部更新导致黑屏 */
    ui_draw_current_page(&last_data);
}

void ui_draw_current_page(system_data_t *data)
{
    if(data == NULL) return;

    switch(current_page)
    {
        case PAGE_MAIN:
            ui_draw_main_page(data);
            /* 添加页面指示器 */
            draw_string_scaled(108, 225, "1/4", COOL_TEXT_DIM, 2);
            break;
        case PAGE_DETAIL:
            draw_detail_page(data);
            break;
        case PAGE_CONTROL:
            draw_control_page(data);
            break;
        case PAGE_SETTINGS:
            draw_settings_page(data);
            break;
        default:
            break;
    }

    last_data = *data;
}

void ui_update_current_page(system_data_t *data)
{
    if(data == NULL) return;

    if(get_tick_ms() - last_ui_refresh < UI_REFRESH_MS)
    {
        return;
    }

    /* 只有主页支持局部更新，其他页面全量刷新 */
    if(current_page == PAGE_MAIN)
    {
        ui_update_main_page(data);
    }
    else
    {
        /* 检查是否有数据变化 */
        if(data->cpu_temp != last_data.cpu_temp ||
           data->health_score != last_data.health_score ||
           data->pump_speed != last_data.pump_speed)
        {
            ui_draw_current_page(data);
        }
    }

    last_ui_refresh = get_tick_ms();
}

void ui_set_page(ui_page_t page)
{
    if(page < PAGE_COUNT)
    {
        current_page = page;
        if (scheduler_is_verbose_log_enabled()) {
            printf("[UI] 切换到页面 %d\r\n", page);
        }
    }
}

ui_page_t ui_get_page(void)
{
    return current_page;
}

void ui_next_page(void)
{
    current_page = (ui_page_t)((current_page + 1) % PAGE_COUNT);
    ui_draw_current_page(&last_data);
    if (scheduler_is_verbose_log_enabled()) {
        printf("[UI] -> 页面 %d\r\n", current_page);
    }
}

void ui_prev_page(void)
{
    current_page = (ui_page_t)((current_page + PAGE_COUNT - 1) % PAGE_COUNT);
    ui_draw_current_page(&last_data);
    if (scheduler_is_verbose_log_enabled()) {
        printf("[UI] <- 页面 %d\r\n", current_page);
    }
}

/*============================ 控制接口 ============================*/

void ui_set_mode(ui_ctrl_mode_t mode)
{
    if(mode < MODE_COUNT)
    {
        current_mode = mode;
        if (scheduler_is_verbose_log_enabled()) {
            printf("[UI] 模式切换: %s\r\n", mode_names[mode]);
        }

        /* 联动PWM控制 */
        switch(mode)
        {
            case MODE_AUTO:
                pwm_set_mode(PWM_MODE_AUTO);
                break;
            case MODE_SILENT:
                pwm_set_mode(PWM_MODE_MANUAL);
                pwm_set_pump(50);
                pwm_set_fan(30);
                break;
            case MODE_BALANCE:
                pwm_set_mode(PWM_MODE_MANUAL);
                pwm_set_pump(70);
                pwm_set_fan(50);
                break;
            case MODE_PERFORMANCE:
                pwm_set_mode(PWM_MODE_MANUAL);
                pwm_set_pump(90);
                pwm_set_fan(80);
                break;
            case MODE_MANUAL:
                pwm_set_mode(PWM_MODE_MANUAL);
                break;
            default:
                break;
        }
    }
}

ui_ctrl_mode_t ui_get_mode(void)
{
    return current_mode;
}

void ui_adjust_target_temp(int8_t delta)
{
    target_temp += delta;
    if(target_temp < 30.0f) target_temp = 30.0f;
    if(target_temp > 90.0f) target_temp = 90.0f;
    if (scheduler_is_verbose_log_enabled()) {
        printf("[UI] 目标温度: %.0f\r\n", target_temp);
    }
}

touch_event_t ui_get_touch_event(void)
{
    return last_event;
}
