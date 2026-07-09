# BS300 I2C 通讯驱动 — 架构设计文档

> 创建日期: 2026-05-27 | 最后更新: 2026-06-05 (VM Flash 冲突重试 + TWS relay 延迟调度)

---

## 1. 项目背景

为 AC897N/AD697N TWS 蓝牙耳机 SDK 构建 BS300 助听器芯片的 I2C 通讯驱动，
实现验配流程：从 BS300 Flash 读取参数 → 本地存储 → 修改参数 → 写回 BS300。

## 2. 数据流

```
┌──────────┐    ┌──────────────┐    ┌─────────────┐    ┌───────────┐
│  BLE App  │───▶│ bs300_param  │───▶│ bs300_frame  │───▶│ iic_soft  │──▶ BS300
│  (TBD)    │    │ 参数读写接口   │    │ 帧组包/解包   │    │ I2C 收发   │    Chip
└──────────┘    └──────┬───────┘    └─────────────┘    └───────────┘
                       │
                  ┌────▼─────┐
                  │  bs300_vm │
                  │  VM 存取   │
                  └──────────┘
```

## 3. 核心流程

### 3.1 上电初始化 (ACTION_EARPHONE_MAIN 最顶部)

> 2026-06-02: BS300 初始化移到 `ACTION_EARPHONE_MAIN` 最顶部，DC-DC 后延时 1s 等 DSP 稳定。

```
echo1603_hardware_init()                ← DC-DC 使能 (PB3) + Hall 合盖检测
delay(1s) + clr_wdt()                   ← 等 DSP 供电稳定

/* === BS300 init === */
┌─ Step 1: VM 判断
│   if (VM 有效: ap < 4, channels 1-16)
│     → printf("VM ok"), 加载 struct, 跳过 Flash 读
│   else
│     → printf("VM empty")
│       bs300_startup(0, 0x012958)      ← 解锁 + 验证通信
│       bs300_init(0, 0..3)             ← 读 Flash 4 个 Program → 存 VM
│       重新加载 VM prog struct
│
├─ Step 2: sync RAM
│     bs300_mute(0)                     ← delay=200 慢速
│     bs300_key_lock(0)
│     bs300_verify_comm(0, 0x012958)
│     bs300_sync_program(0, &prog)      ← 编码全部 31 条 Param I2C
│     bs300_unlock(0)
│     bs300_active(0)                   ← 启动 DSP
│     soft_iic_set_delay(0, 250)        ← 降速 5x
│
└─ bs300_cache_prog_inputs()            ← 缓存全部 4 个 prog 的 input + modules + calib

/* 之后才启动 BT 协议栈、按键等 */
clk_set("sys", BT_NORMAL_HZ); ...
```

**关键点**：
- BS300 在蓝牙协议栈之前初始化，确保 DSP 先就绪
- DC-DC 后延时 1s，DSP 供电稳定后再 I2C 通信
- **不再有** DCDC 硬复位（旧版本拉低 PB3 → 1s → 拉高），Flash 读后直接重载 VM
- init 阶段只加载不 sync，4 个程序全部就绪后才一次性 sync active program
- sync 完成后发 ACTIVE 启动 DSP 音频处理
- ACTIVE 之后 I2C 降为 delay=250（5x），芯片 DSP 运行时不能处理高速 I2C
- 启动末尾 `bs300_cache_prog_inputs()` 建立输入方式缓存，后续切换零 VM 读取

### 3.2 运行时切程序（按键触发，非阻塞）

```
KEY_MODE_SWITCH
  → bs300_switch_program_async(new_prog_idx)
    ├── 对比 old/new struct（复用 switch_diff_* 函数）
    ├── 填充命令队列到 session（只填变化的命令）
    ├── 注册 sys_timeout_add(60ms) 回调
    └── 立即返回（不阻塞）

bs300_sync_tick_cb()                     ← 系统定时器回调
  └── bs300_sync_tick()
        ├── SEND: 发一条 I2C 命令 → 转 POLL
        ├── POLL: 等 60ms → poll FURPROC
        │   ├── 就绪 → 下一包 → SEND
        │   ├── 超时 → 重试（最多 30 次）
        │   └── 未到 60ms → 计算剩余时间，精确重调度
        └── DONE/ERROR → 停止回调
```

**对比阻塞版本**：
| | 阻塞 `bs300_switch_program()` | 非阻塞 `_async()` |
|---|---|---|
| I2C 发送 | 一次性连续发完 | 逐条，每次回调发一包 |
| 定时 | `BS300_MS(60)` 忙等 | `sys_timeout_add` + 剩余时间补偿 |
| 系统影响 | 阻塞 BLE/音频/按键 | 命令间系统正常处理其他任务 |
| 适用场景 | 开机 init | 运行时切程序 |

### 3.3 运行时改参数（App 触发）

```
bs300_param_modify(prog_idx, offset, val, len)       ← 阻塞版本
bs300_param_modify_async(prog_idx, offset, val, len)  ← 非阻塞版本
  ├── VM 加载 old_struct（快照）
  ├── 修改 bytes → 存 VM
  ├── VM 加载 new_struct
  ├── switch_diff_* 对比 → 只发变化的命令
  └── (async) sys_timeout_add 回调逐条发送
```

