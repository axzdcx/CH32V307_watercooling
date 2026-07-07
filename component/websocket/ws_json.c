/**
 * @file    ws_json.c
 * @brief   WebSocket JSON处理层 - 实现文件
 * @author  智能水冷项目
 * @date    2026-02-02
 */

#include "ws_json.h"
#include "jsmn.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*============================ 私有变量 ============================*/

static uint64_t g_timestamp_counter = 0;   /* 时间戳计数器 */
static uint8_t g_tinyml_ext_enabled = WS_JSON_ENABLE_TINYML_EXT ? 1U : 0U; /* TinyML扩展开关 */

/*============================ 私有函数 ============================*/

/**
 * @brief  比较JSON token与字符串
 * @param  json: JSON字符串
 * @param  tok: token
 * @param  str: 要比较的字符串
 * @return 0=相等, 非0=不相等
 */
static int json_token_equals(const char *json, jsmntok_t *tok, const char *str)
{
    int len = tok->end - tok->start;
    if (len != (int)strlen(str)) {
        return -1;
    }
    return strncmp(json + tok->start, str, len);
}

/**
 * @brief  从JSON token提取字符串
 * @param  json: JSON字符串
 * @param  tok: token
 * @param  buffer: 输出缓冲区
 * @param  buffer_size: 缓冲区大小
 * @return 提取的字符串长度
 */
static int json_token_to_string(const char *json, jsmntok_t *tok, char *buffer, int buffer_size)
{
    int len = tok->end - tok->start;
    if (len >= buffer_size) {
        len = buffer_size - 1;
    }
    strncpy(buffer, json + tok->start, len);
    buffer[len] = '\0';
    return len;
}

/**
 * @brief  从JSON token提取浮点数
 * @param  json: JSON字符串
 * @param  tok: token
 * @return 浮点数值
 */
static float json_token_to_float(const char *json, jsmntok_t *tok)
{
    char buffer[32];
    json_token_to_string(json, tok, buffer, sizeof(buffer));
    return atof(buffer);
}

/**
 * @brief  从JSON token提取整数
 * @param  json: JSON字符串
 * @param  tok: token
 * @return 整数值
 */
static int json_token_to_int(const char *json, jsmntok_t *tok)
{
    char buffer[32];
    json_token_to_string(json, tok, buffer, sizeof(buffer));
    return atoi(buffer);
}

/*============================ 公共函数实现 ============================*/

/**
 * @brief  获取当前时间戳（毫秒）
 */
uint64_t json_get_timestamp(void)
{
    /* 简单实现：每次调用递增1000（模拟1秒） */
    /* 实际项目中应该使用RTC或系统时钟 */
    g_timestamp_counter += 1000;
    return g_timestamp_counter;
}

/**
 * @brief  设置TinyML扩展字段上报开关
 */
void json_set_tinyml_ext_enabled(uint8_t enable)
{
    g_tinyml_ext_enabled = enable ? 1U : 0U;
}

/**
 * @brief  查询TinyML扩展字段上报开关
 */
uint8_t json_is_tinyml_ext_enabled(void)
{
    return g_tinyml_ext_enabled;
}

/**
 * @brief  解析JSON消息类型
 */
json_msg_type_t json_parse_type(const char *json, uint16_t len)
{
    jsmn_parser parser;
    jsmntok_t tokens[96]; /* 仅用于识别type，放宽token上限以兼容较大业务JSON */
    int r;
    
    if (json == NULL || len == 0) {
        return JSON_MSG_UNKNOWN;
    }
    
    /* 初始化解析器 */
    jsmn_init(&parser);
    
    /* 解析JSON */
    r = jsmn_parse(&parser, json, len, tokens, (int)(sizeof(tokens) / sizeof(tokens[0])));
    
    if (r < 2) {
        return JSON_MSG_UNKNOWN;
    }
    
    /* 查找"type"字段 */
    for (int i = 1; i < r - 1; i++) {
        if (tokens[i].type == JSMN_STRING) {
            if (json_token_equals(json, &tokens[i], "type") == 0) {
                /* 找到type字段，下一个token是值 */
                if (json_token_equals(json, &tokens[i + 1], "connection_ack") == 0) {
                    return JSON_MSG_CONNECTION_ACK;
                } else if (json_token_equals(json, &tokens[i + 1], "control_command") == 0) {
                    return JSON_MSG_CONTROL_COMMAND;
                } else if (json_token_equals(json, &tokens[i + 1], "realtime_data") == 0) {
                    return JSON_MSG_REALTIME_DATA;
                } else if (json_token_equals(json, &tokens[i + 1], "health_status") == 0) {
                    return JSON_MSG_HEALTH_STATUS;
                } else if (json_token_equals(json, &tokens[i + 1], "error") == 0) {
                    return JSON_MSG_ERROR;
                }
                break;
            }
        }
    }
    
    return JSON_MSG_UNKNOWN;
}

/**
 * @brief  解析控制命令
 */
