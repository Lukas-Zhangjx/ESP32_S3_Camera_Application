/*
 * 推流服务模块（stream_service）
 * 功能概述：把 camera 模块取到的帧通过 mqtt_bridge 发布出去，支持两种模式：
 * 单张抓拍（收到一次指令拍一张）和连续推流（自己按固定间隔不停拍不停发）。
 * 依赖：camera、mqtt_bridge 两个模块的公开接口；推流间隔见 Kconfig。
 */
#ifndef STREAM_SERVICE_H
#define STREAM_SERVICE_H

#include "esp_err.h"

/*
 * 功能：初始化推流服务，创建内部工作任务（初始为停止状态）
 * 参数：无
 * 返回：ESP_OK 成功；ESP_ERR_NO_MEM 任务或锁创建失败
 */
esp_err_t stream_service_init(void);

/*
 * 功能：拍一张并发布（用 QoS 1，确保送达）
 * 参数：无
 * 返回：无（失败只打日志，不中断调用方）
 */
void stream_service_capture_once(void);

/*
 * 功能：开始连续推流，按 Kconfig 配置的间隔持续拍照发布（用 QoS 0）
 * 参数：无
 * 返回：无
 */
void stream_service_start(void);

/*
 * 功能：停止连续推流
 * 参数：无
 * 返回：无
 */
void stream_service_stop(void);

#endif /* STREAM_SERVICE_H */
