# 方案四：Sleep 端 BLE/RM 轮询 实现记录

> 2026-08-05 | 最终功能可用的版本

## 原理

机身 50% 占空比在 BLE 广播（600μA）和 RM RX 搜索（~1mA）之间交替。BLE 阶段用广播事件计数（200ms × 25 = 5s），RM 阶段用 200ms 内核定时器倒计时（25 × 200ms = 5s）。RM 连上就锁住推流，连不上 5s 后回 BLE。

## 涉及文件

| 文件 | 改动 |
|------|------|
| `peripheral_server_sleep/include/app.h` | 新增 polling 状态字段 + `RM_POLL_BLE_TICKS` 宏 |
| `peripheral_server_sleep/include/ble_std.h` | `CFG_ADV_INTERVAL_MS` 100→200（广播间隔对齐 polling 计时） |
| `peripheral_server_sleep/app.c` | 冷启 polling、RM 入口、轻量超时停止 |
| `peripheral_server_sleep/code/rm_app.c` | LINK_DISCONNECTED 支持 poll_locked 立即停止；LINK_ESTABLISHED 延迟 Audio_Init |
| `peripheral_server_sleep/code/app_process.c` | （无实质改动，仅注释调整） |

---

## 详细改动

### 1. `include/app.h` — 新增字段和宏

在 `struct app_env_tag` 的 `#ifdef APP_RM_ENABLE` 块末尾新增：

```c
/* Scheme 4: 50% duty-cycle BLE/RM polling */
uint8_t  poll_enabled;          /* polling mode active (BLE phase counting) */
uint16_t poll_tick;             /* BLE wakeup counter */
uint8_t  poll_locked;           /* RM started from polling → immediate stop on disconnect */
uint8_t  poll_enter_rm;         /* flag: enter RM at loop top next iteration */
```

在文件末尾新增宏：

```c
#define RM_POLL_BLE_TICKS       25   /* 200ms BLE events × 25 = 5s */
```

### 2. `include/ble_std.h` — 广播间隔

```c
// 改前：
#define CFG_ADV_INTERVAL_MS             100

// 改后：
#define CFG_ADV_INTERVAL_MS             200   /* 200ms for scheme4 polling, 25 ticks = 5s */
```

### 3. `app.c` — 核心逻辑

#### 3.1 冷启：启动 polling

替换原来的冷启 RM 入口（`Audio_Init + RF_SwitchToCPMode + RM_Enable`）：

```c
#ifdef APP_RM_ENABLE
    /* Scheme 4: BLE polling → RM search */
    {
        static uint8_t rm_cold_boot_done = 0;
        if (!rm_cold_boot_done) {
            rm_cold_boot_done = 1;
            app_env.poll_enabled = 1;
            app_env.poll_tick  = 0;
        }
    }
#endif
```

#### 3.2 while 循环顶部：RM 入口

```c
/* Scheme 4: enter RM at loop top (same timing as cold boot) */
if (app_env.poll_enter_rm)
{
    app_env.poll_enter_rm = 0;
    app_env.poll_locked = 1;
    app_env.saved_prog_before_rm = bs300_get_active_prog();
    RF_SwitchToCPMode();
    RM_Enable(500);
    app_env.audio_streaming = 1;
    app_env.rm_disc_state = RM_DISC_HEARING_AID;
    app_env.rm_timeout_ticks = RM_POLL_BLE_TICKS;
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);
}
```

#### 3.3 轻量超时停止：5s 无连接回 BLE

在 `rm_stop_requested` 原有处理之前，新增超时专用轻量停止：

