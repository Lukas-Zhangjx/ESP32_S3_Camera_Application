/*
 * MQTT 桥接模块实现
 * 功能概述：封装 esp-mqtt，对外只暴露 mqtt_bridge.h 声明的接口。
 * 依赖：esp-mqtt（ESP-IDF 的 mqtt 组件，公共头文件名 mqtt_client.h，
 * 和本模块头文件 mqtt_bridge.h 刻意区分开避免重名冲突）；
 * TLS 根证书用 ESP-IDF 自带的证书包；登录凭据读取自 mqtt_secrets.h
 * （本地文件，已加入 .gitignore，参考同目录下的 mqtt_secrets.h.example 创建）
 */
#include "mqtt_bridge.h"

#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mqtt_secrets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

static const char *TAG = "mqtt_bridge";

#define MQTT_BRIDGE_CONNECTED_BIT BIT0
#define MQTT_BRIDGE_PUBLISHED_BIT BIT1
#define MQTT_BRIDGE_CMD_TASK_STACK 4096
#define MQTT_BRIDGE_CMD_TASK_PRIO  5
#define MQTT_BRIDGE_CMD_MAX_LEN    31
#define MQTT_BRIDGE_OUT_BUFFER_SIZE 16384

static esp_mqtt_client_handle_t s_client = NULL;
static EventGroupHandle_t s_event_group = NULL;
static mqtt_bridge_capture_cmd_cb_t s_capture_cb = NULL;
static volatile int s_pending_publish_msg_id = -1;
static SemaphoreHandle_t s_cmd_sem = NULL;
static TaskHandle_t s_cmd_task = NULL;
static char s_cmd_payload[MQTT_BRIDGE_CMD_MAX_LEN + 1];
static int s_cmd_payload_len = 0;

/*
 * 功能：指令处理任务，收到通知后在自己的任务上下文里执行回调
 * 参数：arg 未使用
 * 返回：不返回
 *
 * 为什么要单开一个任务：MQTT 事件回调跑在 esp-mqtt 自己的任务里，如果在回调
 * 中直接执行耗时的拍照+发布，发布确认事件就没法被派发（要派发它的正是被堵住
 * 的那个任务），必然超时，进而触发 esp-mqtt 重传、收到重复图片。
 */
static void mqtt_bridge_cmd_task(void *arg)
{
    while (1) {
        /* 有限超时等待，不使用 portMAX_DELAY；超时就继续下一轮空转 */
        if (xSemaphoreTake(s_cmd_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (s_capture_cb != NULL) {
                s_capture_cb(s_cmd_payload, s_cmd_payload_len);
            }
        }
    }
}

/*
 * 功能：MQTT 事件回调，处理连接建立、收到指令、发布确认
 * 参数：标准 esp_event 回调签名，event_data 为 esp_mqtt_event_handle_t
 * 返回：无
 */
static void mqtt_bridge_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to broker");
        esp_mqtt_client_subscribe(s_client, CONFIG_MQTT_BRIDGE_CMD_TOPIC, 1);
        xEventGroupSetBits(s_event_group, MQTT_BRIDGE_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from broker");
        break;
    case MQTT_EVENT_DATA:
        /* 只响应拍照指令 topic 上的消息，忽略其他意外数据 */
        if (event->topic_len == (int)strlen(CONFIG_MQTT_BRIDGE_CMD_TOPIC) &&
            strncmp(event->topic, CONFIG_MQTT_BRIDGE_CMD_TOPIC, event->topic_len) == 0) {
            /* event->data 在回调返回后失效，先拷贝一份指令内容 */
            int n = event->data_len < MQTT_BRIDGE_CMD_MAX_LEN ? event->data_len : MQTT_BRIDGE_CMD_MAX_LEN;
            memcpy(s_cmd_payload, event->data, n);
            s_cmd_payload[n] = '\0';
            s_cmd_payload_len = n;
            /* 这里只做通知，真正的耗时工作交给 mqtt_bridge_cmd_task，
             * 避免堵住 esp-mqtt 的事件派发 */
            xSemaphoreGive(s_cmd_sem);
        }
        break;
    case MQTT_EVENT_PUBLISHED:
        if (event->msg_id == s_pending_publish_msg_id) {
            xEventGroupSetBits(s_event_group, MQTT_BRIDGE_PUBLISHED_BIT);
        }
        break;
    default:
        break;
    }
}

