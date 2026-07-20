# App 切模式 / 调音量 — RSL10 实现文档

> 2026-07-13 | `peripheral_server_sleep` 项目
> 对应协议文档: `docs/瑞听听力产品控制接口文档(1).md`

## 1. 概述

手机 App 通过 BLE 发送 HDLC 协议帧控制助听器。RSL10 端接收 BLE 数据 → HDLC 解帧 → 调用 BS300 I2C 异步接口 → 返回响应。

### 支持的指令

| CMD_ID | 名称 | 方向 | 说明 |
|--------|------|------|------|
| 2 | SetVolume | App→设备 | 设置音量 0-9 |
| 3 | SetDeviceOnOff | App→设备 | 开关机（MUTE/ACTIVE） |
| 4 | GetBatteryInfo | App→设备 | 获取电量 |
| 5 | SetFeedbackOnOff | App→设备 | 设置反馈抑制开关 |
| 15 | GetCurrentScene | App→设备 | 获取当前程序/音量/EQ |
| 16 | SetCurrentScene | App→设备 | 切换程序 0-3 |
| 26 | GetDeviceConfig | App→设备 | 获取设备信息 |
| 33 | GetDeviceOnOff | App→设备 | 获取设备开关状态 |
| 34 | GetFeedbackOnOff | App→设备 | 获取反馈抑制开关状态 |

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
         ├─ 追加 TONE (提示音，vol=9 时 0xFD12F2, 其他 0xFCD2F2) + 0x8060B2 (bin_gain)
         └─ start_async_session → ke_timer 启动
```

固定 2 条 I2C，约 400ms。

## 10. 设计决策

| 决策 | 结论 | 原因 |
|------|------|------|
| 响应不等 I2C 完成 | 立即返回 flag=0 | I2C 操作 ~3s，BLE 响应需在连接间隔内返回 |
| 忙时抢断 | abort_requested + pending_switch | 不覆写 session，避免 I2C/DSP 混乱 |
| flash 持久化—按键 | **立即保存** | 按键是本地操作，用户期望断电不丢 |
| flash 持久化—App | **BLE 断开时保存** | App 操作频繁（验配反复调参），减少擦写 |
| flash 持久化—程序3 | **不保存**，persist 时存 0，restore 时改为 0 | 程序 3 为 RM 音频中转，不应成为开机默认 |
| Settings 存储机制 | **64B slot 追加写**，2KB 扇区存 32 个 slot，32 次写入擦 1 次 | 避免每次按键都触发 Flash_EraseSector(~40ms + 关中断) |
| Program 存储机制 | **512B slot 追加写**，每扇区 4 个 slot，4 次写入擦 1 次 | 验配 SetGain/MPO/CR 重复写入时减少擦除 |
| I2C 速率 | bit_delay=500 | DSP 运行时不能处理高速 I2C |
| BLE CS 快捷命令 | 2B 协议 | 调试用，比 HDLC 简单 |

## 11. MM Plus 解码踩坑记录 (2026-07-15)

### 11.1 Flash 布局（Handbook §MM Plus, Cmd=0x17）

| Byte | 字段 | 类型 | 公式 |
|------|------|------|------|
| 0 | mic_mixing_ratio | uint8 | `raw = 50 + value_in_MT` |
| 1 | type | uint8 | `0x00`=Telecoil, `0x01`=DAI |
| 2 | — | — | `0x00` padding |

### 11.2 历史 bug 链

| # | bug | 根因 | 修复 |
|---|-----|------|------|
| 1 | 0x800062 全零（`frac24=0x080000`） | `mix_ratio` 未从 flash 解码，保持 BSS 默认值 0 | `mix_ratio = raw[0] - 50` |
| 2 | `mix_ratio` 解码后溢出为 200+ | 字段类型是 `uint8_t`，`raw - 50` 为负时回绕 | 改为 `int8_t`，解码用 `int16_t` 中转 |
| 3 | frac24 恒为 0xFFFFFF | `mm_type` 未解码，igd 始终为 0，导致 `mix_ratio=-21` 时 `idx=290`，table 末端值被 clamp | 解码 `raw[1]` 存入 `mm_type` |
| 4 | frac24 不匹配预期（0x7ECA9D） | `mm_type` 映射错误（自编 1=TC/2=DAI vs 手册 0x00=TC/0x01=DAI） | 按手册修正映射 |

### 11.3 最终正确链路

```
Flash raw[0]=29, raw[1]=0x00
    │
    ├─ mix_ratio = raw[0] - 50 = -21 dB
    ├─ mm_type   = raw[1] = 0x00 → Telecoil
    │
    ▼
I2C 0x800062:
    igd = telecoil_gain_diff = -450 (tenth-dB)
    x   = mix_ratio × 10 - igd = -210 - (-450) = 240
    idx = 500 + x = 740
    frac24 = table[740] = 524288 × 10^(24/20) ≈ 0x7ECA9D
