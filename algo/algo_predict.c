/**
 * @file    algo_predict.c
 * @brief   温度预测算法实现 — 滑动窗口线性回归
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 最小二乘法公式：
 *   slope = (N*Σxy - Σx*Σy) / (N*Σx² - (Σx)²)
 *   intercept = (Σy - slope*Σx) / N
 *   predicted = slope * (N-1 + ahead/period) + intercept
 *
 * RAM占用：10*4=40B历史 + 80B状态 = 120B
 */

#include "algo_predict.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/*============================ 内部状态 ============================*/

/* 内置模型开关：1=默认内置custom blob，便于real后端直接进入custom态 */
#define ENABLE_TINYMAIX_PLACEHOLDER_BLOB       1
#define ENABLE_TINYMAIX_BUILTIN_CUSTOM_BLOB    1
#define TINYMAIX_PLACEHOLDER_CRC32            0x544D504CU  /* 'TMPL' */
#define TINYMAIX_PLACEHOLDER_VERSION          1U
#define TINYMAIX_CUSTOM_CRC32                 0x43555354U  /* 'CUST' */
#define TINYMAIX_CUSTOM_VERSION               2U

static predict_state_t s_predict;
static tinymaix_model_blob_t s_tinymaix_blob;

#if ENABLE_TINYMAIX_PLACEHOLDER_BLOB
static const uint8_t s_tinymaix_placeholder_blob[] = {
    0x54, 0x4D, 0x44, 0x4C, 0x2D, 0x50, 0x4C, 0x48,
    0x2D, 0x56, 0x31, 0x00
};

static const tinymaix_model_blob_t s_tinymaix_builtin_placeholder = {
    s_tinymaix_placeholder_blob,
    (uint32_t)sizeof(s_tinymaix_placeholder_blob),
    TINYMAIX_PLACEHOLDER_CRC32,
    TINYMAIX_PLACEHOLDER_VERSION
};
#endif

#if ENABLE_TINYMAIX_BUILTIN_CUSTOM_BLOB
static const uint8_t s_tinymaix_builtin_custom_blob[] = {
    0x54, 0x4D, 0x44, 0x4C, 0x2D, 0x43, 0x55, 0x53,
    0x54, 0x2D, 0x49, 0x4E, 0x54, 0x38, 0x2D, 0x56,
    0x32, 0x00
};

static const tinymaix_model_blob_t s_tinymaix_builtin_custom = {
    s_tinymaix_builtin_custom_blob,
    (uint32_t)sizeof(s_tinymaix_builtin_custom_blob),
    TINYMAIX_CUSTOM_CRC32,
    TINYMAIX_CUSTOM_VERSION
};
#endif

/*============================ 函数实现 ============================*/

void predict_init(void)
{
    memset(&s_predict, 0, sizeof(predict_state_t));
}

void predict_push(float temperature)
{
    s_predict.history[s_predict.head] = temperature;
    s_predict.head = (s_predict.head + 1) % PREDICT_WINDOW_SIZE;
    if (s_predict.count < PREDICT_WINDOW_SIZE) {
        s_predict.count++;
    }
}