### 3.4 语音提示输入切换（零 VM 读取）

> 2026-06-02: 重构为使用启动缓存，切换过程中零 VM 读取。新增 DDM2/MM+ 关闭/恢复逻辑。

播提示音时 BS300 需要切到 Telecoil（直通通道，DSP 不处理）。硬件要求切输入必须 mute→写→active 才生效，否则爆音。

**数据缓存**（`bs300_driver.c`，开机时一次性填充）：
```
s_prog_input[4]       ← 每个 prog 的 input_selection
s_prog_modules[4]     ← 每个 prog 的完整 modules struct
s_calib_cache         ← 校准数据（所有 prog 共享）
s_active_prog         ← 当前活跃程序号
s_boot_cached         ← 标志位，switch/restore 检查
```
缓存更新时机：开机 `bs300_cache_prog_inputs()` + 切程序 `bs300_on_active_prog_changed()`。

**切换流程**：
```
bs300_voice_prompt_input_switch(0, 2)    ← 切到 Telecoil
  ├── [缓存] 读 original_input = s_prog_input[s_active_prog]
  ├── [缓存] 用 s_prog_modules + s_calib_cache 编码 Vol/Beep
  ├── bs300_mute(0)                      ← delay=200
  ├── if original=DDM2(5) → WR 0x800022 全零 (关 DDM2)
  ├── if original=MM+(4)  → WR 0x800062 全零 (关 MM+)
  ├── WR 0x800081 (Vol/Beep, input=Telecoil)
  ├── bs300_active(0)                    ← fire-and-forget
  └── soft_iic_set_delay(250)

bs300_voice_prompt_input_restore(0, original_input)  ← 恢复原输入
  ├── [缓存] 用 s_prog_modules + s_calib_cache 编码
  ├── bs300_mute(0)
  ├── if original=DDM2 → bs300_encode_ddm2 → WR 0x800022 (开 DDM2)
  ├── if original=MM+  → bs300_encode_mm_plus → WR 0x800062 (开 MM+)
  ├── WR 0x800081 (Vol/Beep, input=原值)
  └── bs300_active(0)
```
**关键**：DDM2/MM+ 先关后开的顺序必须遵循 HW 要求（DDM2 → MM+ → Vol/Beep）。

**上层调用**：
```
bs300_prompt_tone_play(tone_idx, on_done)   ← key_event_deal.c 封装
  ├── bs300_voice_prompt_input_switch(0, 2)
  ├── tone_play_index_no_tws(tone_idx, 1)
  ├── 提示音结束 → 等 500ms
  └── [无 on_done 音量调节]:
  │     bs300_mute(0) → _restore() 恢复原输入
      [有 on_done 模式切换]:
        bs300_mute(0) → switch_program_async(target)
```

触发场景:
  音量档位 0-5   → bs300_prompt_tone_play(NUM_0 + level, NULL)
  模式切换 0-2   → bs300_prompt_tone_play(NUM_0 + prog + 6, done_cb)

**提示音映射**：

| 操作 | 提示音 ID |
|------|:--:|
| 音量 0-5 | `IDEX_TONE_NUM_0` ~ `NUM_5` |
| 程序 0 | `NUM_6` |
| 程序 1 | `NUM_7` |
| 程序 2 | `NUM_8` |

**约束**：struct 内部用自有编码（0=FrontMic, 1=RearMic, 2=Telecoil, 3=DAI, 4=MM+, 5=DDM2, 6=DualMic）；`bs300_encode_volume_beep` 内翻译为协议值（0=FrontMic, 1=Telecoil, 2=DAI, 3=RearMic, 4=DDM2, 5=MM+Tel, 6=MM+DAI）。

### 3.5 程序切换规则

- 模式切换（按键）只在 0/1/2 之间循环，`prog 3` 预留给音乐/通话（Telecoil 输入）
- 程序 0 Flash 存储时 input 为 Telecoil（上电静音），首次加载时自动覆盖为 FrontMic

### 3.6 模式切换 I2C 最小化

> 2026-06-02: 不再切到 prog 3 中转，改用 `bs300_voice_prompt_input_switch` 直接切输入到 Telecoil。
> 原因：`bs300_switch_program_async(3)` 会发 prog 0→3 的全量 diff（~28 条 I2C），而只需要换输入这一个操作（3-4 条）。

```
KEY_MODE_SWITCH:
  ├─ bs300_prompt_tone_play(tone_idx, bs300_mode_switch_after_tone)
  │     输入切 Telecoil (3-4 条 I2C) → 播提示音 → 等 500ms → 回调
  │
  └─ bs300_mode_switch_after_tone():
        bs300_mute(0)                           ← 1 条
        bs300_switch_program_async(target)      ← diff (仅变化) + 强制 Vol/Beep + 强制 DDM2/MM+
          异步逐条发 → active 收尾
```

**强制发送**（`bs300_switch_program_start` 末尾，diff 之后追加）：

| 命令 | 条件 | 原因 |
|------|------|------|
| 0x800022 (DDM2) | `new_prog.input == 5` | `voice_prompt_input_switch` 关了 DDM2 但没写 VM，diff 会误判 old=new 跳过 |
| 0x800062 (MM+) | `new_prog.input == 4` | 同上 |
| 0x800081 (Vol/Beep) | 强制 | 输入方式必须保证刷到 |