esp_err_t mqtt_bridge_init(mqtt_bridge_capture_cmd_cb_t on_capture_cmd, uint32_t timeout_ms)
{
    s_capture_cb = on_capture_cmd;

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        ESP_LOGE(TAG, "event group create failed, fallback: abort init");
        return ESP_ERR_NO_MEM;
    }

    s_cmd_sem = xSemaphoreCreateBinary();
    if (s_cmd_sem == NULL) {
        ESP_LOGE(TAG, "semaphore create failed, fallback: abort init");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(mqtt_bridge_cmd_task, "mqtt_cmd", MQTT_BRIDGE_CMD_TASK_STACK,
                    NULL, MQTT_BRIDGE_CMD_TASK_PRIO, &s_cmd_task) != pdPASS) {
        ESP_LOGE(TAG, "cmd task create failed, fallback: abort init");
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_BRIDGE_BROKER_URI,
        /* mqtts:// 地址需要校验服务端证书，用 ESP-IDF 自带的公共根证书包 */
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = MQTT_SECRET_USERNAME,
        .credentials.authentication.password = MQTT_SECRET_PASSWORD,
        /* 默认发送缓冲区只有 1KB，一张 9KB 的图会被切成很多小块写出去，
         * TLS 下每块都要单独加密打包，发布耗时明显变长 */
        .buffer.out_size = MQTT_BRIDGE_OUT_BUFFER_SIZE,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed, fallback: abort init");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_bridge_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: 0x%x, fallback: abort init", err);
        return err;
    }

    /* 有限超时等待连接建立，不使用 portMAX_DELAY 永久阻塞 */
    EventBits_t bits = xEventGroupWaitBits(s_event_group, MQTT_BRIDGE_CONNECTED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (!(bits & MQTT_BRIDGE_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "connect timeout after %u ms, fallback: abort init", (unsigned)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t mqtt_bridge_publish_photo(const uint8_t *buf, size_t len, int qos, uint32_t timeout_ms)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client == NULL) {
        ESP_LOGE(TAG, "mqtt not initialized, fallback: skip publish");
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupClearBits(s_event_group, MQTT_BRIDGE_PUBLISHED_BIT);

    int msg_id = esp_mqtt_client_publish(s_client, CONFIG_MQTT_BRIDGE_IMAGE_TOPIC,
                                          (const char *)buf, (int)len, qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "publish failed to enqueue, fallback: skip");
        return ESP_FAIL;
    }

    /* QoS 0 没有确认报文，esp-mqtt 也就不会产生 PUBLISHED 事件，
     * 这里直接返回，否则每帧都要白等一个超时 */
    if (qos == 0) {
        return ESP_OK;
    }

    s_pending_publish_msg_id = msg_id;

    /* 有限超时等待 Broker 确认，不使用 portMAX_DELAY 永久阻塞 */
    EventBits_t bits = xEventGroupWaitBits(s_event_group, MQTT_BRIDGE_PUBLISHED_BIT,
                                            pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (!(bits & MQTT_BRIDGE_PUBLISHED_BIT)) {
        ESP_LOGE(TAG, "publish ack timeout after %u ms, fallback: treat as failed", (unsigned)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void mqtt_bridge_deinit(void)
{
    if (s_client != NULL) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    if (s_cmd_task != NULL) {
        vTaskDelete(s_cmd_task);
        s_cmd_task = NULL;
    }
}
