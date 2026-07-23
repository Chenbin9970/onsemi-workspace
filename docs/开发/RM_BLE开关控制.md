# RM BLE 开关控制实现

> 通过 BLE 特征值动态开关 Remote Mic，支持低功耗睡眠共存。

---

## 一、功能概述

| 操作 | BLE 命令 | 行为 |
|------|---------|------|
| 启动 RM | 写 0x01 到 RM_ONOFF | mute → 切程序 3(音量=9) → RM_Enable，链路建立后 active |
| 停止 RM | 写 0x00 到 RM_ONOFF | mute → 停音频管道 → 停 RM → 切回 BLE → 恢复原程序 → active |
| TX 断开 | 自动 | mute → 停 RM → 切回 BLE → 恢复原程序 → active |
| TX 重新上电 | 写 0x01 | RM 重新搜索连接 |

> **关键约束**：程序 3 是音频模式，RM 必须在程序 3 下运行。进入 RM 前 mute + 切程序 3，active 推迟到 LINK_ESTABLISHED。退出时先 mute 再拆硬件，防止噗声。

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
uint8_t saved_prog_before_rm;  // 进入 RM 前保存的原程序号
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
// RM 启动 — 先切程序 3，再启 RM（active 推迟到 LINK_ESTABLISHED）
if (app_env.rm_start_requested) {
    app_env.rm_start_requested = 0;

    app_env.saved_prog_before_rm = bs300_get_active_prog();
    bs300_mute();
    if (app_env.saved_prog_before_rm != 3) {
        bs300_set_prog_volume(3, 9);
        bs300_switch_program(3);
        bs300_persist_active_prog(app_env.saved_prog_before_rm);
    }

    APP_RM_Init(ear_side);
    Audio_Init();
    RF_SwitchToCPMode();
    RM_Enable(1000);
    app_env.audio_streaming = 1;
}

// RM 停止 — 先 mute 再拆硬件，防止噗声
if (app_env.rm_stop_requested) {
    app_env.rm_stop_requested = 0;

    bs300_mute();

    /* Stop audio pipeline before RF switch */
    NVIC_DisableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_DisableIRQ(DMA_IRQn(ASRC_IN_IDX));
    NVIC_DisableIRQ(DSP1_IRQn);
    NVIC_DisableIRQ(TIMER_IRQn(TIMER_REGUL));
    Sys_Timers_Stop(1 << TIMER_REGUL);
    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelDisable(OD_DMA_NUM);
    SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP;
    RM_Disable();
    Sys_Timers_Stop(SELECT_TIMER0);
    Sys_Timers_Stop(SELECT_TIMER1);
    NVIC_ClearPendingIRQ(TIMER0_IRQn);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    RF_SwitchToBLEMode();

    if (app_env.saved_prog_before_rm != 3) {
        bs300_switch_program(app_env.saved_prog_before_rm);
        bs300_active();
    }

    app_env.audio_streaming = 0;
}
```

### 3.5 `code/rm_app.c`

```c
// LINK_ESTABLISHED — 链路就绪后启动 BS300 DSP
case LINK_ESTABLISHED:
    RM_PRINTF("__RM_LINK_ESTABLISHED\n");
    bs300_active();
    asrc_stable = false;
    // ... ASRC reset + enable IRQs ...

