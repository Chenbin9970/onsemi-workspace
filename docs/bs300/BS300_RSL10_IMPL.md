# BS300 RSL10 实现文档

> 2026-07-11 | 基于 `peripheral_server_sleep` 项目
> 重构：s_dsp_state 单一状态源 + 逐条增量更新 + abort 中断切换

## 1. 文件结构

```
peripheral_server_sleep/
├── code/
│   ├── bs300_hal.c/h            # I2C GPIO 模拟 (SCL=DIO8, SDA=DIO7)
│   ├── bs300_startup.c/h        # I2C 帧构建/校验/FURPROC轮询/startup序列
│   ├── bs300_program_read.c/h   # Flash 读取(10×48B) + BitReader 解码
│   ├── bs300_calib.c/h          # 校准数据解析 (144B raw → struct)
│   ├── bs300_param_tables.c/h   # 静态查找表 (WNR SSP offset)
│   ├── bs300_param_encode.c/h   # 31 条 Param I2C 编码函数 + 数学辅助
│   ├── bs300_storage.c/h        # Main Flash 存储层 (5 扇区独立擦写 + Settings 掉电记忆)
│   ├── bs300_ram_sync.c/h       # 同步编排器 + s_dsp_state 状态管理 + 提示音
│   ├── bs300_driver.c/h         # 驱动层 (init流程/缓存/刷RAM)
│   └── bs300_test.c/h           # 集成测试
└── include/
    ├── bs300_encode_tables.h    # beep frac24 查找表（codegen 生成）
    └── bs300_ram_sync.h         # sync_session_t 定义 + API 声明
```

## 2. 核心设计：s_dsp_state 单一状态源

### 2.1 概念

`s_dsp_state` 是 490 字节的 `bs300_prog_struct_t`，存储在 `.bss` 中，**始终等于 BS300 DSP 当前参数状态**。

```
┌──────────────────────────────────────────┐
│              s_dsp_state (490B)           │
│  ┌─────────────────────────────────────┐ │
│  │  wdrc (292B)     enr (134B)         │ │
│  │  modules (64B)                      │ │
│  └─────────────────────────────────────┘ │
│         ↑ 逐条命令成功后增量更新          │
│         ↑ 切程序完成后完整同步            │
└──────────────────────────────────────────┘
```

**关键原则**：
- **每条 I2C 命令发送成功 → 立即更新 s_dsp_state 对应字段**（`dsp_state_apply()`）
- **切程序时 s_dsp_state 做 old，目标 Program 做 new，diff 只发差异**
- **切程序中途收到新指令 → abort 当前 session，从当前 s_dsp_state 重新 diff**

### 2.2 静态内存布局

| 变量 | 大小 | 说明 |
|------|------|------|
| `s_dsp_state` | 490B | 当前 DSP 参数状态，唯一权威源 |
| `s_target` | 490B | 切换时的目标 Program，diff + apply 用 |
| `s_calib_cache` | ~80B | 校准数据缓存 |
| `s_volumes[4]` | 4B | 各 Program 的音量（掉电记忆） |
| `s_cur_prog` | 1B | 当前 active program 号 |
| `bs300_work_buf[480]` | 480B | 共享 Flash raw 缓冲（driver + sync 共用） |
| `g_bs300_sync` | ~1700B | 异步 session（命令队列 32×48B） |
| **合计** | **~3245B** | |

> 旧设计用 `s_prog_modules[4]`(256B) + `s_prog_enr[4]`(536B) + `s_prog_input[4]`(4B) + `s_work[480]` + `s_raw[480]`(driver.c) = 1756B。新设计仅多 ~200B 但提供了完整状态追踪和中断切换能力。

## 3. 逐条增量更新

### 3.1 dsp_state_apply()

异步 session 中每条命令 POLL 成功后，按命令地址映射到 `s_dsp_state` 字段，从 `s_target` 拷贝新值：

