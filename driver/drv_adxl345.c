/**
 * @file    drv_adxl345.c
 * @brief   ADXL345 三轴加速度计驱动实现
 * @author  智能水冷项目
 * @date    2025-01-24
 *
 * 功能：
 *   - 检测水泵振动强度
 *   - 异常振动预警
 *   - 设备健康评分依据
 */

#include "drv_adxl345.h"
#include "bsp_i2c.h"
#include "debug.h"
#include <math.h>

/*============================ 内部函数 ============================*/

/**
 * @brief  写寄存器
 */
static int8_t adxl345_write_reg(uint8_t reg, uint8_t value)
{
    return i2c1_write_byte(ADXL345_I2C_ADDR, reg, value);
}

/**
 * @brief  读寄存器
 */
static int8_t adxl345_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c1_read_byte(ADXL345_I2C_ADDR, reg, value);
}

/**
 * @brief  读取多字节
 */
static int8_t adxl345_read_bytes(uint8_t reg, uint8_t *buf, uint8_t len)
{
    return i2c1_read_bytes(ADXL345_I2C_ADDR, reg, buf, len);
}

/*============================ 外部接口函数 ============================*/

/**
 * @brief  初始化 ADXL345
 */
int8_t adxl345_init(void)
{
    uint8_t id;

    /* 1. 初始化I2C（如果还没初始化） */
    i2c1_init();

    /* 2. 读取设备ID验证通信 */
    if (adxl345_read_reg(ADXL345_REG_DEVID, &id) != 0)
    {
        printf("[ADXL345] I2C communication failed!\r\n");
        return -1;
    }

    if (id != 0xE5)
    {
        printf("[ADXL345] Wrong device ID: 0x%02X (expected 0xE5)\r\n", id);
        return -1;
    }

    printf("[ADXL345] Device ID: 0x%02X (correct)\r\n", id);

    /* 3. 配置数据格式：±2g，全分辨率模式 */
    if (adxl345_write_reg(ADXL345_REG_DATA_FORMAT,
                          ADXL345_DFORMAT_FULL_RES | ADXL345_DFORMAT_RANGE_2G) != 0)
    {
        printf("[ADXL345] Config DATA_FORMAT failed!\r\n");
        return -1;
    }

    /* 4. 配置采样率：100Hz */
    if (adxl345_write_reg(ADXL345_REG_BW_RATE, ADXL345_RATE_100HZ) != 0)
    {
        printf("[ADXL345] Config BW_RATE failed!\r\n");
        return -1;
    }

    /* 5. 进入测量模式 */
    if (adxl345_write_reg(ADXL345_REG_POWER_CTL, ADXL345_PCTL_MEASURE) != 0)
    {
        printf("[ADXL345] Enter measure mode failed!\r\n");
        return -1;
    }

    Delay_Ms(10);  /* 等待稳定 */

    printf("[ADXL345] Init success! Range: ±2g, Rate: 100Hz\r\n");
    return 0;
}

/**
 * @brief  读取设备ID
 */
uint8_t adxl345_read_device_id(void)
{
    uint8_t id = 0;
    adxl345_read_reg(ADXL345_REG_DEVID, &id);
    return id;
}

/**
 * @brief  配置 ADXL345
 */
int8_t adxl345_config(adxl345_config_t *config)
{
    uint8_t data_format = 0;

    if (config == NULL)
        return -1;

    /* 配置量程 */
    if (config->full_res)
    {
        data_format = ADXL345_DFORMAT_FULL_RES | config->range;
    }
    else
    {
        data_format = config->range;
    }

    if (adxl345_write_reg(ADXL345_REG_DATA_FORMAT, data_format) != 0)
        return -1;

    /* 配置采样率 */
    if (adxl345_write_reg(ADXL345_REG_BW_RATE, config->rate) != 0)
        return -1;

    return 0;
}

/**
 * @brief  读取原始数据
 */
int8_t adxl345_read_raw(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t buf[6];

    /* 读取6字节数据（X,Y,Z各2字节） */
    if (adxl345_read_bytes(ADXL345_REG_DATAX0, buf, 6) != 0)
    {
        return -1;
    }

    /* 组合为16位有符号数（小端模式） */
    *x = (int16_t)((buf[1] << 8) | buf[0]);
    *y = (int16_t)((buf[3] << 8) | buf[2]);
    *z = (int16_t)((buf[5] << 8) | buf[4]);

    return 0;
}

/**
 * @brief  读取加速度数据
 */
int8_t adxl345_read_accel(adxl345_data_t *data)
{
    int16_t x, y, z;

    if (data == NULL)
        return -1;

    /* 读取原始数据 */
    if (adxl345_read_raw(&x, &y, &z) != 0)
    {
        data->valid = 0;
        return -1;
    }

    /* 保存原始数据 */
    data->x_raw = x;
    data->y_raw = y;
    data->z_raw = z;

    /* 转换为g值（±2g全分辨率模式：3.9mg/LSB） */
    data->x_g = (float)x * ADXL345_SCALE_FACTOR;
    data->y_g = (float)y * ADXL345_SCALE_FACTOR;
    data->z_g = (float)z * ADXL345_SCALE_FACTOR;

    /* 计算合成加速度幅值 */
    data->magnitude = sqrtf(data->x_g * data->x_g +
                            data->y_g * data->y_g +
                            data->z_g * data->z_g);

    /* 计算振动强度（去除重力分量1g） */
    data->vibration = fabsf(data->magnitude - 1.0f);

    /* 振动报警判断 */
    if (data->vibration >= ADXL345_VIBRATION_ALARM)
    {
        data->alarm_critical = 1;
        data->alarm_warning = 1;
    }
    else if (data->vibration >= ADXL345_VIBRATION_WARNING)
    {
        data->alarm_critical = 0;
        data->alarm_warning = 1;
    }
    else
    {
        data->alarm_critical = 0;
        data->alarm_warning = 0;
    }

    data->valid = 1;
    return 0;
}

/**
 * @brief  计算振动强度
 */
float adxl345_calc_vibration(adxl345_data_t *data)
{
    if (data == NULL || !data->valid)
        return 0.0f;

    return data->vibration;
}

/**
 * @brief  自检
 */
int8_t adxl345_self_test(void)
{
    uint8_t id;
    int16_t x, y, z;

    printf("[ADXL345] Self test start...\r\n");

    /* 1. 检查设备ID */
    id = adxl345_read_device_id();
    if (id != 0xE5)
    {
        printf("[ADXL345] Self test failed: Wrong ID 0x%02X\r\n", id);
        return -1;
    }
    printf("  [OK] Device ID: 0x%02X\r\n", id);

    /* 2. 读取加速度数据 */
    if (adxl345_read_raw(&x, &y, &z) != 0)
    {
        printf("[ADXL345] Self test failed: Read data error\r\n");
        return -1;
    }
    printf("  [OK] Raw data: X=%d, Y=%d, Z=%d\r\n", x, y, z);

    /* 3. 检查Z轴（重力方向应该接近±256，对应±1g） */
    if (abs(z) < 100 || abs(z) > 400)
    {
        printf("  [WARN] Z-axis value abnormal (expected ~256)\r\n");
    }
    else
    {
        printf("  [OK] Z-axis gravity detected\r\n");
    }

    printf("[ADXL345] Self test passed!\r\n");
    return 0;
}
