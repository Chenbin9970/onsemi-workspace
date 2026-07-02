# RM 集成过程

> 将 Remote Mic（远程麦）专有 2.4GHz 协议集成到 `peripheral_server_sleep` 低功耗 BLE 工程的全过程。

---

## 一、硬件前提

| 条件 | 值 | 说明 |
|------|-----|------|
| 系统时钟 | **16 MHz** | 8 MHz 会导致 2Mbps 下 CRC 错误 |
| CPCLK | **2 MHz** (CPCLK_PRESCALE_8) | RM 专有协议时钟，缺了 RF 时序全错 |
| BBIF | **BB_WAKEUP** | BB_DEEP_SLEEP 会导致 RM 持续断连 |
| BLE 广播间隔 | 100ms | 10.24s 太慢，调试阶段缩短 |

---

## 二、RTE 组件配置

IDE 中启用 CMSIS-Pack `Remote_Mic` 组件（`Device::Libraries::Remote_Mic`），提供 4 个协议引擎文件：

| 文件 | 来源 |
|------|------|
| `rm_pkt.h` | CMSIS-Pack include |
| `rm_event.c` | `source/firmware/remote_micLib/` |
| `rm_pkt_hdl.c` | `source/firmware/remote_micLib/` |
| `config_data.c` | `source/firmware/remote_micLib/` |

`.rteconfig` 配置见 [RM功能开启.md](RM功能开启.md)。

---

## 三、代码文件清单

### 新增文件

| 文件 | 作用 |
|------|------|
| `code/rm_app.c` | RM 应用层回调：`APP_RM_Init()`、`RM_Callback_TRX()`、`RM_Callback_StatusUpdate()` |

### 修改文件

| 文件 | 改动 |
|------|------|
| `include/app.h` | `APP_RM_ENABLE`、`#include <rm_pkt.h>`、RM 宏（跳频表、AES key、Debug DIO）、`app_env_tag` 新增 RM 字段、`APP_MESSAGE_HANDLER_LIST`、RM 函数声明 |
| `app.c` | `main()` 调用 `App_Initialize()`、`Main_Loop` 加 `RM_StatusHandler()`、RM 活跃时跳休眠 + WFE、BLE 连接打印 |
| `code/app_init.c` | `App_Initialize()` 同步到 RM 能工作的配置、`App_RM_BLE_Initialize()` 保留作为参考 |
| `code/app_process.c` | `APP_Timer()` 简化为 LED 控制、移除 timer 延迟 RM 启动 |
| `include/ble_std.h` | 广播间隔从 10.24s 缩短到 100ms |
| `peripheral_server_sleep.rteconfig` | 注册 Remote_Mic RTE 组件 |
| `.cproject` | 添加 RM RTE 文件链接 |

---

## 四、调试过程中的关键发现

### 4.1 CPCLK 缺失导致 RM 无法工作

`App_Initialize` 原始代码只配置了 `CLK_DIV_CFG2->DCCLK_BYTE`，缺少 **CPCLK**（Custom Protocol Clock）。RM 协议引擎需要 CPCLK = 2MHz 才能正确驱动 RF 时序。在 `App_RM_BLE_Initialize` 中发现差异后补充。

```c
// 错误（只有 DCCLK）
CLK_DIV_CFG2->DCCLK_BYTE = DCCLK_BYTE_VALUE;

// 正确（CPCLK + DCCLK）
CLK->DIV_CFG2 = (CPCLK_PRESCALE_8 | DCCLK_PRESCALE_4);
```

### 4.2 BB_DEEP_SLEEP 导致 RM 持续断连

`App_Initialize` 原始代码使用 `BB_DEEP_SLEEP`，RM 初始化后基带进入深度睡眠，导致 RM 协议时序中断。必须使用 `BB_WAKEUP`。

```c
// 错误
BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP;

// 正确
BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_WAKEUP;
```

### 4.3 `Sleep_Mode_Configure` 与 `RM_Configure` 顺序

`Sleep_Mode_Configure` 调用 `Sys_PowerModes_Sleep_Init` 保存 RF 寄存器状态。若在 `RM_Configure` 之前调用，后续 RM 写 RF 寄存器会与睡眠保存的状态冲突，导致死机。