```
0x800022 (DDM2)      → modules: ddm2_enable, open_ear, polar_pattern, adm_fdm, omni_threshold
0x800062 (MM+)       → modules: mm_plus_enable, mix_ratio
0x800052 (DFBC)      → modules: dfbc_enable_mode
0x8000C2 (ENR Gen)   → enr: enable_num_ch
0x8010C2 (ENR Freq)  → enr: freq_idx[16]
0x8020C2 (ENR SNR)   → enr: snr_th_db[16]
0x8030C2 (ENR MaxAtt)→ enr: max_att_db[16]
0x8040C2 (ENR NT)    → enr: noise_th_db[16]
0x8050C2 (ENR UNT)   → enr: upper_noise_th_db[16]
0x8060C2 (ENR Smooth)→ enr: nfsf, nhsf, nnsf, snasf
0x8070C2 (ENR ETR)   → enr: etr_x100[16]
0x8080C2 (ENR NRR)   → enr: nrr_x10[16]
0x8001B2 (ISS)       → modules: iss_enable, iss_threshold
0x8001C2 (WNR Setup) → modules: wnr_enable_dual, wnr_preset
0x800382 (AGCO)      → modules: agco_enable, agco_threshold_db, agco_attack/release
0x800081 (Vol/Beep)  → modules: vol/beep 全部字段 + input_selection
0x8000B2 (WDRC Gen)  → wdrc: total_channels, nsbc, kp_mode, limiter
0x8010B2 (WDRC Freq) → wdrc: freq_idx[16]
0x8020B2 (WDRC KPTh) → wdrc: kp1_th_db[16], kp2_th_db[16]
0x8030B2 (WDRC Atk)  → wdrc: epd/kp1/kp2 at_idx[16]
0x8040B2 (WDRC Rel)  → wdrc: epd/kp1/kp2 rt_idx[16]
0x8050B2 (WDRC Ratio)→ wdrc: epd/kp1/kp2 r_idx[16]
0x8060B2 (WDRC BG)   → wdrc: bin_gain[32]; modules: volume_level, eq_low/mid/high
0x8070B2 (WDRC LmtTh)→ wdrc: lmt_th_db[16]
0x8080B2 (WDRC LmtAt)→ wdrc: lmt_at_idx[16]
0x8090B2 (WDRC LmtRel)→ wdrc: lmt_rt_idx[16]
0x80A0B2 (WDRC LmtR) → wdrc: lmt_r_idx[16]
```

Session 完成后做最终 `memcpy(s_dsp_state, s_target, 490)` 确保完整一致性。

### 3.2 状态更新时机

| 操作 | s_dsp_state 更新时机 |
|------|---------------------|
| 全量同步（init） | 同步完成后 `s_dsp_state` 已正确（boot cache 加载 + full sync → DSP = s_dsp_state） |
| 切程序（async） | 每条命令 POLL OK → `dsp_state_apply()` 增量更新 |
| 调音量 | 立即改 `s_dsp_state.modules.volume_level` + `s_volumes[cur]`；I2C 发送后 apply 无变化 |
| 调 EQ | 立即改 `s_dsp_state.modules.eq_*`；I2C 发送后 apply |
| 语音提示音 | 直接 I2C 写 DSP 提示音命令，不修改 s_dsp_state |
| 测听 enter/exit | enter 直接写 raw I2C（不改 s_dsp_state）；exit 全量 sync 恢复 |

## 4. 启动 + 刷 RAM 完整流程

