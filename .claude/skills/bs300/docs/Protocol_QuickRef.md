# BS300 协议速查

> 模块表、公式、取整规则的浓缩参考。字段布局以 `BS300 Protocol Handbook v3.md` 为权威来源。

## 1. 协议基础

```
I2C 从机地址: 0x02, 小端序
Simple Command:  [02] 00 [Cmd_L] [Cmd_M] [Cmd_H] [Chk]          (6B)
Read Request:    [02] [0x80|len] [Chk]                            (3B)
Advanced Write:  [02] 10 [Cmd_L] [Cmd_M] [Cmd_H] [Data 48B] [Chk] (54B)

校验和: Chk = 0xFF - sum(Length + Command + Data) & 0xFF  (不含 Slave Addr)

命令字 bit 字段:
  Bit 23  = FURPROC   (0=就绪, 1=需进一步处理)
  Bit 15:12 = PKTNUM  (分包序号 0-15)
  Bit 4   = RDWRTBN   (0=EEPROM, 1=RAM)

写流程: 发 Simple/Adv Write → delay 60ms → Read Req(len=0x00) → 读 4B → FURPROC=0?
读流程: 发准备命令 → delay 60ms → 轮询就绪(FURPROC=0) → delay 60ms → Read Req(len=0x10) → 读 52B
```

## 2. 模块速查表

### 2.1 WDRC 模块

| 功能 | Param 命令字 | Flash 格式 | 状态 |
|------|-------------|------------|:--:|
| General Setup | `0x8000B2` | bit-0:1, bit-1:lim_sel, bit-2:kp_mode | ✓ |
| Freq Spacing | `0x8010B2` | 4×uint6 per word | ✓ |
| KP Threshold | `0x8020B2` | ch×7bit, `= value_in_MT` (原始值) | ✓ |
| Attack Time | `0x8030B2` | ch×7bit 表索引 | ✓ |
| Release Time | `0x8040B2` | ch×7bit 表索引 | ✓ |
| Ratio | `0x8050B2` | ch×7bit 表索引 | ✓ |
| Bin Gain | `0x8060B2` | 32×7bit int7, `= 27 + value_in_MT` | ✓ |
| Lmt Threshold | `0x8070B2` | ch×7bit, `= value_in_MT - 30` | ✓ |
| Lmt Attack | `0x8080B2` | ch×7bit 表索引 | ✓ |
| Lmt Release | `0x8090B2` | ch×7bit 表索引 | ✓ |
| Lmt Ratio | `0x80A0B2` | ch×7bit 表索引 | ✓ |

**Flash per-channel 布局** (119 bit/ch): freq(6) + epd_at/rt/r(7×3) + `0b10` + kp1_th/kp2_th(7×2) + `0b10` + kp1_at/kp2_at(7×2) + `0b10` + kp1_rt/kp2_rt(7×2) + `0b10` + kp1_r/kp2_r(7×2) + lmt_th/at/rt/r(7×4)。4 个 `0b10` = padding marker。

**关键公式 (Param 路径)**:
- KP Th: `data = 60 + th - avg(output.cal - gain.cal) [- igd]` **(float_32/int_16t → saturate int8_t, ±1 容忍)**
- Lmt Th: `data = 60 + th - avg(out[fidx..fidx+1])` **(floor avg, ±1 容忍)**
- Bin Gain: `data = bin_gain - gain_cal + igd`

### 2.2 ENR 模块

| 功能 | Param 命令字 | Flash 格式 | 状态 |
|------|-------------|------------|:--:|
| General Setup | `0x8000C2` | 18-bit header: nfsf(4)+nhsf(4)+nnsf(4)+num_ch-1(6) | ✓ |
| Freq Spacing | `0x8010C2` | — | ✓ |
| SNR Threshold | `0x8020C2` | — | ✓ |
| Max Attenuation | `0x8030C2` | — | ✓ |
| Noise Threshold | `0x8040C2` | ch×6bit, `= value_in_MT - 10` | ⚠ 已知差异 |
| Upper Noise Thr | `0x8050C2` | — | ⚠ 已知差异 |
| Smoothing | `0x8060C2` | — | ✓ |
| ETR | `0x8070C2` | — | ✓ |
| NRR | `0x8080C2` | — | ✓ |
| Speech Adap SF | `0x8090C2` | — | ⚠ 未接入交叉验证 |