```

### 11.4 相关文件

- `bs300_param_encode.h`: `bs300_modules_t` — 新增 `int8_t mix_ratio`、`uint8_t mm_type`
- `bs300_param_encode.c`: `bs300_flash_to_struct()` MOD_INPUT/case 4 — MM Plus 解码
- `bs300_param_encode.c`: `bs300_encode_mm_plus()` — igd 改用 `mod->mm_type` 查手册映射
- `bs300_encode_tables.h`: `bs300_mm_plus_frac24_table[2001]`

## 12. Flash 持久化架构 (2026-07-17 重构)

### 12.1 两条存储路径

| 路径 | 存储目标 | 内容 | 写时机 |
|------|---------|------|--------|
| **Program Flash** (512B slot ×4 slots ×4 扇区) | `0x0015D000~E800` | WDRC + ENR + Volume/Beep + DFBC + ISS + WNR + AGCO + Input | SetGain/MPO/CompressRatio → `fitting_commit()` |
| **Settings Flash** (64B slot ×32 slots) | `0x0015C800` | active_prog + volume[4] + eq_*[4] + denoise[4] | 按键立即; App 指令延至 BLE 断开 |

### 12.2 追加写（Append-Only Slot）机制

两个 Flash 区域均采用 **slot 追加写**：每次写入追加到下一个空 slot，扇区写满后才擦除一次，大幅减少 `Flash_EraseSector` 调用。

```
Save: slot 0 → 1 → 2 → ... → N-1 → (擦除) → slot 0
Load: slot N-1 → N-2 → ... → 0 (倒序扫描，取第一个有效)
```

空 slot 判定：slot 头部 magic 字段为 `0xFFFFFFFF`（擦除态）。

| 存储 | Slot 大小 | Slot 数 | 每 N 次写入擦除 |
|------|:---:|:---:|:---:|
| Settings | 64B | 32 | 32x |
| Program | 512B | 4 | 4x |

### 12.3 Settings slot 格式 (64B)

```
Offset 0:    active_prog (1B)
      1-4:   volume[0..3] (4B)
      5-8:   eq_low[0..3] (4B, int8 step [-5,5])
      9-12:  eq_mid[0..3] (4B)
      13-16: eq_high[0..3] (4B)
      17-20: denoise[0..3] (4B)
      21-24: feedback_onoff[0..3] (4B, 0=Off 1=On)
      25-28: magic "BSST" (4B, =0x54535342)
      29-30: CRC16 XMODEM over bytes 0-28 (2B)
      31:    version (1B, =4)
      32-63: reserved (0xFF)
```

CRC 仅覆盖 bytes 0-24（数据字段 + magic），确保单 slot 完整性。

### 12.4 Program slot 格式 (512B)

```
Offset 0-479:  480B program raw data
      480-483: magic "BSPG" (4B)
      484-485: version (2B, =1)
      486-487: CRC16 XMODEM over bytes 0-479 (2B)
      488-511: reserved (0xFF)
```

与旧格式完全兼容（旧格式在同一扇区 offset 0 写入相同布局，新代码倒序扫描时 slot 0 就是旧数据）。

### 12.5 程序 3 不掉电保存

- `bs300_settings_persist()`: `s_cur_prog==3` 时存 `0` 而非 `3`
- `bs300_restore_settings()`: 恢复的 `active_prog==3` 时改为 `0`
- 按键长按循环: `(prog + 1) % 3`，跳过程序 3
- RM 进入时: `bs300_persist_active_prog(saved)` 保存进入前的程序号

### 12.6 写入触发时机

| 操作 | Settings Flash | Program Flash |
|------|:---:|:---:|
| 按键短按调音量 | **立即** | — |
| 按键长按切模式 | **立即** | — |
| App CMD=2 调音量 | BLE 断开 | — |
| App CMD=10 调 EQ | BLE 断开 | — |
| App CMD=16 切程序 | BLE 断开 | — |
| App CMD=9 降噪 | BLE 断开 | — |
| App SetGain/MPO/CR | — | 立即（验配提交） |
| BLE 断开 | 统一落盘 | — |

### 12.7 struct → 480B flash 编码

`bs300_struct_to_flash()` 重编码 WDRC (0x12) 和 ENR (0x1C) 模块：

```
原始 480B buffer → 扫描 module directory → 找到 WDRC/ENR → 原地替换 bit-packed 数据
```

- `encode_wdrc_flash()`: bin_gain 32×7bit + 通道数据 (lmt_th: raw=value_in_MT-30, kp_th: raw=value_in_MT)
- `encode_enr_flash()`: nfsf/nhsf/nnsf/snasf 4bit + 16ch×(freq/max_att/snr_th/noise_th/upper_noise_th/etr/nrr)
- 其他模块保持原样不变

## 13. 0x8060B2 I2C 编码验证 (2026-07-15)

### 13.1 公式

```
vol_gain = (volume_level - 9) × 3     // 0→-27dB, 9→0dB
eq_gain  = hz<500?eq_low : hz≤2000?eq_mid : eq_high
gain_cal = calib.out_band[i] - calib.mic1_band[i]

data[i] = trunc((bin_gain[i]*10 + vol_gain*10 + eq_gain*10 - gain_cal*10 - igd) / 10)
        = bin_gain[i] + vol_gain + eq_gain - gain_cal       (igd=0 时约简)