```
bs300_driver_init()
  ├─ bs300_hal_init() + delay(2000ms)              ← I2C 初始 + DSP 供电稳定
  ├─ bs300_startup()                               ← MUTE → KEY_LOCK → VERIFY_COMM
  ├─ bs300_storage_is_valid(0) ?
  │   YES → 跳过 Flash 读取 (Main Flash 缓存命中)
  │   NO  ↓
  └─ read_and_save_all():
       for prog 0..3:
         bs300_program_read(i)                      ← I2C 读 480B
         bs300_storage_write_program(i, data)       ← Erase + Write 独立扇区
  ├─ bs300_settings_load() → bs300_restore_settings() ← 恢复掉电保存的模式 + 音量
  ├─ bs300_cache_boot_state()                       ← 加载 active prog → s_dsp_state + 读校准
  ├─ bs300_mute()
  ├─ bs300_sync_program(bs300_get_dsp_state())      ← 全量 31 条 I2C
  └─ bs300_active()                                 ← 启动 DSP
```

**首次启动**: ~40 条 I2C (~2-3s) + 刷 RAM 31 条 (~3-4s) + 5 个 Main Flash 扇区擦写 (~0.5s) ≈ 5-7s  
**后续启动**: 0 条 I2C (Main Flash 缓存) + I2C 读校准 (3 条) + 刷 RAM 31 条 (~3-4s) ≈ 3-4s

## 5. diff 增量切换

### 5.1 构建流程

```
bs300_switch_program_async(new_prog_idx)
  ├─ 若当前 session 忙 → g_bs300_sync.abort_requested = true  ← 中断旧 session
  ├─ load_struct(new_prog_idx) → s_target (490B .bss)
  ├─ s_target.modules.volume_level = s_volumes[new_prog_idx]
  ├─ s_cur_prog = new_prog_idx; save_settings()
  ├─ bs300_sync_session_init(&g_bs300_sync)
  │   g_bs300_sync.dsp_state = &s_dsp_state
  │   g_bs300_sync.target = &s_target
  ├─ session[0] = 提示音 I2C (0xFD52F2 for P0, etc.)
  └─ build_diff_session():
        old = s_dsp_state, new = s_target
        ├─ switch_diff_pre_enr()   ← DDM2 / MM+ / DFBC
        ├─ switch_diff_enr()       ← ENR × 9
        ├─ switch_diff_post_enr()  ← TC/DAI / ISS / WNR / AGCO
        ├─ switch_diff_vol_beep()  ← Vol/Beep
        └─ switch_diff_wdrc()      ← WDRC × 11
        全部按需对比 (ON→ON memcmp)，无强制追加指令。

### 5.2 同程序 re-sync

SetGain/MPO/CompressRatio 修改当前活跃程序时只写 Flash 不同步 BS300 (见 §15)。
App 再次发送 SetCurrentScene(当前程序) 触发 re-sync:
  ├─ load_struct(prog) → s_target (从 Flash 加载最新值)
  ├─ s_cur_prog 不变
  ├─ 不播提示音
  └─ build_diff_session(): s_dsp_state vs s_target → 仅发出实际变化的模块
     (如仅修改 WDRC，则只发 WDRC diff，不发 ENR/AGCO/ISS)
```

### 5.2 每模块 OFF→OFF / OFF→ON / ON→OFF / ON→ON 四分支

所有 diff 函数遵循统一模式：
- **OFF→OFF**: 跳过，不发命令
- **OFF→ON**: 无条件发全部命令（新启用的模块需完整初始化）
- **ON→OFF**: 发 1 条全零帧（disable）
- **ON→ON**: 逐字段 memcmp，只发变化的 + 依赖链上的

### 5.3 igd 依赖链

`input_selection` 变化触发 `igd_changed`，影响最多 13 条命令：

```
input_selection 改变
  → igd = tc_gain_diff/10 或 dai_gain_diff/10 或 0
    → WDRC KP Threshold  (-igd)
    → WDRC Bin Gain      (+igd)
    → ENR Noise Thr      (-igd)
    → ENR Upper Noise Thr(-igd)
    → ISS                (+igd)
    → WNR × 4            (+igd)
    → MM Plus            (-igd)
    → TC/DAI             (直接使用 calib)
```

### 5.4 中断切换（Abort）

