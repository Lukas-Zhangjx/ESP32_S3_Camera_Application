/*
 * 舵机模块实现
 * 功能概述：LEDC 50Hz PWM，按角度换算成脉宽控制标准 RC 舵机。
 * 依赖：esp_driver_ledc
 */
#include "servo.h"

#include <stdbool.h>
#include "driver/ledc.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "servo";

/* 摄像头 XCLK 占了 timer0/channel0，这里必须错开 */
#define SERVO_LEDC_TIMER   LEDC_TIMER_1
#define SERVO_LEDC_CHANNEL LEDC_CHANNEL_1
#define SERVO_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_RES     LEDC_TIMER_14_BIT
#define SERVO_FREQ_HZ      50
#define SERVO_PERIOD_US    (1000000 / SERVO_FREQ_HZ)
#define SERVO_ANGLE_MAX    180

static bool s_ready = false;
static int s_angle = -1;

/*
 * 功能：把角度换算成 LEDC 占空比
 * 参数：angle 0~180
 * 返回：对应的占空比数值
 */
static uint32_t servo_angle_to_duty(int angle)
{
    int span_us = CONFIG_SERVO_MAX_PULSE_US - CONFIG_SERVO_MIN_PULSE_US;
    int pulse_us = CONFIG_SERVO_MIN_PULSE_US + span_us * angle / SERVO_ANGLE_MAX;
    /* 占空比 = 脉宽占整个 20ms 周期的比例 × 满量程 */
    return (uint32_t)(((uint64_t)pulse_us << SERVO_LEDC_RES) / SERVO_PERIOD_US);
}

esp_err_t servo_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_RES,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: 0x%x, fallback: abort init", err);
        return err;
    }

    ledc_channel_config_t channel_cfg = {
        .gpio_num = CONFIG_SERVO_PIN,
        .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: 0x%x, fallback: abort init", err);
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "servo ready on GPIO%d", CONFIG_SERVO_PIN);

    /* 上电先回中位，避免舵机停在未知角度 */
    return servo_set_angle(SERVO_ANGLE_MAX / 2);
}

esp_err_t servo_set_angle(int angle)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (angle < 0) {
        angle = 0;
    } else if (angle > SERVO_ANGLE_MAX) {
        angle = SERVO_ANGLE_MAX;
    }

    uint32_t duty = servo_angle_to_duty(angle);

    esp_err_t err = ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty failed: 0x%x", err);
        return err;
    }
    err = ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_update_duty failed: 0x%x", err);
        return err;
    }

    s_angle = angle;
    ESP_LOGI(TAG, "angle -> %d deg (duty %u)", angle, (unsigned)duty);
    return ESP_OK;
}

int servo_get_angle(void)
{
    return s_ready ? s_angle : -1;
}
