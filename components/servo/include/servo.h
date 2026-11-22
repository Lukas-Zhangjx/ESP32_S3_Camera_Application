/*
 * 舵机模块（servo）
 * 功能概述：用 LEDC 产生 50Hz PWM 驱动标准 RC 舵机，按角度定位。
 * 依赖：esp_driver_ledc；引脚和脉宽范围见 Kconfig。
 *
 * 注意：摄像头的 XCLK 占用了 LEDC 的 timer0/channel0，本模块用 timer1/channel1，
 * 两者不能冲突，否则会把摄像头时钟冲掉。
 */
#ifndef SERVO_H
#define SERVO_H

#include "esp_err.h"

/*
 * 功能：初始化舵机 PWM 输出，并转到中位（90 度）
 * 参数：无
 * 返回：ESP_OK 成功；其他错误码表示 LEDC 配置失败
 */
esp_err_t servo_init(void);

/*
 * 功能：把舵机转到指定角度
 * 参数：angle 目标角度，有效范围 0~180，超出范围会被截断到边界
 * 返回：ESP_OK 成功；ESP_ERR_INVALID_STATE 未初始化
 */
esp_err_t servo_set_angle(int angle);

/*
 * 功能：读取当前设定的角度（不是舵机实际物理位置，舵机没有反馈）
 * 参数：无
 * 返回：当前设定角度 0~180；未初始化时返回 -1
 */
int servo_get_angle(void);

#endif /* SERVO_H */