int json_parse_control_command(const char *json, uint16_t len, control_command_t *cmd)
{
    jsmn_parser parser;
    jsmntok_t tokens[64];
    int r;
    
    if (json == NULL || len == 0 || cmd == NULL) {
        return -1;
    }
    
    /* 清空命令结构 */
    memset(cmd, 0, sizeof(control_command_t));
    cmd->action = CMD_ACTION_UNKNOWN;
    
    /* 初始化解析器 */
    jsmn_init(&parser);
    
    /* 解析JSON */
    r = jsmn_parse(&parser, json, len, tokens, 64);
    
    if (r < 2) {
        return -1;
    }
    
    /* 查找"command"对象 */
    for (int i = 1; i < r - 1; i++) {
        if (tokens[i].type == JSMN_STRING) {
            if (json_token_equals(json, &tokens[i], "command") == 0) {
                /* 找到command对象 */
                if (tokens[i + 1].type == JSMN_OBJECT) {
                    int cmd_obj_idx = i + 1;
                    int cmd_obj_size = tokens[cmd_obj_idx].size;
                    
                    /* 遍历command对象的字段 */
                    int j = cmd_obj_idx + 1;
                    for (int k = 0; k < cmd_obj_size && j < r - 1; k++) {
                        if (tokens[j].type == JSMN_STRING) {
                            /* 解析action字段 */
                            if (json_token_equals(json, &tokens[j], "action") == 0) {
                                if (json_token_equals(json, &tokens[j + 1], "set_mode") == 0) {
                                    cmd->action = CMD_ACTION_SET_MODE;
                                } else if (json_token_equals(json, &tokens[j + 1], "set_pid") == 0) {
                                    cmd->action = CMD_ACTION_SET_PID;
                                } else if (json_token_equals(json, &tokens[j + 1], "reset") == 0) {
                                    cmd->action = CMD_ACTION_RESET;
                                }
                                j += 2;
                            }
                            /* 解析mode字段 */
                            else if (json_token_equals(json, &tokens[j], "mode") == 0) {
                                json_token_to_string(json, &tokens[j + 1], cmd->params.mode, sizeof(cmd->params.mode));
                                j += 2;
                            }
                            /* 解析pidParams对象 */
                            else if (json_token_equals(json, &tokens[j], "pidParams") == 0) {
                                if (tokens[j + 1].type == JSMN_OBJECT) {
                                    int pid_obj_idx = j + 1;
                                    int pid_obj_size = tokens[pid_obj_idx].size;
                                    int m = pid_obj_idx + 1;
                                    
                                    for (int n = 0; n < pid_obj_size && m < r - 1; n++) {
                                        if (tokens[m].type == JSMN_STRING) {
                                            if (json_token_equals(json, &tokens[m], "kp") == 0) {
                                                cmd->params.pid.kp = json_token_to_float(json, &tokens[m + 1]);
                                            } else if (json_token_equals(json, &tokens[m], "ki") == 0) {
                                                cmd->params.pid.ki = json_token_to_float(json, &tokens[m + 1]);
                                            } else if (json_token_equals(json, &tokens[m], "kd") == 0) {
                                                cmd->params.pid.kd = json_token_to_float(json, &tokens[m + 1]);
                                            }
                                            m += 2;
                                        }
                                    }
                                    j = m;
                                } else {
                                    j += 2;
                                }
                            } else {
                                j += 2;
                            }
                        } else {
                            j++;
                        }
                    }
                }
                break;
            }
        }
    }
    
    return (cmd->action != CMD_ACTION_UNKNOWN) ? 0 : -1;
}

/**
 * @brief  构造实时数据JSON
 */
