/*
 * 应用入口
 * 功能概述：组装各功能模块，不实现具体业务逻辑。流程：WiFi 连接 → 摄像头
 * 初始化 → MQTT 桥接初始化；收到 MQTT 拍照指令后拍一张照并发布出去。
 * 依赖：wifi、camera、mqtt_bridge 模块
 */
#include <stdint.h>

#include "camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_bridge.h"
#include "wifi.h"

static const char *TAG = "app_main";

/*
 * 功能：收到 MQTT 拍照指令时的回调，拍一张照并发布出去
 * 参数：无
 * 返回：无
 */
static void on_capture_cmd(void)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    void *handle = NULL;

    esp_err_t cap_err = camera_capture(&buf, &len, &handle, 2000);
    if (cap_err != ESP_OK) {
        ESP_LOGW(TAG, "capture failed: 0x%x, fallback: skip this request", cap_err);
        return;
    }

    esp_err_t pub_err = mqtt_bridge_publish_photo(buf, len, 3000);
    if (pub_err != ESP_OK) {
        ESP_LOGW(TAG, "publish failed: 0x%x", pub_err);
    } else {
        ESP_LOGI(TAG, "published photo, %u bytes", (unsigned)len);
    }

    camera_release_frame(handle);
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

    err = mqtt_bridge_init(on_capture_cmd, 10000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt_bridge_init failed: 0x%x, fallback: stop", err);
        return;
    }

    ESP_LOGI(TAG, "ready, waiting for capture command via MQTT");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
