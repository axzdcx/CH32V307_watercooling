/**
 * @file    main.c
 * @brief   智能水冷系统 - WebSocket测试版本
 * @author  智能水冷项目
 * @date    2026-02-02
 */

#include "mydefine.h"
#include "scheduler.h"
#include "app_tds.h"    /* TDS水质传感器 */

/*============================ 主函数 ============================*/

/**
 * @brief  WebSocket状态转字符串
 * @param  state: WebSocket状态枚举
 * @return 对应状态字符串
 */
static const char *main_ws_state_to_str(ws_state_t state)
{
    switch (state) {
        case WS_STATE_DISCONNECTED: return "DISCONNECTED";
        case WS_STATE_CONNECTING:   return "CONNECTING";
        case WS_STATE_CONNECTED:    return "CONNECTED";
        case WS_STATE_CLOSING:      return "CLOSING";
        case WS_STATE_ERROR:        return "ERROR";
        default:                    return "UNKNOWN";
    }
}

/**
 * @brief  AIR780E状态转字符串
 * @param  state: AIR780E状态枚举
 * @return 对应状态字符串
 */
static const char *main_air_state_to_str(air780e_state_t state)
{
    switch (state) {
        case AIR780E_STATE_IDLE:    return "IDLE";
        case AIR780E_STATE_READY:   return "READY";
        case AIR780E_STATE_SENDING: return "SENDING";
        case AIR780E_STATE_ERROR:   return "ERROR";
        default:                    return "UNKNOWN";
    }
}

/**
 * @brief  打印链路重连状态（WebSocket + AIR780E）
 */
static void main_print_link_reconnect_status(void)
{
    ws_reconnect_status_t status;
    int ret = ws_get_reconnect_status(&status);
    if (ret != 0) {
        printf("[WebSocket] 获取重连状态失败: %d\r\n", ret);
    } else {
        printf("状态: %s\r\n", main_ws_state_to_str(status.state));
        printf("重连计数: %u\r\n", status.reconnect_count);
        printf("冷却中: %s\r\n", status.reconnect_blocked ? "是" : "否");
        if (status.reconnect_blocked) {
            printf("冷却剩余: %lums\r\n", (unsigned long)status.cooldown_left_ms);
        } else if (status.state == WS_STATE_ERROR || status.state == WS_STATE_DISCONNECTED) {
            printf("下次自动重连: %lums\r\n", (unsigned long)status.next_retry_in_ms);
        }
        printf("上次重连时间戳: %lums\r\n", (unsigned long)status.last_reconnect_time_ms);
    }

    {
        air780e_reconnect_status_t air_status;
        int air_ret = air780e_get_reconnect_status(&air_status);
        if (air_ret == 0) {
            printf("AIR状态: %s\r\n", main_air_state_to_str(air_status.state));
            printf("AIR重连计数: %u\r\n", air_status.reconnect_count);
            printf("AIR冷却中: %s\r\n", air_status.reconnect_blocked ? "是" : "否");
            if (air_status.reconnect_blocked) {
                printf("AIR冷却剩余: %lums\r\n", (unsigned long)air_status.cooldown_left_ms);
            } else if (air_status.state == AIR780E_STATE_ERROR) {
                printf("AIR下次自动重连: %lums\r\n", (unsigned long)air_status.next_retry_in_ms);
            }
            printf("AIR最近接收时间戳: %lums\r\n", (unsigned long)air_status.last_rx_time_ms);
        }
    }
}

/**
 * @brief  执行链路自动回归（x->r->c->r）
 */
static void main_run_link_regression(void)
{
    printf("[回归] 步骤1/4: 注入链路异常\r\n");
    air780e_mark_error();
    ws_client_disconnect();
    ws_client_process();

    Delay_Ms(50);
    printf("[回归] 步骤2/4: 查询异常后状态\r\n");
    main_print_link_reconnect_status();

    printf("[回归] 步骤3/4: 手动强制重连\r\n");
    websocket_connect();
    ws_client_process();

    Delay_Ms(50);
    printf("[回归] 步骤4/4: 查询重连后状态\r\n");
    main_print_link_reconnect_status();
    printf("[回归] 完成\r\n");
}

/**
 * @brief  打印通信链路快照（服务器/连接/统计）
 */
static void main_print_comm_snapshot(void)
{
    char host[64] = {0};
    uint16_t port = 0;

    websocket_get_server(host, &port);
    printf("[通信] WebSocket服务器: %s:%u\r\n", host, port);
    printf("[通信] WebSocket连接: %s\r\n", websocket_is_connected() ? "已连接" : "未连接");
    main_print_link_reconnect_status();
    air780e_print_stats();
}

