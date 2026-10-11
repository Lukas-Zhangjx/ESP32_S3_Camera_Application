/*
 * 摄像头模块（camera）
 * 功能概述：封装 esp32-camera 组件，提供初始化、取帧、释放帧、反初始化接口。
 * 依赖：esp32-camera（通过 idf_component.yml 引入），引脚配置来自 Kconfig
 *       "Camera Configuration" 菜单，需按实际硬件在 menuconfig 中确认后填写。
 */
#ifndef CAMERA_H
#define CAMERA_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * 功能：初始化摄像头（时钟、SCCB、传感器探测）
 * 参数：无
 * 返回：ESP_OK 成功；
 *       ESP_ERR_INVALID_STATE 引脚未在 menuconfig 中配置；
 *       其他 esp_err_t 错误码表示底层驱动初始化失败（如传感器探测失败）
 */
esp_err_t camera_init(void);

/*
 * 功能：采集一帧图像（JPEG），超时内未取到帧则返回错误，不会无限阻塞
 * 参数：out_buf    [out] 帧数据指针
 *       out_len    [out] 帧数据长度（字节）
 *       out_handle [out] 内部帧句柄，调用方必须在用完后传给 camera_release_frame 释放
 *       timeout_ms [in]  等待采集的超时时间（毫秒）
 * 返回：ESP_OK 成功；ESP_ERR_TIMEOUT 超时；ESP_ERR_INVALID_STATE 未初始化；
 *       ESP_ERR_INVALID_ARG 参数为空
 */
esp_err_t camera_capture(uint8_t **out_buf, size_t *out_len, void **out_handle, uint32_t timeout_ms);

/*
 * 功能：释放 camera_capture 返回的帧缓冲区
 * 参数：handle camera_capture 输出的帧句柄，可为 NULL（此时不做任何操作）
 * 返回：无
 */
void camera_release_frame(void *handle);

/*
 * 功能：反初始化摄像头，释放底层资源
 * 参数：无
 * 返回：无
 */
void camera_deinit(void);

#endif /* CAMERA_H */
