# RM BLE 开关控制实现

> 通过 BLE 特征值动态开关 Remote Mic，支持低功耗睡眠共存。
> **2026-07-27 重写**：开机默认 RM 模式 + 断连保持 RM 状态 + 超时自动退 BLE。

---

## 一、功能概述

| 操作 | 触发方式 | 行为 |
|------|---------|------|
| 开机 | 冷启动 | 直接进 RM 搜索 + 助听模式，BS300 跑原程序，启动 1min 超时定时器 |
| RM 连上 | TX 连接 | 关定时器，切程序 3，DSP active，音频开始 |
| TX 断开 | 自动 | 保持 RM 状态：mute → 消抖 1s → 切回助听 → RM_Enable 搜索；快速重连直接恢复 |
| 停止 RM | BLE 写 0x00 | 完整清理：mute → 停管道 → RM_Disable → 切 BLE → 恢复原程序 → 低功耗睡眠 |
| 启动 RM | BLE 写 0x01 | **仅 RM 完全关闭时有效**（`audio_streaming=0`），已连接/搜索中直接跳过 |
| 超时退出 | 1min 无连接 | 自动走完整清理切回 BLE 低功耗 |

> **关键约束**：程序 3 是音频模式，RM 必须在程序 3 下运行。进入 RM 前 mute + 切程序 3，active 推迟到 LINK_ESTABLISHED。退出时先 mute 再拆硬件，防止噗声。

---

## 二、app_env_tag 字段（`include/app.h`）

```c
uint8_t  audio_streaming;        // 1=RM活跃(WFE), 0=可低功耗睡眠
uint8_t  rm_start_requested;     // BLE写0x01置1
uint8_t  rm_stop_requested;      // BLE写0x00置1 / 超时置1
uint8_t  saved_prog_before_rm;   // 进入RM前保存的原程序号
uint8_t  init_done;              // App_Initialize完成标志
uint8_t  rm_disc_state;          // 断连状态机: NONE/DEBOUNCE/HEARING_AID
uint16_t rm_disc_counter;        // 消抖计数器（Main_Loop迭代次数）
uint16_t rm_timeout_ticks;       // 搜索超时倒计时（200ms为单位）
uint8_t  timer_200ms;            // APP_Timer触发标志，Main_Loop消费后重投

enum rm_disc_state {
    RM_DISC_NONE = 0,            // 正常（已连接或空闲）
    RM_DISC_DEBOUNCE,            // 消抖等待快速重连
    RM_DISC_HEARING_AID          // 助听模式 + RM搜索
};

#define RM_DISC_DEBOUNCE_THRESHOLD  500   // 消抖阈值（Main_Loop迭代次数，约1s）
#define RM_TIMEOUT_TICKS            300   // 搜索超时 300×200ms = 1分钟
```

---

## 三、文件改动详情

### 3.1 `app.c` — Main_Loop

#### 冷启动 RM 初始化（while(true) 之前）

```c
void Main_Loop(void)
{
    // ... 现有 preamble ...

#ifdef APP_RM_ENABLE
    {
        static uint8_t rm_cold_boot_done = 0;
        if (!rm_cold_boot_done) {
            rm_cold_boot_done = 1;
            app_env.saved_prog_before_rm = bs300_get_active_prog();
            Audio_Init();
            RF_SwitchToCPMode();
            RM_Enable(1000);
            app_env.audio_streaming = 1;
            app_env.rm_disc_state = RM_DISC_HEARING_AID;
            app_env.rm_timeout_ticks = RM_TIMEOUT_TICKS;
        }
    }
#endif

    while (true) { ... }
}
```

#### rm_start_requested 处理

```c
if (app_env.rm_start_requested)
{
    app_env.rm_start_requested = 0;

    /* Only start if RM is completely off (audio_streaming=0).
     * Skip when already connected, searching, or debouncing. */
    if (!app_env.audio_streaming)
    {
        app_env.rm_disc_state = RM_DISC_NONE;
        app_env.rm_timeout_ticks = 0;

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
}
```

#### rm_stop_requested 处理（完整清理）

