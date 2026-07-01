# RM 功能开启（IDE 配置）

> 如何在 ON Semiconductor IDE 中启用 Remote Mic 专有 2.4GHz 协议栈。协议引擎文件由 CMSIS-Pack RTE 组件提供，无需手动添加库文件。

---

## 一、IDE 操作步骤

### 1.1 启用 RTE 组件

**方式一：IDE 图形界面**

菜单 **File → Runtime Environment**，在 `ONSemiconductor::RSL10 → Device → Libraries` 下勾选 **Remote_Mic**。

**方式二：直接编辑 `.rteconfig`**

在 [peripheral_server_sleep.rteconfig](peripheral_server_sleep/peripheral_server_sleep.rteconfig) 的 `<components>` 中加入：

```xml
<component Cclass="Device" Cgroup="Libraries" Csub="Remote_Mic" Cvariant="source"
           Cvendor="ONSemiconductor" Cversion="3.7.606" deviceDependent="1">
  <package name="RSL10" url="www.onsemi.com/" vendor="ONSemiconductor" version="3.7.606"/>
  <file category="header" deviceDependent="1" name="include/rm_pkt.h"/>
  <file category="source" deviceDependent="1" name="source/firmware/remote_micLib/config_data.c"/>
  <file category="source" deviceDependent="1" name="source/firmware/remote_micLib/rm_event.c"/>
  <file category="source" deviceDependent="1" name="source/firmware/remote_micLib/rm_pkt_hdl.c"/>
</component>
```

启用后，IDE 自动将 4 个协议引擎文件加入编译，并添加 `rm_pkt.h` 的 include 路径。

### 1.2 定义 APP_RM_ENABLE 宏

在 [include/app.h](peripheral_server_sleep/include/app.h) 中已默认定义：

```c
#define APP_RM_ENABLE
```

删除/注释此宏即可关闭 RM 功能，无需卸载 RTE 组件。

---

## 二、文件清单

### 2.1 修改的现有文件

| 文件 | 修改内容 |
|------|----------|
| [include/app.h](peripheral_server_sleep/include/app.h) | `APP_RM_ENABLE`、`#include <rm_pkt.h>`、RM 宏、`app_env_tag` 新增字段、`APP_MESSAGE_HANDLER_LIST`、RM 函数声明 |
| [include/ble_custom.h](peripheral_server_sleep/include/ble_custom.h) | 3 个 RM UUID（`...04`/`...05`/`...06`）、9 个 enum 条目、max-length 宏、名称字符串 |
| [code/ble_custom.c](peripheral_server_sleep/code/ble_custom.c) | `CustomService_ServiceAdd` 新增 RM 特征值 att[]、`GATTC_ReadReqInd` / `GATTC_WriteReqInd` 新增 RM 读写分支 + ON_OFF 切换逻辑 |
| [app.c](peripheral_server_sleep/app.c) | `Main_Loop` 新增 `RM_StatusHandler()` 调用、RM 活跃时跳过休眠 |
| [code/app_init.c](peripheral_server_sleep/code/app_init.c) | `App_Initialize` 新增 `APP_RM_Init(ear_side)` + `RF_SwitchToBLEMode()`、`App_Env_Initialize` 新增 `ke_timer_set(APP_TEST_TIMER, ...)` |
| [code/app_process.c](peripheral_server_sleep/code/app_process.c) | 新增 `APP_Timer()` 函数 |

### 2.2 新增文件

| 文件 | 说明 |
|------|------|
| [code/rm_app.c](peripheral_server_sleep/code/rm_app.c) | 应用层回调：`APP_RM_Init()`、`RM_Callback_TRX()`、`RM_Callback_StatusUpdate()` |

### 2.3 RTE 提供的文件（不放在本地）

| 文件 | 提供方 |
|------|--------|
| `rm_pkt.h` | CMSIS-Pack Remote_Mic 组件 |
| `rm_event.c` | CMSIS-Pack Remote_Mic 组件 |
| `rm_pkt_hdl.c` | CMSIS-Pack Remote_Mic 组件 |
| `config_data.c` | CMSIS-Pack Remote_Mic 组件 |

---

## 三、RM 控制方式

### 3.1 BLE 特征值

RM 控制在 Custom Service（UUID `24dc0e6e-...e0`）下新增 3 个特征值：

| 特征值 | UUID 第 6 字节 | 权限 | 大小 | 说明 |
|--------|---------------|------|------|------|
| ON_OFF | `...04...` | RD / WR | 1 byte | 写 `0x01` 启动 RM，写 `0x00` 停止 |
| VOLUME | `...05...` | RD / WR | 1 byte | 音量控制 |
| CHANNEL SIDE | `...06...` | RD / WR | 1 byte | 0=左声道, 1=右声道 |

### 3.2 初始化自动启用（BLE 优先）

RM 在 BLE 广播/连接就绪后自动启动，确保 BLE 先可用：