**I2C 指令数对比**：

| | 旧方案 (prog3 中转) | 新方案 (input switch) |
|---|---|---|
| 切 Telecoil | ~28 条 (全量 diff 0→3) | 3-4 条 |
| 切目标 prog | ~28 条 (全量 diff 3→1) | 0-28 条 (仅变化 + 3 条强制) |
| **总计** | **~56 条** | **4-31 条** |

### 3.7 音频模式切换（通话/音乐 → prog 3）

来电接通或 A2DP 音乐播放时自动切到程序 3，结束后恢复原程序。通话和音乐重叠时只切一次，两者都结束才恢复。

```
bs300_enter_audio_mode(mask)        ← BT_STATUS_PHONE_ACTIVE / A2DP_MEDIA_START
  ├── 首次进入: 保存原程序号
  ├── orig != 3: bs300_mute(0) → bs300_switch_program_async(3) → ... → bs300_active(0)
  ├── 通话 (mask & 1): I2C delay 250→1250（系统时钟降低）
  └── s_bs300_audio_mask |= mask

bs300_leave_audio_mode(mask)        ← BT_STATUS_PHONE_HANGUP / A2DP_MEDIA_STOP
  ├── 通话结束 (mask & 1): I2C delay 1250→250
  ├── s_bs300_audio_mask &= ~mask
  └── mask == 0 时: bs300_mute(0) → bs300_switch_program_async(原程序) → ... → bs300_active(0)
```

**I2C 速率分级**（更新）：

- 模式切换（按键）只在 0/1/2 之间循环，`prog 3` 预留给音乐播放
- 程序 0 Flash 存储时 input 为 Telecoil（上电静音），首次加载时自动覆盖为 FrontMic

```
bs300_init(prog_idx):
  Flash read → decode → prog 0 && input==1 → override to FrontMic → save VM
```

### 3.8 按键关机（三击 → BS300 提示音 → 硬件断电）

> 2026-06-10: 从局部 flag 死代码改造为 BS300 路径直接关机。

**触发**: KEY_0/KEY_1 三击 → key_table 映射 `KEY_EVENT_TRIPLE_CLICK` → `KEY_POWEROFF`

**流程**:
```
三击 → KEY_POWEROFF case (key_event_deal.c)
  ├── 通话中? → 挂断, break (不关机)
  ├── 来自 TWS? → break (不单独关机)
  └── 正常:
        bs300_prompt_sequence(IDEX_TONE_POWER_OFF, on_poweroff_tone_done)
          ├── bs300_voice_prompt_input_switch(0, Telecoil)
          │     mute → 关 DDM2/MM+ → 写 Vol/Beep(Telecoil) → active
          ├── tone_play_index_no_tws(IDEX_TONE_POWER_OFF, 抢断=1)
          ├── 等播完 (poll tone_get_status)
          ├── 等 500ms
          └── on_poweroff_tone_done()
                ├── idle_skip_poweroff_tone()  ← 设标志，防 idle 重复播音
                └── sys_enter_soft_poweroff(NULL)
                      ├── 禁用按键, 退出 sniff
                      ├── 断蓝牙, 关 TWS
                      ├── wait_exit_btstack_flag → 等 BT 音频停
                      └── task_switch("idle", ACTION_IDLE_MAIN)
                            └── idle: 检查 s_skip_poweroff_tone
                                  ├── 已设 → app_idle_enter_softoff() → power_set_soft_poweroff()
                                  └── 未设 → tone_play_index(常规路径播音) → 播完断电
```

**防重复播音**: `idle.c` 内部 static 标志 `s_skip_poweroff_tone`，通过 `idle_skip_poweroff_tone()` 设值。BS300 路径播完提示音后设标志，idle 检测到后跳过第二次播放直接断电。

**涉及文件**:
| 文件 | 改动 |
|------|------|
| `key_event_deal.c` | `on_poweroff_tone_done()` 回调 + `idle_skip_poweroff_tone()` 调用 |
| `idle.c` | `s_skip_poweroff_tone` 标志 + `idle_skip_poweroff_tone()` setter |
| `board_ad697n_demo.c` | key_table: TRIPLE_CLICK → KEY_POWEROFF（已有，未改） |

## 4. 文件结构

| 文件 | 职责 |
|------|------|
| `bs300_driver.h` | API 声明 + 数据结构 + 同步会话定义 |
| `bs300_driver.c` | I2C 帧收发、startup/init、非阻塞状态机、异步 API |
| `bs300_param.h` | 结构化数据定义 (WDRC/ENR/Modules/Calib) + encode 函数声明 |
| `bs300_param.c` | Flash→Struct 解码、Param encode 函数、switch_diff_* 增量对比、sync/switch/modify |
| `bs300_vm.c` | VM 分段存取、校准存取、active_prog 读写 |
| `bs300_encode_tables.h` | encode 查表数据（mic2_cal table、wnr_ssp_offset 等） |

## 5. I2C 底层

