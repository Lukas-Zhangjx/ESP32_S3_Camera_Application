# 项目开发规范

本文件定义本项目（ESP32-S3 摄像头固件）的开发规范，所有人工开发与 Claude Code 辅助开发均须遵循。

## 分支规范

* 禁止直接在 `main` / `master` 分支上提交代码
* 所有开发必须先创建功能分支：
  * 新功能：`feature/描述`，例如 `feature/uart-init`
  * Bug 修复：`bugfix/描述`，例如 `bugfix/can-timeout`
* 完成开发并提交后，询问用户是否需要 merge 回主分支

## 编码规范

### 文件组织

* 每个独立功能模块必须有独立的 `.c` 和 `.h` 文件
* 禁止将不同功能的代码混写在同一文件中
* 新增大功能时，必须新建对应文件，不得追加到现有功能文件里

### 模块目录结构（架构解耦）

* 每个功能模块必须建成独立的 ESP-IDF 组件，放在 `components/<模块名>/` 下，不得堆在 `main/` 里：
  * `components/<模块名>/include/<模块名>.h` — 对外公开接口
  * `components/<模块名>/<模块名>.c` — 实现
  * `components/<模块名>/CMakeLists.txt` — 用 `PRIV_REQUIRES`/`REQUIRES` 显式声明依赖，不得隐式依赖其他组件的内部结构
  * 若模块需要第三方组件（如 `esp32-camera`），依赖写在该模块自己的 `idf_component.yml` 里，不要写到 `main` 或其他模块里
  * 若模块需要可配置项（如引脚），用该模块自己的 `Kconfig` 文件，不要塞进 `main/Kconfig.projbuild`
* `main/` 只做「组装」：调用各模块的公开接口，不实现具体业务逻辑
* 模块之间不得相互 `#include` 对方的实现文件或私有头文件，只能通过公开 `.h` 暴露的函数接口交互

### 函数命名

* 格式：`模块名_功能名`，全小写，单词间用下划线
* 示例：
  * `uart_init()` — UART 模块初始化
  * `can_send_frame()` — CAN 模块发送帧
  * `gpio_set_output()` — GPIO 模块设置输出
* 禁止使用无意义的通用名称，如 `init()`、`process()`、`handle()`

### 代码解耦

* 模块间通过函数接口通信，禁止直接访问其他模块的内部变量
* 每个模块的内部变量声明为 `static`，不对外暴露
* 头文件只暴露必要的公共接口，内部实现细节不放入 `.h`

### 注释规范

* 每个函数前必须写注释，说明功能、参数、返回值
* 关键逻辑行内写行注释，说明为什么这样做
* 模块文件顶部写模块说明注释（功能概述、依赖关系）
* 桩函数必须注释标明 `TODO` 及预期实现内容

### 嵌入式超时策略

* 禁止使用 `portMAX_DELAY` 或任何形式的永久阻塞等待
* 所有阻塞调用必须设置有限超时，并处理超时返回值：
  * `xEventGroupWaitBits()` → 用 `pdMS_TO_TICKS(N)` 替代 `portMAX_DELAY`
  * `xQueueReceive()` / `xSemaphoreTake()` → 同上
  * `wait_for_level()` 等自旋等待 → 必须有计数器上限
* 超时后必须有明确处理：打印错误日志 + 走降级路径（跳过、重试或报错返回），不得卡死
* 示例：

```c
/* 错误：永久阻塞 */
xEventGroupWaitBits(group, BIT0, pdFALSE, pdFALSE, portMAX_DELAY);

/* 正确：有限超时 + 超时处理 */
EventBits_t bits = xEventGroupWaitBits(group, BIT0, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
if (!(bits & BIT0)) {
    ESP_LOGE(TAG, "wait timeout, fallback");
}
```

### 其他

* 头文件必须有 include guard：

```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H
// ...
#endif /* MODULE_NAME_H */
```

## 推送规范

* 每次 `git push` 必须在对话/PR 描述中备注本次推送的改动内容,包括但不限于:
  * 新增了什么(文件、功能、模块)
  * 修改了什么(逻辑变更、配置调整)
  * 删除了什么(废弃代码、文件)
* 禁止只回复"已推送"而不说明具体改动项

## Commit 规范

* 每个 commit 只做一件事
* commit message 格式：`类型: 简短描述`
  * `feat: add uart dma support`
  * `fix: resolve can bus timeout issue`
  * `refactor: split sensor module into separate files`
  * `docs: update readme with build instructions`
* message 使用英文，动词开头，小写