```
时刻 A: P0→P1 diff session 正在运行 (第3条刚完成, s_dsp_state 已更新3次)
时刻 B: BLE 收到切 P2 指令
  → g_bs300_sync.abort_requested = true
  → 下个 ke_timer tick 检测到 → state → IDLE (不发更多 I2C)
  → load_struct(P2) → s_target
  → build_diff_session(s_dsp_state vs P2) → 新 session
  → 从 s_dsp_state 当前状态（含 P1 的 3 条修改）diff 到 P2
```

## 6. 提示音

切模式和调音量时，提示音作为 session 第一条命令发送：

| 场景 | I2C 命令 | 位置 |
|------|---------|------|
| 切到 Program 0 | `0xFD52F2` | session[0] |
| 切到 Program 1 | `0xFD72F2` | session[0] |
| 切到 Program 2 | `0xFD92F2` | session[0] |
| 切到 Program 3 | `0xFDB2F2` | session[0] |
| 音量=0 | `0xFD12F2` | session[0]，后跟 `0x8060B2` bin_gain |
| 音量≠0 | `0xFCD2F2` | session[0]，后跟 `0x8060B2` bin_gain |

不再使用 `voice_prompt_input_switch/restore` 切 Telecoil 的复杂流程，直接用 DSP 内置提示音命令。

## 7. Main Flash 存储布局

全部在主 Flash HIGH 区（`0x0015C800` 起），不占用 NVR3。

### 7.1 Program 扇区（每 Program 独立 2KB）

| 地址 | 大小 | 内容 |
|------|------|------|
| `0x0015D000` | 2KB | Program 0 raw data (480B) + trailer (8B) |
| `0x0015D800` | 2KB | Program 1 raw data (480B) + trailer (8B) |
| `0x0015E000` | 2KB | Program 2 raw data (480B) + trailer (8B) |
| `0x0015E800` | 2KB | Program 3 raw data (480B) + trailer (8B) |

每个扇区 trailer（offset 480）: Magic `"BSPG"`(4B) + Version(2B) + CRC16 XMODEM(2B)

### 7.2 Settings 扇区（2KB，掉电记忆）

基址 `0x0015C800`。

| 偏移 | 大小 | 内容 |
|------|------|------|
| 0 | 1B | active_prog (0-3) |
| 1 | 4B | volume[0..3] (0-9 per program) |
| 5 | 3B | reserved |
| 8 | 4B | Magic `"BSST"` |
| 12 | 2B | CRC16 XMODEM |
| 14 | 2B | Version |

## 8. Param I2C 编码架构

### 8.1 命令发送顺序（31 条）

```
DDM2(0x800022) → MM+(0x800062) → DFBC(0x800052)
→ ENR×8 (0x8000C2-0x8080C2，不含 0x8090C2 SASF)
→ NoiseGen2(0x800172) → TC/DAI(0x804272) → ISS(0x8001B2)
→ WNR×4 (0x8001C2/0x8011C2/0x8411C2/0x8021C2)
→ AGCO(0x800382) → Vol/Beep(0x800081)
→ WDRC×11 (0x8000B2-0x80A0B2)
```

### 8.2 编码函数分类

**A 类 — 简单字节/索引打包**（无需校准数据）：
WDRC general, freq_spacing, attack/release/ratio, lmt_attack/release/ratio
ENR general, freq_spacing, snr_th, max_att, smoothing, etr, nrr

**B 类 — 依赖校准数据**：
WDRC kp_threshold, bin_gain, lmt_threshold
Volume/Beep, DFBC, ISS, WNR×4
ENR noise_th, upper_noise_th
AGCO, DDM2, MM+, TC/DAI

### 8.3 关键公式速查