- **使用**: `iic_soft.c` (软件 GPIO 模拟 I2C)，不碰硬件 I2C
- **从机地址**: `0x02` (写) / `0x03` (读)
- **帧格式**:
  - Simple Command: `{Len(0x00), Cmd_L, Cmd_M, Cmd_H, Chk}` → 5 字节(不含地址)
  - Advanced Write: `{Len(0x10), Cmd_L, Cmd_M, Cmd_H, Data[48], Chk}` → 53 字节
  - Read Request (无数据): `{0x80, Chk}` → 2 字节
  - Read Request (有数据): `{0x90, Chk}` → 2 字节
- **Checksum**: `0xFF - (各字节和的低 8 位)`
  - 发送帧: 不含 Slave Addr，从 Length Section 到 Data Section
  - 接收帧 (Read Response): 从 Command Section 到 Data Section
  - **2026-06-02**: `bs300_poll_ready()` 和 `bs300_read_packet()` 新增响应校验和验证，无效帧返回 -1 触发重试
- **时序**: 每条命令后等 60ms，轮询 bit23 (FURPROC)=0 表示就绪
- **速度策略**:
  | 阶段 | I2C delay | 说明 |
  |------|-----------|------|
  | Mute 命令 | 200 (4x 慢速) | 首次通信，确保稳定。`bs300_mute()` 内部 set_delay(200) → send → reset_delay() |
  | 初始化/切程序 | 50 (正常) | DSP 已 Mute，可高速 |
  | ACTIVE 之后 | 250 (5x 慢速) | DSP 运行时不能处理高速 IIC。**注意**: `bs300_mute()` 内 reset_delay() 会清除此设置，每次 active 后必须重新 `set_delay(250)` |
  | 通话中 | 1250 (25x 慢速) | 系统时钟降低，IIC 必须降速 |

- **Sync 模式切换** (`bs300_param.h:BS300_SYNC_USE_DYNAMIC`):
  - `1` (默认): 动态编码 — `bs300_sync_program_dynamic()` 调用 31 个 C encode 函数从 struct 生成 I2C 数据
  - `0`: 硬编码 — `bs300_sync_program_hardcoded()` 使用原始 I2C trace 的固定 48B 数组
  - 切换只需改宏，两个函数共享同一套 calib 加载逻辑

## 6. 结构化数据模型

> 详细字段布局见 `BS300_PARAM_SYNC_DESIGN.md`

### 6.1 总体布局（per program，共 490B）

```
bs300_prog_struct_t:
  wdrc (292B) + enr (134B) + modules (64B) = 490B
```

VM 存储的是 `value_in_MT` 原始值（验配软件中的参数值），不是 Param I2C 编码后的 48B 帧。Param encode 时再经过校准公式转换。

### 6.2 WDRC（292B）

| 字段 | 类型 | 数量 | 说明 |
|------|------|:--:|------|
| total_channels / nsbc / kp_mode / limiter | uint8 | 4 | Header |
| freq_idx | uint8 | 16 | 频率表索引 |
| kp1_th_db / kp2_th_db | int8 | 16×2 | value_in_MT |
| epd/kp1/kp2 at_idx/rt_idx/r_idx | uint8 | 16×6 | Table 索引 |
| lmt_th_db | int8 | 16 | value_in_MT |
| lmt_at/rt/r_idx | uint8 | 16×3 | Table 索引 |
| bin_gain | int8 | 32 | value_in_MT |

### 6.3 ENR（134B）

| 字段 | 类型 | 数量 | 说明 |
|------|------|:--:|------|
| enable_num_ch | uint8 | 1 | bit7=enable, bit[3:0]=ch_count |
| nfsf/nhsf/nnsf/snasf | uint8 | 4 | [1,16] |
| freq_idx | uint8 | 16 | |
| snr_th_db / max_att_db | uint8 | 16×2 | [4,30] / [0,30] |
| noise_th_db / upper_noise_th_db | uint8 | 16×2 | dB SPL |
| etr_x100 / nrr_x10 / sasf | uint8 | 16×3 | |

### 6.4 Modules（64B）

| 偏移 | 模块 | 关键字段 |
|------|------|---------|
| 0-7 | Volume/Beep | enable, beep_level/freq, min/max_vol, input_selection, batt_beep |
| 8 | DFBC | enable+mode |
| 9-10 | ISS | enable, threshold |
| 11-12 | WNR | enable+dual_mic, preset |
| 13-18 | AGCO | enable, threshold_db, attack/release_01ms |
| 19-20 | MM Plus | enable, mix_ratio |
| 21-24 | DDM2 | enable, open_ear, polar_pattern, adm_fdm |
| 25-28 | Runtime | volume_level (0-5), eq_low/mid/high [-12,12] dB |

### 6.5 校准数据（144B raw → struct）

| 字段 | 说明 |
|------|------|
| mic1_band[32] | output_cal - gain_cal, band 0 无效 |
| output_band[32] | output_cal |
| mic2_gain_diff | 0.1 dB LSB |
| mic_delay | 0.1 us LSB |
| telecoil_gain_diff / dai_gain_diff | 0.1 dB LSB |
| fbc_bulk_delay | 1 us LSB |

## 7. 依赖拓扑与增量对比

### 7.1 差异检测架构

`switch_program` 和 `param_modify` 共用 4 个 `switch_diff_*` static 函数。每个函数实现：