**最终方案**：`RM_Configure` 放在 `App_Initialize` 内（`printf_init` 之后）、`Sleep_Mode_Configure` 放在 RM 启动之后，互不冲突。

### 4.4 WFE 被 `DEBUG_UART_ENABLE` 误杀

主循环的 WFE 被 `#ifndef DEBUG_UART_ENABLE` 包裹，开启调试打印时 WFE 永远不会执行，RM 失去事件同步导致断连。

```c
// 错误：WFE 被 DEBUG_UART_ENABLE 屏蔽
#ifndef DEBUG_UART_ENABLE
    if (app_env.audio_streaming) SYS_WAIT_FOR_EVENT;
    else SYS_WAIT_FOR_INTERRUPT;
#endif

// 正确：WFE 无条件执行
if (app_env.audio_streaming) {
    SYS_WAIT_FOR_EVENT;
} else {
#ifndef DEBUG_UART_ENABLE
    SYS_WAIT_FOR_INTERRUPT;
#endif
}
```

### 4.5 RM 初始化重复调用

多次修改中不慎产生了两处 `APP_RM_Init()` 调用（`App_Initialize` 末尾 + `main()` 内），导致 RM 死机。最终统一放在 `App_Initialize` 内，仅一处。

### 4.6 `printf_init` 必须在 `APP_RM_Init` 之前

调试打印依赖 DMA7，`RM_Configure` 操作 RF 寄存器，若 printf 在 RM 之后初始化，DMA 状态被破坏。顺序：`printf_init` → `APP_RM_Init` → `Sleep_Mode_Configure`。

### 4.7 初始化在 `App_Initialize` 内失败、在 `main()` 内成功

`App_Initialize` 内有校准、BUCK、TX power 等步骤，某些步骤破坏了 RM_Configure 的运行环境。将 RM 初始化移到 `main()` 内（系统完全启动后）可以工作。通过逐项对比 `App_RM_BLE_Initialize` 和 `App_Initialize`，最终定位并同步了所有差异。

---

## 五、低功耗功能集成过程

以能工作的基准配置为起点，逐个加回低功耗功能，每步验证 RM 是否正常：

| 步骤 | 功能 | 结果 |
|------|------|------|
| 1 | srand(1) | 正常 |
| 2 | RC oscillator trim | 正常 |
| 3 | BLE_LLD_Sleep_Params_Set | 正常 |
| 4 | Sleep_Mode_Configure + BLE_Is_Awake_Flag_Set | 正常 |
| 5 | RTC config（low_power_clk_param） | 正常 |
| 6 | BUCK enable | 正常 |

待验证：
- 校准（`CALIB_RECORD`）
- TX power 降级路径（`Load_Tx_Power_Value`）
- `DEBUG_UART_ENABLE` 关闭后的 printf 与校准冲突

---

## 六、RM 开关控制

`audio_streaming` 是 RM 启停的核心标志位：

| audio_streaming | 行为 |
|-----------------|------|
| 1 | 跳睡眠、WFE 同步事件 |
| 0 | 进睡眠 + WFI（低功耗） |

RM 初始化为"配而不启"：
- `APP_RM_Init()` + `RF_SwitchToBLEMode()` — 配置参数，RF 回 BLE 模式
- 不调用 `RM_Enable()` — 不启动扫描
- `audio_streaming = 0` — 允许低功耗

后续通过 BLE ON_OFF 特征值或按键触发 RM 启停。

---

## 七、当前状态

- RM 直接启动 → BLE + RM 共存正常
- RM 配而不启 → BLE 低功耗广播正常
- 低功耗睡眠：待功耗测试验证

---

## 八、相关文档

- [RM功能开启.md](RM功能开启.md) — IDE 配置操作步骤
- [rm_connection_plan.md](../rm_connection_plan.md) — 原始设计文档
- [调试打印.md](调试打印.md) — DEBUG_UART_ENABLE 行为说明
- [硬件配置.md](硬件配置.md) — RM 硬件需求
