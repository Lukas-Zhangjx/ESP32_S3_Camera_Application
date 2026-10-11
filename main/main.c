/*
 * 应用入口
 * 功能概述：组装各功能模块，不实现具体业务逻辑。当前只调用 camera 模块完成
 * 初始化并循环采集一帧图像打印大小，用于验证摄像头链路是否跑通。
 * 依赖：camera 模块
 */
#include <stdint.h>

#include "camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

void app_main(void)
{
    esp_err_t err = camera_init();
    if (err != ESP_OK) {
        /* 初始化失败属于不可恢复的配置问题（引脚未配置/探测失败），打日志后退出，不卡死 */
        ESP_LOGE(TAG, "camera_init failed: 0x%x, fallback: stop demo loop", err);
        return;
    }

    while (true) {
        uint8_t *buf = NULL;
        size_t len = 0;
        void *handle = NULL;

        /* 单帧采集最多等待 2 秒，超时则跳过本次，不无限阻塞 */
        esp_err_t cap_err = camera_capture(&buf, &len, &handle, 2000);
        if (cap_err == ESP_OK) {
            ESP_LOGI(TAG, "captured frame, %u bytes", (unsigned)len);
            camera_release_frame(handle);
        } else {
            ESP_LOGW(TAG, "capture failed: 0x%x, retry next loop", cap_err);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