**Flash per-channel 布局** (39 bit/ch): freq(6)+ma(5)+snrth(5)+nt(6)+unt(6)+etr(7)+nrr(4)。尾部 snasf(4)。

**关键公式 (Param 路径)**:
- NT: `val = round(5.307 * (185 - mic1Cal) - 371.2)` (Telecoil, readbuf=0 时)
- SNR Th: `val = round(32 / 6.02 * snr_th_db)` **(round, 不是 int!)**
- Max Att: `val = floor(max_att / snr_th * 256)` **(floor)**
- ENR Smoothing: `nfsf/nhsf/nnsf = value_in_MT - 1`, `snasf = value_in_MT - 1`

### 2.3 其他 Param 模块

| 模块 | Param 命令字 | 关键公式 | 状态 |
|------|-------------|----------|:--:|
| Volume/Beep/Input | `0x800081` | 直存 | ✓ |
| Telecoil Gain Diff | `0x804272` | `ig_diff = gain/10 + out_diff/10` | ✓ |
| DFBC Configure | `0x800052` | mode 枚举 | ✓ |
| ISS Configure | `0x8001B2` | 见下方 ISS | ✓ |
| WNR Detect Thr | `0x8001C2` | `avg ceil` | ✓ |
| WNR Bands 0-15 | `0x8011C2` | `_WNR_SSP_OFFSET[band][preset]` | ✓ |
| WNR Bands 16-31 | `0x8411C2` | 同上 | ✓ |
| WNR Single Mic | `0x8021C2` | 直存 | ✓ |
| AGCO | `0x800382` | 见下方 AGCO | ✓ (±1) |
| MM Plus | `0x800062` | `ratio = 50 + value_in_MT` | ✓ (disabled→全零) |
| DDM2 | `0x800022` | polar: uint3→frac24 查表; omni: `2^47/10^(0.1*(avg_mic1-mt)-1.2)`, mt=(i2c-2)/4+40 | ✓ (data/ddm2_validation.json) |

**其他 Flash 模块** (简化):
| 模块 | Cmd Data | 数据长度 | 格式 |
|------|----------|---------|------|
| Volume/Beep | `0x07` | 3 words (9B) | beep_level(1)+freq(2)+vol(2)+batt(3)+0x00 |
| Inputs | 可变 (见手册) | 0-2 words | MM Plus: ratio(uint8)+type(uint8)+0x00 |
| DFBC | `0x14` | 1 word (3B) | mode(uint8)+0x0000, 6 种模式枚举 |
| ISS | `0x1D` | 1 word (3B) | threshold(uint8)+0x0000 |
| WNR | `0x1F` | 1 word (3B) | dual_mic_mode(1)+suppression(1)+0x00, 5 级预设 |
| AGCO | `0x23` | 2 words (6B) | atk(uint12)+rel(uint12)+thr(uint8)+0x0000 |

### 2.4 ISS / AGCO 关键公式

