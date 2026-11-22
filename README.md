# ESP32-S3 Camera Application

基于 ESP-IDF 的 ESP32-S3 摄像头固件：通过 MQTT 指令远程触发拍照，并把 JPEG 回传。

## 硬件

| 项目 | 型号 |
|---|---|
| 开发板 | ESP32-S3-WROOM N16R8（16MB Flash + 8MB Octal PSRAM） |
| 摄像头 | OV3660（FPC 排线直插板载插座） |
| 存储卡 | 板子背面 microSD 卡槽，SDMMC 1-bit 模式（CLK 39 / CMD 38 / DAT0 40） |
| 烧录口 | 板上的 **USB-UART** 口（不是 USB-OTG） |

SD 卡需要是 **FAT32** 格式（Mac 上用磁盘工具抹掉，格式选 MS-DOS (FAT)、方案选
主引导记录）。固件不会自动格式化，挂载失败只警告不中断，避免误抹卡上数据。
单张抓拍的照片会存成 `/sdcard/0001.jpg` 这样的递增编号；连续推流的帧不存卡。

摄像头引脚已在 `components/camera/Kconfig` 中配置好，实机验证通过：

| 信号 | GPIO | 信号 | GPIO |
|---|---|---|---|
| XCLK | 15 | PCLK | 13 |
| SIOD | 4 | VSYNC | 6 |
| SIOC | 5 | HREF | 7 |
| D0~D3 | 11, 9, 8, 10 | D4~D7 | 12, 18, 17, 16 |

PWDN / RESET 未连接（配置为 -1）。

## 模块结构

```
components/
├── camera/          摄像头初始化与取帧
├── wifi/            WiFi 连接
├── mqtt_bridge/     MQTT 连接、订阅指令、发布图片
├── storage/         microSD 卡存储
└── stream_service/  单张抓拍与连续推流，串起上面几个模块
main/                只做组装，调用各模块的公开接口
tools/               调试辅助工具
```

模块之间只通过公开头文件里的函数接口交互，详见 [CLAUDE.md](CLAUDE.md) 中的开发规范。

## 首次构建前

账号密码一律不入库，需要自己创建两个本地配置文件：

```bash
cp components/wifi/wifi_secrets.h.example components/wifi/wifi_secrets.h
cp components/mqtt_bridge/mqtt_secrets.h.example components/mqtt_bridge/mqtt_secrets.h
```

分别填入：

- `wifi_secrets.h` — WiFi 的 SSID 和密码
- `mqtt_secrets.h` — MQTT Broker 的用户名和密码（HiveMQ Cloud 控制台 → Access Management 创建）

两个文件都已在 `.gitignore` 中，不会被提交。

## 构建与烧录

```bash
source $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

端口名用 `ls /dev/cu.*` 查看（USB-UART 口一般显示为 `cu.usbmodem*` 或 `cu.usbserial*`）。

## 使用方式

板子上电后会依次连接 WiFi、初始化摄像头、连接 MQTT Broker，日志出现
`ready, waiting for capture command via MQTT` 即表示就绪。

| 用途 | Topic | 消息内容 |
|---|---|---|
| 拍单张 | `esp32cam/s3/capture/cmd` | 任意内容（如 `go`） |
| 开始连续推流 | `esp32cam/s3/capture/cmd` | `start` |
| 停止连续推流 | `esp32cam/s3/capture/cmd` | `stop` |
| 接收 JPEG 图片数据 | `esp32cam/s3/capture/image` | — |

连续推流时板子按 `Stream Service Configuration` 里配置的间隔自己持续拍照发布，
用 QoS 0（丢帧可接受，省掉一次确认往返）；单张抓拍用 QoS 1 保证送达。

实测通过 HiveMQ Cloud 约 **3.2 fps**（VGA，约 8.8KB/帧，带宽约 29KB/s）。
瓶颈是每帧的 TLS 加密+上传耗时，不是摄像头（取帧只要 0~1ms）。想要更流畅的
视频需要改用 RTSP 或 WebRTC，MQTT 这条路到不了那个量级。

Broker 地址和 topic 可在 `idf.py menuconfig` → `MQTT Bridge Configuration` 中修改，
用户名密码在 `mqtt_secrets.h` 里。当前使用 HiveMQ Cloud 私有集群，强制 TLS
（`mqtts://`，端口 8883），服务端证书用 ESP-IDF 自带的根证书包校验。

在 HiveMQ 控制台的 Access Management 里，除了创建凭据，还要给凭据加一条
权限：Permission Type 选 `Publish and Subscribe`，Topic Filter 填 `esp32cam/#`，
否则连得上但收发会被拒绝。

### 查看图片

MQTT Explorer 之类的通用客户端会把图片当文本显示成乱码，仓库里提供了一个网页查看器：

**直接用浏览器打开 [`tools/mqtt_viewer.html`](tools/mqtt_viewer.html) 即可**，不需要任何服务器。
页面功能：

- 填入用户名密码（同 `mqtt_secrets.h` 里那组），点「连接」接入 Broker
  （走 MQTT over WebSocket Secure，`wss://`，端口 8884）
- 点「拍照」下发单张抓拍指令
- 点「实时」开启连续推流（再点一次停止），页面会显示实时帧率
- 回传的 JPEG 直接渲染显示，并显示大小和时间
- Broker 地址与 topic 可在页面上直接修改

> 用 MQTT Explorer 等通用客户端时注意：订阅主题要填具体路径（如 `esp32cam/#`）。
> 有些 Broker 会拒绝通配符 `#` 的订阅请求且不返回任何错误提示，界面会一直是空的。

## 开发规范

本项目的分支策略、命名规范、超时策略等要求见 [CLAUDE.md](CLAUDE.md)。
