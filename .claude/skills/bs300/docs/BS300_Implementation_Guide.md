# BS300 DSP 协议实现 — 分步验证清单

> **此文件是验证清单，不是参考手册。** 协议基础、公式、模块表、取整规则见 `../../bs300.md`。
> 字段布局以 `BS300 Protocol Handbook v3.md` 为唯一权威来源。

## 依赖链

```
Step 0: 协议基础 (frame/checksum/word)
  ↓
Step 1: 校准数据读取  ← 所有验配参数编码的前置条件
  ↓
Step 2: Program Burn 数据读取 (Flash 格式解析)
  ↓
Step 3: Program Burn 数据写入 (value_in_MT → Flash bit-packed → I2C)
  ↓
Step 4: 定点数工具 + 查找表
  ↓
Step 5: 验配参数 Param I2C 指令 (value_in_MT + 校准值 → word-aligned → I2C)
  ↓
Step 6: 信号发生器 + 提示音 (待细化)
  ↓
Step 7: 系统配置 (待细化)
  ↓
Step 8: 目标语言代码生成 (C / Java)
```

**核心约束**: Step 5 的所有验配参数编码函数，必须传入 Step 1 的 `CalibData` 作为参数。

---

## Step 0: 协议基础层 — 验证清单

**产出**: `bs300_codegen.py` (Step 0 函数)
**参考**: `../../bs300.md` §5 协议基础

### 验证检查点 (6 项)

| # | 测试 | 断言 |
|---|------|------|
| 0.1 | Checksum | `bs300_checksum(buf) == 0xAD` (对照手册行 144-163) |
| 0.2 | Frame Build | Mute/PrepCal/ReadReq×2 4 帧全匹配手册示例 |
| 0.3 | Word R/W | `bs300_set_word` + `bs300_get_word` 往返 0x123456 |
| 0.4 | Cmd Fields | FURPROC/PKTNUM/RDWRTBN 6 个断言 |
| 0.5 | Adv Write | 54B `bs300_build_advanced_write` 校验和 0x6E |
| 0.6 | Parse | `bs300_parse_response` 4B + 52B 返回一致 |

---

## Step 1: 校准数据读取与解析 — 验证清单

**产出**: `bs300_codegen.py` (CalibData + parse_calibration)
**参考**: `BS300 Protocol Handbook v3.md` §校准数据

### 单元测试 (7 项)

| # | 测试 |
|---|------|
| 1.1 | Packet header validation (count=3, module_count+1=9) |
| 1.2 | Mic1 band array (32 × uint8) |
| 1.3 | Output band array (32 × uint8) |
| 1.4 | Short calibration modules (6 个) |
| 1.5 | Derived values (avg_mic1_cal, avg_output_cal, input_gain_diff_db) |
| 1.6 | Zero-padding (36 bytes) |
| 1.7 | Roundtrip consistency |

### 交叉验证 (对照真实芯片数据)

输入: `calibration.json` + `calibration_values.json`

| # | 校验项 | 要求 |
|---|--------|------|
| C1 | 32 个 mic1_band | 全部匹配 |
| C2 | 32 个 output_band | 全部匹配 |
| C3 | 6 个短模块 | 全部匹配 |
| C4 | gain_cal 自洽 (output - mic1 == gain_cal) | 32 bands |
| C5 | 派生值计算 | 匹配 |

---

## Step 2+3: Program Burn Flash 读写 — 验证清单

**产出**: `bs300_codegen.py` (BitReader + decode/encode + ProgramData) + `program_burn_write_0.json`
**详细指南**: `Program_Burn_Guide.md`

### 解码单元测试 (8 项)

| # | 测试 |
|---|------|
| 2.1 | Minimal Program (WDRC 2ch + Volume + FrontMic) |
| 2.2 | WDRC module: kp_mode/limiter/num_ch/bin_gain/channel fields |
| 2.3 | Volume module: beep_level/min_vol/max_vol |
| 2.4 | Inputs module: input_type |
| 2.5 | WDRC channel roundtrip: 12 channel fields all match |
| 2.6 | ENR module (bit-packed roundtrip): header + snasf |
| 2.7 | Optional modules (DFBC/ISS/WNR/AGCO) |
| 2.8 | MM Plus input: mixing_ratio |

### 编码交叉验证 (readback → param 比对, 8 项)

| # | 模块 | 验证内容 |
|---|------|----------|
| 3.1 | WDRC | header(kp/limiter/ch), bin_gain×32, 16ch 全部字段 |
| 3.2 | Volume | beep_level/freq, min/max_vol |
| 3.3 | Inputs | 输入类型匹配 |
| 3.4 | DFBC | 模式值匹配 |
| 3.5 | ENR | header(nfsf/nhsf/nnsf/ch), snasf, 16ch 全部字段 |
| 3.6 | ISS | threshold |
| 3.7 | WNR | dual_mic=off, preset |
| 3.8 | AGCO | atk/rel/thr |

