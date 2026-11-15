/*
 * MQTT 桥接模块实现
 * 功能概述：封装 esp-mqtt，对外只暴露 mqtt_bridge.h 声明的接口。
 * 依赖：esp-mqtt（ESP-IDF 的 mqtt 组件，公共头文件名 mqtt_client.h，
 * 和本模块头文件 mqtt_bridge.h 刻意区分开避免重名冲突）
 */
#include "mqtt_bridge.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

static const char *TAG = "mqtt_bridge";

#define MQTT_BRIDGE_CONNECTED_BIT BIT0
#define MQTT_BRIDGE_PUBLISHED_BIT BIT1

static esp_mqtt_client_handle_t s_client = NULL;
static EventGroupHandle_t s_event_group = NULL;
static mqtt_bridge_capture_cmd_cb_t s_capture_cb = NULL;
static volatile int s_pending_publish_msg_id = -1;

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
        if (s_capture_cb != NULL &&
            event->topic_len == (int)strlen(CONFIG_MQTT_BRIDGE_CMD_TOPIC) &&
            strncmp(event->topic, CONFIG_MQTT_BRIDGE_CMD_TOPIC, event->topic_len) == 0) {
            s_capture_cb();
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

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_BRIDGE_BROKER_URI,
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

esp_err_t mqtt_bridge_publish_photo(const uint8_t *buf, size_t len, uint32_t timeout_ms)
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
                                          (const char *)buf, (int)len, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "publish failed to enqueue, fallback: skip");
        return ESP_FAIL;
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
}
