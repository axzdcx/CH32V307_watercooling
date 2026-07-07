/**
 * @file    algo_predict.h
 * @brief   温度预测算法 — 滑动窗口线性回归
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 算法说明：
 *   使用最近N个温度采样点（默认10点，10秒历史）
 *   最小二乘法拟合 y = a*x + b
 *   外推预测未来温度（默认30秒后）
 *
 * 资源占用：
 *   RAM: ~120B（历史缓冲 + 计算变量）
 *   计算时间: <0.1ms @ 144MHz
 */

#ifndef __ALGO_PREDICT_H
#define __ALGO_PREDICT_H

#include <stdint.h>

/*============================ 配置 ============================*/

#define PREDICT_WINDOW_SIZE     10      /* 滑动窗口大小（采样点数） */
#define PREDICT_DEFAULT_AHEAD   30.0f   /* 默认预测提前量（秒） */
#define PREDICT_SAMPLE_PERIOD   1.0f    /* 采样周期（秒） */

/*============================ 数据结构 ============================*/

/**
 * @brief  预测器状态
 */
typedef struct {
    float history[PREDICT_WINDOW_SIZE]; /* 温度历史缓冲 */
    uint8_t count;                       /* 已填充的采样点数 */
    uint8_t head;                        /* 环形缓冲写入位置 */

    /* 回归结果 */
    float slope;            /* 斜率 a（°C/s） */
    float intercept;        /* 截距 b */
    float r_squared;        /* 拟合优度 R² (0~1) */

    /* 预测结果 */
    float predicted_temp;   /* 预测温度 °C */
    float current_rate;     /* 当前温度变化率 °C/s */

    uint8_t valid;          /* 预测是否有效（数据点>=3时有效） */
} predict_state_t;

/**
 * @brief  TinyMaix模型二进制信息
 */
typedef struct {
    const uint8_t *data;    /* 模型数据首地址 */
    uint32_t size;          /* 模型大小（字节） */
    uint32_t crc32;         /* 模型CRC32（可选，未知填0） */
    uint16_t version;       /* 模型版本号（可选，未知填0） */
} tinymaix_model_blob_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  初始化预测器
 */
void predict_init(void);

/**
 * @brief  输入新的温度采样点（由调度器1s周期调用）
 * @param  temperature: 当前CPU温度 °C
 */
void predict_push(float temperature);

/**
 * @brief  执行预测计算
 * @param  ahead_seconds: 预测提前量（秒），0则使用默认值30s
 * @return 预测温度 °C，数据不足时返回当前温度
 */
float predict_compute(float ahead_seconds);

/**
 * @brief  获取预测器状态
 * @return 预测器状态指针
 */
const predict_state_t* predict_get_state(void);

/**
 * @brief  获取当前温度变化率
 * @return 变化率 °C/s（正=升温，负=降温）
 */
float predict_get_rate(void);

/**
 * @brief  获取拟合优度
 * @return R² (0~1)，越接近1拟合越好
 */
float predict_get_r_squared(void);

/**
 * @brief  预测是否有效
 * @return 1=有效, 0=数据不足
 */
uint8_t predict_is_valid(void);

/**
 * @brief  注册TinyMaix模型数据
 * @param  blob: 模型信息（data/size必填）
 * @return 0=成功, -1=失败
 */
int8_t tinymaix_real_register_model_blob(const tinymaix_model_blob_t *blob);

/**
 * @brief  清除已注册的TinyMaix模型数据
 */
void tinymaix_real_clear_model_blob(void);

/**
 * @brief  查询TinyMaix模型是否已注册
 * @return 1=已注册, 0=未注册
 */
uint8_t tinymaix_real_is_model_registered(void);

/**
 * @brief  查询当前是否为占位模型
 * @return 1=占位模型, 0=非占位模型/未注册
 */
uint8_t tinymaix_real_is_placeholder_model(void);

/**
 * @brief  打印TinyMaix模型信息
 */
void tinymaix_real_print_model_info(void);

/**
 * @brief  获取内置TinyMaix模型（弱符号钩子）
 * @note   可在其他文件提供同名强符号，返回静态模型信息
 * @return 内置模型信息指针，NULL表示未提供
 */
const tinymaix_model_blob_t* tinymaix_real_get_builtin_model(void);

#endif /* __ALGO_PREDICT_H */
