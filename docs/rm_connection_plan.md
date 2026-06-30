# 计划：为 peripheral_server_sleep 添加远程麦连接功能

## 背景

当前 `peripheral_server_sleep` demo 只有 BLE 外设 + 电池服务 + 按键 + debug printf，缺少远程麦协议。原始远程麦提交（`ad2cdae`）包含完整的 G722 音频解码/DMA/ASRC/OD 输出。本次**只实现连接部分**（专有 2.4GHz 链路建立/搜索），音频传输后续再做。

## 核心思路

直接复用原始 demo 中已有的远程麦协议引擎（rm_pkt.h、rm_event.c、rm_pkt_hdl.c、config_data.c），这些是"连接"层面的代码，不做改动。唯一不同的是 rm_app.c（应用层回调）：原始版本回调里启动了音频 DSP 和 DMA，本次写一个精简版，只处理连接状态变更和包计数。

## 新增文件

### 从 git ad2cdae 原样复制（协议引擎，不改）

1. **`include/rm_pkt.h`** — 协议结构体、RF 寄存器地址、状态机枚举、RM_Configure/RM_Enable 等函数声明
2. **`code/config_data.c`** — 2Mbps 模式 RF 寄存器配置表 + 40 通道频率表
3. **`code/rm_event.c`** — `RM_Configure` / `RM_Enable` / `RM_Disable` / `RM_EventHandler` / `RM_StatusHandler`
4. **`code/rm_pkt_hdl.c`** — 无线电中断处理 + 状态机（~1043 行）。专有 2.4GHz 协议核心，含 BLE 共存逻辑

### 新建（连接专用版，非原始版本）

5. **`code/rm_app.c`** — 精简版应用层，与原始版本的区别：
   - `APP_RM_Init()` — 不变：设置 rm_param、注册回调、调用 RM_Configure
   - `RM_Callback_TRX()` — 精简：只统计收发包数量，**不做**音频缓冲处理
   - `RM_Callback_StatusUpdate()` — 精简：记录连接状态 + 控制 LED；**不做**音频 ISR 开关（无 AUDIOSINK/DMA/DSP/TIMER_REGUL 操作）
   - 去掉 coded_sample 数组、SPI/DMA 输出等音频相关代码

## 修改文件

6. **`include/app.h`**
   - 新增 `#define APP_RM_ENABLE`
   - 新增 `#include <rm_pkt.h>`（在 rsl10_protocol.h 之后）
   - 新增 RM 宏：`RM_HOPLIST`、`RM_LEFT`/`RM_RIGHT`、`APP_RM_AUDIO_CHANNEL`、`APP_RM_DATA_REQUEST_TYPE`、`BUTTON_DIO`(2)、`DEBUG_DIO_FIRST`(15)、`DEBUG_DIO_SECOND`(11)、`MEMCPY_DMA_NUM`(0)
   - `app_env_tag` 新增字段：`RM_on_off`、`volume`、`rm_param`、`rm_link_status`、计数器、`audio_streaming`
   - `APP_MESSAGE_HANDLER_LIST` 在 `#ifdef APP_RM_ENABLE` 下包含 `APP_TEST_TIMER → APP_Timer`
   - 新增 RM 函数声明：`APP_RM_Init`、`RM_Callback_TRX`、`RM_Callback_StatusUpdate`、`APP_Timer`
   - 新增 `extern uint8_t ear_side;`

7. **`include/ble_custom.h`**
   - 新增 3 个 UUID：`CS_RM_ONOFF_UUID`、`CS_RM_VOLUME_UUID`、`CS_RM_CHNLSIDE_UUID`
   - `#ifdef APP_RM_ENABLE` 下新增 9 个 enum 条目（ONOFF/VOLUME/CHNLSIDE 各 3 个）
   - 新增 3 个 max-length 宏 + 3 个名称字符串

8. **`code/ble_custom.c`**
   - `CustomService_ServiceAdd`：在 `#ifdef APP_RM_ENABLE` 下新增 RM 特征值 att[] 条目
   - `GATTC_ReadReqInd`：新增 RM 特征值读取分支
   - `GATTC_WriteReqInd`：新增 RM 特征值写入分支 + ON_OFF 切换逻辑：
     - ON→OFF：`RM_Disable()` + `RF_SwitchToBLEMode()` + `audio_streaming = 0`
     - OFF→ON：`RM_Configure()` + `RF_SwitchToCPMode()` + `RM_Enable(1000)` + `audio_streaming = 1`

9. **`app.c`**
   - `Main_Loop`：在 `while(true)` 循环内（Kernel_Schedule 之后）添加 `RM_StatusHandler()` 调用
   - 用 `audio_streaming` 控制休眠：RM 活跃时跳过 `BLE_Power_Mode_Enter` + `SYS_WAIT_FOR_INTERRUPT`（改用 WFE 或直接跳过）

10. **`code/app_init.c`**
    - `App_Initialize()`：在 `App_Env_Initialize()` 之后新增 `APP_RM_Init(ear_side)` + `RF_SwitchToBLEMode()`
    - `App_Env_Initialize()`：在 `ke_task_create` 之后新增 `ke_timer_set(APP_TEST_TIMER, ...)`

11. **`code/app_process.c`**
    - 新增 `APP_Timer()` 函数：调用 `RM_StatusHandler()` 后重新 arm 定时器
    - handler 列表新增 `DEFINE_MESSAGE_HANDLER(APP_TEST_TIMER, APP_Timer)`

## 不动的文件（后续再做）
- `code/audio_func.c` — 音频 DSP/DMA/ASRC/OD 输出
- `code/dsp_pm_dm.c` / `.h` — LPDSP32 程序/数据镜像
- `code/queue.c` / `.h` — 音频子帧缓冲队列
- `syslib/` — RTE System 组件已提供，不需要本地副本

## 验证方式

1. **编译**：IDE Build Project (Ctrl+B)，确认无编译错误
2. **连接**：烧录后 BLE 连接手机，向 RM_ONOFF 特征值写 `0x01` → 观察 LED/Debug DIO 确认 RM 开始搜索
3. **按键**：按 DIO2 按键 → 确认 RM 开关切换正常
4. **休眠共存**：RM OFF 时设备正常休眠；RM ON 时跳过休眠（CPU 保持唤醒以保证 RM 协议时序）