// 断开 — 统一走 Main_Loop 的 rm_stop_requested 路径
case LINK_DISCONNECTED:
    if (app_env.init_done && app_env.audio_streaming) {
        RM_PRINTF("__RM_LINK_DISCONNECTED\r\n");
        app_env.rm_stop_requested = 1;
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
| 退出 RM 噗声 | 硬件清理（DMA/BB/DSP）在 BS300 活跃时执行 | 先 `bs300_mute()` 再拆硬件管道 |
| 进入 RM 杂音 | RM 搜索期间 BS300 处于 active 状态 | active 推迟到 `LINK_ESTABLISHED` 回调 |
| 第二次进 RM 有杂音 | 退出时 `bs300_active()` 在无有效输入时启动 DSP | 退出路径 mute → 恢复程序 → active 在同一轮完成 |

---

## 五、主循环流程

```
开机 → audio_streaming=0 → BLE_Power_Mode_Enter（低功耗睡眠）

写 0x01 → rm_start_requested=1
       → 保存当前程序 → mute → 切程序 3(vol=9) → 持久化原程序到 Flash
       → APP_RM_Init → Audio_Init → RF_SwitchToCPMode → RM_Enable(1000)
       → audio_streaming=1 → 跳过睡眠 → WFE → RM 搜索（BS300 muted, 无杂音）
       → LINK_ESTABLISHED → bs300_active() → 音频开始

BLE 写 0x00 → rm_stop_requested=1
       → bs300_mute() → 停音频管道 → 停 DSP → BB_DEEP_SLEEP
       → RM_Disable → RF_SwitchToBLEMode
       → 恢复原程序 → bs300_active()
       → audio_streaming=0 → 回低功耗睡眠（助听正常）

TX 断开 → LINK_DISCONNECTED 回调 → rm_stop_requested=1
       → （与主动关闭走同一路径，见上）

写 0x01 → 重复上述流程
```

> **2026-07-22 简化**：TX 断开不再在中断回调中自行拆管道，改为设 `rm_stop_requested=1` 由 Main_Loop 统一处理。避免两处维护相同的清理代码。

---

## 六、程序 3 特殊处理

### 6.1 设计决策

| 决策 | 原因 |
|------|------|
| 程序 3 不记忆到 Flash | 程序 3 仅为 RM 音频中转，不应成为开机默认程序 |
| 程序 3 音量固定 9 档 | RM 模式下音量由 App 端控制，本地固定最大增益 |
| 切换必须 mute/active 包裹 | 硬件要求：更改 DSP 参数前必须 mute，否则爆音 |
| 阻塞切换 (blocking) | 切程序 3 和恢复原程序都发生在主循环，需要 I2C 完成后再继续 |

### 6.2 新增 API

| 函数 | 位置 | 作用 |
|------|------|------|
| `bs300_set_prog_volume(prog, level)` | `bs300_ram_sync.c:249` | 设置指定程序的音量到 `s_volumes[]`（仅 RAM） |
| `bs300_persist_active_prog(prog)` | `bs300_ram_sync.c:255` | 将指定程序号持久化到 Flash，覆盖 `bs300_switch_program` 写入的程序 3 |

### 6.3 进入/退出时序

```
进入 RM:
  saved = get_active_prog()          // e.g. 0
  mute()                              // I2C: 0x800000 — 停止 DSP, 防止搜索期杂音
  if saved != 3:
    set_prog_volume(3, 9)            // s_volumes[3] = 9
    switch_program(3)                 // I2C diff → BS300 切到程序 3
    persist_active_prog(saved)        // Flash: active_prog = 0 (不是 3!)
  RM_Init + RM_Enable
  ... RM 搜索中 (BS300 muted, 无杂音) ...
  LINK_ESTABLISHED → active()         // I2C: 0x800010 — 链路就绪, 启动 DSP

退出 RM（BLE 关断 / TX 断连统一路径）:
  LINK_DISCONNECTED → rm_stop_requested=1  （仅设 flag）
  BLE 写 0x00    → rm_stop_requested=1
                       ↓
              Main_Loop 统一处理：
              mute()                   // I2C: 0x800000 — 先停 DSP
              停 IRQ/DMA → DSS_LPDSP32_PAUSE → BB_DEEP_SLEEP → RM_Disable
              RF_SwitchToBLEMode
              if saved != 3:
                switch_program(saved)  // I2C diff → BS300 恢复原程序
                active()               // I2C: 0x800010 — 助听正常工作
              audio_streaming = 0
              low_power_enable = true  → 回低功耗睡眠
```

> **防重启保护**：`persist_active_prog(saved)` 确保即使 RM 运行期间设备异常重启，开机后仍然加载的是进入 RM 前的程序，而非程序 3。

---

## 七、功耗表现

| 模式 | 表现 |
|------|------|
| BLE 低功耗睡眠 | 正常低功耗 |
| RM 搜索/连接 | WFE 模式，200-500μA |
| RM 断开后 | 自动回低功耗睡眠 |
