# App 切模式 / 调音量 — RSL10 实现文档

> 2026-07-13 | `peripheral_server_sleep` 项目
> 对应协议文档: `docs/瑞听听力产品控制接口文档(1).md`

## 1. 概述

手机 App 通过 BLE 发送 HDLC 协议帧控制助听器。RSL10 端接收 BLE 数据 → HDLC 解帧 → 调用 BS300 I2C 异步接口 → 返回响应。

### 支持的指令

| CMD_ID | 名称 | 方向 | 说明 |
|--------|------|------|------|
| 2 | SetVolume | App→设备 | 设置音量 0-9 |
| 3 | SetDeviceOnOff | App→设备 | 开关机（TODO） |
| 4 | GetBatteryInfo | App→设备 | 获取电量 |
| 15 | GetCurrentScene | App→设备 | 获取当前程序/音量/EQ |
| 16 | SetCurrentScene | App→设备 | 切换程序 0-3 |
| 26 | GetDeviceConfig | App→设备 | 获取设备信息 |

## 2. 数据流

```
┌──────────┐   BLE Notification   ┌──────────────┐   HDLC Frame    ┌────────────────┐
│  BLE App  │ ──────────────────▶ │  RSL10 BLE   │ ──────────────▶ │ rempro_cmd_    │
│  (手机)   │                     │  (ROLE char) │                 │ process()      │
└──────────┘                     └──────────────┘                 └───────┬────────┘
                                                                         │
                                                    ┌────────────────────┘
                                                    ▼
                              ┌──────────────────────────────────────────┐
                              │  HDLC 解帧                                │
                              │  ├─ 分片重组 (reasm_buf, ≤100B)           │
                              │  ├─ 7E 分隔符定位                         │
                              │  ├─ byte-unstuffing (7D5E→7E, 7D5D→7D)  │
                              │  ├─ FCS 校验                              │
                              │  └─ 提取 CMD_ID + Data                    │
                              └────────────────────┬─────────────────────┘
                                                   ▼
                              ┌──────────────────────────────────────────┐
                              │  Command Handler                          │
                              │  ├─ cmd_setvolume      → bs300_set_volume_async │
                              │  ├─ cmd_setcurrentscene → bs300_switch_program_async │
                              │  ├─ cmd_getcurrentscene → bs300_get_dsp_state() │
                              │  └─ ...                                   │
                              └────────────────────┬─────────────────────┘
                                                   ▼
                              ┌──────────────────────────────────────────┐
                              │  HDLC 组帧 + BLE 发送                     │
                              │  ├─ hdlc_response(cmd_id, flag, data)    │
                              │  ├─ byte-stuffing                         │
                              │  ├─ 7E 包裹                               │
                              │  └─ RemproService_SendNotification (≤20B/chunk) │
                              └──────────────────────────────────────────┘
```

## 3. 文件结构

```
peripheral_server_sleep/
├── include/
│   └── ble_rempro_cmd.h        # CMD ID 常量 + rempro_cmd_process() 声明
├── code/
│   ├── ble_rempro_cmd.c        # HDLC 协议栈 + 6 个指令处理函数
│   ├── bs300_ram_sync.c        # BS300 I2C 异步引擎 (switch/volume/deferred)
│   └── app.c                   # Main_Loop 调度: BLE RX 分发 + deferred 处理
└── include/
    └── bs300_ram_sync.h        # bs300_xxx_async() + bs300_process_deferred()
```

## 4. HDLC 协议实现

### 4.1 帧格式

```
┌──────┬────────┬────────┬────────┬──────────┬─────┬──────┐
│ 0x7E │ SYS_ID │ CMD_ID │ CMD_ID │ Data...  │ FCS │ 0x7E │
│  1B  │   1B   │  (low) │ (high) │  (可变)   │ 1B  │  1B  │
└──────┴────────┴────────┴────────┴──────────┴─────┴──────┘
```

- **SYS_ID**: 固定 0x00
- **CMD_ID**: 2 字节 little-endian
- **FCS**: 所有字段（不含分隔符）的 8-bit 累加和

### 4.2 Byte-stuffing

| 原始值 | 转义后 |
|--------|--------|
| 0x7E | 0x7D 0x5E |
| 0x7D | 0x7D 0x5D |

只在分隔符之间的帧体内做 stuff/unstuff。处理时先解 stuff 再校验 FCS。

### 4.3 分片重组

BLE 一次最多发 20 字节，HDLC 响应可能超过 20 字节。接收端用 `reasm_buf[100]` 累积分片，逐帧解析。发送端 `hdlc_response()` 内自动切 ≤20B chunk 分多次 `RemproService_SendNotification`。

### 4.4 关键实现