```
OFF→OFF   → 跳过
OFF→ON    → 无条件发全部命令（newly enabled）
ON→OFF    → 发 1 条 disable 帧（word[0]=0）
ON→ON     → 逐字段 memcmp，只发变化的 + 依赖链上的
```

### 7.2 igd 依赖链

`input_selection` 变化触发 `igd_changed`，影响最多 11 条命令：

```
Volume/Beep (input_selection)
  → input_type → igd = cal_tc_gd/10 或 cal_dai_gd/10 或 0
    → WDRC KP Threshold  (-igd)
    → WDRC Bin Gain      (+igd)
    → ENR NT              (-igd)
    → ENR UNT             (-igd)
    → ISS                 (+igd)
    → WNR Band 0-15       (+igd)
    → WNR Band 16-31      (+igd)
    → WNR Single Mic      (+igd)
    → MM Plus             (-igd)
    → TC/DAI              (直接使用 calib)
```

### 7.3 同模块内依赖

| 源头字段 | 触发命令 | 原因 |
|---------|---------|------|
| `kp_mode` | Attack/Release/Ratio/KP Thr（5 条） | data layout 变化 |
| `freq_idx` | Freq Spacing + KP Thr + Lmt Thr（3 条） | 校准 band 映射变化 |
| `snr_th_db` | Max Att → ETR + NRR（3 条） | Max Att 公式含 snr_th |
| `enr freq_idx` | Freq Spacing + NT + UNT（3 条） | mic1Cal 范围变化 |

### 7.4 Flash → Struct 转换

仅首次上电（VM 空时）使用。BS300 Flash 480B bit-packed → `bs300_flash_to_struct()` → 结构化值 → 存 VM。

参照 `bs300_codegen.py` 中已交叉验证的 decode 函数：
- `decode_wdrc_flash()` — bit-packed header + per-channel 展开
- `decode_enr_flash()` — enable + per-channel 数组
- `decode_vol_beep_flash()` — 标量字段
- DFBC/ISS/WNR/AGCO/MM+/DDM2 各模块独立 decode

## 8. 协议层

### Program Burn
- **读写统一**: 10 包 (packet 0–9), 10×48=480 字节
- **读**: Read Start(Y) → 逐包读 → Read cmd `0x800011+0x1000*pkt`
- **写**: 逐包写 → Burn End(Y), Write cmd `0x800001+0x1000*pkt`
- **Y 移位**: `Y << 12` (非 `Y << 16`)

### Param I2C
- **写**: Advanced Write 命令 (53 字节帧)
- **命令范围**: 各模块不同 (WDRC: `0x8000B2`, DFBC: `0x800052`, ENR: `0x8000C2`, ...)
- **分包**: PKTNUM 在 bit15:12，多包间 `cmd += 0x1000`

### Sync All
- 解析 buffer 头: byte[0]=pkt_cnt, byte[1-3]=0x80/0x00/N+1
- Module 命令从 byte[4] 开始，每个 3 字节 `{cmd_data, 0x00, data_length_words}`
- 数据从 byte[6+3N] 开始
- 逐个模块通过 Param I2C 写入 BS300 RAM

### Input Tone Generator (0x8001E2)

信号发生器命令，用于产生指定频率和电平的纯音信号。

**命令**: `0x8001E2` (Advanced Write，单包 48B)

**数据段布局**:

| Word | Byte | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 0-2 | itg_level | frac24 | `0x7FFFFF * 10^((level_db - mic1_band) / 20)`，清除时 = 0 |
| 1 | 3-5 | itg_frequency | uint24 | 频率索引 = `freq_hz / 250`，范围 [1, 31] (250-7750Hz) |
| 2 | 6-8 | itg_selection | uint24 | `0x000001` = 生成，`0x000000` = 清除 |
| 3-15 | 9-47 | — | — | 零填充 |

**频率索引表** (250Hz 步进):

| 频率 (Hz) | 索引 | 频率 (Hz) | 索引 |
|-----------|------|-----------|------|
| 250 | 1 | 4250 | 17 |
| 500 | 2 | 4500 | 18 |
| 1000 | 4 | 5000 | 20 |
| 2000 | 8 | 6000 | 24 |
| 3000 | 12 | 7000 | 28 |
| 4000 | 16 | 7750 | 31 |

完整表见 `BS300 Protocol Handbook v3.md` §信号发生器。

**itg_level 计算**:

```
itg_level_frac24 = 0x7FFFFF * 10^((level_db - mic1_band) / 20)
```

复用 `bs300_beep_frac24_table[]`（公式相同），查表索引 = `(level_db - mic1_band) + BS300_BEEP_TABLE_OFFSET(255)`。

**操作流程**:
1. `bs300_itg_write(iic, level_db, freq_hz, &calib)` → 写入 0x8001E2
2. `bs300_active(iic)` → 启动 DSP 输出
3. 等待所需持续时间
4. `bs300_mute(iic)` → 停止 DSP
5. `bs300_itg_clear(iic)` → 写入全零清除

**实现文件**: `bs300_param.c:1744-1798` — `bs300_encode_itg()` / `bs300_itg_write()` / `bs300_itg_clear()`