- **ISS Param**: `thr_frac24 = 0x010000 - ceil(thr * 65536 / 6.02)` **(ceil, 注意 0x010000 减法)**
- **ISS frac48**: `exponent = (-3 - thr + mic1_cal_avg + igd) / 10`, `val = round(1.0/(10**exponent) * (1<<47))` **(round, 不是 int)**
- **AGCO Param**: `val = 0xFA0000 - ceil(abs(thr) * 65536 / 6.02)` **(ceil, 不是 truncation! 历史 bug #1)**

## 3. 关键公式速查（取整规则标注）

```
┌──────────────────────┬────────────────────────────────────────────────┬──────────────┐
│ 公式                 │ 表达式                                          │ 取整规则      │
├──────────────────────┼────────────────────────────────────────────────┼──────────────┤
│ WDRC KP Threshold   │ 60 + th - avg(output.cal - gain.cal) - igd     │ ±1 容忍      │
│ WDRC Lmt Threshold  │ 60 + th - avg(out[fidx..fidx+1])               │ ±1 容忍      │
│ WDRC Bin Gain       │ bin_gain - gain_cal + igd                      │ 直接 int     │
│ ENR SNR Threshold   │ round(32 / 6.02 * snr_th_db)                   │ **round**    │
│ ENR Max Attenuation │ floor(max_att / snr_th * 256)                  │ **floor**    │
│ ENR NT (Telecoil)   │ round(5.307 * (185 - mic1Cal) - 371.2)         │ **round**    │
│ AGCO Threshold      │ 0xFA0000 - ceil(|thr| * 65536 / 6.02)          │ **ceil**     │
│ ISS Param           │ 0x010000 - ceil(thr * 65536 / 6.02)            │ **ceil**     │
│ ISS frac48          │ round(1.0 / (10**exp) * (1 << 47))             │ **round**    │
│ WNR Detect Thr      │ ceil(avg(values))                               │ **ceil**     │
│ Volume Gain (frac)  │ int(gain * 327680 / 301)                        │ **trunc**    │
└──────────────────────┴────────────────────────────────────────────────┴──────────────┘

实现速查:
  ceil (65536/6.02):  (N * 327680 + 300) // 301
  floor (65536/6.02): (N * 327680) // 301
  round (65536/6.02): round(N * 327680 / 301)
```

## 4. 输入切换影响清单

切换 `input_selection` 时，以下 10 条 I2C RAM 指令的编码值会因 `igd` (Input Gain Diff) 或 `input_type` 变化而改变：

### 4.1 igd 来源

```c
get_input_gain_diff_tenth_db(input_type, calib):
  input_type == TELECOIL → calib->telecoil_gain_diff   // 0.1dB 单位
  input_type == DAI      → calib->dai_gain_diff
  其他                   → 0
```

### 4.2 受影响指令

| # | 命令 | 模块 | 依赖 |
|---|------|------|------|
| 1 | `0x800081` | Vol/Beep/Input | `input_selection` 字段直接变化 |
| 2 | `0x8060B2` | WDRC Bin Gain | `igd` |
| 3 | `0x8020B2` | WDRC KP Threshold | `igd` |
| 4 | `0x8040C2` | ENR Noise Threshold | `igd` |
| 5 | `0x8050C2` | ENR Upper Noise Thr | `igd` |
| 6 | `0x8001B2` | ISS | `igd` |
| 7 | `0x8011C2` | WNR Bands 0-15 | `igd` |
| 8 | `0x8411C2` | WNR Bands 16-31 | `igd` |
| 9 | `0x8021C2` | WNR Single Mic | `igd` |
| 10 | `0x804272` | TC/DAI Gain Diff | 进入/退出 TC/DAI 时 |

### 4.3 不受影响

以下模块**不用 igd**，输入切换时编码值不变：
- **WDRC**: Freq Spacing, Attack, Release, Ratio, Lmt Threshold/Attack/Release/Ratio
- **ENR**: General, Freq Spacing, SNR Threshold, Max Att, Smoothing, ETR, NRR
- **WNR Setup** (`0x8001C2`) — 用 `calib->mic2_gain_diff`，不用 igd
- **AGCO** — 只用自己的 threshold/attack/release，不用 igd
- **DFBC** — 独立于输入

### 4.4 DDM2 / MM+ 开关

DDM2 (`0x800022`) 仅在 `input=5` 时启用，MM+ (`0x800062`) 仅在 `input=4` 时启用。切到 Telecoil (input=2) 时两者均不触发。

### 4.5 实现提示

`bs300_voice_prompt_input_switch()` 只处理 Vol/Beep + DDM2/MM+ 开关，不重编 igd 相关指令。**完整的输入切换必须走全量 diff sync**（`bs300_switch_program_start` / `bs300_resync_diff`），让 `switch_diff_*` 函数自动计算 igd 变化影响的指令并只发真正变了的。```