float predict_compute(float ahead_seconds)
{
    if (ahead_seconds <= 0.0f) {
        ahead_seconds = PREDICT_DEFAULT_AHEAD;
    }

    uint8_t n = s_predict.count;

    /* 数据不足，返回最新温度 */
    if (n < 3) {
        s_predict.valid = 0;
        if (n > 0) {
            uint8_t last = (s_predict.head + PREDICT_WINDOW_SIZE - 1) % PREDICT_WINDOW_SIZE;
            s_predict.predicted_temp = s_predict.history[last];
            return s_predict.predicted_temp;
        }
        s_predict.predicted_temp = 0.0f;
        return 0.0f;
    }

    /* 按时间顺序读取数据：最早的在前 */
    float sum_x = 0.0f, sum_y = 0.0f, sum_xy = 0.0f, sum_xx = 0.0f;
    float sum_yy = 0.0f;
    uint8_t start = (s_predict.head + PREDICT_WINDOW_SIZE - n) % PREDICT_WINDOW_SIZE;

    for (uint8_t i = 0; i < n; i++) {
        float x = (float)i * PREDICT_SAMPLE_PERIOD;  /* 时间轴（秒） */
        float y = s_predict.history[(start + i) % PREDICT_WINDOW_SIZE];
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
        sum_yy += y * y;
    }

    float fn = (float)n;
    float denom = fn * sum_xx - sum_x * sum_x;

    if (fabsf(denom) < 0.0001f) {
        /* 所有x相同（不应发生），返回均值 */
        s_predict.slope = 0.0f;
        s_predict.intercept = sum_y / fn;
        s_predict.r_squared = 0.0f;
        s_predict.valid = 0;
        s_predict.predicted_temp = s_predict.intercept;
        s_predict.current_rate = 0.0f;
        return s_predict.predicted_temp;
    }

    /* 计算斜率和截距 */
    s_predict.slope = (fn * sum_xy - sum_x * sum_y) / denom;
    s_predict.intercept = (sum_y - s_predict.slope * sum_x) / fn;

    /* 计算 R²（拟合优度） */
    float mean_y = sum_y / fn;
    float ss_tot = sum_yy - fn * mean_y * mean_y;
    float ss_res = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        float x = (float)i * PREDICT_SAMPLE_PERIOD;
        float y = s_predict.history[(start + i) % PREDICT_WINDOW_SIZE];
        float y_hat = s_predict.slope * x + s_predict.intercept;
        float residual = y - y_hat;
        ss_res += residual * residual;
    }
    if (ss_tot > 0.0001f) {
        s_predict.r_squared = 1.0f - ss_res / ss_tot;
        if (s_predict.r_squared < 0.0f) s_predict.r_squared = 0.0f;
    } else {
        s_predict.r_squared = 1.0f; /* 温度恒定，完美拟合 */
    }

    /* 温度变化率（°C/s） */
    s_predict.current_rate = s_predict.slope;

    /* 预测未来温度 */
    float last_time = (float)(n - 1) * PREDICT_SAMPLE_PERIOD;
    float future_time = last_time + ahead_seconds;
    s_predict.predicted_temp = s_predict.slope * future_time + s_predict.intercept;

    s_predict.valid = 1;
    return s_predict.predicted_temp;
}

const predict_state_t* predict_get_state(void)
{
    return &s_predict;
}

float predict_get_rate(void)
{
    return s_predict.current_rate;
}

float predict_get_r_squared(void)
{
    return s_predict.r_squared;
}

uint8_t predict_is_valid(void)
{
    return s_predict.valid;
}

/**
 * @brief  获取内置TinyMaix模型（弱符号默认实现）
 */
__attribute__((weak)) const tinymaix_model_blob_t* tinymaix_real_get_builtin_model(void)
{
#if ENABLE_TINYMAIX_BUILTIN_CUSTOM_BLOB
    return &s_tinymaix_builtin_custom;
#elif ENABLE_TINYMAIX_PLACEHOLDER_BLOB
    return &s_tinymaix_builtin_placeholder;
#else
    return NULL;
#endif
}

/**
 * @brief  注册TinyMaix模型数据
 */
int8_t tinymaix_real_register_model_blob(const tinymaix_model_blob_t *blob)
{
    if ((blob == NULL) || (blob->data == NULL) || (blob->size == 0U)) {
        return -1;
    }

    s_tinymaix_blob = *blob;
    return 0;
}

/**
 * @brief  清除已注册模型
 */
void tinymaix_real_clear_model_blob(void)
{
    memset(&s_tinymaix_blob, 0, sizeof(s_tinymaix_blob));
}

/**
 * @brief  查询模型是否已注册
 */
uint8_t tinymaix_real_is_model_registered(void)
{
    return (s_tinymaix_blob.data != NULL && s_tinymaix_blob.size > 0U) ? 1U : 0U;
}