```c
/* Scheme 4: RM search timeout → lightweight stop, back to BLE polling */
if (app_env.poll_locked
    && app_env.rm_timeout_ticks == 0
    && app_env.rm_disc_state == RM_DISC_HEARING_AID
    && app_env.rm_stop_requested)
{
    app_env.rm_stop_requested = 0;
    app_env.audio_streaming = 0;
    RM_Disable();
    Sys_Timers_Stop(SELECT_TIMER0);
    Sys_Timers_Stop(SELECT_TIMER1);
    NVIC_ClearPendingIRQ(TIMER0_IRQn);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    RF_SwitchToBLEMode();
    low_power_clk_param.low_power_enable = true;
    app_env.poll_enabled  = 1;
    app_env.poll_tick     = 0;
    app_env.poll_locked   = 0;
}
```

#### 3.4 RM 停止后恢复 polling

在原有的 `rm_stop_requested` 处理末尾（`audio_streaming = 0` 之后），新增：

```c
/* Scheme 4: resume polling after RM stop */
app_env.poll_enabled  = 1;
app_env.poll_tick     = 0;
app_env.poll_locked   = 0;
```

#### 3.5 RM 断开消抖后 polling 恢复

在 `RM_DISC_DEBOUNCE` 处理中 `RM_Enable(500)` 调用前，如需要可加 polling 状态处理（当前未加，走原有流程）。

#### 3.6 while 循环末尾：BLE 唤醒计数

```c
/* Scheme 4: count BLE wakeups, flag RM entry for next loop top */
if (app_env.poll_enabled) {
    app_env.poll_tick++;
    if (app_env.poll_tick >= RM_POLL_BLE_TICKS) {
        app_env.poll_tick = 0;
        app_env.poll_enabled = 0;
        app_env.poll_enter_rm = 1;
    }
}
```

#### 3.7 定时器 200ms 处理

原有的 `timer_200ms` 处理保持不动（CP 模式倒计时 + re-arm）。新增 polling 超时触发轻量停止（见 3.3）。

#### 3.8 TX 连接 / BLE 指令 触发 RM

原有的 `tx_connect_detected` 和 `rm_start_requested` 处理前，加 `poll_enabled = 0` 停止 polling。

### 4. `code/rm_app.c` — 回调适配

#### 4.1 LINK_DISCONNECTED：poll_locked 立即停止

```c
case LINK_DISCONNECTED:
{
    if (app_env.init_done && app_env.audio_streaming)
    {
        if (app_env.poll_locked)
        {
            /* Scheme 4: immediate stop, resume polling */
            app_env.rm_stop_requested = 1;
        }
        else
        {
            // ... 原有的 DEBOUNCE 逻辑 ...
        }
    }
    app_env.rm_lostLink_counter++;
}
```

#### 4.2 LINK_ESTABLISHED：polling 时延迟 Audio_Init

```c
case LINK_ESTABLISHED:
{
    /* Scheme 4: Audio_Init deferred from RM search to save 100μA */
    if (app_env.poll_locked) {
        Audio_Init();
    }
    // ... 原有逻辑 ...
}
```

---

## 状态流转

```
冷启 → poll_enabled=1, BLE 广播 200ms

while 循环末尾:
  poll_tick++ (每个 BLE 唤醒)
  poll_tick >= 25 → poll_enter_rm=1

while 循环顶部:
  poll_enter_rm → RF_SwitchToCPMode + RM_Enable
  audio_streaming=1, timeout=25 (5s)

  ├─ LINK_ESTABLISHED → Audio_Init → BS300 prog3 → 推流
  │   └─ LINK_DISCONNECTED → poll_locked → rm_stop → 回 polling
  │
  └─ 5s 超时 → 轻量停止 → 回 polling
```

## 已知限制

1. **RM 搜索功耗偏高 ~100μA**：BLE 广播 5s 后进 RM，BLE baseband 残留功耗。`BB_DEEP_SLEEP` 能省但阻塞回 BLE 通路（硬件限制）。平均 polling 功耗 ~800μA（vs 理想 750μA）。

2. **RM 阶段无 Audio_Init**：省 100μA 但 LINK_ESTABLISHED 时才初始化，首次连接有短暂延迟。

3. **200ms 广播间隔**：BLE 事件频率较高，BLE 广播功耗略增。如需更低功耗可适当拉大间隔和阈值。