```c
if (app_env.rm_stop_requested)
{
    app_env.rm_stop_requested = 0;
    app_env.rm_disc_state = RM_DISC_NONE;
    app_env.rm_timeout_ticks = 0;

    bs300_mute();

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
    low_power_clk_param.low_power_enable = true;
}
```

#### DEBOUNCE 状态机

```c
if (app_env.rm_disc_state == RM_DISC_DEBOUNCE) {
    app_env.rm_disc_counter++;
    if (app_env.rm_disc_counter >= RM_DISC_DEBOUNCE_THRESHOLD) {
        app_env.rm_disc_state = RM_DISC_HEARING_AID;
        if (app_env.saved_prog_before_rm != 3) {
            bs300_switch_program(app_env.saved_prog_before_rm);
        }
        bs300_active();
        RM_Enable(1000);
    }
}
```

#### 200ms 定时器驱动（timer_200ms 标志 + 搜索超时）

```c
if (app_env.timer_200ms) {
    app_env.timer_200ms = 0;
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);

    if (app_env.rm_timeout_ticks > 0
        && app_env.rm_disc_state != RM_DISC_NONE) {
        app_env.rm_timeout_ticks--;
        if (app_env.rm_timeout_ticks == 0)
            app_env.rm_stop_requested = 1;
    }
}
```

> **设计要点**：`ke_timer_set` 在 **handler 内部 re-arm 自己会失败**（CP 模式下），因此 APP_Timer 只设 `timer_200ms = 1` 标志，真正的 `ke_timer_set` 重投在 Main_Loop 中执行。

### 3.2 `rm_app.c` — 回调

```c
case LINK_ESTABLISHED:
    app_env.rm_timeout_ticks = 0;   // 停止超时定时器

    switch (app_env.rm_disc_state) {
    case RM_DISC_NONE:              // 首次连接
        bs300_active();
        break;
    case RM_DISC_DEBOUNCE:          // 快速重连 — 程序3还在
        bs300_active();
        app_env.rm_disc_state = RM_DISC_NONE;
        break;
    case RM_DISC_HEARING_AID:       // 搜索中重连 — 切回程序3
        bs300_mute();
        if (app_env.saved_prog_before_rm != 3)
            bs300_switch_program(3);
        bs300_active();
        app_env.rm_disc_state = RM_DISC_NONE;
        break;
    }

    asrc_stable = false;
    // ... ASRC reset + enable IRQs ...

case LINK_DISCONNECTED:
    if (app_env.init_done && app_env.audio_streaming) {
        app_env.rm_disc_state = RM_DISC_DEBOUNCE;
        app_env.rm_disc_counter = 0;
        app_env.rm_timeout_ticks = RM_TIMEOUT_TICKS;  // 重启超时
        bs300_mute();
    }
    app_env.rm_lostLink_counter++;
    break;
```

### 3.3 `app_process.c` — APP_Timer

```c
int APP_Timer(ke_msg_id_t const msg_id, void const *param,
              ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    /* Do NOT self-re-arm here — handler re-arm fails in CP mode.
     * Re-arm is done in Main_Loop, and only when audio_streaming=1. */
    app_env.timer_200ms = 1;

    if (ble_env.state == APPM_CONNECTED)
        Sys_GPIO_Set_High(LED_DIO);
    else if (ble_env.state == APPM_ADVERTISING)
        Sys_GPIO_Toggle(LED_DIO);
    else
        Sys_GPIO_Set_Low(LED_DIO);

    return (KE_MSG_CONSUMED);
}
```

### 3.4 `app_init.c`

RM 参数配置保留在 `App_Initialize` 中（配而不启），实际启动移到 Main_Loop 冷启块。

```c
#ifdef APP_RM_ENABLE
    APP_RM_Init(ear_side);
    RF_SwitchToBLEMode();
    app_env.audio_streaming = 0;
#endif
```

### 3.5 `ble_custom.c`

RM_ONOFF 特征值写入无状态判断，直接设标志：
- 写 0x01 → `rm_start_requested = 1`（Main_Loop 中判断 `audio_streaming` 后决定是否执行）
- 写 0x00 → `rm_stop_requested = 1`

