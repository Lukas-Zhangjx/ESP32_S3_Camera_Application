/*
 * WiFi 模块
 * 功能概述：连接到 Kconfig 中配置的 WiFi 热点，带超时，断线后有限次数重连。
 * 依赖：esp_wifi, esp_netif, esp_event, nvs_flash（内部自行完成 nvs_flash_init）
 */
#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include "esp_err.h"

/*
 * 功能：初始化并连接 WiFi，阻塞等待直到拿到 IP 或超时
 * 参数：timeout_ms 等待连接成功的超时时间（毫秒）
 * 返回：ESP_OK 连接成功；ESP_ERR_TIMEOUT 超时未连上；
 *       其他错误码表示底层初始化失败
 */
esp_err_t wifi_init(uint32_t timeout_ms);

#endif /* WIFI_H */