| 公式 | 表达式 | 取整规则 |
|------|--------|---------|
| WDRC KP Threshold | `60 + th - avg(mic1[fidx..fidx+1]) - igd` | floor avg |
| WDRC Lmt Threshold | `60 + th - avg(output[fidx..fidx+1])` | floor avg |
| WDRC Bin Gain | `bin_gain - (output[i] - mic1[i]) + igd` | 直接 int |
| ENR SNR Threshold | `round(32 / 6.02 * snr_th_db)` | **round** |
| AGCO Threshold | `0xFA0000 - ceil(|thr| * 65536 / 6.02)` | **ceil** |
| ISS frac48 | `round(1.0 / (10**exp) * (1<<47))` | **round** |
| WNR Detect Thr | `ceil(avg(mic1))` | **ceil** |

**整数运算速查**（`db_to_frac24` = `(n * 327680 + 300) / 301`）：
- ceil: `(N * 327680 + 300) / 301`
- floor: `(N * 327680) / 301`
- round: `(N * 327680 + 150) / 301`

### 8.4 Flash 值 → value_in_MT 转换

RSL10 结构体存储 flash 原始值，编码时需转换：

| 字段 | Flash 存储值 | → 编码用 value_in_MT |
|------|-------------|---------------------|
| `kp1_th`, `kp2_th` | value_in_MT（原始值） | 直接使用 |
| `lmt_th` | value_in_MT - 30 | `+ 30` |
| `bin_gain[i]` | 27 + value_in_MT | `- 27`（有符号） |
| `nt`, `unt` | value_in_MT - 10 | `+ 10` |

### 8.5 内存策略

- 逐条编码 + 发送，每次栈上仅分配 48B buffer
- 校准结构体 `bs300_calib_t` ≈ 80B，栈上分配
- 查找表在 .rodata，不占 RAM
- `bs300_encode_tables.h` (beep frac24 表) 396 条 uint32 ≈ 1.6KB .rodata

## 9. 驱动层 API

```c
/* 初始化 */
bool bs300_driver_init(void);                               // I2C + Main Flash 缓存 + 同步 active_prog
bool bs300_driver_refresh(void);                            // 强制从 BS300 重读全部

/* 数据读取 */
const bs300_program_data_t *bs300_driver_get_program(uint8_t idx);  // 按需读 Main Flash → 解析
const bs300_prog_struct_t   *bs300_driver_get_struct(uint8_t idx);  // 按需读 Main Flash → 结构化
const bs300_calib_t         *bs300_driver_get_calib(void);          // 结构化校准数据
bool bs300_driver_is_cached(void);                                  // Main Flash 缓存是否有效

/* DSP 状态直接访问 */
bs300_prog_struct_t *bs300_get_dsp_state(void);             // 返回 &s_dsp_state
```

### 9.1 运行时 API（`bs300_ram_sync.h`）

```c
/* 同步 — 阻塞 */
int bs300_sync_program(bs300_prog_struct_t *prog);          // 全量 31 条 I2C
int bs300_switch_program(uint8_t new_prog_idx);             // 增量 diff 切换

/* 同步 — 非阻塞 (ke_timer 驱动) */
int bs300_switch_program_async(uint8_t new_prog_idx, void (*on_done)(void));
int bs300_set_volume_async(uint8_t level, void (*on_done)(void));  // 异步音量（自动掉电保存）
int bs300_sync_is_busy(void);                               // 查询是否正在切换中
void bs300_sync_timer_handler(void);                        // 定时器回调入口

/* 快速控制 */
int bs300_mute(void);                                       // MUTE (0x800000)
int bs300_active(void);                                     // ACTIVE (0x800010)
int bs300_set_volume(uint8_t level);                        // 音量 (0-9)
int bs300_set_eq(int8_t low, int8_t mid, int8_t high);     // EQ

/* 提示音 */
void bs300_play_prompt_tone(uint8_t program, uint8_t volume);

/* Settings 掉电记忆 */
void bs300_restore_settings(uint8_t active_prog, const uint8_t *volume);
bool bs300_settings_load(uint8_t *active_prog, uint8_t *volume);
bool bs300_settings_save(uint8_t active_prog, const uint8_t *volume);

/* 查询 */
uint8_t bs300_get_active_prog(void);
uint8_t bs300_get_module_volume(uint8_t prog_idx);
bool bs300_is_boot_cached(void);
const bs300_calib_t *bs300_get_cached_calib(void);

/* 存储 */
void bs300_storage_invalidate(uint8_t idx);
void bs300_settings_invalidate(void);
```