```c
// ble_rempro_cmd.c

// BLE RX 特性触发 → rempro_env.role_value_changed = 1
// Main_Loop 中调用:
void rempro_cmd_process(void)
{
    if (!rempro_env.role_value_changed) return;
    rempro_env.role_value_changed = 0;

    // 追加到重组缓冲
    memcpy(reasm_buf + reasm_len, rempro_env.role_value, chunk_len);
    reasm_len += chunk_len;

    // 逐帧解析
    while (reasm_len >= 7) {
        // 跳过前导垃圾字节
        if (reasm_buf[0] != 0x7E) { memmove... }

        hdlc_parse_frame(reasm_buf, reasm_len, &cmd_id, &data_len, &consumed);
        if (consumed == 0) return;  // 帧不完整，等更多数据

        switch (cmd_id) { ... }     // 分发到具体 handler

        // 从缓冲中移除已处理的帧
        reasm_len -= consumed;
        memmove(reasm_buf, reasm_buf + consumed, reasm_len);
    }
}
```

## 5. 异步 I2C 引擎

### 5.1 架构

所有 BS300 I2C 操作通过 `bs300_sync_session_t` 状态机异步执行：

```
┌──────────────┐    ke_timer     ┌──────────────┐
│  SEND        │ ──────────────▶ │  POLL        │
│  发 I2C 写   │                 │  等 FURPROC   │
└──────────────┘                 └──────┬───────┘
       ▲                               │
       │        ke_timer               │
       └───────────────────────────────┘
                                       │ 最后一条完成
                                       ▼
                               ┌──────────────┐
                               │  DONE/ERROR  │
                               │  回调 on_done │
                               └──────────────┘
```

- **SEND**: 发一条 I2C Advanced Write (53B)，转 POLL，ke_timer 延迟 2 (20ms)
- **POLL**: 发 Read Request (2B) + Read Response (4B)，检查 bit23。重试最多 30 次。延迟 6 (60ms)
- **DONE**: 所有命令发送完成，copy s_target → s_dsp_state，调回调
- **ERROR**: 连续失败，调回调

### 5.2 异步 API

```c
// 切换程序 — 加载 struct → diff → 异步逐条 I2C
int bs300_switch_program_async(uint8_t new_prog_idx, void (*on_done)(void));

// 设置音量 — 重新编码 bin_gain → 异步 I2C
int bs300_set_volume_async(uint8_t level, void (*on_done)(void));

// 增量同步 — 对比 s_target vs s_dsp_state
int bs300_resync_diff_async(bs300_prog_struct_t *_new, void (*on_done)(void));
```

所有 async 函数立即返回，I2C 由 `BS300_SyncTimer` (ke_timer) 驱动。

### 5.3 忙状态处理 — 抢断 + 延迟

当新的切模式/调音量请求到达时，如果当前 I2C 会话正在执行：

```
bs300_switch_program_async(new_prog)
  if (bs300_sync_is_busy())
    → g_bs300_sync.abort_requested = true   // 中止当前会话
    → s_pending_switch = new_prog           // 存储延迟请求
    → return 0                              // 不阻塞
```

**bs300_sync_tick()** 每步检查 `abort_requested`，一旦置位立即将状态切为 IDLE。

**bs300_process_deferred()** 在主循环中调用，检测 `s_pending_switch >= 0` 或 `s_pending_volume >= 0`，执行延迟的切换/音量操作。

```
bs300_process_deferred()
  if (!bs300_sync_is_busy())
    if (s_pending_switch >= 0)
      → bs300_switch_program_async(s_pending_switch, s_pending_switch_on_done)
    else if (s_pending_volume >= 0)
      → reencode_bin_gain_async_core(s_pending_volume_cb, ...)
```

### 5.4 关键约束

- **抢断时不能覆写 session 结构体**: busy 时只设 `abort_requested`，不调 `bs300_sync_session_init()`（会清零 abort_requested）
- **延迟执行不能栈溢出**: `save_settings()` 原来的 960B 栈分配已改为复用全局 `bs300_work_buf[480]`。`bs300_process_deferred()` 必须在主循环调用（不在 timer handler 内），避免 flash erase 期间栈溢出
- **Flash erase 不在连接态执行**: `save_settings()`（程序号+音量持久化）已从切换路径移除，改在 BLE 断开时调用 `bs300_settings_persist()`。Flash_EraseSector 耗时 ~40ms，连接态执行会阻塞 BLE 时序

## 6. Main_Loop 调度

