/**
 * @file    ws_json.h
 * @brief   WebSocket JSON处理层 - 头文件
 * @author  智能水冷项目
 * @date    2026-02-02
 * 
 * 功能说明:
 *   - 解析WebSocket接收到的JSON消息
 *   - 构造要发送的JSON消息
 *   - 支持control_command、realtime_data、health_status等消息类型
 */

#ifndef __WS_JSON_H
#define __WS_JSON_H

#include <stdint.h>

/*============================ 配置参数 ============================*/

#define WS_JSON_MAX_FAULTS      8       /* 最大故障记录数 */
#define WS_JSON_SCHEMA_VERSION  "ws_v2" /* 上报JSON版本 */
#define WS_JSON_ENABLE_TINYML_EXT 0     /* 0=保持兼容v2格式, 1=附带TinyML扩展字段 */

/*============================ JSON消息类型 ============================*/

typedef enum {
    JSON_MSG_UNKNOWN = 0,               /* 未知类型 */
    JSON_MSG_CONNECTION_ACK,            /* 连接确认 */
    JSON_MSG_CONTROL_COMMAND,           /* 控制指令 */
    JSON_MSG_REALTIME_DATA,             /* 实时数据 */
    JSON_MSG_HEALTH_STATUS,             /* 健康状态 */
    JSON_MSG_ERROR                      /* 错误消息 */
} json_msg_type_t;

/*============================ 控制命令 ============================*/

typedef enum {
    CMD_ACTION_UNKNOWN = 0,             /* 未知动作 */
    CMD_ACTION_SET_MODE,                /* 设置模式 */
    CMD_ACTION_SET_PID,                 /* 设置PID参数 */
    CMD_ACTION_RESET                    /* 系统重置 */
} cmd_action_t;

typedef struct {
    cmd_action_t action;                /* 命令动作 */
    union {
        char mode[16];                  /* 模式: silent/balanced/performance */
        struct {
            float kp;                   /* 比例系数 */
            float ki;                   /* 积分系数 */
            float kd;                   /* 微分系数 */
        } pid;
    } params;
} control_command_t;

/*============================ 实时数据 ============================*/

typedef struct {
    float cpu_temp;                     /* CPU温度 (°C) */
    float water_temp;                   /* 水温 (°C) */
    float flow_rate;                    /* 流量 (L/h) */
    uint16_t pump_speed;                /* 泵转速 (RPM) */
    uint16_t fan_speed;                 /* 风扇转速 (RPM) */
    float power;                        /* 功耗 (W) */
    uint16_t tds_ppm;                   /* TDS水质 (ppm) */
    char tds_level[16];                 /* TDS水质等级 */
    uint8_t tinyml_ready;               /* TinyML预热状态 */
    uint8_t tinyml_anomaly;             /* TinyML异常状态 */
    float tinyml_score;                 /* TinyML异常分数(0~1) */
    char tinyml_backend[20];            /* TinyML后端 */
    uint8_t tinyml_model_ready;         /* TinyML模型就绪状态 */
    uint8_t tinyml_fallback;            /* TinyML是否已降级 */
} realtime_data_t;

/*============================ 健康状态 ============================*/

typedef struct {
    uint8_t health_score;               /* 健康度评分 (0-100) */
    char pump_status[16];               /* 水泵状态 */
    char fan_status[16];                /* 风扇状态 */
    char sensor_status[16];             /* 传感器状态 */
    char cooling_status[16];            /* 冷却系统状态 */
    uint8_t fault_count;                /* 故障数量 */
    struct {
        char code[16];                  /* 故障代码 */
        char description[64];           /* 故障描述 */
        char severity[16];              /* 严重程度 */
        uint64_t timestamp;             /* 故障时间戳 */
    } faults[WS_JSON_MAX_FAULTS];
    uint8_t tinyml_ready;               /* TinyML预热状态 */
    uint8_t tinyml_anomaly;             /* TinyML异常状态 */
    float tinyml_score;                 /* TinyML异常分数(0~1) */
    char tinyml_backend[20];            /* TinyML后端 */
    uint8_t tinyml_model_ready;         /* TinyML模型就绪状态 */
    uint8_t tinyml_fallback;            /* TinyML是否已降级 */
} health_status_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  获取当前时间戳（毫秒）
 * @return 时间戳
 * @note   这是一个简单的实现，实际项目中应该使用RTC或系统时钟
 */
uint64_t json_get_timestamp(void);

/**
 * @brief  设置TinyML扩展字段上报开关
 * @param  enable: 1=开启, 0=关闭
 */
void json_set_tinyml_ext_enabled(uint8_t enable);

/**
 * @brief  查询TinyML扩展字段上报开关
 * @return 1=开启, 0=关闭
 */
uint8_t json_is_tinyml_ext_enabled(void);

/**
 * @brief  解析JSON消息类型
 * @param  json: JSON字符串
 * @param  len: 字符串长度
 * @return 消息类型
 * 
 * @example
 *   const char *json = "{\"type\":\"control_command\"}";
 *   json_msg_type_t type = json_parse_type(json, strlen(json));
 */
json_msg_type_t json_parse_type(const char *json, uint16_t len);

/**
 * @brief  解析控制命令
 * @param  json: JSON字符串
 * @param  len: 字符串长度
 * @param  cmd: 输出命令结构
 * @return 0=成功, -1=失败
 * 
 * @example
 *   const char *json = "{\"type\":\"control_command\",\"command\":{\"action\":\"set_mode\",\"mode\":\"performance\"}}";
 *   control_command_t cmd;
 *   if (json_parse_control_command(json, strlen(json), &cmd) == 0) {
 *       // 处理命令
 *   }
 */
int json_parse_control_command(const char *json, uint16_t len, control_command_t *cmd);

/**
 * @brief  构造实时数据JSON
 * @param  data: 实时数据
 * @param  buffer: 输出缓冲区
 * @param  buffer_size: 缓冲区大小
 * @return 生成的JSON长度，-1表示失败
 * 
 * @example
 *   realtime_data_t data = {
 *       .cpu_temp = 65.5,
 *       .water_temp = 35.2,
 *       .flow_rate = 120.5,
 *       .pump_speed = 2500,
 *       .fan_speed = 1800,
 *       .power = 25.3
 *   };
 *   char json[512];
 *   int len = json_build_realtime_data(&data, json, sizeof(json));
 */
int json_build_realtime_data(const realtime_data_t *data, char *buffer, uint16_t buffer_size);

/**
 * @brief  构造健康状态JSON
 * @param  status: 健康状态
 * @param  buffer: 输出缓冲区
 * @param  buffer_size: 缓冲区大小
 * @return 生成的JSON长度，-1表示失败
 * 
 * @example
 *   health_status_t status = {
 *       .health_score = 85,
 *       .pump_status = "normal",
 *       .fan_status = "normal",
 *       .sensor_status = "normal",
 *       .cooling_status = "normal",
 *       .fault_count = 0
 *   };
 *   char json[1024];
 *   int len = json_build_health_status(&status, json, sizeof(json));
 */
int json_build_health_status(const health_status_t *status, char *buffer, uint16_t buffer_size);

#endif /* __WS_JSON_H */