## 10. 内存占用

| 区域 | 使用 | 总量 | 占比 |
|------|------|------|------|
| Flash (text) | 242.8 KB | 380 KB | 63.9% |
| RAM (.bss) | 21.6 KB | 24 KB | 89.8% |

**BS300 模块 .bss 明细**（新设计）：

| 变量 | 大小 | 说明 |
|------|------|------|
| `s_dsp_state` | 490 B | 当前 DSP 参数状态 |
| `s_target` | 490 B | 切换目标 Program |
| `bs300_work_buf[480]` | 480 B | Flash raw 缓冲（driver + sync 共用） |
| `s_calib_cache` | ~80 B | 校准数据缓存 |
| `g_bs300_sync` | ~1700 B | 异步 session (32×48B + cmd 列表) |
| 其他小变量 | ~20 B | 标志位、volume[4] 等 |
| **合计** | **~3260 B** | |

### 10.1 与旧设计对比

| | 旧设计 | 新设计 |
|------|---------|------|
| Program 缓存 | `s_prog_modules[4]`(256B) + `s_prog_enr[4]`(536B) | `s_dsp_state`(490B) |
| 工作缓冲 | `s_work[480]` + `s_raw[480]`(driver.c) | `bs300_work_buf[480]` 共用 |
| 切程序栈峰值 | old+new = 980B | target only = 490B (省 490B) |
| 切程序 Flash 读 | 读 old + new (2 次) | 只读 new (1 次) |
| 中断切换 | 排队机制（等当前完成） | abort → 从当前 DSP 状态重新 diff |
| DSP 状态追踪 | 无（4 个 program 分开缓存） | 单一 s_dsp_state，永远准确 |

## 11. I2C 协议摘要

| 帧类型 | 字节数 | 格式 |
|--------|--------|------|
| Simple Command | 5 | `{Len(0x00), Cmd[3], Chk}` |
| Advanced Write | 53 | `{Len(0x10), Cmd[3], Data[48], Chk}` |
| Read Request | 2 | `{0x80\|0x90, Chk}` |
| Read Response | 52 | `{Cmd[3], Data[48], Chk}` |

- 从机地址: `0x01` (7-bit, 写 0x02 / 读 0x03)
- Checksum: `0xFF - (sum & 0xFF)`
- FURPROC 轮询: 发 Read Request(0x80) → 读 4B → 检查 bit23=0
- Program Read: `READ_START(0x80Y031)` → 10×`0x800011+0x1000×pkt`
- Calibration: 3×`0x800051+0x1000×pkt`

### 11.1 I2C 速率自动切换

| 宏 | 值 | 对应速率 | 用途 |
|---|:---:|:---:|------|
| `BS300_I2C_DELAY_NORMAL` | 500 | ~2 kHz | 关键指令发送前 |
| `BS300_I2C_DELAY_ACTIVE` | 250 | ~4 kHz | DSP 运行时（ACTIVE 后） |
| `BS300_I2C_DELAY_FAST` | 10 | ~50 kHz | DSP 停止时（mute 成功后） |

| 时机 | bit_delay | 说明 |
|------|:---:|------|
| 初始 (hal_init) | 500 | 安全默认值 |
| MUTE 命令发送前 | 500 | 关键指令，慢速确保可靠 |
| MUTE 发送成功 | 10 | DSP 已停，后续 Param 命令全速发送 |
| ACTIVE 命令发送前 | 500 | 关键指令，慢速确保可靠 |
| ACTIVE 发送后 | 250 | DSP 运行中，中等速度 |

## 12. 已实现 vs 待实现