### 覆盖要求

- WDRC bit-packed encode/decode roundtrip (119 bit/ch × N channels)
- ENR bit-packed encode/decode roundtrip (39 bit/ch × N channels)
- 4 个 padding marker (`0b10`) 验证
- BitReader/BitWriter LSB-first 跨字节正确性
- I2C 帧: 54 bytes, 校验和验证

---

## Step 4: 定点数学工具 + 查找表 — 验证清单

**产出**: `bs300_codegen.py` (frac24_to_s32, clamp, _TIME_TABLE, _RATIO_TABLE, 频率表)

### 验证检查点 (5 项, 42 断言)

| # | 测试 | 断言数 |
|---|------|--------|
| 4.1 | frac24_to_s32 符号扩展 (0, max+, max-, -1) | 6 |
| 4.2 | clamp_u32 / clamp_s32 边界 | 6 |
| 4.3 | Table 2-2 (122 entries, [0/30/60/80/121]) + Table 2-3 (128 entries, [0/1/32/64/96/127], monotonic) | 7 |
| 4.4 | Freq table (32 entries, [0/1/31], step=250Hz) | 4 |
| 4.5 | 往返验证: time 11 项, ratio 9 项 | 19 |

---

## Step 5: 验配参数 Param I2C 指令生成 — 验证清单

**产出**: `bs300_codegen.py` (所有 Param 编码器)
**详细指南**: 见 `../../bs300.md` §6 模块速查表 + §7 公式速查

### 单元测试 (23 子测试)

- 每个编码器至少 1 个独立测试
- 确认 CalibData 参数正确传入
- 确认输出为固定 48 bytes

### 交叉验证 (Param I2C 逐 byte 对比)

**±1 容忍规则**: byte 级差异 ≤ ±1 视为浮点/整数运算取整差异 (rounding tolerance)，标记为 TOLERATED，不阻塞通过。根因：芯片使用 float_32/int_16t 运算后 saturate 到 int8_t，不同运算类型导致 ±1 差异。

| 状态 | P0 | P1 | 说明 |
|------|:--:|:--:|------|
| byte-exact 匹配 | 28 | 27 | — |
| Tolerated (±1) | 1 | 2 | AGCO (两 Program 共通) + WDRC KP Threshold (P1 only) |
| 已知差异 | 2 | 2 | ENR NT (0x8040C2) + ENR UNT (0x8050C2), 根因: SNR_Frequency_Spacing 数组与 ENR 频段划分不一致, mic1Cal 差异 ≤3 暂时接受, 等待芯片端提供数组真值 |

---

## Step 6-7: 待细化

- Step 6: 信号发生器 (Input ToneGen `0x8001E2`, Tune Alerts `0x8xx2F2`, Stimulate `0x800012`)
- Step 7: 系统配置 (Device Config 2 packets + commit `0x800040`)

---

## Step 8: 目标语言代码生成

**前置条件**: Step 0-7 全部测试通过。

### 生成的 C 代码结构

```
sdk/apps/common/device/bs300/
├── bs300_types.h        # 类型、错误码、协议常量
├── bs300_proto.h/.c     # 帧构建/解析/校验和/读写流程
├── bs300_calib.h/.c     # 校准数据 3-packet 读写 + 解析
├── bs300_program.h/.c   # Program x Flash 读写 + bit-packed 编解码
├── bs300_wdrc.h/.c      # WDRC (14 子命令)
├── bs300_enr.h/.c       # ENR (10 子命令)
├── bs300_dfbc.h/.c      # DFBC (2 子命令)
├── bs300_iss.h/.c       # ISS (2 子命令)
├── bs300_wnr.h/.c       # WNR (5 子命令)
├── bs300_agco.h/.c      # AGCO (1 子命令)
├── bs300_ctrl.h/.c      # 基本控制 + 音量/蜂鸣
├── bs300_input.h/.c     # 输入源切换
└── bs300_config.h/.c    # 设备配置 + 系统配置
```

### 生成的 Java 代码结构

```
bs300/
├── Bs300Protocol.java      # 帧构建/校验和
├── Bs300CalibData.java     # 校准数据结构
├── Bs300Command.java       # 命令字枚举
├── Bs300Encoder.java       # 验配参数编码
└── Bs300Program.java       # Flash Program 编解码
```

---

## 验证方式

- Python 脚本已有的全部 test case assert
- 生成的 C 代码通过 `make all` 编译
- 生成的 Java 代码无编译错误
