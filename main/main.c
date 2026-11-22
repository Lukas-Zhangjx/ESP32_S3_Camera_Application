/*
 * 应用入口
 * 功能概述：组装各功能模块，不实现具体业务逻辑。流程：WiFi 连接 → 摄像头
 * 初始化 → 推流服务初始化 → MQTT 桥接初始化；收到 MQTT 指令后按内容分发给
 * 推流服务。
 * 依赖：wifi、camera、mqtt_bridge、stream_service 模块
 */
#include <string.h>

#include "camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_bridge.h"
#include "stream_service.h"
#include "wifi.h"

static const char *TAG = "app_main";

/*
 * 功能：收到 MQTT 指令时的回调，按指令内容分发
 * 参数：payload 指令内容；payload_len 内容长度
 * 返回：无
 *
 * 指令约定："start" 开始连续推流，"stop" 停止，其他内容（含空）拍单张。
 */
static void on_mqtt_cmd(const char *payload, int payload_len)
{
    if (payload_len == 5 && strncmp(payload, "start", 5) == 0) {
        stream_service_start();
    } else if (payload_len == 4 && strncmp(payload, "stop", 4) == 0) {
        stream_service_stop();
    } else {
        stream_service_capture_once();
    }
}

void app_main(void)
{
    esp_err_t err = wifi_init(CONFIG_WIFI_CONNECT_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_init failed: 0x%x, fallback: stop", err);
        return;
    }

    err = camera_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera_init failed: 0x%x, fallback: stop", err);
        return;
    }

    err = stream_service_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stream_service_init failed: 0x%x, fallback: stop", err);
        return;
    }

    err = mqtt_bridge_init(on_mqtt_cmd, 10000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt_bridge_init failed: 0x%x, fallback: stop", err);
        return;
    }

    ESP_LOGI(TAG, "ready, waiting for command via MQTT");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