---

## 四、主循环完整流程

```
冷启
  → BS300已active在默认程序
  → saved_prog_before_rm = get_active_prog()
  → Audio_Init → RF_SwitchToCPMode → RM_Enable(1000)
  → audio_streaming=1, state=HEARING_AID, timeout_ticks=300
  → WFE + RM搜索 + BS300助听

LINK_ESTABLISHED
  → timeout_ticks=0（停超时）
  → 按state分支:
    NONE         → active()                    首次连接
    DEBOUNCE     → active() → state=NONE       快速重连
    HEARING_AID  → mute → 切程序3 → active()   搜索中重连

LINK_DISCONNECTED
  → state=DEBOUNCE, counter=0, timeout_ticks=300（重启超时）
  → mute()（立即静音）

Main_Loop DEBOUNCE:
  counter++
  ├─ <阈值 且 LINK_ESTABLISHED → active() → state=NONE（秒恢复）
  └─ >=阈值 → state=HEARING_AID
       → 切助听程序 → active() → RM_Enable(1000) → 搜索+助听

200ms Tick（仅 CP 模式，audio_streaming=1）:
  timer_200ms标志 → Main_Loop消费 → ke_timer_set重投(仅CP) + 递减计数
  timeout_ticks==0 → rm_stop_requested → 完整清理 → BLE低功耗（定时器自然死亡）

BLE 写 0x01（仅 audio_streaming==0 时生效）:
  → 完整RM启动序列

BLE 写 0x00:
  → 完整清理 → BLE低功耗
```

---

## 五、状态机

### 5.1 状态定义

```
RM_DISC_NONE (0)        正常（已连接或空闲）
RM_DISC_DEBOUNCE (1)    消抖等待，计数器累加
RM_DISC_HEARING_AID (2) 助听模式，BS300跑原程序，RM持续搜索
```

### 5.2 状态转换

```
NONE ──[LINK_DISCONNECTED]──▶ DEBOUNCE
  ▲                              │
  │    ┌── LINK_ESTABLISHED ─────┘  (快速重连，counter < 阈值)
  │    │
  │    └── counter >= 阈值 ──▶ HEARING_AID
  │                                │
  └── LINK_ESTABLISHED ────────────┘  (搜索中重连)

  任意状态 ──[rm_stop_requested]──▶ 完整清理 → NONE
  任意状态 ──[rm_start_requested, audio_streaming==0]──▶ 完整启动
```

### 5.3 竞态保护

`LINK_ESTABLISHED` 由 `RM_StatusHandler()` 触发（Main_Loop 顶部），`bs300_switch_program()` 是阻塞 I2C（Main_Loop 中后段）。两者不会在同一轮并发。

如果在 `switch_program` 期间 RM 链路建立：`LINK_ESTABLISHED` 在**下一轮**触发，此时状态已变为 `HEARING_AID`，走"搜索中重连"分支（多一次程序往返切换，但因已超时 >1s，用户感知为正常重连延迟）。

---

## 六、定时器机制

### 6.1 核心问题

`ke_timer_set` 在 handler 内部 re-arm 自己在 **CP 模式下失败**（首次触发后不再周期触发）。但从 **Main_Loop 调用 `ke_timer_set` 在 BLE 模式下会导致功耗异常**（内核睡眠唤醒计时被破坏）。

最终方案：**CP/BLE 双模分工**。

### 6.2 实现

```
APP_Timer (handler) → 不 re-arm，只设 timer_200ms = 1 + LED
        ↓
Main_Loop → 消费 timer_200ms
        ├─ audio_streaming=1 (CP) → ke_timer_set 重投 + 递减计数
        └─ audio_streaming=0 (BLE) → 什么都不做，定时器自然死亡
```

