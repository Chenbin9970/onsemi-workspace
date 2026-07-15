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

## 12. Flash 持久化架构 (2026-07-15)

### 12.1 两条存储路径

| 路径 | 存储目标 | 内容 | 写时机 |
|------|---------|------|--------|
| **Program Flash** (480B×4) | `0x0015D000~E800` | WDRC + ENR + Volume/Beep + DFBC + ISS + WNR + AGCO + Input | SetGain/MPO/CompressRatio/Denoise → `fitting_commit()` |
| **Settings Flash** (2KB) | `0x0015C800` | active_prog + volume[4] + eq_*[4] + denoise[4] | 切音量/EQ/降噪 → `save_settings()` |

### 12.2 struct → 480B flash 编码

`bs300_struct_to_flash()` 重编码 WDRC (0x12) 和 ENR (0x1C) 模块：

```
原始 480B buffer → 扫描 module directory → 找到 WDRC/ENR → 原地替换 bit-packed 数据
```

- `encode_wdrc_flash()`: bin_gain 32×7bit + 通道数据 (lmt_th: raw=value_in_MT-30, kp_th: raw=value_in_MT)
- `encode_enr_flash()`: nfsf/nhsf/nnsf/snasf 4bit + 16ch×(freq/max_att/snr_th/noise_th/upper_noise_th/etr/nrr)
- 其他模块保持原样不变

### 12.3 Settings 格式演进

| 版本 | 新增字段 | Offset |
|:---:|------|--------|
| v1 | active_prog + volume[4] | 0~4 |
| v2 | eq_low/mid/high[4] | 5~16 |
| v3 | denoise[4] | 17~20 |
| — | magic + CRC + version | 21~28 |

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
| 6 | `CMD_SETGAIN` | 设置增益 | dev(1)+prog(1)+(spectrum(1)+dB(1))* | flash load→改 bin_gain→`fitting_commit` |
| 7 | `CMD_SETMPO` | 设置 MPO | dev(1)+prog(1)+(ch(1)+mpo(1))* | flash load→改 lmt_th_db→`fitting_commit` |
| 8 | `CMD_SETCOMPRESSRATIO` | 设置压缩比 | dev(1)+prog(1)+turn(1)+(ch(1)+step(1))* | flash load→改 kp_r_idx→`fitting_commit` |
| 9 | `CMD_SETDENOISE` | 设置降噪 | dev(1)+prog(1)+level(1) | flash load→enr.max_att_db[16]→`fitting_commit`+Settings |
| 10 | `CMD_SETEQUALIZER` | 设置均衡器 | dev(1)+type(1)+val(1) | `bs300_set_eq_async` → Settings persist |
| 15 | `CMD_GETCURRENTSCENE` | 获取当前程序 | 无 | 读取 s_dsp_state → 12B 响应 |
| 16 | `CMD_SETCURRENTSCENE` | 切换程序 | dev(1)+scene(1) | `bs300_switch_program_async` |
| 26 | `CMD_GETDEVICECONFIG` | 获取设备信息 | 无 | 固件版本/MAC/产品型号等 → 29B 响应 |

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
| **运行时** | SetVolume, SetEqualizer, SetCurrentScene, GetCurrentScene | 只改 Settings Flash，不改 480B 程序 Flash |
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