| 模块 | 状态 | 文件 |
|------|:--:|------|
| I2C HAL (GPIO 模拟) | ✓ | bs300_hal.c |
| 帧构建/校验/轮询 | ✓ | bs300_startup.c |
| Flash Read + 解码 | ✓ | bs300_program_read.c |
| Main Flash 存储 (5 扇区) | ✓ | bs300_storage.c |
| Settings 掉电记忆 | ✓ | bs300_storage.c + bs300_ram_sync.c |
| 校准解析 | ✓ | bs300_calib.c |
| Param I2C 编码 (31 条) | ✓ | bs300_param_encode.c |
| 刷 RAM 同步器 | ✓ | bs300_ram_sync.c |
| s_dsp_state 单一状态源 | ✓ | bs300_ram_sync.c |
| 逐条增量更新 (dsp_state_apply) | ✓ | bs300_ram_sync.c |
| Abort 中断切换 | ✓ | bs300_ram_sync.c |
| DSP 提示音 I2C | ✓ | bs300_ram_sync.c |
| 驱动初始化 + 缓存 | ✓ | bs300_driver.c |
| 集成测试 | ✓ | bs300_test.c |
| BLE 异步切模式 (diff) | ✓ | bs300_ram_sync.c + app.c |
| BLE 异步调音量 | ✓ | bs300_ram_sync.c + app.c |
| 非阻塞状态机 (ke_timer) | ✓ | bs300_ram_sync.c + app_process.c |
| DDM2/MM+ enable 模式 | ✗ | 当前仅 disabled |
| Flash Write (Program Burn) | ✗ | 待实现 |

## 13. 关键设计决策

- **s_dsp_state 单一状态源**: 490B .bss，始终等于 DSP 当前参数状态。每帧不缓存全部 4 个 Program，只存当前 + 目标
- **逐条增量更新**: 每条 I2C 成功后 `dsp_state_apply()` 更新对应字段，session 完成后 `memcpy` 全量兜底
- **Abort 中断切换**: 收到新切换指令时立即 abort 当前 session，从 s_dsp_state 重新 diff，不排队
- **RAW 存储**: 存 480B raw 而非 decoded struct，格式稳定
- **Main Flash HIGH 区独立扇区**: 5 个 2KB 扇区（1 Settings + 4 Program），改一个只擦写一个，互不影响
- **不占 NVR3**: NVR3 留给 BLE 栈（设备地址/IRK/CSRK），免冲突
- **校准不存 Flash**: 144B 每次从 BS300 重读 (3 包 ~200ms)
- **共用 work buffer**: `bs300_work_buf[480]` 在 driver 和 sync 间共用，省 480B
- **按需加载**: `get_program()` 每次从 Main Flash 读 480B 并解析
- **逐条编码**: 每次栈上 48B buffer，不缓存全部 31 条 payload
- **PRINTF fallback**: 所有文件 `#ifndef PRINTF` → `#define PRINTF(...) ((void)0)`，关 `DEBUG_UART_ENABLE` 不崩
- **BS300 头文件不入 app.h**: 避免 `bs300_encode_tables.h` (1.6KB+ 静态表) 注入所有编译单元
- **CRC16**: XMODEM 多项式 0x1021，防止半写入/flash 损坏
- **不依赖 malloc**: 全部静态分配
- **异步切模式 = diff timer → 通知**: 提示音排 session 最前，主循环不阻塞
- **异步调音量 = 提示音 + bin_gain → timer → 通知**: 自动掉电保存
- **增量 diff 切换**: 只发送新旧 Program 间变化的模块，大幅减少 I2C 指令数
- **C 编码函数 = Python 逐行翻译**: 遵循 Rule 16，不"理解后重写"
- **掉电记忆 = Settings 扇区**: 独立 2KB 扇区存 active_prog + volume[4]，切模式/调音量自动保存，上电恢复

## 14. 已知差异（不阻塞）