```c
// app_process.c — APP_Timer
int APP_Timer(...) {
    app_env.timer_200ms = 1;   // 只设 flag，不 re-arm
    // ... LED ...
    return (KE_MSG_CONSUMED);
}

// app.c — Main_Loop
if (app_env.timer_200ms) {
    app_env.timer_200ms = 0;
    if (app_env.audio_streaming) {
        if (rm_timeout_ticks > 0 && state != NONE) {
            rm_timeout_ticks--;
            if (rm_timeout_ticks == 0)
                rm_stop_requested = 1;     // 超时触发，不 re-arm（定时器死）
            else
                ke_timer_set(...);         // 继续下一轮
        }
    }
}
```

### 6.3 模式切换行为

| 事件 | CP 模式 | BLE 模式 |
|------|---------|----------|
| APP_Timer 触发 | 设 flag | 设 flag |
| Main_Loop 处理 | re-arm + 计数 | 不 re-arm |
| 定时器状态 | 持续运行 | 自然死亡 |
| 原因 | handler re-arm 在 CP 失败，需 Main_Loop 补 | Main_Loop re-arm 破坏 BLE 睡眠，需避免 |

### 6.4 超时参数

| 参数 | 值 | 说明 |
|------|-----|------|
| `TIMER_200MS_SETTING` | 20 | ke_timer 单位 10ms，20×10 = 200ms |
| `RM_TIMEOUT_TICKS` | 150 | 150×200ms = 30s（调试值，发布时改 300 = 1min） |

---

## 七、BLE 指令守卫

| 指令 | 条件 | 行为 |
|------|------|------|
| 0x01（启动） | `audio_streaming == 0` | 完整 RM 启动 |
| 0x01（启动） | `audio_streaming == 1` | 跳过（已连接/搜索/消抖中） |
| 0x00（停止） | 无守卫 | 完整清理（已清理时多跑一次空操作，无害） |

---

## 八、功耗表现

| 模式 | 行为 |
|------|------|
| 冷启 | WFE 模式，RM 搜索 + BS300 助听 |
| RM 连接 | WFE 模式，RM 音频推流 |
| RM 断连消抖期 | WFE 模式（`audio_streaming=1`，不睡眠） |
| RM 断连后（助听+搜索） | WFE 模式 |
| 超时/BLE 写 0x00 后 | BLE 低功耗睡眠 |

---

## 九、关键问题与解决

| 问题 | 根因 | 解决 |
|------|------|------|
| `ke_timer_set` handler 内 re-arm 在 CP 模式失败 | CP 模式下内核定时器只触发一次 | Main_Loop 在 `audio_streaming=1` 时重投 |
| `ke_timer_set` Main_Loop 调用导致 BLE 功耗异常 | 定时器在 Kernel_Schedule 外部设置，破坏睡眠唤醒计时 | BLE 模式（`audio_streaming=0`）不调用，定时器自然死亡 |
| 切 BLE 前多调一次 ke_timer_set | 超时触发同一轮内 re-arm 了才去切 | 超时归零那轮不 re-arm |
| 冷启 `saved_prog_before_rm` 为 0 | `bs300_driver_init` 在 `App_Initialize` 之后 | 冷启块放在 Main_Loop preamble（`bs300_test_run` 之后） |
| BLE 写 0x01 打断已连接 RM | 无状态判断 | `audio_streaming==1` 时跳过 |
| RM 断连后噗声 | DSP 无数据仍在 active | `LINK_DISCONNECTED` 立即 `bs300_mute()` |
| 进入 RM 杂音 | 搜索期 DSP 有输出 | active 推迟到 `LINK_ESTABLISHED` |
| 退出 RM 噗声 | 硬件清理时 BS300 活跃 | 先 `bs300_mute()` 再拆硬件管道 |

---

## 十、测试要点

1. 冷启 → 1min 无 TX → 自动切 BLE 低功耗
2. 冷启 → TX 连接 → 音频正常 → TX 断开 → 消抖后切助听 → 1min 超时退 BLE
3. TX 快速断开再连（<1s）→ 秒恢复，无程序切换
4. BLE 写 0x01 在 RM 搜索中 → 跳过，不打断
5. BLE 写 0x00 在任何状态 → 完整清理回 BLE
6. 超时退 BLE 后 → BLE 写 0x01 → 正常启动 RM