/**
 * @brief  执行P2收工检查（上报+回归+快照）
 */
static void main_run_p2_closeout(void)
{
    printf("[P2] 步骤1/3: 主动上报实时+健康\r\n");
    websocket_send_realtime();
    Delay_Ms(WS_TX_MIN_GAP_MS);
    ws_client_process();
    websocket_send_health();
    ws_client_process();

    Delay_Ms(50);
    printf("[P2] 步骤2/3: 链路回归验证\r\n");
    main_run_link_regression();

    Delay_Ms(50);
    printf("[P2] 步骤3/3: 通信快照汇总\r\n");
    main_print_comm_snapshot();
    printf("[P2] 收工检查完成\r\n");
}

/**
 * @brief  打印诊断阈值配置
 */
static void main_print_diag_thresholds(void)
{
    diag_threshold_config_t cfg;
    if (diag_threshold_get(&cfg) != 0) {
        printf("[DIAG] 获取阈值失败\r\n");
        return;
    }

    printf("[DIAG] 阈值配置:\r\n");
    printf("  堵塞: flow<%.2f L/min, temp>%.1f℃, hold=%u, pump_pwm>=%u%%\r\n",
           cfg.blockage_flow_thresh, cfg.blockage_temp_thresh,
           cfg.blockage_hold_time, cfg.blockage_pump_pwm_min);
    printf("  气泡: flow_cv>%.3f\r\n", cfg.bubble_flow_cv_thresh);
    printf("  老化: current_cv>%.3f\r\n", cfg.pump_current_cv_thresh);
    printf("  漏水: pressure_slope<%.4f MPa/min, flow_drop>%.1f%%\r\n",
           cfg.leak_pressure_slope, cfg.leak_flow_drop_percent);
    printf("  水质: warning>%uppm, critical>%uppm\r\n",
           cfg.water_tds_warning, cfg.water_tds_critical);
    printf("  效率: diff_warn>%.1f℃, diff_critical>%.1f℃, pump_pwm>%u%%\r\n",
           cfg.eff_temp_diff_warning, cfg.eff_temp_diff_critical, cfg.eff_pump_pwm_thresh);
}

/**
 * @brief  微调堵塞流量阈值
 * @param  delta: 增量（L/min）
 */
static void main_adjust_diag_blockage_flow(float delta)
{
    diag_threshold_config_t cfg;
    if (diag_threshold_get(&cfg) != 0) {
        printf("[DIAG] 获取阈值失败\r\n");
        return;
    }

    cfg.blockage_flow_thresh += delta;
    if (diag_threshold_set(&cfg) == 0) {
        printf("[DIAG] 堵塞流量阈值已更新: %.2f L/min\r\n", cfg.blockage_flow_thresh);
    } else {
        printf("[DIAG] 堵塞流量阈值更新失败（超范围）\r\n");
    }
}

/**
 * @brief  微调堵塞判定最小水泵PWM阈值
 * @param  delta: 增量（%）
 */
static void main_adjust_diag_blockage_pump_pwm(int8_t delta)
{
    diag_threshold_config_t cfg;
    int16_t next_pwm;
    if (diag_threshold_get(&cfg) != 0) {
        printf("[DIAG] 获取阈值失败\r\n");
        return;
    }

    next_pwm = (int16_t)cfg.blockage_pump_pwm_min + delta;
    if (next_pwm < 0) next_pwm = 0;
    if (next_pwm > 100) next_pwm = 100;
    cfg.blockage_pump_pwm_min = (uint8_t)next_pwm;

    if (diag_threshold_set(&cfg) == 0) {
        printf("[DIAG] 堵塞最小水泵PWM阈值已更新: %u%%\r\n", cfg.blockage_pump_pwm_min);
    } else {
        printf("[DIAG] 堵塞最小水泵PWM阈值更新失败\r\n");
    }
}

/**
 * @brief  打印串口命令帮助
 */