int json_build_realtime_data(const realtime_data_t *data, char *buffer, uint16_t buffer_size)
{
    int len;
    uint32_t timestamp;
    uint8_t tinyml_ext_enabled;
    
    if (data == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    /* 获取时间戳并转换为uint32_t（毫秒） */
    timestamp = (uint32_t)json_get_timestamp();
    tinyml_ext_enabled = json_is_tinyml_ext_enabled();
    
    /* 构造JSON字符串 */
    if (tinyml_ext_enabled) {
        len = snprintf(buffer, buffer_size,
                       "{"
                       "\"type\":\"realtime_data\","
                       "\"schema\":\"%s\","
                       "\"timestamp\":%lu,"
                       "\"data\":{"
                       "\"cpuTemp\":%.1f,"
                       "\"waterTemp\":%.1f,"
                       "\"flowRate\":%.1f,"
                       "\"pumpSpeed\":%u,"
                       "\"fanSpeed\":%u,"
                       "\"power\":%.1f,"
                       "\"tdsPpm\":%u,"
                       "\"tdsLevel\":\"%s\","
                       "\"tinyml\":{"
                       "\"ready\":%u,"
                       "\"anomaly\":%u,"
                       "\"score\":%.2f,"
                       "\"backend\":\"%s\","
                       "\"modelReady\":%u,"
                       "\"fallback\":%u"
                       "}"
                       "}"
                       "}",
                       WS_JSON_SCHEMA_VERSION,
                       (unsigned long)timestamp,
                       data->cpu_temp,
                       data->water_temp,
                       data->flow_rate,
                       data->pump_speed,
                       data->fan_speed,
                       data->power,
                       data->tds_ppm,
                       data->tds_level,
                       data->tinyml_ready,
                       data->tinyml_anomaly,
                       data->tinyml_score,
                       data->tinyml_backend,
                       data->tinyml_model_ready,
                       data->tinyml_fallback);
    } else {
        len = snprintf(buffer, buffer_size,
                       "{"
                       "\"type\":\"realtime_data\","
                       "\"schema\":\"%s\","
                       "\"timestamp\":%lu,"
                       "\"data\":{"
                       "\"cpuTemp\":%.1f,"
                       "\"waterTemp\":%.1f,"
                       "\"flowRate\":%.1f,"
                       "\"pumpSpeed\":%u,"
                       "\"fanSpeed\":%u,"
                       "\"power\":%.1f,"
                       "\"tdsPpm\":%u,"
                       "\"tdsLevel\":\"%s\""
                       "}"
                       "}",
                       WS_JSON_SCHEMA_VERSION,
                       (unsigned long)timestamp,
                       data->cpu_temp,
                       data->water_temp,
                       data->flow_rate,
                       data->pump_speed,
                       data->fan_speed,
                       data->power,
                       data->tds_ppm,
                       data->tds_level);
    }
    
    if (len < 0 || len >= buffer_size) {
        return -1;
    }
    
    return len;
}

/**
 * @brief  构造健康状态JSON
 */
int json_build_health_status(const health_status_t *status, char *buffer, uint16_t buffer_size)
{
    int len;
    int offset = 0;
    uint32_t timestamp;
    uint8_t tinyml_ext_enabled;
    
    if (status == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    /* 获取时间戳并转换为uint32_t */
    timestamp = (uint32_t)json_get_timestamp();
    tinyml_ext_enabled = json_is_tinyml_ext_enabled();
    
    /* 构造JSON开头 */
    len = snprintf(buffer + offset, buffer_size - offset,
                   "{"
                   "\"type\":\"health_status\","
                   "\"schema\":\"%s\","
                   "\"timestamp\":%lu,"
                   "\"data\":{"
                   "\"healthScore\":%u,"
                   "\"status\":{"
                   "\"pump\":\"%s\","
                   "\"fan\":\"%s\","
                   "\"sensor\":\"%s\","
                   "\"cooling\":\"%s\""
                   "},",
                   WS_JSON_SCHEMA_VERSION,
                   (unsigned long)timestamp,
                   status->health_score,
                   status->pump_status,
                   status->fan_status,
                   status->sensor_status,
                   status->cooling_status);
    
    if (len < 0 || offset + len >= buffer_size) {
        return -1;
    }
    offset += len;
    
    /* 构造faults数组 */
    len = snprintf(buffer + offset, buffer_size - offset, "\"faults\":[");
    if (len < 0 || offset + len >= buffer_size) {
        return -1;
    }
    offset += len;
    
    for (int i = 0; i < status->fault_count && i < WS_JSON_MAX_FAULTS; i++) {
        if (i > 0) {
            len = snprintf(buffer + offset, buffer_size - offset, ",");
            if (len < 0 || offset + len >= buffer_size) {
                return -1;
            }
            offset += len;
        }
        
        /* 获取故障时间戳并转换为uint32_t */
        uint32_t fault_timestamp = (uint32_t)status->faults[i].timestamp;
        
        len = snprintf(buffer + offset, buffer_size - offset,
                       "{"
                       "\"code\":\"%s\","
                       "\"description\":\"%s\","
                       "\"timestamp\":%lu,"
                       "\"severity\":\"%s\""
                       "}",
                       status->faults[i].code,
                       status->faults[i].description,
                       (unsigned long)fault_timestamp,
                       status->faults[i].severity);
        
        if (len < 0 || offset + len >= buffer_size) {
            return -1;
        }
        offset += len;
    }
    
    if (tinyml_ext_enabled) {
        /* 追加AI状态 */
        len = snprintf(buffer + offset, buffer_size - offset,
                       "],"
                       "\"ai\":{"
                       "\"tinyml\":{"
                       "\"ready\":%u,"
                       "\"anomaly\":%u,"
                       "\"score\":%.2f,"
                       "\"backend\":\"%s\","
                       "\"modelReady\":%u,"
                       "\"fallback\":%u"
                       "}"
                       "}"
                       "}}",
                       status->tinyml_ready,
                       status->tinyml_anomaly,
                       status->tinyml_score,
                       status->tinyml_backend,
                       status->tinyml_model_ready,
                       status->tinyml_fallback);
    } else {
        /* 构造JSON结尾（兼容ws_v2） */
        len = snprintf(buffer + offset, buffer_size - offset, "]}}");
    }
    if (len < 0 || offset + len >= buffer_size) {
        return -1;
    }
    offset += len;
    
    return offset;
}
