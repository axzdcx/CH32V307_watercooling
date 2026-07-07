/**
 * @file    ws_protocol.h
 * @brief   WebSocket协议层 - 头文件
 * @author  智能水冷项目
 * @date    2026-02-02
 * 
 * 功能说明:
 *   - 实现WebSocket RFC 6455协议
 *   - 支持握手、帧构造、帧解析
 *   - 支持掩码处理
 */

#ifndef __WS_PROTOCOL_H
#define __WS_PROTOCOL_H

#include <stdint.h>

/*============================ WebSocket操作码 ============================*/

typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,   /* 继续帧 */
    WS_OPCODE_TEXT = 0x1,           /* 文本帧 */
    WS_OPCODE_BINARY = 0x2,         /* 二进制帧 */
    WS_OPCODE_CLOSE = 0x8,          /* 关闭帧 */
    WS_OPCODE_PING = 0x9,           /* Ping帧 */
    WS_OPCODE_PONG = 0xA            /* Pong帧 */
} ws_opcode_t;

/*============================ WebSocket帧头 ============================*/

typedef struct {
    uint8_t fin;                    /* FIN位 */
    uint8_t opcode;                 /* 操作码 */
    uint8_t mask;                   /* 掩码位 */
    uint64_t payload_len;           /* 负载长度 */
    uint8_t masking_key[4];         /* 掩码密钥 */
} ws_frame_header_t;

/*============================ WebSocket帧 ============================*/

typedef struct {
    ws_frame_header_t header;       /* 帧头 */
    uint8_t *payload;               /* 负载数据 */
    uint16_t payload_size;          /* 负载大小 */
} ws_frame_t;

/*============================ 函数声明 ============================*/

/**
 * @brief  生成WebSocket握手请求
 * @param  host: 服务器地址
 * @param  port: 服务器端口
 * @param  path: WebSocket路径
 * @param  buffer: 输出缓冲区
 * @param  buffer_size: 缓冲区大小
 * @return 生成的请求长度，-1表示失败
 * 
 * @example
 *   char handshake[512];
 *   int len = ws_generate_handshake("192.168.1.100", 8080, "/", handshake, sizeof(handshake));
 */
int ws_generate_handshake(const char *host, uint16_t port, const char *path,
                          char *buffer, uint16_t buffer_size);

/**
 * @brief  解析WebSocket握手响应
 * @param  response: 响应数据
 * @param  len: 响应长度
 * @return 0=成功, -1=失败
 * 
 * @note   简化实现，只检查是否包含"101 Switching Protocols"
 */
int ws_parse_handshake_response(const uint8_t *response, uint16_t len);

/**
 * @brief  构造WebSocket帧
 * @param  opcode: 操作码
 * @param  payload: 负载数据
 * @param  payload_len: 负载长度
 * @param  buffer: 输出缓冲区
 * @param  buffer_size: 缓冲区大小
 * @return 构造的帧长度，-1表示失败
 * 
 * @example
 *   const char *json = "{\"type\":\"realtime_data\"}";
 *   uint8_t frame[512];
 *   int len = ws_build_frame(WS_OPCODE_TEXT, (uint8_t*)json, strlen(json), frame, sizeof(frame));
 */
int ws_build_frame(ws_opcode_t opcode, const uint8_t *payload, uint16_t payload_len,
                   uint8_t *buffer, uint16_t buffer_size);

/**
 * @brief  解析WebSocket帧
 * @param  data: 接收到的数据
 * @param  len: 数据长度
 * @param  frame: 输出帧结构
 * @return 解析的字节数，-1表示失败，0表示需要更多数据
 * 
 * @note   frame->payload指向data中的负载位置，不会拷贝数据
 * 
 * @example
 *   ws_frame_t frame;
 *   int parsed = ws_parse_frame(rx_buffer, rx_len, &frame);
 *   if (parsed > 0) {
 *       // 处理frame.payload
 *   }
 */
int ws_parse_frame(const uint8_t *data, uint16_t len, ws_frame_t *frame);

/**
 * @brief  应用掩码
 * @param  data: 数据
 * @param  len: 数据长度
 * @param  masking_key: 掩码密钥
 * 
 * @note   掩码操作是可逆的，应用两次得到原始数据
 */
void ws_apply_mask(uint8_t *data, uint16_t len, const uint8_t masking_key[4]);

/**
 * @brief  生成随机掩码密钥
 * @param  key: 输出密钥（4字节）
 * 
 * @note   简单实现，使用伪随机数
 */
void ws_generate_mask_key(uint8_t key[4]);

#endif /* __WS_PROTOCOL_H */