**纯音测听集成**: CMD 40 (Fitting_Status=0) 先调用 `bs300_audiometry_enter()` 设置 DSP 环境（关非 WDRC 模块 + 写测试 WDRC），之后 App 用 CMD 13/14 控制 ITG 纯音。CMD 40 (Fitting_Status=1) 调用 `bs300_audiometry_exit()` 全量恢复。详见 `bs300_param.c:1800-1908`。

## 9. 存储方案

### VM ID 分配
| VM ID | 内容 | 大小 |
|-------|------|------|
| 20-27 | Program 结构化数据，每 program 2 ID（WDRC + ENR+Modules） | 490B / program |
| 28 | 校准数据 raw | 144B (3 pkt × 48B) |
| 29 | 校准校验和 | 4B |
| 30 | Program 累积校验和 | 4B |
| 31 | 当前激活 Program 号 (0-3) + struct 版本号 | 2B |

### 结构化存储（非 bit-packed）
- VM 存储的是 `bs300_prog_struct_t`（WDRC 292B + ENR 134B + Modules 64B = 490B）
- 首次开机从 BS300 Flash 读取 bit-packed 数据 → `bs300_flash_to_struct()` 解码 → 存 VM
- 后续开机直接从 VM 加载 struct，跳过 Flash 读取
- 版本升级通过 `BS300_STRUCT_VERSION` 触发重新读取
- 校验和: `BS300_CALIB_CHECKSUM = 133805`（p0+p1+p2 480B 字节和）

### 内存策略
- `bs300_init`: malloc 临时 672B + struct，用完释放
- `bs300_sync_session_t`: 静态分配，64 条命令 × 48B ≈ 3KB 栈空间
- 无全局常驻大 buffer

## 10. 设计决策记录

| 决策 | 结论 | 原因 |
|------|------|------|
| I2C 实现 | `iic_soft.c` | 白名单文件 |
| 本地存储 | VM 结构化存储 | 避免每次开机重复读 Flash |
| VM 分段 | 2 ID × 240B / program | 单 VM ID 容量受限 |
| 存储格式 | `bs300_prog_struct_t` 结构化 | 比 bit-packed 更快，VM 内可直接对比 |
| 无静态 buffer | malloc 临时，用完释放 | 节省 RAM |
| **输入切换零 VM 读** | 缓存 prog modules + calib | 提示音播放路径延迟最小化 |
| **DDM2/MM+ 强制发送** | switch 末尾 force-append | prompt 关了 DDM2/MM+ 但没写 VM，diff 会误判 |
| **Vol/Beep 强制发送** | switch 末尾 force-append | 输入方式必须保证正确 |
| **响应校验和验证** | poll_ready + read_packet 检查 | `00 00 00 00` 无效帧被 FURPROC=0 误判成功 |
| **Sync 动态/硬编码切换** | 宏 `BS300_SYNC_USE_DYNAMIC` | 动态编码便于调试，硬编码作为 fallback |
| **BS300 在最顶部初始化** | DC-DC → 延时 1s → BS300 → BT | DSP 先就绪，再启蓝牙 |
| **模式切换不用 prog3 中转** | 直接 input switch 换 Telecoil | 省 ~28 条 I2C 指令 |
| 上电初始化 | startup → init×4 → sync → ACTIVE | 先全部加载，最后统一同步 |
| 校准独立存储 | VM ID 28 | 全局一份，不按 Program 分 |
| 切程序增量对比 | switch_diff_* 函数，4 分支状态机 | 只发变化的命令，减少 I2C 流量 |
| 非阻塞切程序 | `sys_timeout_add` 回调 + 状态机 | 不阻塞 BLE/音频/按键 |
| I2C 速率 | 全局 500，mute→active 区间 250 | `bs300_mute` 内 reset→250，caller 恢复 500；`bs300_switch_done`/v_prompt/trial 末尾设 500 |
| WNR enable bit | `\|= 0x01` 强制使能 | 协议 `dual_mic_mode_sel` 被重新解释为总开关 |
| init 不写 BS300 RAM | 延迟到全部加载后统一 sync | 避免 prog 0 先 sync 阻塞后续加载 |
| Prog 3 预留给音乐 | 模式切换只循环 0/1/2 | 音乐播放时切 prog 3，不参与按键循环 |
| Prog 0 Flash 覆盖 | Telecoil → FrontMic | Flash 存 Telecoil(1) 上电静音；实际工作是 Mic(0)。协议 input_selection: 1=Telecoil, 2=DAI |
| 提示音输入切换 | mute→写→active→set_delay(250) | 防止爆音；active fire-and-forget (DSP 启动后轮询可能失败)；mute 内 reset_delay 需重新降速 |
| 提示音接口 | `tone_play_index_no_tws` 抢断=1 | 本地播放不通过 TWS；抢断模式确保新提示音打断旧的 |
| ACTIVE 不轮询 | `bs300_send_simple_cmd` 不发 wait_ready | DSP 启动后可能不响应轮询，fire-and-forget |
| 通话/音乐切程序 3 | phone_active / a2dp_start | bitmask 防重叠，两者都结束才切回 |
| 通话 I2C 不降速 | 全局统一 250 | (2026-06-04) 不再区分通话/正常速率 |
| DSP 音量独立 | 0-9 档，3dB/step，改 bin_gain | 与系统音量完全独立，按键只改 DSP 音量 |
| 异步冲突重试 | busy→sys_timeout_add(50ms) | 单命令/试听 diff sync 忙时自动重试 |
| VM Flash 冲突重试 | `bs300_vm_load/save_seg` 内 5 次重试+clr_wdt | TWS BTIF 与 BS300 VM 共用 Flash，冲突时等 1ms 重试 |
| 程序切换喂狗 | `bs300_switch_program_start` + `resync_diff_start` | VM 读之前 clr_wdt，防止大量 Flash 操作触发 WDT |
| 提示音映射 | 音量 NUM_0-5, 程序 NUM_6-8 | 调试用，后续可能调整 |
| ITG itg_level 查表复用 | `bs300_beep_frac24_table[]` | `itg_level = 0x7FFFFF * 10^((level_db - mic1_band) / 20)` 与 beep 公式完全一致，无需新增表 |
| I2C 命令统一顺序 | sync 和 switch 完全一致 | switch_diff_modules 拆为 pre/post ENR 两段，ENR 夹中间 |
| struct→proto 翻译 | `bs300_encode_volume_beep` 内查表翻译 | struct 编码 (2=Telecoil) ≠ 协议编码 (1=Telecoil)，I2C 线路上用协议值 |
| ENR 0x8090C2 移除 | sync/switch 都去掉 SASF | HW 不需要此命令 |
| 提示音流程重构 | mute→tele→active→音→500ms→mute→action→active | 500ms 间隔后 mute 再操作，切模式时 skip restore |
| 异步逐条喂狗 | `clr_wdt()` after SEND & POLL | 防止大量 I2C 命令喂狗超时 |
| 下载擦 VM | download.bat `-format vm` | 每次烧录清空 VM，避免旧 struct 编码不兼容 |
| 音频模式切程序 mute | enter/leave 都先 bs300_mute(0) | mute→I2C→active 闭环，与按键切程序一致；async 状态机末尾自带 active |
| **按键关机 BS300 路径** | `bs300_prompt_sequence` 播 IDEX_TONE_POWER_OFF → idle_skip_poweroff_tone 防重播 | 与音量/模式切换共用同一条 BS300 提示音链路；标志用函数调用传递，不碰 app_main.h 避免预编译库偏移错位 |
| **app_main.h 不可改** | 禁止在 APP_VAR 中间插入字段 | 预编译 .a 库按旧偏移访问，插入字段会导致全 struct 偏移错位 |
| **纯音测听 WDRC 参数** | 复用 b300_TESTWDRC_SET 参考值，5 条 raw I2C | 测试用 WDRC 参数固定，不依赖当前程序的 WDRC 配置 |
| **纯音测听退出恢复** | 全量 sync（bs300_sync_program），不发 diff | 进入时直接写 raw I2C，VM 未跟踪改动，diff 为空；必须全量恢复 |
| **测听/试听按键拦截** | `key_event_deal.c` 三个入口加 `g_audiometry_state != 0` 拦截 | 防止按键切模式/调音量打断测听/试听流程 |