```

### 13.2 I2C 帧结构 (Advanced Write, 53 bytes)

```
[0]=0x10  [1..3]=CMD_LE  [4..51]=48B payload  [52]=chk
```

chk = 0xFF - sum(payload[0..51]) & 0xFF

### 13.3 验证过程中的 bug

- vol=0 导致 vol_gain=-27 → I2C 数据全为负值。根因：Settings 清空后 s_volumes 默认值 0 而非 9。修复：Settings 无效时设默认值 {9,9,9,9} 再调用 bs300_restore_settings。

## 14. 按键主动推送 (2026-07-15)

### 14.1 推送协议

| CMD | 名称 | SYS_ID | 数据 |
|-----|------|:---:|------|
| 4 | Receive Device Volume | 1 | prog(1) + dev_type(1) + vol(1) + vol2(1) |
| 5 | Receive Current Scene | 1 | scene_id(1) |

### 14.2 推送时机

按键长按/短按 → **先推后切**（动作分发时立即 push，不等 I2C 完成）

### 14.3 相关函数

- `hdlc_push()`: 组帧 SYS_ID=1, 无 Flag, ≤20B 分片发送
- `rempro_push_scene_change()`, `rempro_push_volume_change()`: BLE 未连接时跳过

## 15. 上电流程变更 (2026-07-15)

```
Step 3: 强制从 DSP I2C 读取全部 4 个程序（不做缓存检查）→ erase → write flash
Step 4: Settings 清空 (bs300_settings_invalidate) → 用默认值恢复 (prog=0, vol=9, EQ=0)
Step 5: Boot cache → 覆盖 volume + EQ from Settings
Step 6: MUTE → sync full program → ACTIVE
```

每次上电都是干净状态，DSP 程序重读，Settings 默认。调试用。`#ifdef` 或配置开关待加。

## 16. 调试打印速查

- `bs300_print_settings()`: 打印 cur_prog + volume/EQ/denoise 4×4 数组
- 切程序 / 调音量 / 设 EQ / SetGain/MPO/CR/Denoise 入口自动调用
- 8060B2 编码：`[8060B2] vol= vol_gain= igd= input=` — 临时调试用，已清除
- MM Plus：`[MM+] en= mix_ratio= igd= x= idx= frac24=` — 临时调试用，已清除
- Flash decode MM+：`[FLASH] MM+ pos= len= raw= → mix_ratio=` — 临时调试用，已清除

## 17. App HDLC 协议指令清单 (2026-07-15)

### 17.1 指令总览

**App → 设备：**

| CMD_ID | 宏 | 功能 | 数据格式 | 处理链路 |
|:---:|------|------|------|------|
| 2 | `CMD_SETVOLUME` | 设置音量 | dev_type(1)+vol(1)+vol2(1) | `bs300_set_volume_notone_async` → Settings persist |
| 3 | `CMD_SETDEVICEONOFF` | 设置开关机 | dev_type(1)+onoff(1) | ON→`bs300_active()`, OFF→`bs300_mute()` |
| 5 | `CMD_SETFEEDBACKONOFF` | 设置反馈抑制开关 | dev_type(1)+prog(1)+onoff(1) | RAM-only, 立即发 I2C 0x800052, 不改程序 Flash |
| 6 | `CMD_SETGAIN` | 设置增益 | dev(1)+prog(1)+(spectrum(1)+dB(1))* | flash load→改 bin_gain→`fitting_commit`(sync=false) |
| 7 | `CMD_SETMPO` | 设置 MPO | dev(1)+prog(1)+(ch(1)+mpo(1))* | flash load→改 lmt_th_db→`fitting_commit`(sync=false) |
| 8 | `CMD_SETCOMPRESSRATIO` | 设置压缩比 | dev(1)+prog(1)+turn(1)+(ch(1)+step(1))* | flash load→改 kp_r_idx→`fitting_commit`(sync=false) |
| 9 | `CMD_SETDENOISE` | 设置降噪 | dev(1)+prog(1)+level(1) | `s_denoise[prog]=level`→Settings persist (不改 Flash) |
| 10 | `CMD_SETEQUALIZER` | 设置均衡器 | dev(1)+type(1)+val(1) | `bs300_set_eq_async` → Settings persist |
| 15 | `CMD_GETCURRENTSCENE` | 获取当前程序 | 无 | 读取 s_dsp_state → 12B 响应 |
| 16 | `CMD_SETCURRENTSCENE` | 切换/刷新程序 | dev(1)+scene(1) | `bs300_switch_program_async` (同程序触发 re-sync diff) |
| 26 | `CMD_GETDEVICECONFIG` | 获取设备信息 | 无 | 固件版本/MAC/产品型号等 → 29B 响应 |
| 33 | `CMD_GETDEVICEONOFF` | 获取设备开关状态 | 无 | 返回 Left_OnOff(1)+Right_OnOff(1)，读取 `s_device_on` |
| 34 | `CMD_GETFEEDBACKONOFF` | 获取反馈抑制状态 | dev_type(1)+prog(1) | 返回 Left_OnOff(1)+Right_OnOff(1) |

**设备 → App 主动推送：**

| CMD_ID | 宏 | 触发条件 | 数据 | SYS_ID |
|:---:|------|------|------|:---:|
| 4 | `CMD_PUSH_VOLUME` | 按键短按/App调音量 | prog(1)+dev_type(1)+vol(1)+vol2(1) | 1 |
| 5 | `CMD_PUSH_SCENE` | 按键长按 | scene_id(1) | 1 |

