/**
 * @file    drv_adxl345.h
 * @brief   ADXL345 三轴加速度计驱动头文件
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * 应用场景：
 *   - 检测水泵振动强度
 *   - 异常振动预警（轴承磨损、叶轮损坏）
 *   - 设备健康评分
 *
 * 硬件连接（I2C模式）：
 *   - VCC → 3.3V
 *   - GND → GND
 *   - SCL → PB6 (I2C1_SCL)
 *   - SDA → PB7 (I2C1_SDA)
 *   - CS  → 3.3V (I2C模式)
 *   - SDO → GND  (I2C地址: 0x53)
 */

#ifndef __DRV_ADXL345_H
#define __DRV_ADXL345_H

#include "ch32v30x.h"

/*============================ I2C地址 ============================*/

#define ADXL345_I2C_ADDR        0x53    /* SDO接GND时的地址 */
#define ADXL345_I2C_ADDR_ALT    0x1D    /* SDO接VCC时的地址 */

/*============================ 寄存器地址 ============================*/

#define ADXL345_REG_DEVID       0x00    /* 设备ID (固定值0xE5) */
#define ADXL345_REG_THRESH_TAP  0x1D    /* 敲击阈值 */
#define ADXL345_REG_OFSX        0x1E    /* X轴偏移 */
#define ADXL345_REG_OFSY        0x1F    /* Y轴偏移 */
#define ADXL345_REG_OFSZ        0x20    /* Z轴偏移 */
#define ADXL345_REG_DUR         0x21    /* 敲击持续时间 */
#define ADXL345_REG_LATENT      0x22    /* 敲击延迟 */
#define ADXL345_REG_WINDOW      0x23    /* 敲击窗口 */
#define ADXL345_REG_THRESH_ACT  0x24    /* 活动阈值 */
#define ADXL345_REG_THRESH_INACT 0x25   /* 静止阈值 */
#define ADXL345_REG_TIME_INACT  0x26    /* 静止时间 */
#define ADXL345_REG_ACT_INACT_CTL 0x27  /* 活动/静止控制 */
#define ADXL345_REG_THRESH_FF   0x28    /* 自由落体阈值 */
#define ADXL345_REG_TIME_FF     0x29    /* 自由落体时间 */
#define ADXL345_REG_TAP_AXES    0x2A    /* 敲击轴 */
#define ADXL345_REG_ACT_TAP_STATUS 0x2B /* 敲击状态 */
#define ADXL345_REG_BW_RATE     0x2C    /* 数据速率和功耗模式 */
#define ADXL345_REG_POWER_CTL   0x2D    /* 电源控制 */
#define ADXL345_REG_INT_ENABLE  0x2E    /* 中断使能 */
#define ADXL345_REG_INT_MAP     0x2F    /* 中断映射 */
#define ADXL345_REG_INT_SOURCE  0x30    /* 中断源 */
#define ADXL345_REG_DATA_FORMAT 0x31    /* 数据格式 */
#define ADXL345_REG_DATAX0      0x32    /* X轴数据低字节 */
#define ADXL345_REG_DATAX1      0x33    /* X轴数据高字节 */
#define ADXL345_REG_DATAY0      0x34    /* Y轴数据低字节 */
#define ADXL345_REG_DATAY1      0x35    /* Y轴数据高字节 */
#define ADXL345_REG_DATAZ0      0x36    /* Z轴数据低字节 */
#define ADXL345_REG_DATAZ1      0x37    /* Z轴数据高字节 */
#define ADXL345_REG_FIFO_CTL    0x38    /* FIFO控制 */
#define ADXL345_REG_FIFO_STATUS 0x39    /* FIFO状态 */

/*============================ 寄存器位定义 ============================*/

/* POWER_CTL 寄存器 */
#define ADXL345_PCTL_MEASURE    (1 << 3)  /* 测量模式 */
#define ADXL345_PCTL_SLEEP      (1 << 2)  /* 睡眠模式 */
#define ADXL345_PCTL_WAKEUP_8HZ (0 << 0)  /* 唤醒频率 8Hz */

