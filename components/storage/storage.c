/*
 * 存储模块实现
 * 功能概述：以 SDMMC 1-bit 模式挂载板载 microSD 卡，按序号把 JPEG 存成文件。
 * 依赖：esp_driver_sdmmc、sdmmc、fatfs
 */
#include "storage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";

#define STORAGE_PATH_MAX 64

static sdmmc_card_t *s_card = NULL;
static bool s_ready = false;
static unsigned s_next_index = 1;

/*
 * 功能：扫描卡上已有的 NNNN.jpg 文件，把下一个可用序号记下来
 * 参数：无
 * 返回：无；目录打不开时保持默认序号 1
 *
 * 为什么要扫描：掉电重启后序号不能从头开始，否则会覆盖之前存的照片。
 */
static void storage_scan_next_index(void)
{
    DIR *dir = opendir(CONFIG_STORAGE_MOUNT_POINT);
    if (dir == NULL) {
        ESP_LOGW(TAG, "cannot open %s, fallback: start numbering at 1", CONFIG_STORAGE_MOUNT_POINT);
        return;
    }

    unsigned max_index = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        unsigned index = 0;
        if (sscanf(entry->d_name, "%4u.JPG", &index) == 1 ||
            sscanf(entry->d_name, "%4u.jpg", &index) == 1) {
            if (index > max_index) {
                max_index = index;
            }
        }
    }
    closedir(dir);

    s_next_index = max_index + 1;
    ESP_LOGI(TAG, "next photo index: %u", s_next_index);
}

esp_err_t storage_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = CONFIG_STORAGE_FREQ_KHZ;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = CONFIG_STORAGE_PIN_CLK;
    slot_config.cmd = CONFIG_STORAGE_PIN_CMD;
    slot_config.d0 = CONFIG_STORAGE_PIN_D0;
    /* 板子上一般没有外部上拉，打开内部上拉 */
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        /* 不自动格式化：挂载失败宁可报错，也不要把用户卡里的数据抹了 */
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(CONFIG_STORAGE_MOUNT_POINT, &host,
                                             &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "mount failed: filesystem not FAT? fallback: run without SD card");
        } else {
            ESP_LOGE(TAG, "sd card init failed: 0x%x (card inserted?), fallback: run without SD card", err);
        }
        return err == ESP_FAIL ? ESP_FAIL : ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "sd card mounted at %s, %lluMB", CONFIG_STORAGE_MOUNT_POINT,
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));

    s_ready = true;
    storage_scan_next_index();
    return ESP_OK;
}

esp_err_t storage_save_jpeg(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[STORAGE_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%04u.jpg", CONFIG_STORAGE_MOUNT_POINT, s_next_index);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot open %s for writing, fallback: skip saving", path);
        return ESP_FAIL;
    }

    size_t written = fwrite(buf, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "short write %u/%u bytes (card full?), fallback: skip saving",
                 (unsigned)written, (unsigned)len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "saved %s, %u bytes", path, (unsigned)len);
    s_next_index++;
    return ESP_OK;
}

bool storage_is_ready(void)
{
    return s_ready;
}