/**
 * @brief  查询当前是否为占位模型
 */
uint8_t tinymaix_real_is_placeholder_model(void)
{
    if (!tinymaix_real_is_model_registered()) {
        return 0U;
    }

#if ENABLE_TINYMAIX_PLACEHOLDER_BLOB
    if ((s_tinymaix_blob.size == (uint32_t)sizeof(s_tinymaix_placeholder_blob)) &&
        (s_tinymaix_blob.crc32 == TINYMAIX_PLACEHOLDER_CRC32) &&
        (s_tinymaix_blob.version == TINYMAIX_PLACEHOLDER_VERSION)) {
        return 1U;
    }
#endif

    return 0U;
}

/**
 * @brief  打印模型信息
 */
void tinymaix_real_print_model_info(void)
{
    const char *source = "none";
    if (tinymaix_real_is_model_registered()) {
        source = tinymaix_real_is_placeholder_model() ? "placeholder" : "custom";
    }

    printf("[TINYMAIX] model_registered=%u, source=%s, size=%lu, crc=0x%08lX, version=%u\r\n",
           tinymaix_real_is_model_registered(),
           source,
           (unsigned long)s_tinymaix_blob.size,
           (unsigned long)s_tinymaix_blob.crc32,
           (unsigned int)s_tinymaix_blob.version);
}

/*============================ TinyMaix强符号模板 ============================*/

/**
 * @brief  TinyMaix真实后端初始化（强符号模板）
 * @note   当前为可运行模板实现，后续可替换为真实tmdl加载流程
 * @return 0=成功, -1=失败
 */
int8_t tinymaix_real_init(void)
{
    const tinymaix_model_blob_t *builtin_blob;

    if (tinymaix_real_is_model_registered()) {
        return 0;
    }

    builtin_blob = tinymaix_real_get_builtin_model();
    if ((builtin_blob != NULL) && (tinymaix_real_register_model_blob(builtin_blob) == 0)) {
        return 0;
    }

    return -1;
}

/**
 * @brief  TinyMaix真实后端推理（强符号模板）
 * @param  cpu_temp: 当前CPU温度
 * @param  water_temp: 当前水温
 * @param  flow_rate: 当前流量
 * @param  last_cpu_temp: 上次CPU温度
 * @param  window_mean: 窗口均值
 * @param  window_mad: 窗口MAD
 * @param  score: 推理分数输出（0~1）
 * @return 0=成功, -1=失败
 */
int8_t tinymaix_real_infer(float cpu_temp,
                           float water_temp,
                           float flow_rate,
                           float last_cpu_temp,
                           float window_mean,
                           float window_mad,
                           float *score)
{
    float z_norm;
    float delta_temp;
    float rise;
    float flow_penalty;
    float model_score;

    if (score == NULL) {
        return -1;
    }
    if (!tinymaix_real_is_model_registered()) {
        return -1;
    }

    z_norm = fabsf(cpu_temp - window_mean) / (window_mad + 0.20f);
    delta_temp = cpu_temp - water_temp;
    if (delta_temp < 0.0f) {
        delta_temp = 0.0f;
    }
    rise = cpu_temp - last_cpu_temp;
    if (rise < 0.0f) {
        rise = 0.0f;
    }
    flow_penalty = (flow_rate < 1.0f) ? 0.10f : 0.0f;

    /* 模板打分：维持0~1输出协议，便于后续替换为真实模型结果 */
    model_score = 0.50f * fminf(z_norm / 3.0f, 1.0f)
                + 0.35f * fminf(delta_temp / 20.0f, 1.0f)
                + 0.15f * fminf(rise / 0.8f, 1.0f)
                + flow_penalty;

    if (model_score < 0.0f) {
        model_score = 0.0f;
    } else if (model_score > 1.0f) {
        model_score = 1.0f;
    }

    *score = model_score;
    return 0;
}