## 11. API 参考

### 阻塞 API（适合开机一次性操作）
```c
// 初始化
int bs300_mute(soft_iic_dev iic);                                  // 停止 DSP (delay=200)
int bs300_startup(soft_iic_dev iic, u32 security_code);           // 一次性启动 (mute+key_lock+verify)
int bs300_active(soft_iic_dev iic);                               // 启动 DSP (fire-and-forget)
int bs300_init(soft_iic_dev iic, u8 prog_idx);                    // Flash 读→struct→存 VM
int bs300_sync_program(soft_iic_dev iic, bs300_prog_struct_t *p); // 全量同步 Param I2C (阻塞)
int bs300_switch_program(soft_iic_dev iic, u8 new_prog_idx);      // 阻塞切程序 (diff + Vol/Beep/DDM2/MM+ 强制)
int bs300_param_modify(soft_iic_dev iic, u8 prog_idx, u16 offset,
                       const u8 *val, u8 len);                    // 阻塞改参数
// 音量 + EQ
int bs300_set_volume(soft_iic_dev iic, u8 level);                 // 音量 0-9, 3dB/step
int bs300_set_eq(soft_iic_dev iic, s8 low, s8 mid, s8 high);      // 3 段 EQ [-12,12] dB
int bs300_reencode_bin_gain(soft_iic_dev iic);                     // 公开: 重编 Bin Gain, 发 0x8060B2
int bs300_resync_active(soft_iic_dev iic);                         // 从 VM 重新加载同步全部 I2C
// 输入切换 (零 VM 读，用启动缓存)
u8  bs300_voice_prompt_input_switch(soft_iic_dev iic, u8 target_input);   // 切输入 (mute→写→active)
int bs300_voice_prompt_input_restore(soft_iic_dev iic, u8 original_input); // 恢复输入
// 启动缓存 (开机调一次)
void bs300_cache_prog_inputs(void);                                // 缓存全部 prog 的 input+modules+calib
u8   bs300_get_prog_input(u8 prog_idx);                            // 返回缓存的 input_selection
void bs300_on_active_prog_changed(u8 new_prog_idx);                // 切程序后更新缓存
```

