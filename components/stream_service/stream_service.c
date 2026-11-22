/*
 * 推流服务模块实现
 * 功能概述：内部跑一个工作任务，推流开启时按固定间隔取帧并发布；
 * 单张抓拍和推流共用同一把锁，避免同时抢摄像头帧缓冲区。
 * 依赖：camera.h、mqtt_bridge.h 的公开接口
 */
#include "stream_service.h"

#include <stdbool.h>
#include "camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_bridge.h"
#include "sdkconfig.h"

static const char *TAG = "stream_service";

#define STREAM_TASK_STACK      4096
#define STREAM_TASK_PRIO       5
#define STREAM_IDLE_POLL_MS    200
#define STREAM_CAPTURE_TIMEOUT 2000
#define STREAM_PUBLISH_TIMEOUT 3000
#define STREAM_LOCK_TIMEOUT    3000

static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_lock = NULL;
static volatile bool s_streaming = false;

/*
 * 功能：取一帧并发布出去
 * 参数：qos MQTT 服务质量（1 单张抓拍，0 连续推流）
 *       log_ok 成功时是否打日志（推流时每帧都打会刷屏）
 * 返回：无；任何一步失败都打日志并返回，不重试
 */
static void stream_service_publish_frame(int qos, bool log_ok)
{
    /* 单张抓拍和推流可能来自不同任务，用锁串行化，避免同时抢帧缓冲区 */
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(STREAM_LOCK_TIMEOUT)) != pdTRUE) {
        ESP_LOGW(TAG, "busy, fallback: skip this frame");
        return;
    }

    uint8_t *buf = NULL;
    size_t len = 0;
    void *handle = NULL;

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = camera_capture(&buf, &len, &handle, STREAM_CAPTURE_TIMEOUT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "capture failed: 0x%x, fallback: skip this frame", err);
        xSemaphoreGive(s_lock);
        return;
    }
    int64_t t1 = esp_timer_get_time();

    err = mqtt_bridge_publish_photo(buf, len, qos, STREAM_PUBLISH_TIMEOUT);
    int64_t t2 = esp_timer_get_time();
    ESP_LOGD(TAG, "timing: capture %lld ms, publish %lld ms, %u bytes",
             (t1 - t0) / 1000, (t2 - t1) / 1000, (unsigned)len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "publish failed: 0x%x", err);
    } else if (log_ok) {
        ESP_LOGI(TAG, "published photo, %u bytes", (unsigned)len);
    }

    camera_release_frame(handle);
    xSemaphoreGive(s_lock);
}

/*
 * 功能：推流工作任务，开启时按配置间隔持续取帧发布，停止时空转等待
 * 参数：arg 未使用
 * 返回：不返回
 */
static void stream_service_task(void *arg)
{
    while (1) {
        if (!s_streaming) {
            vTaskDelay(pdMS_TO_TICKS(STREAM_IDLE_POLL_MS));
            continue;
        }
        /* 推流帧用 QoS 0：丢一帧无所谓，省掉等待确认的一次往返 */
        stream_service_publish_frame(0, false);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_STREAM_SERVICE_INTERVAL_MS));
    }
}

esp_err_t stream_service_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "mutex create failed, fallback: abort init");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(stream_service_task, "stream_svc", STREAM_TASK_STACK,
                    NULL, STREAM_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "task create failed, fallback: abort init");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void stream_service_capture_once(void)
{
    stream_service_publish_frame(1, true);
}

void stream_service_start(void)
{
    if (!s_streaming) {
        s_streaming = true;
        ESP_LOGI(TAG, "streaming started, interval %d ms", CONFIG_STREAM_SERVICE_INTERVAL_MS);
    }
}

void stream_service_stop(void)
{
    if (s_streaming) {
        s_streaming = false;
        ESP_LOGI(TAG, "streaming stopped");
    }
}