static void main_print_uart_help(void)
{
    printf("\r\n========== 串口命令速查 ==========\r\n");
    printf("  h/? - 显示本帮助\r\n");
    printf("  v   - 切换详细日志(开/关)\r\n");
    printf("  c   - 手动强制重连链路(WebSocket+AIR780E)\r\n");
    printf("  x   - 注入链路异常(测试自动重连)\r\n");
    printf("  t   - 一键链路回归(x->r->c->r)\r\n");
    printf("  a   - 通信快照(服务器/状态/统计)\r\n");
    printf("  z   - P2收工检查(上报+回归+快照)\r\n");
    printf("  k   - 打印诊断阈值配置\r\n");
    printf("  u   - 恢复诊断阈值默认值\r\n");
    printf("  q/w - 堵塞流量阈值 -/+0.1 L/min\r\n");
    printf("  Q   - 打印诊断快照(各故障置信度)\r\n");
    printf("  g/G - 堵塞水泵PWM阈值 -/+5%%\r\n");
    printf("  d   - 切换流量台架覆盖模式(无水路联调)\r\n");
    printf("  D   - 切换流量台架预设(0.0/1.2/3.0/6.0 L/min)\r\n");
    printf("  b   - 打印蜂鸣器状态\r\n");
    printf("  y   - 切换蜂鸣器静音\r\n");
    printf("  j/J - 蜂鸣器CRIT置信度阈值 -/+0.05\r\n");
    printf("  m   - 打印TinyML状态\r\n");
    printf("  M   - 切换TinyML开关\r\n");
    printf("  n/N - TinyML异常阈值 -/+0.05\r\n");
    printf("  o   - 切换TinyML后端(heuristic/stub/real)\r\n");
    printf("  O   - 执行TinyMaix真实后端自检(init+infer)\r\n");
    printf("  i   - 切换WebSocket TinyML扩展字段上报(开/关)\r\n");
    printf("  I   - 查看WebSocket TinyML扩展字段上报状态\r\n");
    printf("  1   - 立即发送实时数据\r\n");
    printf("  2   - 立即发送健康状态\r\n");
    printf("  3   - 查看WebSocket连接状态\r\n");
    printf("  r   - 查看链路重连状态(WebSocket+AIR780E)\r\n");
    printf("  4   - 查看温度/流量实时值\r\n");
    printf("  5   - 快速调整PID(Kp/Ki/Kd)\r\n");
    printf("  6   - 打印TDS详细信息\r\n");
    printf("  0   - 打印ADC快照(压力/电流/TDS)\r\n");
    printf("  7   - 打印当前标定参数\r\n");
    printf("  8   - 电流零点校准(模拟模式自动跳过)\r\n");
    printf("  9   - 恢复方案默认标定参数\r\n");
    printf("  p   - 压力零点校准(无压状态)\r\n");
    printf("  [/] - 流量每升脉冲-5/+5\r\n");
    printf("  f   - 查看Flash标定状态(简版)\r\n");
    printf("  F   - 查看Flash标定状态(详细)\r\n");
    printf("  e   - 清空Flash标定并恢复默认参数\r\n");
    printf("  s   - 保存标定参数到Flash(含全量诊断阈值)\r\n");
    printf("  l   - 从Flash加载标定参数(含全量诊断阈值)\r\n");
    printf("==================================\r\n\r\n");
}

