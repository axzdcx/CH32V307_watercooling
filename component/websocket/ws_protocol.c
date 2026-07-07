/**
 * @file    ws_protocol.c
 * @brief   WebSocket协议层 - 实现文件
 * @author  智能水冷项目
 * @date    2026-02-02
 */

#include "ws_protocol.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*============================ 私有变量 ============================*/

static uint32_t g_random_seed = 12345;  /* 随机数种子 */

/*============================ 私有函数 ============================*/

/**
 * @brief  简单的伪随机数生成器
 */
static uint32_t simple_rand(void)
{
    g_random_seed = g_random_seed * 1103515245 + 12345;
    return (g_random_seed / 65536) % 32768;
}

/**
 * @brief  Base64编码表
 */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief  Base64编码
 * @param  input: 输入数据
 * @param  input_len: 输入长度
 * @param  output: 输出缓冲区
 * @param  output_size: 输出缓冲区大小
 * @return 编码后的长度
 */
static int base64_encode(const uint8_t *input, int input_len, char *output, int output_size)
{
    int i, j;
    int output_len = ((input_len + 2) / 3) * 4;
    
    if (output_len >= output_size) {
        return -1;
    }
    
    for (i = 0, j = 0; i < input_len; ) {
        uint32_t octet_a = i < input_len ? input[i++] : 0;
        uint32_t octet_b = i < input_len ? input[i++] : 0;
        uint32_t octet_c = i < input_len ? input[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        output[j++] = base64_table[(triple >> 18) & 0x3F];
        output[j++] = base64_table[(triple >> 12) & 0x3F];
        output[j++] = base64_table[(triple >> 6) & 0x3F];
        output[j++] = base64_table[triple & 0x3F];
    }
    
    /* 添加填充 */
    int padding = input_len % 3;
    if (padding > 0) {
        for (i = 0; i < 3 - padding; i++) {
            output[output_len - 1 - i] = '=';
        }
    }
    
    output[output_len] = '\0';
    return output_len;
}

/*============================ 公共函数实现 ============================*/

/**
 * @brief  生成WebSocket握手请求
 */
int ws_generate_handshake(const char *host, uint16_t port, const char *path,
                          char *buffer, uint16_t buffer_size)
{
    uint8_t random_bytes[16];
    char sec_key[32];
    int len;
    
    if (host == NULL || path == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    /* 生成16字节随机数 */
    for (int i = 0; i < 16; i++) {
        random_bytes[i] = simple_rand() & 0xFF;
    }
    
    /* Base64编码 */
    if (base64_encode(random_bytes, 16, sec_key, sizeof(sec_key)) < 0) {
        return -1;
    }
    
    /* 构造握手请求 */
    len = snprintf(buffer, buffer_size,
                   "GET %s HTTP/1.1\r\n"
                   "Host: %s:%u\r\n"
                   "Upgrade: websocket\r\n"
                   "Connection: Upgrade\r\n"
                   "Sec-WebSocket-Key: %s\r\n"
                   "Sec-WebSocket-Version: 13\r\n"
                   "\r\n",
                   path, host, port, sec_key);
    
    if (len < 0 || len >= buffer_size) {
        return -1;
    }
    
    return len;
}

/**
 * @brief  解析WebSocket握手响应
 */
int ws_parse_handshake_response(const uint8_t *response, uint16_t len)
{
    if (response == NULL || len == 0) {
        return -1;
    }
    
    /* 简化实现：只检查是否包含"101 Switching Protocols" */
    const char *status_line = "101 Switching Protocols";
    
    if (len < strlen(status_line)) {
        return -1;
    }
    
    /* 查找状态行 */
    for (uint16_t i = 0; i < len - strlen(status_line); i++) {
        if (strncmp((char*)&response[i], status_line, strlen(status_line)) == 0) {
            return 0;  /* 握手成功 */
        }
    }
    
    return -1;  /* 握手失败 */
}

/**
 * @brief  构造WebSocket帧
 */
int ws_build_frame(ws_opcode_t opcode, const uint8_t *payload, uint16_t payload_len,
                   uint8_t *buffer, uint16_t buffer_size)
{
    int offset = 0;
    uint8_t masking_key[4];
    
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    /* 检查缓冲区大小 */
    int required_size = 2 + 4 + payload_len;  /* 最小：2字节头 + 4字节掩码 + 负载 */
    if (payload_len >= 126) {
        required_size += 2;  /* 扩展负载长度 */
    }
    if (required_size > buffer_size) {
        return -1;
    }
    
    /* 第一个字节：FIN=1, RSV=0, opcode */
    buffer[offset++] = 0x80 | (opcode & 0x0F);
    
    /* 第二个字节：MASK=1, payload length */
    if (payload_len < 126) {
        buffer[offset++] = 0x80 | payload_len;
    } else if (payload_len < 65536) {
        buffer[offset++] = 0x80 | 126;
        buffer[offset++] = (payload_len >> 8) & 0xFF;
        buffer[offset++] = payload_len & 0xFF;
    } else {
        /* 不支持超过64KB的负载 */
        return -1;
    }
    
    /* 生成掩码密钥 */
    ws_generate_mask_key(masking_key);
    buffer[offset++] = masking_key[0];
    buffer[offset++] = masking_key[1];
    buffer[offset++] = masking_key[2];
    buffer[offset++] = masking_key[3];
    
    /* 拷贝并应用掩码到负载 */
    if (payload != NULL && payload_len > 0) {
        memcpy(buffer + offset, payload, payload_len);
        ws_apply_mask(buffer + offset, payload_len, masking_key);
        offset += payload_len;
    }
    
    return offset;
}

/**
 * @brief  解析WebSocket帧
 */
int ws_parse_frame(const uint8_t *data, uint16_t len, ws_frame_t *frame)
{
    int offset = 0;
    
    if (data == NULL || len < 2 || frame == NULL) {
        return -1;
    }
    
    /* 清空帧结构 */
    memset(frame, 0, sizeof(ws_frame_t));
    
    /* 解析第一个字节 */
    frame->header.fin = (data[offset] & 0x80) >> 7;
    frame->header.opcode = data[offset] & 0x0F;
    offset++;
    
    /* 解析第二个字节 */
    frame->header.mask = (data[offset] & 0x80) >> 7;
    frame->header.payload_len = data[offset] & 0x7F;
    offset++;
    
    /* 解析扩展负载长度 */
    if (frame->header.payload_len == 126) {
        if (len < offset + 2) {
            return 0;  /* 需要更多数据 */
        }
        frame->header.payload_len = (data[offset] << 8) | data[offset + 1];
        offset += 2;
    } else if (frame->header.payload_len == 127) {
        /* 不支持超过64KB的负载 */
        return -1;
    }
    
    /* 解析掩码密钥（服务器发给客户端的帧通常没有掩码） */
    if (frame->header.mask) {
        if (len < offset + 4) {
            return 0;  /* 需要更多数据 */
        }
        frame->header.masking_key[0] = data[offset++];
        frame->header.masking_key[1] = data[offset++];
        frame->header.masking_key[2] = data[offset++];
        frame->header.masking_key[3] = data[offset++];
    }
    
    /* 检查是否有足够的负载数据 */
    if (len < offset + frame->header.payload_len) {
        return 0;  /* 需要更多数据 */
    }
    
    /* 设置负载指针（直接指向原始数据，不修改） */
    frame->payload = (uint8_t*)&data[offset];
    frame->payload_size = frame->header.payload_len;
    
    /* 注意：服务器发给客户端的帧通常没有掩码，所以不需要解除掩码 */
    /* 如果确实有掩码，需要在使用payload时手动解除 */
    
    offset += frame->header.payload_len;
    
    return offset;
}

/**
 * @brief  应用掩码
 */
void ws_apply_mask(uint8_t *data, uint16_t len, const uint8_t masking_key[4])
{
    if (data == NULL || masking_key == NULL) {
        return;
    }
    
    for (uint16_t i = 0; i < len; i++) {
        data[i] ^= masking_key[i % 4];
    }
}

/**
 * @brief  生成随机掩码密钥
 */
void ws_generate_mask_key(uint8_t key[4])
{
    if (key == NULL) {
        return;
    }
    
    key[0] = simple_rand() & 0xFF;
    key[1] = simple_rand() & 0xFF;
    key[2] = simple_rand() & 0xFF;
    key[3] = simple_rand() & 0xFF;
}