| 模块 | 差异 | 根因 |
|------|------|------|
| ENR NT/UNT | ~3 unit 偏差 | 芯片 mic1Cal 数组与 ENR 频段不一致 |
| AGCO | byte-level ±1 | rounding tolerance (float_32 vs int_16t) |
| WDRC KP Th (P1) | byte-level ±1 | rounding tolerance |
| DDM2/MM+ | 仅支持 disabled (全零) | enable 模式编码待补 |

## 15. 验配参数持久化

### 15.1 Flash-Only (SetGain / SetMPO / SetCompressRatio)

SetGain (ID:6)、SetMPO (ID:7)、SetCompressRatio (ID:8) 只写 Main Flash，不通过 I2C 同步到 BS300 RAM。
参数生效需重启或切程序。

```
cmd_setgain / cmd_setmpo / cmd_setcompressratio
  ├─ 解析 App 数据 (Flash raw 格式 → value_in_MT)
  │   bin_gain: vmt = raw - 27
  │   lmt_th:   vmt = raw + 30
  │   kp_r_idx: vmt = raw (无偏移)
  ├─ bs300_storage_load_program(prog) → flash_to_struct → s_fit_buf
  ├─ 修改 s_fit_buf 对应字段
  └─ fitting_commit(prog, false)
       ├─ struct_to_flash → 480B 编码
       ├─ bs300_storage_write_program → Erase + Write 扇区
       └─ 不调用 bs300_resync_diff_async (sync_dsp=false)
```

**生效流程**: App 配完参数 → 发 SetCurrentScene(当前程序) → 同程序 re-sync (见 §5.2) → diff 同步到 BS300。

### 15.2 RAM-Only (SetDenoise)

SetDenoise (ID:9) 只存 RAM，**不修改 Program Flash**。类似音量/EQ，切程序时自动在 Flash 原始值上叠加偏移。

```
cmd_setdenoise
  ├─ bs300_set_prog_denoise(prog, level) → s_denoise[prog] = level
  │                                       → bs300_settings_save() 立即持久化
  └─ 若 prog == active: bs300_switch_program_async(prog) → re-sync diff 到 DSP
```

**偏移模式**（非固定绝对值，保留各通道原始差异）：

| level | offset | max_att_db | clamp |
|:--:|:--:|------|:--:|
| 0 | +0 | Flash 原始值（不覆盖） | — |
| 1 | +3 | 各通道 +3 | ≤30 |
| 2 | +6 | 各通道 +6 | ≤30 |
| 3 | +9 | 各通道 +9 | ≤30 |
| 4 | +12 | 各通道 +12 | ≤30 |
| 5 | +15 | 各通道 +15 | ≤30 |

max_att_db 为 5-bit Flash 字段，手册定义范围 [0, 30]，叠加后超过 30 截断。

覆盖时机（两处，逻辑一致）：
- `load_struct()` — 切程序/re-sync 时，Flash 加载后叠加
- `bs300_cache_boot_state()` — 开机加载活跃程序后叠加
- 公式: `if (level >= 1) max_att_db[i] = min(max_att_db[i] + level*3, 30)`

## 16. 已修复 Bug

| # | 模块 | bug | 修复 |
|---|------|-----|------|
| 1 | ENR num_ch | `encode_enr_flash` 提取 num_ch 缺少 `-1`，导致 `enable_num_ch` 每次 round-trip 漂移 +1 | `num_ch = (enable_num_ch & 0x0F) - 1`，循环 `i <= num_ch` |

## 17. BLE 0xFE 清空缓存

写 RX 特征值 `0xFE`:
1. 擦除 4 个 Program 扇区 (`bs300_storage_invalidate(i)`)
2. 擦除 Settings 扇区 (`bs300_settings_invalidate()`)
3. 重置 RAM 状态 (`bs300_reset_to_defaults()`): s_cur_prog=0, vol=9, EQ=0, denoise=0
4. 下次开机从 BS300 芯片重新读取全部程序