1. `App_Initialize()` → `APP_RM_Init(ear_side)` 配参数 → `RF_SwitchToBLEMode()` 保持 BLE 模式
2. 内核启动 BLE 广播 → 状态变为 `APPM_ADVERTISING`
3. `APP_Timer`（200ms 间隔）检测到 BLE 就绪 → `RF_SwitchToCPMode()` → `RM_Enable(1000)` → `audio_streaming = 1`

BBIF_COEX 硬件负责 BLE 和 RM 之间的 RF 仲裁。Phase 2 将增加 BLE ON_OFF 特征值来动态开关。

### 3.3 休眠共存

RM 活跃（`audio_streaming == 1`）时：
- 跳过 `BLE_Power_Mode_Enter`（不进 Power Mode Sleep）
- 使用 `SYS_WAIT_FOR_EVENT` 代替 `SYS_WAIT_FOR_INTERRUPT`
- CPU 保持唤醒以保证 RM 协议时序

---

## 四、RM 协议参数

### 4.1 链路参数

| 参数 | 值 | 位置 |
|------|-----|------|
| 角色 | Slave (`RM_SLAVE_ROLE`) | `APP_RM_ROLE` in app.h |
| 连接间隔 | 10,000 us (10ms) | `rm_app.c` APP_RM_Init |
| 重传间隔 | 5,000 us (5ms) | `rm_app.c` APP_RM_Init |
| 扫描超时 | 6,500 us (6.5ms) | `rm_app.c` APP_RM_Init |
| Preamble | 0x55 | `rm_app.c` APP_RM_Init |
| Access Word | `0xf2cde629` | `rm_app.c` APP_RM_Init |
| 调制索引 | `BLE_MOD_IDX` | `rm_app.c` APP_RM_Init |
| DMA 通道 | 0 (`MEMCPY_DMA_NUM`) | `rm_app.c` APP_RM_Init |

### 4.2 音频参数（协议层使用，Phase 1 不启用音频输出）

| 参数 | 值 | 说明 |
|------|-----|------|
| 音频速率 | 48 kbps | G.722 |
| 射频速率 | 2,000 kbps | 2 Mbps |
| payloadFlowRequest | `RM_APP_REQUEST` | `APP_RM_DATA_REQUEST_TYPE` |
| renderDelay | 200 us | |
| preFetchDelay | 1,300 us | RM_APP_REQUEST 模式 |

### 4.3 跳频参数

| 参数 | 值 | 位置 |
|------|-----|------|
| 信道列表 | `{ 3, 9, 15, 21, 24, 33, 36 }` | `RM_HOPLIST` in app.h |
| 信道数 | 7 | `rm_app.c` APP_RM_Init |
| stepSize | 1 | `rm_app.c` APP_RM_Init |
| 载波频率表 | 2402-2480 MHz（40 信道） | `config_data.c` (RTE) |

### 4.4 链路预算阈值

| 参数 | 值 | 位置 |
|------|-----|------|
| pktLostLowThrshld | 10 | `rm_app.c` APP_RM_Init |
| pktLostHighThrshld | 200 | `rm_app.c` APP_RM_Init |
| pktLostLowThrshldSlow | 1 | `rm_app.c` APP_RM_Init |
| searchTryCntThrshld | 10 | `rm_app.c` APP_RM_Init |
| waitCntGranularity | 200 | `rm_app.c` APP_RM_Init |

### 4.5 加密参数

| 参数 | 值 | 位置 |
|------|-----|------|
| KEY_AES_128_ECB | `{ 0x4138684C, 0xD874F539, 0x4EF3BC36, 0xBF01FB9D }` | app.h |
| CRY_AES_128_ECB | 0 | app.h |

### 4.6 Debug DIO

| 参数 | 值 | 说明 |
|------|-----|------|
| DEBUG_DIO_FIRST | 15 | |
| DEBUG_DIO_SECOND | 11 | |
| DEBUG_DIO_THIRD | 10 | |

### 4.7 其他 IO

| 参数 | 值 | 说明 |
|------|-----|------|
| BUTTON_DIO | 2 | 预留按键（Phase 2） |
| APP_RM_AUDIO_CHANNEL | RM_LEFT (0) | 初始声道 |

---

## 五、编译验证

1. **确认 RTE 组件已启用**：检查 `.rteconfig` 中 `Remote_Mic` 勾选
2. **确认宏已定义**：`app.h` 中 `#define APP_RM_ENABLE` 未被注释
3. **IDE Build**：Ctrl+B，确认 `rm_event.c`、`rm_pkt_hdl.c`、`config_data.c`、`rm_app.c` 均参与编译且无错误
4. **烧录测试**：上电后 RM 自动开始搜索，观察 LED（连接时亮 / 广播时闪烁 / 断开时灭）和 Debug DIO 信号确认 RM 状态

---

## 六、当前限制（Phase 1：仅连接）

- 未实现音频 DSP/DMA/ASRC/OD 输出路径（后续 Phase 2）
- 未实现按键短按切换 RM ON/OFF（预留 `BUTTON_DIO = 2`）
- 系统时钟 8 MHz，手册建议 ≥ 16 MHz 以保证 2 Mbps 稳定，可在 Phase 2 切换