### 非阻塞 API（适合运行时操作）
```c
int bs300_switch_program_async(u8 new_prog_idx);                  // 异步切程序
int bs300_resync_diff_async(bs300_prog_struct_t *_new,
                             void (*on_done)(void));               // 异步 diff sync
int bs300_param_modify_async(u8 prog_idx, u16 offset,
                             const u8 *val, u8 len);              // 异步改参数
int bs300_set_volume_async(u8 level, void (*on_done)(void));      // 异步音量 (VM→0x8060B2 tick)
int bs300_set_eq_async(s8 low, s8 mid, s8 high,
                        void (*on_done)(void));                    // 异步 EQ (VM→0x8060B2 tick)
int bs300_reencode_bin_gain_async(void (*on_done)(void));         // 异步重编 Bin Gain (单条 tick)
```

### 底层 API
```c
int bs300_param_write_packet(soft_iic_dev iic, u32 cmd, const u8 *data);  // 单包 I2C
int bs300_program_read(soft_iic_dev iic, u8 prog_idx, u8 *buf);           // 读 Flash
int bs300_program_write(soft_iic_dev iic, u8 prog_idx, const u8 *buf);    // 写 Flash

/* Input Tone Generator (0x8001E2) */
int bs300_encode_itg(u8 level_db, u16 freq_hz, u8 enable,                // 编码 48B 数据
                     const bs300_calib_t *calib, u8 *data);
int bs300_itg_write(soft_iic_dev iic, u8 level_db, u16 freq_hz,          // 生成信号
                    const bs300_calib_t *calib);
int bs300_itg_clear(soft_iic_dev iic);                                   // 清除信号

/* Pure-tone Audiometry (CMD 40, state=0/1) */
int bs300_audiometry_enter(soft_iic_dev iic);                            // 进入纯音测听
int bs300_audiometry_exit(soft_iic_dev iic);                             // 退出纯音测听
```

## 12. I2C 命令发送顺序

全量同步 (`sync_program_inner`) 和增量切换 (`switch_diff_*`) 使用统一顺序：

```
800022(DDM2) → 800062(MM+) → 800052(DFBC)
→ 8000C2-8090C2(ENR×9, 不含 0x8090C2)
→ [800172(NoiseGen2) — 仅全量同步，固定 disable]
→ 804272(TC/DAI) → 8001B2(ISS)
→ 8001C2-8021C2(WNR×4) → 800382(AGCO)
→ 800081(Vol/Beep) → 8000B2-80A0B2(WDRC×11)
```

开关增量对比拆为 `switch_diff_pre_enr`(DDM2/MM+/DFBC) → ENR → `switch_diff_post_enr`(TC/DAI/ISS/WNR/AGCO) → Vol/Beep → WDRC，保证 ENR 在 DFBC 之后、TC/DAI 之前。

异步状态机逐条喂狗 (`clr_wdt()` after SEND & POLL)。

## 13. 待解决事项

- [x] VM 分段存储 → 2×240B per program
- [x] 校准数据存储 → VM ID 28
- [x] 校验和 → 133805 验证通过
- [x] 结构化存储 → bs300_prog_struct_t
- [x] 增量对比 → switch_diff_* 函数
- [x] 非阻塞切程序 → bs300_sync_session_t 状态机
- [x] I2C 速率统一 → 全局 250 (2026-06-04)
- [x] 异步 I2C → session tick 逐条发送，conflict 50ms 重试
- [x] DSP 音量 0-5 → 0-9 (3dB/step)
- [x] WNR enable → |= 0x01 防御加固
- [x] init 延迟 sync → 4 程序全部加载后统一同步
- [x] ACTIVE 命令 → sync 完成后启动 DSP
- [x] 语音提示输入切换 → bs300_prompt_tone_play 封装
- [x] Prog 0 Flash Telecoil→FrontMic 覆盖 (input_selection==1)
- [x] 模式切换只循环 0/1/2
- [x] input_selection 值映射修正 (1=Telecoil, 2=DAI → match 协议)
- [x] 提示音 mute→active 包裹 + I2C 降速修复
- [x] ACTIVE fire-and-forget (不轮询)
- [x] 提示音 tone_play_index_no_tws + 抢断=1
- [x] 通话/音乐自动切程序 3 → bs300_enter/leave_audio_mode
- [x] I2C 命令统一发送顺序 (sync 和 switch 一致)
- [x] ENR 0x8090C2 (SASF) 移除
- [x] struct→protocol input_selection 翻译 (bs300_encode_volume_beep)
- [x] 提示音流程重构: mute→tele→active→音→500ms→mute→action→active
- [x] 异步 I2C 逐条喂狗 (clr_wdt)
- [x] 下载擦除 VM (-format vm)
- [x] DSP 音量独立控制 (0-5) → bs300_set_volume
- [x] 通话 I2C 降速 → soft_iic_set_delay(1250)
- [x] 信号发生器 0x8001E2 → `bs300_encode_itg()` / `bs300_itg_write()` / `bs300_itg_clear()`
- [x] 纯音测听 → `bs300_audiometry_enter()` / `bs300_audiometry_exit()`（CMD 40 status=0/1）
- [ ] 提示音 DDM2 disable/enable（开启 DDM2 参数后补）
- [ ] Noise Gen2 模块
- [ ] BLE 完整协议定义
