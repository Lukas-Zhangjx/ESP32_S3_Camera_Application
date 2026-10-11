/*
 * 摄像头模块（camera）实现
 * 功能概述：封装 esp32-camera 组件，对外只暴露 camera.h 声明的接口。
 * 依赖：esp32-camera（esp_camera.h），引脚/时钟频率配置来自 Kconfig。
 */
#include "camera.h"

#include <stdbool.h>
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "camera";

/* 内部状态，禁止模块外直接访问 */
static bool s_camera_ready = false;

/*
 * 功能：校验 menuconfig 中的引脚是否已配置（默认值为 -1，表示未填写）
 * 参数：无
 * 返回：true 表示必需引脚均已配置；false 表示有未配置项
 */
static bool camera_pins_configured(void)
{
    /* XCLK/SIOD/SIOC/VSYNC/HREF/PCLK 是时序必需引脚，缺一不可；D0-D7 同理 */
    return CONFIG_CAMERA_PIN_XCLK >= 0 && CONFIG_CAMERA_PIN_SIOD >= 0 &&
           CONFIG_CAMERA_PIN_SIOC >= 0 && CONFIG_CAMERA_PIN_VSYNC >= 0 &&
           CONFIG_CAMERA_PIN_HREF >= 0 && CONFIG_CAMERA_PIN_PCLK >= 0 &&
           CONFIG_CAMERA_PIN_D0 >= 0 && CONFIG_CAMERA_PIN_D1 >= 0 &&
           CONFIG_CAMERA_PIN_D2 >= 0 && CONFIG_CAMERA_PIN_D3 >= 0 &&
           CONFIG_CAMERA_PIN_D4 >= 0 && CONFIG_CAMERA_PIN_D5 >= 0 &&
           CONFIG_CAMERA_PIN_D6 >= 0 && CONFIG_CAMERA_PIN_D7 >= 0;
}

esp_err_t camera_init(void)
{
    if (s_camera_ready) {
        return ESP_OK;
    }

    /* 引脚未经硬件验证前拒绝初始化，避免用错误引脚跑出难以定位的现象 */
    if (!camera_pins_configured()) {
        ESP_LOGE(TAG, "camera pins not configured, run `idf.py menuconfig` -> Camera Configuration, fallback: abort init");
        return ESP_ERR_INVALID_STATE;
    }

    camera_config_t config = {
        .pin_pwdn = CONFIG_CAMERA_PIN_PWDN,
        .pin_reset = CONFIG_CAMERA_PIN_RESET,
        .pin_xclk = CONFIG_CAMERA_PIN_XCLK,
        .pin_sccb_sda = CONFIG_CAMERA_PIN_SIOD,
        .pin_sccb_scl = CONFIG_CAMERA_PIN_SIOC,
        .pin_d7 = CONFIG_CAMERA_PIN_D7,
        .pin_d6 = CONFIG_CAMERA_PIN_D6,
        .pin_d5 = CONFIG_CAMERA_PIN_D5,
        .pin_d4 = CONFIG_CAMERA_PIN_D4,
        .pin_d3 = CONFIG_CAMERA_PIN_D3,
        .pin_d2 = CONFIG_CAMERA_PIN_D2,
        .pin_d1 = CONFIG_CAMERA_PIN_D1,
        .pin_d0 = CONFIG_CAMERA_PIN_D0,
        .pin_vsync = CONFIG_CAMERA_PIN_VSYNC,
        .pin_href = CONFIG_CAMERA_PIN_HREF,
        .pin_pclk = CONFIG_CAMERA_PIN_PCLK,
        .xclk_freq_hz = CONFIG_CAMERA_XCLK_FREQ_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_VGA,
        .jpeg_quality = 12,
        .fb_count = 1,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x, fallback: check pin mapping/wiring", err);
        return err;
    }

    /* OV3660 出厂默认图像方向/色彩参数不理想，按传感器型号做一次性校正 */
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL && sensor->id.PID == OV3660_PID) {
        sensor->set_vflip(sensor, 1);
        sensor->set_brightness(sensor, 1);
        sensor->set_saturation(sensor, -2);
    }

    s_camera_ready = true;
    ESP_LOGI(TAG, "camera init ok, sensor PID=0x%x", sensor != NULL ? sensor->id.PID : 0);
    return ESP_OK;
}

esp_err_t camera_capture(uint8_t **out_buf, size_t *out_len, void **out_handle, uint32_t timeout_ms)
{
    if (out_buf == NULL || out_len == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_camera_ready) {
        ESP_LOGE(TAG, "camera not initialized, fallback: skip capture");
        return ESP_ERR_INVALID_STATE;
    }

    /* esp_camera_fb_get() 底层不带超时参数，这里用时间片轮询加上限，避免永久阻塞 */
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    camera_fb_t *fb = NULL;
    while (1) {
        fb = esp_camera_fb_get();
        if (fb != NULL) {
            break;
        }
        if (xTaskGetTickCount() >= deadline) {
            ESP_LOGE(TAG, "capture timeout after %u ms, fallback: skip this frame", (unsigned)timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    *out_buf = fb->buf;
    *out_len = fb->len;
    *out_handle = fb;
    return ESP_OK;
}

void camera_release_frame(void *handle)
{
    if (handle != NULL) {
        esp_camera_fb_return((camera_fb_t *)handle);
    }
}

void camera_deinit(void)
{
    if (!s_camera_ready) {
        return;
    }
    esp_camera_deinit();
    s_camera_ready = false;
}
