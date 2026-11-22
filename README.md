# ESP32-S3 Camera Application

基于 ESP-IDF 的 ESP32-S3 摄像头固件：通过 MQTT 指令远程触发拍照，并把 JPEG 回传。

## 硬件

| 项目 | 型号 |
|---|---|
| 开发板 | ESP32-S3-WROOM N16R8（16MB Flash + 8MB Octal PSRAM） |
| 摄像头 | OV3660（FPC 排线直插板载插座） |
| 烧录口 | 板上的 **USB-UART** 口（不是 USB-OTG） |

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
├── camera/        摄像头初始化与取帧
├── wifi/          WiFi 连接
└── mqtt_bridge/   MQTT 连接、订阅指令、发布图片
main/              只做组装，把上面三个模块接起来
tools/             调试辅助工具
```

模块之间只通过公开头文件里的函数接口交互，详见 [CLAUDE.md](CLAUDE.md) 中的开发规范。

## 首次构建前

WiFi 账号密码不入库，需要自己创建本地配置文件：

```bash
cp components/wifi/wifi_secrets.h.example components/wifi/wifi_secrets.h
```

然后编辑 `wifi_secrets.h` 填入真实的 SSID 和密码。该文件已在 `.gitignore` 中，不会被提交。

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

| 用途 | Topic |
|---|---|
| 发送拍照指令（内容任意） | `esp32cam/s3/capture/cmd` |
| 接收 JPEG 图片数据 | `esp32cam/s3/capture/image` |

Broker 地址和 topic 可在 `idf.py menuconfig` → `MQTT Bridge Configuration` 中修改。
默认使用公共测试服务器 `broker.emqx.io:1883`，无鉴权，任何人知道 topic 名即可访问，
正式使用建议换成有账号鉴权的 IoT 平台。

### 查看图片

MQTT Explorer 之类的通用客户端会把图片当文本显示成乱码，仓库里提供了一个网页查看器：

**直接用浏览器打开 [`tools/mqtt_viewer.html`](tools/mqtt_viewer.html) 即可**，不需要任何服务器。
页面功能：

- 点「连接」接入 Broker（通过 MQTT over WebSocket）
- 点「拍照」下发指令
- 回传的 JPEG 直接渲染显示，并显示大小和时间
- Broker 地址与 topic 可在页面上直接修改

> 注意：公共 Broker `broker.emqx.io` 禁止订阅通配符 `#`。若用 MQTT Explorer，
> 需要在连接的 ADVANCED 设置里把订阅主题改成具体路径（如 `esp32cam/#`），
> 否则界面会一直是空的且不报错。

## 开发规范

本项目的分支策略、命名规范、超时策略等要求见 [CLAUDE.md](CLAUDE.md)。
