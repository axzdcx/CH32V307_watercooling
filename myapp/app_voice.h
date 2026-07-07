/**
 * @file    app_voice.h
 * @brief   语音交互应用层 — CI03(CI1302)离线语音集成
 * @author  智能水冷项目
 * @date    2026-02-11
 *
 * 功能：
 *   - 离线语音命令识别（12个命令）
 *   - 语音播报系统状态
 *   - 语音控制水泵/风扇/模式
 *   - 纯本地运行，不依赖网络
 */

#ifndef __APP_VOICE_H
#define __APP_VOICE_H

#include "ch32v30x.h"

/*============================ 函数声明 ============================*/

/**
 * @brief  语音应用初始化
 * @return 0=成功, -1=失败
 */
int8_t voice_app_init(void);

/**
 * @brief  语音任务（由调度器定时调用，建议200ms周期）
 */
void task_voice(void);

/**
 * @brief  主动播报温度信息
 */
void voice_announce_temp(void);

/**
 * @brief  主动播报健康评分
 */
void voice_announce_health(void);

/**
 * @brief  主动播报故障告警
 */
void voice_announce_fault(void);

#endif /* __APP_VOICE_H */
