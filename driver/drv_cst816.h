/**
 * @file    drv_cst816.h
 * @brief   CST816 电容触摸驱动 - 用于TK013F2261屏幕
 * @author  智能水冷项目
 * @date    2025-01-26
 */

#ifndef __DRV_CST816_H
#define __DRV_CST816_H

#include "ch32v30x.h"

/*============================ 宏定义 ============================*/

#define CST816_ADDR     0x15    /* CST816 7位I2C地址 */

/*============================ 触摸数据结构 ============================*/

typedef struct {
    uint8_t  gesture_id;    /* 手势ID */
    uint8_t  touch_num;     /* 触摸点数 */
    uint16_t x;             /* X坐标 */
    uint16_t y;             /* Y坐标 */
} cst816_touch_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化CST816触摸芯片
 * @return 0-成功  其他-失败
 */
uint8_t cst816_init(void);

/**
 * @brief  读取触摸数据
 * @param  touch: 触摸数据结构指针
 * @return 0-无触摸  1-有触摸
 */
uint8_t cst816_read_touch(cst816_touch_t *touch);

/**
 * @brief  检测CST816是否存在
 * @return 0-存在  其他-不存在
 */
uint8_t cst816_check(void);

#endif /* __DRV_CST816_H */