### 17.2 HDLC 帧封装

**App→设备 请求帧：** `7E + SYS_ID(0) + CMD(2B LE) + Data + FCS + 7E`

**设备→App 响应帧：** `7E + SYS_ID(0) + CMD(2B LE) + Flag + Data + FCS + 7E`

**设备→App 推送帧：** `7E + SYS_ID(1) + CMD(2B LE) + Data + FCS + 7E`（无 Flag）

Byte-stuffing 规则：`0x7E→0x7D 0x5E`, `0x7D→0x7D 0x5D`。BLE ≤20B 自动分片。

### 17.3 指令分类

| 类别 | 指令 | 共性 |
|------|------|------|
| **运行时** | SetVolume, SetFeedbackOnOff, SetEqualizer, SetCurrentScene, GetCurrentScene | RAM-only 或 Settings Flash，不改 480B 程序 Flash |
| **验配（持久化）** | SetGain, SetMPO, SetCompressRatio, SetDenoise | 改 480B 程序 Flash + resync_diff_async |
| **查询** | GetCurrentScene, GetDeviceConfig | 只读，不写 |

### 17.4 fitting_commit 流程

```
s_fit_buf (已修改的 struct)
  → bs300_struct_to_flash()    // 重编码 WDRC + ENR 到 bs300_work_buf
    → bs300_storage_write_program()  // 写入 480B 到 Main Flash
      → if (prog == active)
          → bs300_resync_diff_async()  // 差异 I2C 同步到 DSP
```

### 17.5 关键设计决策

| 决策 | 结论 | 原因 |
|------|------|------|
| 响应不等 I2C 完成 | flag=0 立即返回 | I2C ~3s, BLE 响应需在连接间隔内 |
| 忙时拒绝验配指令 | flag=1 返回，App 重试 | 验配指令需写 flash，不可抢断 |
| SetGain 重置用户参数 | volume=9, EQ=0, denoise=0 | 验配修改基参时归零用户微调 |
| SetDenoise 双写 | 480B Flash + Settings Flash | ENR 数据 + denoise level 都需要掉电保存 |
| 推送先推后切 | 按键动作分发时立即 push | App 无需等 I2C 完成就能更新 UI |

## 18. MM Plus igd 识别 bug (2026-07-15)

### 18.1 现象

P0 (front_mic) 切 P1 (MM+ + Telecoil, `mm_type=0x00`) 时，diff 引擎只发了 13 条命令，缺失了 8 条 igd 相关命令：

| 缺失命令 | 原因 |
|---------|------|
| 0x8020B2 WDRC KP Th | igd 公式依赖 `input_gain_diff`，P1 需补偿 telecoil_gain_diff |
| 0x8040C2 ENR NT | 同上 |
| 0x8050C2 ENR UNT | 同上 |
| 0x8001B2 ISS | 同上 |
| 0x8001C2 WNR Setup | 同上 |
| 0x8011C2 WNR Band 0-15 | 同上 |
| 0x8411C2 WNR Band 16-31 | 同上 |
| 0x8021C2 WNR Single Mic | 同上 |

同时多发了两条不必要的：
- 0x8070C2 ENR ETR — ETR 公式只依赖 `ma`，不依赖 `snr_th`，被 `snr_changed` 误触发
- 0x8080C2 ENR NRR — 同上

### 18.2 根因

**`get_input_type()` 不认识 MM Plus**

```c
// 修复前
static uint8_t get_input_type(uint8_t input_selection)
{
    case 2: return 1;  // Telecoil
    case 3: return 2;  // DAI
    default: return 0; // Mic  ← P0 (0) 和 P1 (4) 都走这里!
}
```

P1 的 `input_selection=4` (MM+) 落在 default，返回 0 (Mic)，和 P0 一致。`igd_changed=0`，diff 跳过所有 igd 依赖命令。

**TC/DAI diff 同样不认 MM+**

```c
// 修复前：只判断 input_selection==2 或 3
uint8_t n_tc = (nm->input_selection == 2 || nm->input_selection == 3) ? 1 : 0;
// MM+ (input_selection=4) 不管 mm_type 是什么都返回 0
```

### 18.3 修复（`bs300_ram_sync.c`）

**修复 1：`get_input_type()` 增加 `mm_type` 参数**

```c
static uint8_t get_input_type(uint8_t input_selection, uint8_t mm_type)
{
    case 4:  // MM Plus
        if (mm_type == 0x00) return 1;  // Telecoil
        if (mm_type == 0x01) return 2;  // DAI
        return 0;
}
```

11 处调用全部更新为 `get_input_type(sel, mod->mm_type)`。

**修复 2：去掉 ETR/NRR 对 `snr_changed` 的依赖**

```c
// ETR: coded = 2524971008*(etr-100) / (1600*etr*ma)  → 只依赖 ma + etr
if (ma_changed || etr_changed)  // 原来: snr_changed || ma_changed || etr_changed

// NRR: coded = 2524970707*nrr / (16000*ma)  → 只依赖 ma + nrr
if (ma_changed || nrr_changed)  // 原来: snr_changed || ma_changed || nrr_changed
```

**修复 3：TC/DAI diff 条件改用 `get_input_type()`**

