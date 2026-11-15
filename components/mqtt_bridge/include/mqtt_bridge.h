/*
 * MQTT 桥接模块（mqtt_bridge）
 * 功能概述：连接 MQTT Broker，订阅拍照指令 topic，对外提供"收到指令回调"
 * 和"发布照片"两个接口；不直接依赖 camera 模块，图像数据由调用方传入。
 * 依赖：esp-mqtt（ESP-IDF 的 mqtt 组件），需在 WiFi 已连接后调用。
 */
#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* 收到拍照指令时触发的回调函数类型 */
typedef void (*mqtt_bridge_capture_cmd_cb_t)(void);

/*
 * 功能：连接 MQTT Broker 并订阅拍照指令 topic，带超时等待连接建立
 * 参数：on_capture_cmd 收到拍照指令时触发的回调，可为 NULL（不处理指令）
 *       timeout_ms     等待连接建立的超时时间（毫秒）
 * 返回：ESP_OK 成功；ESP_ERR_TIMEOUT 超时；其他错误码表示初始化失败
 */
esp_err_t mqtt_bridge_init(mqtt_bridge_capture_cmd_cb_t on_capture_cmd, uint32_t timeout_ms);

/*
 * 功能：把一张图片发布到图片 topic，带超时等待 Broker 确认
 * 参数：buf 图片数据指针；len 数据长度（字节）；timeout_ms 等待确认的超时时间（毫秒）
 * 返回：ESP_OK 成功；ESP_ERR_TIMEOUT 超时；ESP_ERR_INVALID_STATE 未初始化；
 *       ESP_ERR_INVALID_ARG 参数非法
 */
esp_err_t mqtt_bridge_publish_photo(const uint8_t *buf, size_t len, uint32_t timeout_ms);

/*
 * 功能：断开连接并释放 MQTT 客户端资源
 */
void mqtt_bridge_deinit(void);

#endif /* MQTT_BRIDGE_H */
