/*
 * 存储模块（storage）
 * 功能概述：挂载板载 microSD 卡（SDMMC 1-bit 模式），提供把 JPEG 存成文件的接口。
 * 依赖：esp_driver_sdmmc / fatfs；引脚和挂载点见 Kconfig。
 */
#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * 功能：挂载 SD 卡（FAT 文件系统）
 * 参数：无
 * 返回：ESP_OK 挂载成功；ESP_ERR_NOT_FOUND 没插卡或卡无法识别；
 *       其他错误码表示挂载失败（如文件系统损坏）
 */
esp_err_t storage_init(void);

/*
 * 功能：把一张 JPEG 存成文件，文件名自动按序号递增（如 0001.jpg）
 * 参数：buf 图片数据；len 数据长度（字节）
 * 返回：ESP_OK 成功；ESP_ERR_INVALID_STATE 未挂载；
 *       ESP_ERR_INVALID_ARG 参数非法；ESP_FAIL 写入失败
 */
esp_err_t storage_save_jpeg(const uint8_t *buf, size_t len);

/*
 * 功能：查询 SD 卡是否已挂载可用
 * 参数：无
 * 返回：true 已挂载；false 未挂载
 */
bool storage_is_ready(void);

#endif /* STORAGE_H */