```c
// 修复前
uint8_t n_tc = (nm->input_selection == 2 || nm->input_selection == 3) ? 1 : 0;

// 修复后：MM+ 的 mm_type=0x00/0x01 也视为需要 TC/DAI
uint8_t n_tc = (get_input_type(nm->input_selection, nm->mm_type) != 0) ? 1 : 0;
```

### 18.4 影响范围

- MM+ 模式 (input_selection=4) 切换时 igd 正确识别，ig-dependent WDRC KP/ENR NT/ENR UNT/ISS/WNR 等命令会被发送
- `bs300_set_volume_async` / `bs300_set_eq_async` 在 MM+ 模式下的 bin_gain 编码也会正确应用 igd 补偿
- MM+ 的 `mm_type` 已在 Flash 解码阶段正确读取（`bs300_param_encode.c` decode MM Plus 分支）

## 19. WNR preset 编码 bug (2026-07-15)

### 19.1 现象

P1→P2 切换时 WNR band 命令（0x8011C2/0x8411C2/0x8021C2）数据与 P1 完全相同，但 P1(preset=1) 和 P2(preset=3) 的 `strength_preset` 不同，I2C 数据应随之变化。

### 19.2 根因

两个问题，都在 `bs300_param_encode.c`：

**问题 1：band 编码硬编码 ssp=0**

```c
// 修复前
ssp = 0;  /* chip uses SSP level 0 for band data offsets */
```

Python 交叉验证只覆盖了 P0/P1（都是 preset=1, ssp=0），没发现其他 preset 需要不同的 ssp level。正确映射：`ssp = preset - 1`（DSP preset 1~5 对应表列 0~4）。

**问题 2：WNR Setup word3 阈值错误**

```c
// 修复前
set_word(data, 3, (ssp >= 12) ? 0x000006 : 0x000003);
```

阈值 12 导致 preset 0~3 (ssp=0,1,3,6) 全映射到 0x000003。正确阈值应为 6，区分 Low suppression（ssp<6→3）和 High suppression（ssp≥6→6）。

### 19.3 修复（`bs300_param_encode.c`）

**修复 1：band 编码使用 preset-1 作为 ssp level**

```c
// 修复后
ssp = (mod->wnr_preset > 0) ? (mod->wnr_preset - 1) : 0;
```

| DSP preset | struct preset | ssp_level | 表列 |
|:--:|:--:|:--:|:--:|
| 1 (Off) | 0 | 0 | col 0 |
| 2 (Minimal) | 1 | 0 | col 0 |
| 3 (Low) | 2 | 1 | col 1 |
| 4 (Medium) | 3 | **2** | col 2 |
| 5 (High) | 4 | 3 | col 3 |

P0/P1 (preset=1, ssp_level=0) 与原来的硬编码 ssp=0 一致，不破坏已有验证。

**修复 2：WNR Setup word3 阈值改为 6**

```c
// 修复后
set_word(data, 3, (ssp >= 6) ? 0x000006 : 0x000003);
```

**修复 3：WNR diff 增加 `wnr_changed` 触发 band 命令**

`bs300_ram_sync.c` WNR ON→ON 分支，原先 band 命令只在 `igd_changed` 时发送，补充 `wnr_changed` 条件：

```c
if (wnr_changed || igd_changed) {
    SEND_IF_DIRTY(session, 0x8001C2, ...);  // Setup
    SEND_IF_DIRTY(session, 0x8011C2, ...);  // Band 0-15
    SEND_IF_DIRTY(session, 0x8411C2, ...);  // Band 16-31
    SEND_IF_DIRTY(session, 0x8021C2, ...);  // Single Mic
}
```

### 19.4 验证

- P0→P1 (preset=1, ssp=0)：band 数据不变，与 Python 交叉验证一致 ✓
- P1→P2 (preset=1→3, ssp=0→2)：band 数据变化，Setup word3 从 3→6 ✓
- 全量同步 P2：WNR 数据正确 ✓

## 20. 完整文件变更清单 (2026-07-15)

| 文件 | 变更内容 |
|------|---------|
| `include/ble_rempro_cmd.h` | 新增 9 个 CMD 宏 + 2 个 push 函数声明 + SYS_ID_DEVICE |
| `code/ble_rempro_cmd.c` | 新增 7 个 handler + fitting_commit + hdlc_push + s_fit_buf |
| `include/bs300_param_encode.h` | 新增 `bs300_struct_to_flash` + mix_ratio 改 int8 + mm_type 字段 |
| `code/bs300_param_encode.c` | 新增 bit_writer + encode_wdrc/enr_flash + struct_to_flash + MM Plus 解码 |
| `include/bs300_ram_sync.h` | 新增 set_volume_notone_async + set_prog_denoise + reset_user_params + print_settings |
| `code/bs300_ram_sync.c` | 新增 s_eq_*/s_denoise 数组 + 持久化 + 切程序恢复 EQ + save_settings 重构 |
| `include/bs300_storage.h` | Settings 接口扩展（新增 EQ/denoise 参数） |
| `code/bs300_storage.c` | Settings v1→v3 演进 + Watchdog 刷新 + EQ/denoise 读写 |
| `code/bs300_driver.c` | 强制重读 DSP + Settings 清空 + EQ/denoise 传递 |
| `include/app.h` | DEBUG_UART_ENABLE 开启 |
| `app.c` | 按键回调加 push + persist + 无提示音音量路径 |

