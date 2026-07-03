# RM BLE 开关控制实现

> 通过 BLE 特征值动态开关 Remote Mic，支持低功耗睡眠共存。

---

## 一、功能概述

| 操作 | BLE 命令 | 行为 |
|------|---------|------|
| 启动 RM | 写 0x01 到 RM_ONOFF | 切换 RF 到 CP 模式，启动 RM 搜索 |
| 停止 RM | 写 0x00 到 RM_ONOFF | 停 RM，切回 BLE，进入低功耗睡眠 |
| TX 断开 | 自动 | 停 RM，回低功耗，BLE 不断连 |
| TX 重新上电 | 写 0x01 | RM 重新搜索连接 |

---

## 二、硬件配置

| 配置项 | 值 | 位置 |
|--------|-----|------|
| 系统时钟 | 16MHz | `app.h:124` |
| CPCLK | 2MHz (÷8) | `app_init.c:390` |
| BBIF | BB_DEEP_SLEEP | `app_init.c:392` |
| SLOWCLK | 2MHz (÷8) | `app_init.c:388` |
| BBCLK | 8MHz (÷2) | `app_init.c:388` |
| DMA（RM） | 通道 3 | `app.h:119` |
| DMA（睡眠） | 通道 0/1 | `app.h:226-228` |

---

## 三、文件改动

### 3.1 `include/app.h`

```c
#define RFCLK_FREQ  16000000
#define APP_SLEEP_2MBPS_SUPPORT
#define MEMCPY_DMA_NUM  3

// LOW_POWER_CLK_SCALE_AVERAGE_PERIOD 自适应 16MHz
#define LOW_POWER_CLK_SCALE_AVERAGE_PERIOD ((float)(RFCLK_FREQ / 1000000) * 16.0)

// app_env_tag 新增字段
uint8_t audio_streaming;
uint8_t rm_start_requested;
uint8_t rm_stop_requested;
uint8_t init_done;
```

### 3.2 `code/app_init.c`

```c
// 时钟硬编码（对齐 App_RM_BLE_Initialize）
CLK->DIV_CFG0 = (SLOWCLK_PRESCALE_8 | BBCLK_PRESCALE_2 | USRCLK_PRESCALE_1);
CLK->DIV_CFG2 = (CPCLK_PRESCALE_8 | DCCLK_PRESCALE_4);
BBIF->CTRL = (BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP);

// RM 配而不启
APP_RM_Init(ear_side);
RF_SwitchToBLEMode();
app_env.audio_streaming = 0;

// 初始化完成标志
app_env.init_done = 1;
```

### 3.3 `code/app_process.c`

```c
// Continue_Application：唤醒后恢复时钟（硬编码，对齐 init）
CLK->DIV_CFG0 = (SLOWCLK_PRESCALE_8 | BBCLK_PRESCALE_2 | USRCLK_PRESCALE_1);
CLK->DIV_CFG2 = (CPCLK_PRESCALE_8 | DCCLK_PRESCALE_4);

// Audiosink ISR：RM 活跃时跳过测量，避免干扰时序
void AUDIOSINK_PERIOD_IRQHandler(void) {
    if (app_env.audio_streaming) return;
    // ...
}
```

### 3.4 `app.c`

```c
// RM 启动
if (app_env.rm_start_requested) {
    app_env.rm_start_requested = 0;
    RF_SwitchToCPMode();
    RM_Enable(1000);
    app_env.audio_streaming = 1;
}

// RM 停止
if (app_env.rm_stop_requested) {
    app_env.rm_stop_requested = 0;
    RM_Disable();
    Sys_Timers_Stop(SELECT_TIMER0);
    Sys_Timers_Stop(SELECT_TIMER1);
    NVIC_ClearPendingIRQ(TIMER0_IRQn);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    RF_SwitchToBLEMode();
    app_env.audio_streaming = 0;
}
```

### 3.5 `code/rm_app.c`

```c
// 断开自停（init_done 保护初始化阶段）
case LINK_DISCONNECTED:
    if (app_env.init_done) {
        RM_Disable();
        Sys_Timers_Stop(SELECT_TIMER0);
        Sys_Timers_Stop(SELECT_TIMER1);
        NVIC_ClearPendingIRQ(TIMER0_IRQn);
        NVIC_ClearPendingIRQ(TIMER1_IRQn);
        RF_SwitchToBLEMode();
        low_power_clk_param.low_power_enable = true;
        app_env.audio_streaming = 0;
    }
    app_env.rm_lostLink_counter++;
    break;
```

### 3.6 `include/ble_custom.h` / `code/ble_custom.c`

新增 RM_ONOFF 特征值：
- UUID: `24dc0e6e-0440-ca9e-e5a9-a300b5f393e0`
- 权限：RD / WR
- 大小：1 byte
- 写 0x01 → `rm_start_requested = 1`
- 写 0x00 → `rm_stop_requested = 1`

---

## 四、关键问题与解决

| 问题 | 根因 | 解决 |
|------|------|------|
| RM 连接不稳定 | `Continue_Application` 唤醒后 CPCLK/SLOWCLK 丢失 | 硬编码对齐 `App_Initialize` |
| 16MHz 下 BLE 低功耗失败 | `LOW_POWER_CLK_SCALE_AVERAGE_PERIOD` 硬编码 8MHz | 自适应公式 |
| RM 断开 7s 后重启 | `BLE_Power_Mode_Enter` 与 RM 射频状态冲突 | 断开时自停 RM + 恢复 `low_power_enable` |
| 初始化回调死机 | `APP_RM_Init` 末尾触发 `LINK_DISCONNECTED` 回调 | `init_done` 保护 |
| BLE 断连 | `Sleep_Mode_Configure` 重建破坏 BLE 状态 | 去掉重建，仅设 `audio_streaming=0` |
| 不能回连 | `audio_streaming=1` 阻止 BLE 回调条件 | 无条件触发 `rm_start_requested` |

---

## 五、主循环流程

```
开机 → audio_streaming=0 → BLE_Power_Mode_Enter（低功耗睡眠）

写 0x01 → rm_start_requested=1 → RF_SwitchToCPMode → RM_Enable(1000) → audio_streaming=1
       → 睡眠跳过 → WFE 模式 → RM 搜索/连接

TX 断开 → LINK_DISCONNECTED 回调 → RM_Disable + 停定时器 + RF_SwitchToBLEMode
        → low_power_enable=true → audio_streaming=0 → 回低功耗睡眠

写 0x01 → 重复上述流程
```

---

## 六、功耗表现

| 模式 | 表现 |
|------|------|
| BLE 低功耗睡眠 | 正常低功耗 |
| RM 搜索/连接 | WFE 模式，200-500μA |
| RM 断开后 | 自动回低功耗睡眠 |