/* DATA_FORMAT 寄存器 */
#define ADXL345_DFORMAT_SELF_TEST  (1 << 7)  /* 自检 */
#define ADXL345_DFORMAT_SPI        (1 << 6)  /* SPI模式 */
#define ADXL345_DFORMAT_INT_INVERT (1 << 5)  /* 中断极性反转 */
#define ADXL345_DFORMAT_FULL_RES   (1 << 3)  /* 全分辨率模式 */
#define ADXL345_DFORMAT_JUSTIFY    (1 << 2)  /* 左对齐 */
#define ADXL345_DFORMAT_RANGE_2G   (0 << 0)  /* ±2g */
#define ADXL345_DFORMAT_RANGE_4G   (1 << 0)  /* ±4g */
#define ADXL345_DFORMAT_RANGE_8G   (2 << 0)  /* ±8g */
#define ADXL345_DFORMAT_RANGE_16G  (3 << 0)  /* ±16g */

/* BW_RATE 寄存器 */
#define ADXL345_RATE_3200HZ     0x0F
#define ADXL345_RATE_1600HZ     0x0E
#define ADXL345_RATE_800HZ      0x0D
#define ADXL345_RATE_400HZ      0x0C
#define ADXL345_RATE_200HZ      0x0B
#define ADXL345_RATE_100HZ      0x0A
#define ADXL345_RATE_50HZ       0x09
#define ADXL345_RATE_25HZ       0x08

/*============================ 配置参数 ============================*/

#define ADXL345_SCALE_FACTOR    0.0039f   /* ±2g全分辨率模式：3.9mg/LSB */

/* 振动阈值 */
#define ADXL345_VIBRATION_NORMAL_MAX  0.5f   /* 正常振动上限 (g) */
#define ADXL345_VIBRATION_WARNING     1.0f   /* 振动警告阈值 (g) */
#define ADXL345_VIBRATION_ALARM       2.0f   /* 振动报警阈值 (g) */

/*============================ 数据结构 ============================*/

/**
 * @brief  三轴加速度数据
 */
typedef struct {
    int16_t x_raw;          /* X轴原始数据 */
    int16_t y_raw;          /* Y轴原始数据 */
    int16_t z_raw;          /* Z轴原始数据 */

    float   x_g;            /* X轴加速度 (g) */
    float   y_g;            /* Y轴加速度 (g) */
    float   z_g;            /* Z轴加速度 (g) */

    float   magnitude;      /* 合成加速度幅值 (g) */
    float   vibration;      /* 振动强度 (去除重力后) */

    uint8_t valid;          /* 数据有效标志 */
    uint8_t alarm_warning;  /* 振动警告 */
    uint8_t alarm_critical; /* 振动严重报警 */
} adxl345_data_t;

/**
 * @brief  ADXL345 配置结构体
 */
typedef struct {
    uint8_t range;          /* 测量范围: 2g/4g/8g/16g */
    uint8_t rate;           /* 采样率 */
    uint8_t full_res;       /* 全分辨率模式 */
} adxl345_config_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化 ADXL345
 * @return 0=成功, -1=失败
 */
int8_t adxl345_init(void);

/**
 * @brief  读取设备ID
 * @return 设备ID (应该返回 0xE5)
 */
uint8_t adxl345_read_device_id(void);

/**
 * @brief  配置 ADXL345
 * @param  config: 配置结构体指针
 * @return 0=成功, -1=失败
 */
int8_t adxl345_config(adxl345_config_t *config);

/**
 * @brief  读取加速度数据
 * @param  data: 数据结构指针
 * @return 0=成功, -1=失败
 */
int8_t adxl345_read_accel(adxl345_data_t *data);

/**
 * @brief  读取原始数据
 * @param  x: X轴数据指针
 * @param  y: Y轴数据指针
 * @param  z: Z轴数据指针
 * @return 0=成功, -1=失败
 */
int8_t adxl345_read_raw(int16_t *x, int16_t *y, int16_t *z);

/**
 * @brief  计算振动强度
 * @param  data: 数据结构指针
 * @return 振动强度 (g)
 */
float adxl345_calc_vibration(adxl345_data_t *data);

/**
 * @brief  自检
 * @return 0=成功, -1=失败
 */
int8_t adxl345_self_test(void);

#endif /* __DRV_ADXL345_H */