## 21. Flash persist 机制演进 (2026-07-17 重构)

### 21.1 演进历史

| 阶段 | 日期 | 策略 | 问题 |
|------|------|------|------|
| v1 | 07-15 前 | 所有操作立即 `Flash_EraseSector` | 每次擦写 ~40ms + 关中断，频繁操作影响 BLE |
| v2 | 07-15 | 全部延迟到 BLE 断开 | 按键断电丢数据；且每次断开必擦写 |
| v3 | 07-17 | **按键立即 + App 延迟 + slot 追加写** | — |

### 21.2 v3 当前策略

| 触发源 | 写 Settings Flash 时机 | 机制 |
|------|:---:|------|
| 按键长按（切模式） | 立即 `bs300_settings_persist()` | slot 追加写，32 次才擦 1 次 |
| 按键短按（调音量） | 立即 | 同上 |
| App CMD=2（调音量） | BLE 断开 | 只更新内存数组，断开时统一落盘 |
| App CMD=10（调 EQ） | BLE 断开 | 同上 |
| App CMD=16（切程序） | BLE 断开 | 同上 |
| App CMD=9（降噪） | BLE 断开 | `bs300_set_prog_denoise()` 只写 `s_denoise[]` |
| BLE 断开 | 统一 `bs300_settings_persist()` | 一次性落盘所有内存状态 |

按键立即保存 + slot 追加写 = 既保证断电不丢，又避免每次擦除扇区。

### 21.3 最终 persist 调用点

- **按键路径**: `app.c` → `bs300_settings_persist()` → `bs300_settings_save()`
- **BLE 断开**: `ble_std.c GAPC_DisconnectInd()` → `bs300_settings_persist()`
- **RM 进入**: `app.c` → `bs300_persist_active_prog(saved)` → `bs300_settings_save()`（保存进入 RM 前的程序号）

### 21.4 移除的死代码

- `bs300_set_volume()` — 阻塞版本，无调用者
- `bs300_set_eq()` — 阻塞版本，无调用者
- `bs300_vol_commit()` — 无调用者
- `save_settings()` static 函数 — 去掉中间层，`bs300_settings_persist()` 直接调用 `bs300_settings_save()`

## 22. CMD=15/10 EQ/Denoise 协议修正 (2026-07-15)

### 22.1 问题

CMD=15 (GetCurrentScene) 和 CMD=10 (SetEqualizer) 的 EQ/Denoise 字段存在问题：

| 问题 | 详情 |
|------|------|
| Denoise 字段取错源 | offset 4 取 `wnr_preset` (0-4) 而非 `s_denoise[]` (0-4)，含义不同 |
| EQ Get/Set 不一致 | Get 返回 `[0,100]` 转换值，但 App 实际需要 dB 值 |
| EQ Set 多余转换 | 接收端做 `(val-50)/4` 转换，App 实际已发 dB 值 |

### 22.2 修复

**CMD=15 响应 12B**：

| 偏移 | 字段 | 修复前 | 修复后 |
|------|------|--------|--------|
| 4 | Denoise | `wnr_preset` | `bs300_get_prog_denoise(prog)` |
| 5-10 | EQ_Low/Mid/High | `50 + dB*4` [0,100] | 原始 step [-5,5] |

**CMD=10 接收**：去掉 `(val-50)/4` 转换，直接取 `(int8_t)data[2]`，clamp [-5,5]。

### 22.3 新增接口

- `bs300_get_prog_denoise(prog_idx)` — 与已有 `bs300_set_prog_denoise()` 配对

## 23. EQ 档位重构 (2026-07-15)

### 23.1 变更

| 项目 | 旧值 | 新值 |
|------|------|------|
| 存储范围 | [-12, 12] dB | [-5, 5] step |
| 每档增益 | 1 dB | 3 dB |
| 实际增益范围 | [-12, 12] dB | [-15, 15] dB |

### 23.2 受影响位置

| 文件 | 改动 |
|------|------|
| `bs300_param_encode.h` | struct 注释更新 |
| `bs300_param_encode.c` `get_eq_gain_for_band()` | 返回值 `×3`，step→dB |
| `bs300_ram_sync.c` `bs300_set_eq_async()` | 增加 `[-5,5]` 范围校验 |
| `ble_rempro_cmd.c` `cmd_setequalizer()` | clamp `[-12,12]` → `[-5,5]` |

### 23.3 编码链路

```
App 传 step [-5,5] → struct 存 step
  → get_eq_gain_for_band() ×3 → dB [-15,15]
  → baseline = bin_gain + vol_gain + eq_dB
  → 0x8060B2 编码 → clamp [-27,96]
```

## 24. bin_gain 范围限制 (2026-07-15)

### 24.1 问题

叠加 `bin_gain + vol_gain + eq_gain ± cal ± igd` 后可能超出芯片可接受范围，当前 clamp `[-128,127]` 过于宽松。

### 24.2 修复

手册 §Bin Gain (0x8060B2): 范围 **[-27, 96] dB**。将 clamp 改为 `[-27, 96]`：