```c
// app.c — 简化后的主循环关键路径

while (true) {
    Kernel_Schedule();           // BLE 事件 + BS300 timer

    // [按钮处理 — 已移除，待重新实现]

    if (ble_env.state == APPM_CONNECTED) {

        // BS300 快捷命令 (BLE CS characteristic, 2B cmd)
        if (cs_env.rx_value_changed) {
            if (cmd == 0x01) bs300_switch_program_async(arg, on_bs300_switch_done);
            if (cmd == 0x02) bs300_set_volume_async(arg, on_bs300_volume_done);
        }

        // 延迟的切换/音量 (abort 后的重试)
        bs300_process_deferred();

        // App HDLC 命令 (BLE ROLE characteristic, 分片重组)
        rempro_cmd_process();
    }

    // 休眠 (low_power_enable == true 时进入 POWER_MODE_SLEEP)
}
```

## 7. 指令实现细节

### 7.1 SetVolume (CMD=2)

```
请求: dev_type(1) + volume(1) + volume2(1) = 3B
      dev_type: 0=左右, 1=左, 2=右
      volume: 0-9

处理:
  if (dev_type == 0 || dev_type == 1)
    app_env.volume = volume
    bs300_set_volume_async(volume, NULL)

响应: flag(0) + status(1) = 2B
```

### 7.2 SetCurrentScene (CMD=16)

```
请求: dev_type(1) + scene_id(1) = 2B
      scene_id: 0-3

处理:
  if (scene_id < 4)
    bs300_switch_program_async(scene_id, NULL)
    // busy 时自动 abort + defer

响应: flag(0) + status(1) = 2B
```

### 7.3 GetCurrentScene (CMD=15)

```
请求: 无数据

响应: 12B
  Left_Scene_ID(1) + Right_Scene_ID(1)
  + Volume_Left(1) + Volume_Right(1)
  + Denoise(1)
  + Left_EQ_Low/Mid/High(3) + Right_EQ_Low/Mid/High(3)
  + reserved(1)

EQ 转换: dB [-12,12] → app scale [0,100]
  app_val = 50 + dB * 4   (clamped to 0-100)
  50 = 0dB (flat)
```

### 7.4 GetDeviceConfig (CMD=26)

```
响应: 29B
  固件版本(4) + Program数量(1)
  + 左耳MAC(6) + 产品型号(2, =101) + 芯片类型(1, =1) + 拐点数(1, =2) + 通道数(1, =16)
  + 右耳同上(11)
  + 音量档位(1, =9)
```

## 8. BS300 快捷命令 (BLE CS characteristic)

除了 HDLC 协议，还支持通过 BLE Custom Service characteristic 发 2 字节快捷命令：

| cmd | arg | 功能 |
|-----|-----|------|
| 0x01 | 0-3 | 切程序 |
| 0x02 | 0-9 | 调音量 |
| 0xFE | — | 清缓存 |

用于调试和简单控制场景，无需 HDLC 组帧。

## 9. 时序参考

### 切程序 (CMD=16)

```
CMD=16 收到 → rempro_cmd_process()
  → cmd_setcurrentscene()
    → bs300_switch_program_async()
      ├─ load_struct (flash→s_target): ~5ms
      ├─ s_cur_prog = new_prog_idx
      ├─ bs300_session_init + build_diff_session
      └─ start_async_session → ke_timer 启动
  → hdlc_response (立即返回, 不等 I2C 完成)

BS300_SyncTimer tick (每 20-60ms):
  → bs300_sync_tick() → SEND I2C → POLL → ... → DONE
  → on_done 回调
```

I2C 指令数取决于新旧程序的差异字段。全量 ~14 条，增量 1-14 条。每条耗时 ~200ms (I2C + 60ms poll)。全量切换总耗时 ~2.8s。

### 调音量 (CMD=2)

```
CMD=2 收到 → cmd_setvolume()
  → bs300_set_volume_async()
    ├─ 更新 s_dsp_state.modules.volume_level
    ├─ 更新 s_volumes[s_cur_prog]
    ├─ busy? → s_pending_volume = level, return
    └─ !busy? → reencode_bin_gain_async_core()
         ├─ bs300_encode_wdrc_bin_gain (编码 bin_gain 48B)
         ├─ 追加 0xFCD2F2 (提示音) + 0x8060B2 (bin_gain)
         └─ start_async_session → ke_timer 启动
```

固定 2 条 I2C，约 400ms。

## 10. 设计决策

| 决策 | 结论 | 原因 |
|------|------|------|
| 响应不等 I2C 完成 | 立即返回 flag=0 | I2C 操作 ~3s，BLE 响应需在连接间隔内返回 |
| 忙时抢断 | abort_requested + pending_switch | 不覆写 session，避免 I2C/DSP 混乱 |
| flash 持久化延迟 | BLE 断开时保存 | Flash_EraseSector ~40ms，连接态会阻塞 BLE |
| I2C 速率 | bit_delay=500 | DSP 运行时不能处理高速 I2C |
| BLE CS 快捷命令 | 2B 协议 | 调试用，比 HDLC 简单 |