int main(void)
{
    /* 系统初始化 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    printf("\r\n========================================\r\n");
    printf("  智能水冷系统 - WebSocket测试版本\r\n");
    printf("  作者: 智能水冷项目团队\r\n");
    printf("  日期: 2026-02-02\r\n");
    printf("========================================\r\n");

    /* 初始化调度器（包含WebSocket初始化） */
    printf("\r\n初始化调度器...\r\n");
    scheduler_init();
    printf("调度器初始化完成\r\n");

    printf("系统启动完成，输入 h 查看串口命令\r\n\r\n");

    /* 主循环 */
    while(1)
    {
        /* 运行调度器 */
        scheduler_run();
        
        /* 检测串口输入（手动触发测试） */
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET)
        {
            uint8_t ch = USART_ReceiveData(USART1);
            
            switch (ch) {
                case 'c':
                case 'C':
                    printf("\r\n[手动测试] 手动强制重连链路(WebSocket+AIR780E)\r\n");
                    websocket_connect();
                    break;

                case 'x':
                case 'X':
                    printf("\r\n[手动测试] 注入链路异常，验证自动重连\r\n");
                    air780e_mark_error();
                    ws_client_disconnect();
                    break;
                    
                case '1':
                    printf("\r\n[手动测试] 发送实时数据\r\n");
                    websocket_send_realtime();
                    break;
                    
                case '2':
                    printf("\r\n[手动测试] 发送健康状态\r\n");
                    websocket_send_health();
                    break;
                    
                case '3':
                    printf("\r\n[手动测试] 查看连接状态\r\n");
                    if (websocket_is_connected()) {
                        printf("WebSocket: 已连接\r\n");
                    } else {
                        printf("WebSocket: 未连接\r\n");
                    }
                    break;

                case 'r':
                case 'R':
                    printf("\r\n[手动测试] 查看链路重连状态\r\n");
                    main_print_link_reconnect_status();
                    break;

                case 't':
                case 'T':
                    printf("\r\n[手动测试] 一键链路回归\r\n");
                    main_run_link_regression();
                    break;

                case 'a':
                case 'A':
                    printf("\r\n[手动测试] 通信链路快照\r\n");
                    main_print_comm_snapshot();
                    break;

                case 'z':
                case 'Z':
                    printf("\r\n[手动测试] P2收工检查\r\n");
                    main_run_p2_closeout();
                    break;

                case 'k':
                case 'K':
                    printf("\r\n[手动测试] 打印诊断阈值配置\r\n");
                    main_print_diag_thresholds();
                    break;

                case 'u':
                case 'U':
                    printf("\r\n[手动测试] 恢复诊断阈值默认值\r\n");
                    diag_threshold_reset_defaults();
                    main_print_diag_thresholds();
                    break;

                case 'q':
                    printf("\r\n[手动测试] 堵塞流量阈值 -0.1 L/min\r\n");
                    main_adjust_diag_blockage_flow(-0.1f);
                    break;

                case 'w':
                    printf("\r\n[手动测试] 堵塞流量阈值 +0.1 L/min\r\n");
                    main_adjust_diag_blockage_flow(0.1f);
                    break;

                case 'Q':
                    printf("\r\n[手动测试] 打印诊断快照\r\n");
                    scheduler_diag_print_snapshot();
                    break;

                case 'g':
                    printf("\r\n[手动测试] 堵塞最小水泵PWM阈值 -5%%\r\n");
                    main_adjust_diag_blockage_pump_pwm(-5);
                    break;

                case 'G':
                    printf("\r\n[手动测试] 堵塞最小水泵PWM阈值 +5%%\r\n");
                    main_adjust_diag_blockage_pump_pwm(5);
                    break;

                case 'd':
                    printf("\r\n[手动测试] 切换流量台架覆盖模式\r\n");
                    flow_set_bench_override((uint8_t)!flow_is_bench_override_enabled());
                    printf("[FLOW] bench_override=%u, preset=%.1f L/min\r\n",
                           flow_is_bench_override_enabled(),
                           flow_get_bench_override_rate());
                    break;

                case 'D':
                    printf("\r\n[手动测试] 切换流量台架覆盖预设\r\n");
                    flow_cycle_bench_override_rate();
                    printf("[FLOW] bench_override=%u, preset=%.1f L/min\r\n",
                           flow_is_bench_override_enabled(),
                           flow_get_bench_override_rate());
                    break;

                case 'b':
                case 'B':
                    printf("\r\n[手动测试] 查看蜂鸣器状态\r\n");
                    scheduler_alarm_print_status();
                    break;

                case 'y':
                case 'Y':
                    printf("\r\n[手动测试] 切换蜂鸣器静音\r\n");
                    scheduler_alarm_toggle_mute();
                    break;

                case 'j':
                    printf("\r\n[手动测试] 蜂鸣器CRIT置信度阈值 -0.05\r\n");
                    scheduler_alarm_adjust_diag_crit_conf(-0.05f);
                    break;

                case 'J':
                    printf("\r\n[手动测试] 蜂鸣器CRIT置信度阈值 +0.05\r\n");
                    scheduler_alarm_adjust_diag_crit_conf(0.05f);
                    break;

                case 'm':
                    printf("\r\n[手动测试] 查看TinyML状态\r\n");
                    scheduler_tinyml_print_status();
                    break;

                case 'M':
                    printf("\r\n[手动测试] 切换TinyML开关\r\n");
                    scheduler_tinyml_toggle_enable();
                    break;

                case 'n':
                    printf("\r\n[手动测试] TinyML异常阈值 -0.05\r\n");
                    scheduler_tinyml_adjust_threshold(-0.05f);
                    break;

                case 'N':
                    printf("\r\n[手动测试] TinyML异常阈值 +0.05\r\n");
                    scheduler_tinyml_adjust_threshold(0.05f);
                    break;

                case 'o':
                    printf("\r\n[手动测试] 切换TinyML推理后端\r\n");
                    scheduler_tinyml_cycle_backend();
                    break;

                case 'O':
                    printf("\r\n[手动测试] TinyMaix真实后端自检\r\n");
                    scheduler_tinyml_self_test();
                    break;

                case 'i':
                    printf("\r\n[手动测试] 切换WebSocket TinyML扩展字段上报\r\n");
                    json_set_tinyml_ext_enabled((uint8_t)!json_is_tinyml_ext_enabled());
                    printf("[WS_JSON] TinyML扩展字段: %s\r\n",
                           json_is_tinyml_ext_enabled() ? "开启" : "关闭");
                    break;

                case 'I':
                    printf("\r\n[手动测试] 查看WebSocket TinyML扩展字段上报状态\r\n");
                    printf("[WS_JSON] TinyML扩展字段: %s\r\n",
                           json_is_tinyml_ext_enabled() ? "开启" : "关闭");
                    break;
                    
                case '4':
                    printf("\r\n[手动测试] 查看传感器数据\r\n");
                    {
                        const temp_data_t *temp = temp_get_data();
                        const flow_data_t *flow = flow_get_data();
                        printf("CPU温度: %.1f°C\r\n", temp->cpu_temp);
                        printf("水温: %.1f°C\r\n", temp->water_temp);
                        printf("环境温度: %.1f°C\r\n", temp->env_temp);
                        printf("流速: %.1fL/min\r\n", flow->flow_rate);
                    }
                    break;
                    
                case '5':
                    printf("\r\n[手动测试] 调整PID参数\r\n");
                    {
                        extern void pid_tune_online(float kp, float ki, float kd);
                        pid_tune_online(6.0f, 1.2f, 0.6f);
                    }
                    break;

                case '6':
                    printf("\r\n[手动测试] TDS水质信息\r\n");
                    app_tds_print_info();
                    break;

                case '0':
                    printf("\r\n[手动测试] ADC快照\r\n");
                    scheduler_calibration_print_adc_snapshot();
                    break;

                case '7':
                    printf("\r\n[手动测试] 查看标定参数\r\n");
                    scheduler_print_calibration();
                    break;

                case '8':
                    printf("\r\n[手动测试] 执行电流零点校准（模拟模式会自动跳过）\r\n");
                    scheduler_calibrate_current_zero();
                    break;

                case '9':
                    printf("\r\n[手动测试] 恢复方案默认标定参数\r\n");
                    scheduler_apply_calibration_defaults();
                    scheduler_print_calibration();
                    break;

                case 's':
                case 'S':
                    printf("\r\n[手动测试] 保存当前标定参数到Flash（含全量诊断阈值）\r\n");
                    scheduler_calibration_save_to_flash();
                    break;

                case 'l':
                case 'L':
                    printf("\r\n[手动测试] 从Flash加载标定参数（含全量诊断阈值）\r\n");
                    if (scheduler_calibration_load_from_flash() == 0) {
                        scheduler_print_calibration();
                    } else {
                        printf("[CAL] Flash中暂无有效标定参数\r\n");
                    }
                    break;

                case 'p':
                case 'P':
                    printf("\r\n[手动测试] 压力零点校准（无压状态）\r\n");
                    scheduler_calibrate_pressure_zero();
                    break;

                case '[':
                    printf("\r\n[手动测试] 流量标定微调：每升脉冲 -5\r\n");
                    scheduler_adjust_flow_pulses_per_liter(-5);
                    break;

                case ']':
                    printf("\r\n[手动测试] 流量标定微调：每升脉冲 +5\r\n");
                    scheduler_adjust_flow_pulses_per_liter(5);
                    break;

                case 'f':
                    printf("\r\n[手动测试] 查看Flash标定状态（简版）\r\n");
                    scheduler_calibration_print_flash_status_brief();
                    break;

                case 'F':
                    printf("\r\n[手动测试] 查看Flash标定状态（详细）\r\n");
                    scheduler_calibration_print_flash_status();
                    break;

                case 'e':
                case 'E':
                    printf("\r\n[手动测试] 清空Flash标定并恢复默认参数\r\n");
                    scheduler_calibration_clear_flash();
                    break;

                case 'v':
                case 'V':
                    scheduler_set_verbose_log((uint8_t)!scheduler_is_verbose_log_enabled());
                    break;

                case 'h':
                case 'H':
                case '?':
                    main_print_uart_help();
                    break;
                    
                default:
                    break;
            }
        }
        
        Delay_Ms(10);  /* 短暂延时 */
    }
}