```c
// bs300_param_encode.c encode_wdrc_bin_gain()
values[i] = clamp_s32(apply_igd_trunc(numer_tenth, -igd), -27, 96);
```

极端情况（vol=0 + eq=-5×3 + bin_gain=-27 = -69dB）会被 clamp 到 -27。

## 25. 切程序补 EQ 恢复 (2026-07-15)

### 25.1 问题

`bs300_switch_program_async()` 加载目标程序 struct 后只恢复了 `volume_level`，漏了 `eq_low/mid/high`：

```c
s_target.modules.volume_level = s_volumes[new_prog_idx];
// ← 缺 eq 三行
```

导致切程序时 0x8060B2 的 `vol_eq_changed` 检测依赖 Flash struct 中的旧 EQ 值（可能全零），用户当前设置丢失。

### 25.2 修复

补三行，与 `bs300_resync_diff_start()` 对齐：

```c
s_target.modules.volume_level = s_volumes[new_prog_idx];
s_target.modules.eq_low  = s_eq_low[new_prog_idx];
s_target.modules.eq_mid  = s_eq_mid[new_prog_idx];
s_target.modules.eq_high = s_eq_high[new_prog_idx];
```

## 26. Flash_EraseSector 中断保护 (2026-07-15)

### 26.1 问题

之前分析认为 Flash_EraseSector 重启是因为 BLE 连接态阻塞。但测试发现**无 BLE 连接时擦写 Flash 也会重启**。根因是 RSL10 单 Flash 宏，擦除期间整个 Flash 不可读。SysTick（Keil RTX 内核调度）每毫秒触发一次，ISR 在 Flash 中 → CPU 取指失败 → HardFault → 复位。

### 26.2 修复

`bs300_storage.c` 两处 Flash 操作加 `__disable_irq()` / `__enable_irq()` 包裹：

```c
Sys_Watchdog_Refresh();
__disable_irq();
Flash_EraseSector(base);
// ... Flash_WriteBuffer ...
__enable_irq();
```

### 26.3 受影响函数

- `bs300_settings_save()` — Settings Flash 擦写
- `bs300_storage_write_program()` — Program Flash 擦写

## 27. HDLC 分片重组修复 (2026-07-15)

### 27.1 问题 A: GATT 单缓冲丢数据

`rempro_env.role_value` 只有 20 字节单槽位。App 连续发多包 BLE Write（如 SetGain 4 包），GATT 回调之间 Main_Loop 来不及跑，后面的包覆盖前面的包。

### 27.2 修复 A: 直接追加

GATT 回调直接调 `rempro_reasm_append(param->value, param->length)` 追加到 `reasm_buf`，绕过 `role_value` 中转。

新增函数：
- `rempro_reasm_append()` — GATT 回调中调用，直接追加到重组缓冲
- `rempro_reasm_reset()` — 断连时清空缓冲

### 27.3 问题 B: 6 字节帧永远卡住

`while (reasm_len >= 7)` 和 `if (len < 7)` 用的阈值是 7，但最短 HDLC 帧（无负载的查询指令如 CMD=4/26）是 6 字节：`7E SY CMDL CMDH FCS 7E`。6 字节帧落在 buffer 末尾时被 while 跳过，永远不处理，App 等不到回复就重发。

### 27.4 修复 B: off-by-one

`>= 7` → `>= 6`，`< 7` → `< 6`。

### 27.5 问题 C: FCS 失败误消费

帧不完整（缺 FCS+尾 7E）时 `hdlc_parse_frame()` 不设 `*consumed`，调用方未初始化的 `consumed` 读到栈随机值，跳过 "waiting" 路径。

### 27.6 修复 C: 显式置零

- 调用方 `consumed = 0` 初始化
- `hdlc_parse_frame()` 入口 `*consumed = 0`
- FCS 校验失败时 `*consumed = 0`（等待更多数据到达，而非错误消费）

## 28. SetFeedbackOnOff / GetFeedbackOnOff 实现 (2026-07-17)

### 28.1 协议格式

**SetFeedbackOnOff (CMD=5) — App→设备：**

| 字段 | 长度 | 描述 |
|------|:---:|------|
| Device_Type | 1 | 0=左右, 1=左, 2=右 |
| Scene_ID | 1 | 程序号 0-3 |
| OnOff | 1 | 0=关闭, 1=开启 |

响应：`flag(1) + status(1)` = 2B

**GetFeedbackOnOff (CMD=34) — App→设备：**

| 字段 | 长度 | 描述 |
|------|:---:|------|
| Device_Type | 1 | 0=左右, 1=左, 2=右 |
| Scene_ID | 1 | 程序号 0-3 |

响应：`flag(1) + Left_OnOff(1) + Right_OnOff(1)` = 3B

### 28.2 架构

```
s_feedback_onoff[4]  (per-program RAM 变量，独立于程序 Flash)
    │
    ├─ 开机: 从 BS300 芯片读取时，取 dfbc_enable_mode & 0x0F 初始化
    │        mode bits 非零 → ON(1)，为零 → OFF(0)
    │
    ├─ Settings Flash: 持久化，[0,0,0,0] 视为"未设置"
    │        恢复时 fallback 到芯片默认值
    │
    ├─ boot_cache / load_struct: 将 s_feedback_onoff 应用到 dfbc_enable_mode
    │        ON:  dfbc_enable_mode = 0x80 | mode  (mode 默认 SlowStrong 0x07)
    │        OFF: dfbc_enable_mode = 0x00
    │
    └─ App SetFeedbackOnOff:
         ├─ 存 s_feedback_onoff[prog] = onoff
         ├─ 目标程序=活跃程序 → 立即发 I2C 0x800052
         └─ 目标程序≠活跃程序 → 仅存状态，切过来时 load_struct 自动生效
```

### 28.3 与降噪等级的对照

| | denoise | feedback_onoff |
|---|---|---|
| RAM 变量 | `s_denoise[4]` | `s_feedback_onoff[4]` |
| 作用域 | per-program | per-program |
| 覆盖 Flash | `enr.max_att_db` += offset | `dfbc_enable_mode` bit7 |
| 程序切换 | `load_struct` 自动应用 | `load_struct` 自动应用 |
| 芯片默认值 | Flash 原始值 | `dfbc_enable_mode & 0x0F ? 1 : 0` |
| 不写程序 Flash | 是 | 是 |

### 28.4 DFBC I2C 命令 (0x800052)

```
Byte 0-2:   dfbc_mode (uint24)  0=Disable, 0x0F=FastStrong, 0x07=SlowStrong
Byte 3-5:   delay_n_sample (uint24)  round(fbc_bulk_delay_us / 62.5)
Byte 6-47:  零填充
```

- **开启**: `bs300_encode_dfbc()` → mode bits + delay_n → 48B data → Advanced Write
- **关闭**: 全零 48B → 芯片收到 mode=0 = Disable

### 28.5 关键设计决策

| 决策 | 结论 | 原因 |
|------|------|------|
| RAM 独立于程序 Flash | `s_feedback_onoff[4]` 独立于 Flash `dfbc_enable_mode` | 用户开关不应受验配数据约束 |
| OFF 时清零整个字节 | `dfbc_enable_mode = 0x00`，非 `&= ~0x80` | encoder 取 `& 0x0F`，保留 mode bits 会导致芯片收到非零 mode（实际未关闭） |
| Settings 全零当"未设置" | 不覆盖芯片默认值 | 旧 slot 存 [0,0,0,0] 不应覆盖芯片有效配置 |
| 非活跃程序仅存状态 | 不发 I2C | 切过来时 `load_struct` 自动应用，避免冗余 I2C |
| 默认 mode 0x07 (SlowStrong) | Flash 无配置时 fallback | 中等强度，适用范围广 |

### 28.6 相关文件

| 文件 | 变更 |
|------|------|
| `include/ble_rempro_cmd.h` | 新增 `CMD_SETFEEDBACKONOFF (5)` + `CMD_GETFEEDBACKONOFF (34)` |
| `code/ble_rempro_cmd.c` | 新增 `cmd_setfeedbackonoff()` + `cmd_getfeedbackonoff()` + switch cases |
| `include/bs300_ram_sync.h` | 新增 `bs300_set/get_feedback_onoff()` + `bs300_init_feedback_from_flash()` + `restore_settings` 增加 `feedback_onoff` 参数 |
| `code/bs300_ram_sync.c` | 新增 `s_feedback_onoff[4]` + getter/setter + `boot_cache`/`load_struct` override + `reset_*` 清理 |
| `include/bs300_storage.h` | `save/load` 增加 `feedback_onoff` 参数 |
| `code/bs300_storage.c` | Settings slot 扩展 v3→v4: 新增 bytes 21-24, magic/CRC/ver 后移 |
| `code/bs300_driver.c` | Boot/refresh 序列: init_from_flash → settings restore → fallback |

## 29. SetDeviceOnOff / GetDeviceOnOff 实现 (2026-07-17)

### 29.1 协议格式

**SetDeviceOnOff (CMD=3) — App→设备：**

| 字段 | 长度 | 描述 |
|------|:---:|------|
| Device_Type | 1 | 0=左右, 1=左, 2=右 |
| OnOff | 1 | 0=关机(MUTE), 1=开机(ACTIVE) |

响应：`flag(1) + status(1)` = 2B

**GetDeviceOnOff (CMD=33) — App→设备：**

无请求数据。

响应：`flag(1) + Left_OnOff(1) + Right_OnOff(1)` = 3B

### 29.2 实现

- `s_device_on` 静态变量跟踪 MUTE/ACTIVE 状态，初始值 1（boot 后为 ACTIVE）
- **SetDeviceOnOff**: ON → `bs300_active()` (I2C 0x800010)，OFF → `bs300_mute()` (I2C 0x800000)，`bs300_sync_is_busy()` 时拒绝
- **GetDeviceOnOff**: 返回 `[s_device_on, s_device_on]`，反映当前 DSP 实际状态

### 29.3 相关文件

| 文件 | 变更 |
|------|------|
| `include/ble_rempro_cmd.h` | 新增 `CMD_GETDEVICEONOFF (33)` |
| `code/ble_rempro_cmd.c` | 新增 `s_device_on` + 重写 `cmd_setdeviceonoff()` + 新增 `cmd_getdeviceonoff()` + switch cases |
