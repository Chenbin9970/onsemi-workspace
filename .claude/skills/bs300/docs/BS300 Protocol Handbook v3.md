# BS300 Protocol Handbook v3

> **版本**: v3.6 (2026-05-28) | **合并来源**: 原始 `.doc` 提取 + 用户修正版 + v2 驱动参考补充 + cross-validation 修正
>
> 面向驱动开发者的完整参考手册。含 I2C 帧格式、命令全集、Flash/Param 双路径编码、C 代码骨架。
>
> **v3.6 新增**: 芯片 WDRC KP/Lmt Threshold 厂商公式 (float_32/int_16t → saturate int8_t) + byte 级 ±1 rounding tolerance 规则。AGCO ±1 归类为容忍。WDRC KP Threshold P1 (Front Mic) 全部 ±1 内。
> **v3.5 新增**: ENR Smoothing snasf 硬编码 4（芯片覆盖）+ AGCO 单位修正完成。待修正 2 条：ENR NT/UNT（芯片 SNR_Frequency_Spacing 数组不一致）。
> **v3.4 新增**: 23/31 命令逐字节匹配 + WDRC 2-band 校准平均发现 + 整数算术取整规则 (ceil/floor/banker's) + ENR 频率映射修正 + 芯片配置差异分类。
> **v3.3 新增**: KP/Lmt Threshold + ISS + ENR Freq Spacing 修正完成，27/31 匹配。
> **v3.2 新增**: Param I2C 交叉验证结果 — 18/31 命令逐字节匹配，包含已验证公式清单、待确认公式清单、校准值单位问题分析。
> **v3.1 新增**: Param 数据段字节排列约定 (Byte-Packing Rule) — 明确 int8/uint8 字段为连续字节排列, 不使用 word 对齐。

## 基础信息

| 项目 | 内容 |
|------|------|
| 协议名称 | BS300 (LHX Hearing Aid Communication Protocol) |
| 来源文档 | `docs/BS300 Communication Protocol Handbook.doc` |
| 物理层 | **I2C (IIC)** |
| 从机地址 | 7-bit `0b0000001` |
| 写入地址 | `0x02` |
| 读取地址 | `0x03` |
| 大小端 | **小端 (Little Endian)** — 所有多字节字段 LSB 在前 |
| 最大数据段 | 48 字节 (16 个 24-bit word) |

### 数据类型约定

| 类型 | 位宽 | 说明 |
|------|------|------|
| `uint8` / `uint16` / `uint24` | 8/16/24 | 无符号整数，小端序存储 |
| `int8` / `int16` / `int24` | 8/16/24 | 有符号整数 (2's complement)，小端序 |
| `frac16_t` | 16 | 定点小数: 1 sign + 15 frac bits，范围 [-1, 1)，`float = val / 2^15` |
| `frac24_t` | 24 | 定点小数: 1 sign + 23 frac bits，范围 [-1, 1)，`float = val / 2^23` |
| | | **编码**: `frac24_val = int(float_val × 0x7FFFFF) & 0xFFFFFF` (truncation, scale=2^23-1) |
| `frac48_t` | 48 | 定点小数: 1 sign + 47 frac bits，由两个 `uint24` 拼接 `{HIGH, LOW}` |
| `uint1` ~ `uint7` | 1-7 | bit-packed 子字段，在字节内与其他字段紧密排列 |

> **拼接记号**: `{B30[4:0], B29[7]}` = B30 的 bit4~bit0 为高 5-bit，B29 的 bit7 为低 1-bit。前面字节存低位、后面字节存高位。
> **位流方向**: 字节内 LSB→MSB (bit0→bit7)，跨字节连续，bit7 之后接下一字节 bit0。

---

## I2C 帧格式总览

一帧 I2C 传输由以下字段组成：

| 字段 | 长度(字节) | 说明 |
|------|-----------|------|
| Slave Address | 1 | `0x02 \| R/W-BIT` |
| Length Section | 0 或 1 | R/W-REQ 标志 + 数据段长度(triplet 数) |
| Command Section | 0 或 3 | 24-bit 命令字，小端序 |
| Data Section | 0 或 48 | 16 个 24-bit word，小端序，不足填充 0x00 |
| Checksum | 1 | 0xFF - (前序字节和的低 8 位) |

---

## 帧字段详解

### 1. Slave Address (1 byte)

| Bit | 名称 | 说明 |
|-----|------|------|
| 7:1 | HA-ADDR | 助听器从机地址，固定 `0b0000001` |
| 0 | R/W-BIT | 0 = Master Transmit (I2C Write), 1 = Master Receive (I2C Read) |

I2C 总线上的首字节总是 `0x02 | R/W-BIT`。

### 2. Length Section (1 byte)

| Bit | 名称 | 说明 |
|-----|------|------|
| 7 | R/W-REQ | 0 = 本帧数据段长度; 1 = 读请求(下一帧 I2C Read 的数据段长度) |
| 6:0 | Length Data | Data Section 的 triplet 数，取值 `0x00` 或 `0x10` |

Length Data 与 Data Section 字节数的换算：

```c
Length Data = Data Section 字节数 / 3

0x00 = 0/3 = 0  (无数据段)
0x10 = 48/3 = 16 (48 字节数据段)
```

> R/W-REQ=1 时，传输仅含 Slave Address + Length Section + Checksum，无 Command Section 和 Data Section。

### 3. Command Section (3 bytes, 24-bit word)

**I2C 传输顺序 (小端):**
1. Byte 0: bit 7-0 (LSB)
2. Byte 1: bit 15-8
3. Byte 2: bit 23-16 (MSB)

**24-bit 命令字位域:**

| Bit | 名称 | 说明 |
|-----|------|------|
| 23 | FURPROC | 进一步处理标志 |
| 22:16 | — | (待确认) |
| 15:12 | PKTNUM | 分包序号 0-15 |
| 11:5 | — | (待确认) |
| 4 | RDWRTBN | 0 = 烧录 EEPROM, 1 = 读写 RAM |
| 3:0 | — | (待确认) |

**FURPROC (bit23) 详解:**

| 值 | 说明 |
|----|------|
| 0x0 | 不需要进一步处理，Master 可进入下一命令 |
| 0x1 | 需要进一步处理，Master 应在 60ms 后重新查询状态 |

**PKTNUM (bit15:12):**
- 数据量 ≤ 48 字节时: PKTNUM = 0
- 数据量 > 48 字节时: 分包传输，PKTNUM 从 0 开始递增，最大 0xF (16 包)

**RDWRTBN (bit4):**

| 值 | 说明 |
|----|------|
| 0x0 | 数据写入 EEPROM (烧录) |
| 0x1 | 读写 RAM |

### 4. Data Section (0 或 48 bytes)

固定长度 48 字节，由 16 个 24-bit word 组成。每 word 3 字节，**小端序**排列：

| Byte 偏移 | 所属 Word | 说明 |
|-----------|----------|------|
| 0-2 | word 0 | byte0=LSB, byte1, byte2=MSB |
| 3-5 | word 1 | byte3=LSB, byte4, byte5=MSB |
| 6-8 | word 2 | byte6=LSB, byte7, byte8=MSB |
| ... | ... | ... |
| 45-47 | word 15 | byte45=LSB, byte46, byte47=MSB |

**Word N 重构公式:**
```c
word_N = (byte[N*3+2] << 16) | (byte[N*3+1] << 8) | byte[N*3]
```

**填充规则:** 有效数据不足 48 字节时，剩余字节填 `0x00`。

### 5. Checksum (1 byte)

**算法:**
```c
SUM = 对 Length Section + Command Section + Data Section 各字节求和
LastByte = SUM & 0xFF
Checksum = 0xFF - LastByte
```

**验算示例 (原文提供的标准示例):**

| 字段 | 十六进制 | 十进制 |
|------|---------|--------|
| Length Section | 0x10 | 16 |
| Command Section | 0x12 | 18 |
| | 0x10 | 16 |
| | 0x80 | 128 |
| Data Section | 0xD8 | 216 |
| | 0xF5 | 245 |
| | 0x03 | 3 |
| | 0xD8 | 216 |
| | 0xF5 | 245 |
| | 0x03 | 3 |
| (其余 42 字节) | 0x00 | 0 |

```c
SUM = 16+18+16+128+216+245+3+216+245+3 + 42×0 = 1106 = 0x452
LastByte = 0x452 & 0xFF = 0x52
Checksum = 0xFF - 0x52 = 0xAD
```

---

## I2C 命令类型

三种命令类型的帧组成各不相同：

| | Simple Command | Advanced Write | Advanced Read | Read Request |
|------|:-:|:-:|:-:|:-:|
| Slave Address | ✓ | ✓ | ✓ (I2C_READ) | ✓ |
| Length Section | ✓ | ✓ | — | ✓ |
| Command Section | ✓ | ✓ | ✓ | — |
| Data Section | — | ✓ (48B) | ✓ (48B) | — |
| Checksum | ✓ | ✓ | ✓ | ✓ |
| I2C 总线字节数 | **5** | **53** | **52** | **3** |

### Simple Command (简单命令)

- I2C Write 方向
- 仅发命令，不传输数据
- bit23 (FURPROC) 通常置 1，告知从机后续操作针对此命令

**示例 — Mute 命令 `0x800000`:**

```
I2C Write: 0x02 | 0x00 | 0x00 | 0x00 | 0x80 | 0x7F
            Addr   Len    Cmd_L  Cmd_M  Cmd_H  Chk
```

**示例 — Prepare Calibration Packet 0 `0x800051`:**

```
I2C Write: 0x02 | 0x00 | 0x51 | 0x00 | 0x80 | 0x2E
            Addr   Len    Cmd_L  Cmd_M  Cmd_H  Chk
```

### Read Request Command (读请求)

- I2C Write 方向
- 仅 3 字节: Slave Address + Length Section + Checksum
- R/W-REQ (bit7 of Length) 必须为 1
- Length Data 决定后续 I2C Read 返回的数据量

**Length=0x00 请求 (查询状态):**

```
I2C Write: 0x02 | 0x80 | 0x7F
            Addr   Len    Chk
```

Slave 在后续 I2C Read 返回 **4 字节**: Command Section(3B) + Checksum(1B)

**Length=0x10 请求 (读取数据):**

```
I2C Write: 0x02 | 0x90 | 0x6F
            Addr   Len    Chk
```

Slave 在后续 I2C Read 返回 **52 字节**: Command Section(3B) + Data Section(48B) + Checksum(1B)

### Advanced Write Command (高级写命令)

- I2C Write 方向
- 传输完整 53 字节帧: Addr + Len + Cmd(3B) + Data(48B) + Chk

### Advanced Read Command (高级读命令)

- I2C Read 方向
- 必须先发 Read Request (len=0x10)，再从从机读取 52 字节

---

## I2C 通信流程

### 写流程 (Write-oriented Communication)

```
1. Master → I2C Write: 发送 Simple Command 或 Advanced Write Command
2. 等待 60 ms
3. Master → I2C Write: 发送 Read Request (len=0x00)
4. Master → I2C Read: 读取 4 字节
5. 检查返回的 Command Section bit23:
   - bit23=1 → 从机未就绪 → 回到步骤 2
   - bit23=0 → 从机就绪 → 完成
```

### 读流程 (Read-oriented Communication)

```
1. Master → I2C Write: 发送 Simple Command (准备数据)
2. 等待 60 ms
3. Master → I2C Write: 发送 Read Request (len=0x00)
4. Master → I2C Read: 读取 4 字节
5. 检查返回的 Command Section bit23:
   - bit23=1 → 从机未就绪 → 回到步骤 2
   - bit23=0 → 从机就绪 → 继续步骤 6
6. 等待 60 ms
7. Master → I2C Write: 发送 Read Request (len=0x10)
8. Master → I2C Read: 读取 52 字节 → 完成
```

---

## I2C 时序参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 命令后等待 | **60 ms** | Simple/Advanced 命令发送后 |
| 状态轮询间隔 | **60 ms** | bit23=1 时重新查询的间隔 |
| 读请求后等待 | **60 ms** | Read Request 到 I2C Read 之间 |
| 重试条件 | bit23=1 | 从机 FURPROC 位置位时持续轮询 |

---

## 典型 I2C 通信示例

### 示例 1: 静音操作 (写流程)

```
Step 1: I2C Write Mute 命令
  0x02 0x00 0x00 0x00 0x80 0x7F

Step 2: 等待 60ms

Step 3: I2C Write Read Request (len=0x00)
  0x02 0x80 0x7F

Step 4: I2C Read 4 字节 (Master 读取 0x02|1)
  从机返回: 0x00 0x00 0x00 0xFF
           Cmd_L Cmd_M Cmd_H Chk
  
  bit23(0x00.bit7) = 0 → 就绪 → 完成
```

### 示例 2: 读取校准数据 (读流程)

```
Step 1: I2C Write Simple Command (准备校准包0)
  0x02 0x00 0x51 0x00 0x80 0x2E

Step 2: 等待 60ms

Step 3: I2C Write Read Request (len=0x00)
  0x02 0x80 0x7F

Step 4: I2C Read 4 字节
  从机返回: 0x51 0x00 0x00 0x?? → bit23=0 就绪

Step 5: 等待 60ms

Step 6: I2C Write Read Request (len=0x10)
  0x02 0x90 0x6F

Step 7: I2C Read 52 字节
  从机返回: [Command 3B] [Data 48B] [Checksum 1B]
```

---

## 命令分类速查

所有同步指令按功能分为 6 类，以下为快速索引。详细数据段布局见下方各节。

### 1. 保存到 Flash

与 EEPROM 持久化存储相关的读写操作。

| 数据对象 | 读命令 | 写命令 | 包数 | 详见 |
|---------|--------|--------|:--:|------|
| Calibration | `0x800051` / `0x801051` / `0x802051` | `0x800041` / `0x801041` / `0x802041` | 3 | §校准 (Calibration) |
| Device Config | `0x800130` / `0x801130` | `0x800120` / `0x801120` | 2 | §设备配置 (Device Configuration) |
| MDA | `0x800110` ~ `0x80F110` | `0x800100` ~ `0x80F100` | 16 | §MDA |
| Program Burn | `0x800011` ~ `0x809011` | `0x800001` ~ `0x809001` | 10 | §2.35 |
| Burn End | — | `0x80Y021` | — | §2.35 |
| Read Start | — | `0x80Y031` | — | §2.35 |
| Burn Voice Prompt | — | `0x801352` | 1 | §2.38 |
| Clear Voice Prompt | — | `0x804352` / `0x814352`~`0x8D4352` | ~14 | §2.39 |

### 2. 控制指令

触发芯片执行动作，多为 Simple Command，参数极少。

| 命令 | 命令字 | 类型 | 详见 |
|------|--------|------|------|
| Mute | `0x800000` | Simple | §基本控制 |
| Active | `0x800010` | Simple | §基本控制 |
| Is Connect | `0x800050` | Simple | §基本控制 |
| Key Lock | `0x801020` | Simple | §2.24 |
| Key Unlock | `0x800020` | Simple | §2.24 |
| ADM Release | `0x802022` | Simple | §2.22 |
| ADM Freeze | `0x801022` | Simple | §2.22 |
| Play Voice Prompt | `0x805352`~`0x8D5352` | Advanced | §2.36 |
| Verify Comm Code | `0x800030` | Advanced | §2.8 |
| Device Config Commit | `0x800040` | Simple | §设备配置 |

### 3. 系统配置指令

芯片硬件行为、人机交互、统计诊断，与音频算法链路无关。

| 模块 | 读命令 | 写命令 | 用途 | 详见 |
|------|--------|--------|------|------|
| Digital VC | `0x8002E2` | `0x8012E2` | 数字音量脉冲数 | §2.11 |
| Standby Mode | `0x801292` | `0x800292` | 待机超时/蜂鸣 | §2.12 |
| Rocker Switch | `0x8011A2` | `0x8001A2` | 摇杆模式/阈值 | §2.9 |
| Startup Delay | — | `0x800061` | 开机延时 | §2.29 |
| Global Profile | `0x800071` | — | 当前程序状态 | §2.30 |
| Usage | `0x801202` | `0x800202` | 使用时长/次数 | §2.13 |
| Battery Life | `0x801252` | `0x800252` | 电池更换/阈值 | §2.14 |
| Noise/Quiet | `0x801232` | `0x800232` | 噪声/安静统计 | §2.17 |
| Vol & Ambient Noise | `0x842272` / `0x882272` / `0x8C2272` | `0x801272` | 音量+环境噪声 | §2.15 |
| Volume Learning | `0x8203E2`~`0x8243E2` | `0x8003E2`~`0x8043E2` | 自动音量学习 | §2.16 |
| WDRC Acclimatization | — | `0x8022A2`~`0x8432A2` | WDRC 适应参数 | §2.28 |
| WDRC Acclim Logging | `0x8002A2` | `0x8012A2` | 适应进度记录 | §2.18 |
| Firmware Version | `0x800140` | — | 固件版本 | §2.42 |
| Voice Prompt Volume | `0x802352`~`0x8D2352` | `0x800352`~`0x8D0352` | 每段语音音量 | §2.37 |
| General Voice Prompt | `0x806352` | `0x807352` | 语音注入通路 | §2.40 |
| Voice Prompt Info | `0x803352` | — | 最大语音长度 | §2.41 |

### 4. 参数配置指令

音频 DSP 链路算法参数，与 §2.35 Program RAM 模块数据一一对应。

| 模块 | 写命令范围 | 说明 | 详见 | Program 顺序 |
|------|------|------|------|:--:|
| WDRC | `0x8000B2`~`0x80A0B2` (10W) + `0x8100B2`~`0x8130B2` (4R) | 宽动态范围压缩 | §2.34 | 1 |
| Volume, Beep & Input | `0x800081` | 蜂鸣/音量/输入选择 | §2.21 | 2 |
| Input Source | `0x800062` / `0x800022` / `0x804272` / `0x803022`(R) | MM+/DDM2/T-coil/DAI | §2.22 | 3 |
| DFBC | `0x800052` / `0x810052`(R) | 反馈消除 | §2.19 | 4 |
| ENR | `0x8000C2`~`0x8090C2` (10W) | 增强降噪 | §2.27 | 5 |
| Noise Gen 2 | `0x800172` | 测试噪声发生器 | §2.26 | 6 |
| ISS | `0x8001B2` / `0x8101B2`(R) | 冲击噪声抑制 | §2.20 | 7 |
| WNR | `0x8001C2`~`0x8021C2` (4W) + `0x8101C2`(R) | 风噪抑制 | §2.25 | 8 |
| AGCO | `0x800382` | 自动增益控制输出 | §2.23 | 10 |

### 5. 纯音测听

产生测试信号，用于验配听力评估。共用流程: Mute → 设参 → Active → 等待 → Mute → 停止。

| 命令 | 命令字 | 类型 | 详见 |
|------|--------|------|------|
| Stimulate Frequency | `0x800012` | Advanced | §刺激 (Stimulate) |
| Stimulate Comfort Level | `0x801012` | Advanced | §刺激 (Stimulate) |
| Input Tone Generator | `0x8001E2` | Advanced | §信号发生器 (Input Tone Generator) |

### 6. 提示音

告警与程序切换提示音的设定。

| 对象 | 读命令 | 写命令 | 详见 |
|------|--------|--------|------|
| Inject Position | `0xF802F2` | `0xFC02F2` | §2.10 |
| Battery Low | `0x8002F2` | `0x8012F2` | §2.10 |
| Battery Dead | `0x8022F2` | `0x8032F2` | §2.10 |
| Telecoil Tone | `0x8042F2` | `0x8052F2` | §2.10 |
| Powerup Delay | `0x8062F2` | `0x8072F2` | §2.10 |
| Powerup Ramp | `0x8082F2` | `0x8092F2` | §2.10 |
| Volume Down | `0x80A2F2` | `0x80B2F2` | §2.10 |
| Volume Up | `0x80C2F2` | `0x80D2F2` | §2.10 |
| Volume Min | `0x80E2F2` | `0x80F2F2` | §2.10 |
| Volume Max | `0x8102F2` | `0x8112F2` | §2.10 |
| Standby Tune | `0x8122F2` | `0x8132F2` | §2.10 |
| Program 1-6 | `0x8142F2`~`0x81F2F2` | `0x8152F2`~`0x81F2F2` | §2.10 |

---

## 命令速查表

### 基本控制

| 命令 | 命令字 | 类型 | 方向 |
|------|--------|------|------|
| Mute | `0x800000` | Simple | Write |
| Active | `0x800010` | Simple | Write |
| Is Connect | `0x800050` | Simple | Write |

### 校准 (Calibration) — 3 包

每个 packet 固定 **48 字节**，3 个 packet 共 **144 字节**，固定 8 个校准模块。

| 包 | 读命令 | 写命令 |
|----|--------|--------|
| Packet 0 | `0x800051` | `0x800041` |
| Packet 1 | `0x801051` | `0x801041` |
| Packet 2 | `0x802051` | `0x802041` |

**校准数据段结构:**

| 段序号 | 段名称 | 长度 |
|--------|--------|------|
| 1 | Packet information | 3 bytes |
| 2 | Module Calibration data information | 17 bytes (8×2 + 1)，末尾 `0xFB` 为信息段结束标记 |
| 3 | Module calibration data | 88 bytes (35 + 35 + 6×3) |
| 4 | Zero padding | `0x00` 填充至 48 字节 |

**Packet Information 段 (3 bytes):**

| Byte | 数据 | 说明 |
|------|------|------|
| Byte 0 | 数据包长度 | 校准数据烧录/读取数据包的数量 |
| Byte 1 | `0x00` | — |
| Byte 2 | 校准模块数 + 1 | — |

**模块校准信息排序 (固定顺序):**

| 序号 | 模块 | Index | 数据长度 |
|------|------|-------|---------|
| 1 | Mic1 calibration | 0x01 | 0x23 (35B) |
| 2 | Output calibration | 0x03 | 0x23 (35B) |
| 3 | Mic2 gain difference | 0x02 | 0x03 (3B) |
| 4 | Mic delay | 0x04 | 0x03 (3B) |
| 5 | Telecoil gain difference | 0x05 | 0x03 (3B) |
| 6 | DAI gain difference | 0x06 | 0x03 (3B) |
| 7 | FBC bulk delay | 0x07 | 0x03 (3B) |
| 8 | Digital audio sensitivity | 0x08 | 0x03 (3B) |

### 校准数据内容

#### Mic1 Calibration (35 bytes, header 0x02 0xFA 0x00)

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | Header | — | 固定 `0x02 0xFA 0x00` (250Hz 频率间隔) |
| 3-34 | mic1_band_0 ~ mic1_band_31 | uint8 × 32 | mic1_band_x = Output calibration - Gain calibration, 例 110-(-20)=130=0x82 |

> 注意: mic1_band_0 的数据不可用，使用 mic1_band_1 替代。

#### Output Calibration (35 bytes, header 0x01 0xFA 0x00)

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | Header | — | 固定 `0x01 0xFA 0x00` |
| 3-34 | op_band_0 ~ op_band_31 | uint8 × 32 | 范围 [0, 255]，例 110 = 0x6E |

> 注意: op_band_0 数据不可用，使用 op_band_1 替代。

#### 短校准模块 (3 bytes each, header=0x40)

| 模块 | Byte1-2 字段 | 类型 | 范围 | LSB |
|------|-------------|------|------|-----|
| Mic2 gain diff | cal_m2_gd | int16 | [-5.0, 5.0] dB | 0.1 dB |
| Mic delay | cal_mic_delay | uint16 | [5.0, 62.5] us (原始值 [50, 625]) | 0.1 us |
| Telecoil gain diff | cal_tc_gd | int16 | [-50.0, 50.0] dB | 0.1 dB |
| DAI gain diff | cal_dai_gd | int16 | [-50.0, 50.0] dB | 0.1 dB |
| FBC bulk delay | cal_fbc_bd | uint16 | [0, 32767] us (附注: 原始文档认为此上限值过大) | 1 us |
| Digital audio sensitivity | cal_das | int16 | [-16959, 16969] | 1 |

> **DAI/Telecoil gain difference 用途:** `cal_tc_gd` / `cal_dai_gd` 不仅用于 §2.22 Telecoil/DAI 命令本身，还用于 WDRC KP Threshold (减)、WDRC Bin Gain (加)、ISS (加)、MM Plus (减) 的公式补偿。详见 §2.22 Telecoil/DAI 的全局影响表。编码时注意此为 int16_t 原始值 (0.1 dB LSB)，使用时需 `/10` 转为 dB。

#### 3-Packet 逐字节布局

数据段按顺序连续排布，跨 packet 边界不中断：

```
总数据: 3 + 17 + 88 = 108 bytes
总空间: 3 × 48 = 144 bytes
零填充: 144 - 108 = 36 bytes (填 0x00)
```

**Packet 0** (读 `0x800051` / 写 `0x800041`):

| Byte | 所属段 | 内容 |
|------|--------|------|
| 0 | Packet info | 数据包长度 (=3) |
| 1 | Packet info | `0x00` |
| 2 | Packet info | 校准模块数 + 1 (=9) |
| 3 | Module info[0] | 模块索引 `0x01` (Mic1) |
| 4 | Module info[0] | 数据长度 `0x23` (35B) |
| 5 | Module info[1] | 模块索引 `0x03` (Output) |
| 6 | Module info[1] | 数据长度 `0x23` (35B) |
| 7 | Module info[2] | 模块索引 `0x02` (Mic2 gain diff) |
| 8 | Module info[2] | 数据长度 `0x03` (3B) |
| 9 | Module info[3] | 模块索引 `0x04` (Mic delay) |
| 10 | Module info[3] | 数据长度 `0x03` (3B) |
| 11 | Module info[4] | 模块索引 `0x05` (Telecoil gain diff) |
| 12 | Module info[4] | 数据长度 `0x03` (3B) |
| 13 | Module info[5] | 模块索引 `0x06` (DAI gain diff) |
| 14 | Module info[5] | 数据长度 `0x03` (3B) |
| 15 | Module info[6] | 模块索引 `0x07` (FBC bulk delay) |
| 16 | Module info[6] | 数据长度 `0x03` (3B) |
| 17 | Module info[7] | 模块索引 `0x08` (Digital audio sensitivity) |
| 18 | Module info[7] | 数据长度 `0x03` (3B) |
| 19 | Module info end | `0xFB` (信息段结束标记) |
| 20 | Mic1 header | `0x02` (固定头) |
| 21 | Mic1 header | `0xFA` (=250, 频率间隔) |
| 22 | Mic1 header | `0x00` |
| 23 | Mic1 data | mic1_band_0 |
| 24 | Mic1 data | mic1_band_1 |
| ... | ... | ... |
| 47 | Mic1 data | mic1_band_24 |

**Packet 1** (读 `0x801051` / 写 `0x801041`):

| Byte | 所属段 | 内容 |
|------|--------|------|
| 0 | Mic1 data | mic1_band_25 |
| ... | ... | ... |
| 6 | Mic1 data | mic1_band_31 |
| 7 | Output header | `0x01` (固定头) |
| 8 | Output header | `0xFA` (=250, 频率间隔) |
| 9 | Output header | `0x00` |
| 10 | Output data | op_band_0 |
| ... | ... | ... |
| 41 | Output data | op_band_31 |
| 42 | Short[0] Mic2 gd | `0x40` (头) |
| 43 | Short[0] Mic2 gd | cal_m2_gd [L] |
| 44 | Short[0] Mic2 gd | cal_m2_gd [H] |
| 45 | Short[1] Mic delay | `0x40` (头) |
| 46 | Short[1] Mic delay | cal_mic_delay [L] |
| 47 | Short[1] Mic delay | cal_mic_delay [H] |

**Packet 2** (读 `0x802051` / 写 `0x802041`):

| Byte | 所属段 | 内容 |
|------|--------|------|
| 0 | Short[2] Telecoil gd | `0x40` (头) |
| 1 | Short[2] Telecoil gd | cal_tc_gd [L] |
| 2 | Short[2] Telecoil gd | cal_tc_gd [H] |
| 3 | Short[3] DAI gd | `0x40` (头) |
| 4 | Short[3] DAI gd | cal_dai_gd [L] |
| 5 | Short[3] DAI gd | cal_dai_gd [H] |
| 6 | Short[4] FBC bd | `0x40` (头) |
| 7 | Short[4] FBC bd | cal_fbc_bd [L] |
| 8 | Short[4] FBC bd | cal_fbc_bd [H] |
| 9 | Short[5] DAS | `0x40` (头) |
| 10 | Short[5] DAS | cal_das [L] |
| 11 | Short[5] DAS | cal_das [H] |
| 12-47 | Zero padding | `0x00` × 36 |

### 刺激 (Stimulate)

| 命令 | 命令字 | 类型 |
|------|--------|------|
| Frequency | `0x800012` | Advanced Write |
| Comfort level | `0x801012` | Advanced Write |

**Frequency 数据段 (48 bytes):**

| Word | Byte | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 0-2 | stimulate_selection | uint24 | `0x000000`=disable, `0x000001`=enable |
| 1 | 3-5 | first_bin_index | uint24 | 起始频带索引 |
| 2 | 6-8 | total_bin_number | uint24 | 频带总数 |
| 3-15 | 9-47 | — | — | `0x00` 填充 |

**频率索引表:**

| 频率(Hz) | first_bin_index | total_bin_number | 实际中心频率(Hz) |
|----------|:-:|:-:|:-:|
| 125/250 | 0x000001 | 0x000001 | 250 |
| 500 | 0x000002 | 0x000001 | 500 |
| 750 | 0x000003 | 0x000001 | 750 |
| 1000 | 0x000004 | 0x000001 | 1000 |
| 1500 | 0x000006 | 0x000001 | 1500 |
| 2000 | 0x000008 | 0x000001 | 2000 |
| 3000 | 0x00000C | 0x000002 | 3125 |
| 4000 | 0x000010 | 0x000002 | 4125 |
| 6000 | 0x000017 | 0x000003 | 6000 |

**Comfort Level 计算公式:**
```c
data = 0x0475DC - Frequency_offset_data + (ComfortLevel - 10 - bin_Output_cal) × (65536 / 6.02)
```

**Frequency Offset Data:**

| total_bin_number | Offset |
|:-:|:-:|
| 0x000001 | 0x000000 |
| 0x000002 | 0x008003 |
| 0x000003 | 0x00CAE5 |

**刺激生成流程:**
1. Mute HA
2. 必要时读取 Output Calibration 数据
3. 确定频率、舒适度、时长
4. 计算 Comfort Level 数据
5. 发送 Comfort Level 命令
6. 发送 Frequency 命令 (selection=0x000001)
7. Active HA
8. 等待刺激时长
9. Mute HA
10. 发送 Frequency 命令 (selection=0x000000) 停止

### 信号发生器 (Input Tone Generator)

| 命令 | 命令字 | 类型 |
|------|--------|------|
| Input tone generator | `0x8001E2` | Advanced Write |

**数据段 (48 bytes, 单包):**

| Word | Byte | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 0-2 | itg_level | frac24 | 原始数据范围 `[0x000000, 0x7FFFFF]`，清除时 `= 0` |
| | | | | **生成转换:** `itg_level = 1 / 10^(((output_cal_band_x - gain_cal_band_x) - value) / 20)` |
| | | | | `value` = 目标输出 dB SPL [20, 90]；`output_cal_band_x - gain_cal_band_x` = mic1_band_x |
| | | | | **frac24 → float:** `float_val = frac24_val / 2^23`，`0x7FFFFF` ≈ 1.0 |
| 1 | 3-5 | itg_frequency | uint24 | 频率索引 (见下方频率索引表) |
| 2 | 6-8 | itg_selection | uint24 | `0x000001` = 生成, `0x000000` = 清除 |
| 3-15 | 9-47 | — | — | `0x00` 零填充 |

**频率索引 (250 Hz 步进, 0x01~0x1F):**

| 频率 (Hz) | 索引 | 频率 (Hz) | 索引 |
|-----------|------|-----------|------|
| 清除 | `0x000000` | 4000 | `0x000010` |
| 250 | `0x000001` | 4250 | `0x000011` |
| 500 | `0x000002` | 4500 | `0x000012` |
| 750 | `0x000003` | 4750 | `0x000013` |
| 1000 | `0x000004` | 5000 | `0x000014` |
| 1250 | `0x000005` | 5250 | `0x000015` |
| 1500 | `0x000006` | 5500 | `0x000016` |
| 1750 | `0x000007` | 5750 | `0x000017` |
| 2000 | `0x000008` | 6000 | `0x000018` |
| 2250 | `0x000009` | 6250 | `0x000019` |
| 2500 | `0x00000A` | 6500 | `0x00001A` |
| 2750 | `0x00000B` | 6750 | `0x00001B` |
| 3000 | `0x00000C` | 7000 | `0x00001C` |
| 3250 | `0x00000D` | 7250 | `0x00001D` |
| 3500 | `0x00000E` | 7500 | `0x00001E` |
| 3750 | `0x00000F` | 7750 | `0x00001F` |

**生成/清除操作流程:**

1. 使用命令 `0x8001E2` 写入 level、frequency、selection 信息到 HA
2. 检查连接
3. 必要时写入 program RAM 数据
4. Active HA
5. 等待所需持续时间
6. 检查连接
7. Mute HA
8. 使用 `0x8001E2` 清除 level、frequency、selection (全写 0)

### MDA (Multi-dimensional Audio) — 16 包

| 包 | 读命令 | 写命令 |
|----|--------|--------|
| 0 | `0x800110` | `0x800100` |
| 1 | `0x801110` | `0x801100` |
| ... | ... | ... |
| 15 | `0x80F110` | `0x80F100` |

MDA 数据段为 16×48=768 字节的连续数据，直接按 byte 索引 0-767 分包排列。

### 设备配置 (Device Configuration) — 2 包

| 包 | 读命令 | 写命令 |
|----|--------|--------|
| 0 | `0x800130` | `0x800120` |
| 1 | `0x801130` | `0x801120` |

> 注意: 写完设备配置后，需发送 Simple Write `0x800040`。

### Device Config 1 (0x800120) 关键字段

| Word | Byte | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 0-2 | Beeps program duration | uint24 | 范围 [0, 33554430], 1 lsb = 2ms |
| 1 | 3-5 | Beeps program config | uint24 | bit0=单响, bit1=使能, bit2=禁用音频 |
| 2 | 6-8 | Switch configuration | uint24 | 1=瞬时按钮 2=静态开关 3=混合... |
| 3 | 9-11 | Battery beep duration | uint24 | 1 lsb = 2ms, 范围 [0, 33554430] |
| 4 | 12-14 | Battery warning count | uint24 | Data = 输入值 |
| 5 | 15-17 | Battery warning threshold | uint24 | Value 0-80, `Data = Value × 1020 / 80` |
| 6 | 18-20 | Time below threshold | uint24 | Value 0-4100, `Data = Value × 64 / 4096` |
| 7 | 21-23 | Battery shutdown threshold | uint24 | 同 warning threshold 公式 |
| 8 | 24-26 | Time between checks | uint24 | `Data = Value × 10` |
| 9 | 27-29 | beeps_battery_data_2 | uint24 | `Data = duration × 0.04 × nbbs + 2` |
| 10 | 30-32 | Beeps before shutdown | uint24 | Data = Value |
| 11 | 33-35 | Security code | uint24 | Data = Value (写此字段需再发一个 Advanced Command) |
| 12 | 36-38 | Volume beep enable | uint24 | 0=Enable, 1=Disable |
| 13 | 39-41 | VC Mode | uint24 | 0=模拟 1=LSAD 2=GPIO按下 3=GPIO释放 4=LSAD释放 |
| 14 | 42-44 | Digital VC Step Size | uint24 | Value 1-6, `Data = Value × 65536 / 6.02` |
| 15 | 45-47 | VC volume preserved | uint24 | 0=Disable, 1=Enable |

### Device Config 2 (0x801120)

| Word | Byte | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 0-2 | Preserve Program Enable | uint24 | 0=Disable, 1=Enable |
| 1 | 3-5 | Single Button Mode | uint24 | 0=up/down, 1=up only, 2=down only |
| 2-15 | 6-47 | — | — | `0x00` 填充 |

---

### 2.8 Verify Communication Code (通信验证码)

| 命令 | 命令字 | 类型 | 方向 |
|------|--------|------|------|
| Verify communication code | `0x800030` | Advanced | Write |

验证码错误时，HA 在下次通信返回 `0xFFFFFF`。

**数据段 (48 bytes):**

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | security_code | uint24 | [0, 16777215] | Data = communication code, **大端序** (MSB-first). 例: `0x012958` → `[0]=0x01 [1]=0x29 [2]=0x58` |
| 3-47 | 0x00 | — | — | 填充 |

---

### 2.9 Rocker Switch (摇杆开关)

| 方向 | 命令字 | 类型 |
|------|--------|------|
| Read | `0x8011A2` | Advanced |
| Write | `0x8001A2` | Advanced |

> 写完需发 Simple Write `0x800091`。读前需 Simple Write `0x8000A1`。

**数据段 (48 bytes):**

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | rocker_switch_selection | uint24 | `0x000000`=Disable, `0x000001`=Enable |
| 3-5 | mode | uint24 | `0x000000`=VC short, program long; `0x000001`=VC long, program short; `0x000002`=VC short, program long on release (Program only) |
| 6-8 | threshold | uint24 | 范围 [1, 100]; mode≠Program only 时 Data=threshold MT 值; mode=Program only 时固定写 `0x7FFFFF` |
| 9-47 | 0x00 | — | 填充 |

---

### 2.10 Tune Alerts (告警音)

| 对象 | 读命令 | 写命令 | 类型 |
|------|--------|--------|------|
| Inject position | `0xF802F2` | `0xFC02F2` | Advanced |
| Battery low warning | `0x8002F2` | `0x8012F2` | Advanced |
| Battery dead | `0x8022F2` | `0x8032F2` | Advanced |
| Telecoil tone | `0x8042F2` | `0x8052F2` | Advanced |
| Powerup delay | `0x8062F2` | `0x8072F2` | Advanced |
| Powerup ramp | `0x8082F2` | `0x8092F2` | Advanced |
| Volume down | `0x80A2F2` | `0x80B2F2` | Advanced |
| Volume up | `0x80C2F2` | `0x80D2F2` | Advanced |
| Volume min | `0x80E2F2` | `0x80F2F2` | Advanced |
| Volume max | `0x8102F2` | `0x8112F2` | Advanced |
| Standby tune | `0x8122F2` | `0x8132F2` | Advanced |
| Program 1-6 | `0x8142F2`-`0x81F2F2` | `0x8152F2`-`0x81F2F2` | Advanced |

> 写完需发 Simple Write `0x800091`。读前需 Simple Write `0x8000A1` 和 `0x8000C1`。
>
> Telecoil tone 在 EZ5965 中已移除。读取时仅返回首个 24-bit word 的 Enable/Disable 标志。
>
> ⚠️ **分包机制**: Play 命令 = `0xFC000000 | write_command_data`，通过 `command_data | (packet_number << 18)` 分包 (packet 0/1/2)。

**Inject Position 数据段 (48 bytes):**

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | inject_tunes_at_input | uint24 | `0x000000`=Disable, `0x000001`=Enable |
| 3-47 | 0x00 | — | 填充 |

**主数据段 (144 bytes，分 3 包传输):**

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | tune_enabled + custom_id | bitfield | bit[3:0]=tune_enabled (1=enable per note); bit[23:4]=custom_id, Data=value |
| 3-5 | note_1 level | frac24 | 范围 [0, 150]; Data = 1/10^((output_cal - gain_cal - value)/20), 非 inject 时 gain_cal=0 |
| 6-8 | note_1 freq_cos | frac24 | 频率 [50, 7000]Hz; ((frac16_t)(cos(2π×freq/16000))) << 8 |
| 9-11 | note_1 freq_sin | frac24 | ((frac16_t)(sin(2π×freq/16000))) << 8 |
| 12-14 | note_1 duration | uint24 | 范围 [10, 10000]ms; 1 lsb = 2ms |
| 15-26 | note_2 | — | 同 note_1 布局: level(3B) + freq_cos(3B) + freq_sin(3B) + duration(3B) |
| 27-38 | note_3 | — | 同 note_1 布局 |
| 39-50 | note_4 | — | 同 note_1 布局 |
| 51-62 | note_5 | — | 同 note_1 布局 |
| 63-74 | note_6 | — | 同 note_1 布局 |
| 75-86 | note_7 | — | 同 note_1 布局 |
| 87-98 | note_8 | — | 同 note_1 布局 |
| 99-110 | note_9 | — | 同 note_1 布局 |
| 111-122 | note_10 | — | 同 note_1 布局 |
| 123-134 | note_11 | — | 同 note_1 布局 |
| 135-143 | 0x00 | — | 填充 |

> 未使用的 note 填 `0x00`。

---

### 2.11 Digital VC (数字音量控制)

| 读 | 写 | 类型 |
|----|----|------|
| `0x8002E2` | `0x8012E2` | Advanced |

**数据段 (48 bytes):**

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | pulses_per_step | uint24 | [1, 10] | Data = value - 1 |
| 3-47 | 0x00 | — | — | 填充 |

---

### 2.12 Standby Mode (待机模式)

| 读 | 写 | 类型 |
|----|----|------|
| `0x801292` | `0x800292` | Advanced |

**数据段 (48 bytes):**

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | standby_selection | uint24 | 0=Disable, 1=Enable | |
| 3-5 | time_threshold | uint24 | [1, 10]s, Data=[0x0A, 0x64] | 1 lsb = 100ms |
| 6-8 | number_of_beeps | uint24 | [1, 1000] | Data = value |
| 9-11 | beep_length | uint24 | [2, 20000]ms | 1 lsb = 2ms |
| 12-47 | 0x00 | — | — | 填充 |

---

### 2.13 Usage (使用统计)

| 读 | 写 | 类型 |
|----|----|------|
| `0x801202` | `0x800202` | Advanced |

> 写操作前需 Simple Write `0x8000B1`，写完需 `0x800091`。

**数据段 (48 bytes):**

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | usage_selection | uint24 | 0=Disable, 1=Enable | |
| 3-5 | total_time | uint24 | [0, 167772150]min | stepsize=10, Data=value/10 (整除), 1lsb=10min |
| 6-8 | activation_count | uint24 | [0, 16777215] | 1 lsb = 1 |
| 9-11 | time_in_prog1 | uint24 | [0, 167772150]min, stepsize=10, 1lsb=10min | |
| 12-14 | time_in_prog2 | uint24 | 同 time_in_prog1 | |
| 15-17 | time_in_prog3 | uint24 | 同 time_in_prog1 | |
| 18-20 | time_in_prog4 | uint24 | 同 time_in_prog1 | |
| 21-23 | time_in_prog5 | uint24 | 同 time_in_prog1 | |
| 24-26 | time_in_prog6 | uint24 | 同 time_in_prog1 | |
| 27-47 | 0x00 | — | — | 填充 |

---

### 2.14 Battery Life (电池寿命)

| 读 | 写 | 类型 |
|----|----|------|
| `0x801252` | `0x800252` | Advanced |

> 写操作前需 Simple Write `0x8000B1`，写完需 `0x800091`。

**数据段 (48 bytes):**

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | battery_life_sel | uint24 | 0=Disable, 1=Enable | |
| 3-5 | new_battery_threshold | uint24 | [0, 2000]mV | Data = threshold × 255 / 1000 (整除) |
| 6-8 | old_battery_threshold | uint24 | [0, 2000]mV | `= threshold × 255 / 1000` |
| 9-11 | battery_change_count | uint24 | [0, 16777215] | 1 lsb = 1 |
| 12-47 | 0x00 | — | — | 填充 |

---

### 2.15 Volume and Ambient Noise (音量与环境噪声)

| 对象 | 读 | 写 | 类型 |
|------|----|----|------|
| Selection | `0x800272` | `0x801272` | Advanced |
| Data Packet 0 | `0x842272` | — | Advanced Read |
| Data Packet 1 | `0x882272` | — | Advanced Read |
| Data Packet 2 | `0x8C2272` | — | Advanced Read |

**Selection 数据段 (48 bytes):**

| Byte | 字段 | 范围 |
|------|------|------|
| 0-2 | sel | 0=Disable, 1=Enable |
| 3-47 | 0x00 | 填充 |

**Data Packet 0 (Program 1 + Program 2 统计):**

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | p1_time_cnt | uint24 | 1 lsb = 10min |
| 3-5 | p1_integrated_volume | int24 | 1 lsb = 1dB |
| 6-8 | p1_integrated_ambient | int24 | avg = round((val/time_cnt)/(8/6.02)+70+avg(outCal-gainCal)-130) |
| 9-11 | p1_vc_mul_al [H] | uint24 | 48bit 乘积高 24bit |
| 12-14 | p1_vc_mul_al [L] | uint24 | 低 24bit |
| 15-17 | p1_al_mul_al [H] | uint24 | 48bit 乘积高 24bit |
| 18-20 | p1_al_mul_al [L] | uint24 | 低 24bit |
| 21-23 | p2_time_cnt | uint24 | 同 p1_time_cnt, Program 2 |
| 24-41 | ... p2 剩余字段 | — | 同 p1 布局 (integrated_volume + integrated_ambient + vc_mul_al H/L + al_mul_al H/L) |
| 42-47 | 0x00 | — | 填充 |

Packet 1 和 Packet 2 分别为 Program 3/4 和 Program 5/6，布局相同。

---

### 2.16 Volume Learning (音量学习)

| 对象 | 读 | 写 | 类型 |
|------|----|----|------|
| Selection | `0x8203E2` | `0x8003E2` | Advanced |
| Time constant | `0x8213E2` | `0x8013E2` | Advanced |
| Min % | `0x8223E2` | `0x8023E2` | Advanced |
| Max % | `0x8233E2` | `0x8033E2` | Advanced |
| Learned volume | `0x8243E2` | `0x8043E2` | Advanced |

> 写操作前需 Simple Write `0x8000B1`，写完需 `0x800091`。

**Selection (0x8003E2):** Byte 0-2 = 0/1 (Disable/Enable), 其余填充 0x00。

**Time Constant (0x8013E2), 6 个 Program 各 3 bytes:**

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | Prog1 TC | int24 | Data=(1-exp(-10/(value_in_MT+1/3600)))/2 |
| 3-5 | Prog2 TC | int24 | 同 Prog1 TC: `Data=(1-exp(-10/(value_in_MT+1/3600)))/2` |
| ... | ... (Prog3-6) | | |

**Min % / Max % (6 Program 各 3 bytes):** Data = value × 2^22 / 100 (整除)。

**Learned Volume (6 Program 各 3 bytes):** Data = Volume(dB) × 65536 / 6.02, 范围 [-48, 18] dB。

---

### 2.17 Noise/Quiet (噪声/安静统计)

| 读 | 写 | 类型 |
|----|----|------|
| `0x801232` | `0x800232` | Advanced |

| Byte | 字段 | 类型 | 范围 |
|------|------|------|------|
| 0-2 | selection | uint24 | 0=Disable, 1=Enable |
| 3-5 | noise_count | uint24 | [0, 0xFFFFFF] |
| 6-8 | quiet_count | uint24 | [0, 0xFFFFFF] |
| 9-47 | 0x00 | — | 填充 |

---

### 2.18 WDRC Acclimatization Logging (WDRC 适应日志)

> WDRC = Wide Dynamic Range Compression (宽动态范围压缩)

---

## DSP (数字信号处理器)

### 概述

本节描述设备中 DSP（数字信号处理器）相关的数据布局、与 Program RAM 的交互机制、常用参数的编码以及通过 I2C 更新/读取 DSP 参数的示例流程。DSP 参数以 Program RAM 的包（每包 48 字节）形式传输，算法链路（WDRC/DFBC/ENR/ISS/WNR 等）通过指定的命令范围写入/读取。

### Program RAM 与模块映射

- Program RAM 以 48 字节 packet 传输，连续包可构成更大参数块（例如 MDA、Calibration 等）。
- 常见 DSP 模块与命令范围（速查，同 §参数配置指令）:
  - WDRC: `0x8000B2` ~ `0x80A0B2` (写)
  - DFBC (反馈消除): `0x800052` / `0x810052` (读)
  - ENR (降噪增强): `0x8000C2` ~ `0x8090C2` (写)
  - ISS / WNR / AGCO 等: 参见命令速查表

### 数据编码与定点格式

- 音频增益/幅值常用 `frac16_t` / `frac24_t` 表示，见文档前述 "数据类型约定"。
- 多字节字段按小端（LSB 首）编码；每个 48B packet 内按 byte 索引从 0 到 47 排列。

### 常见操作流程（示例：写入 WDRC 参数）

1. 组装 Advanced Write 命令：SlaveAddr + Length(0x10) + Command(3B) + Data(48B) + Checksum。
2. Master → I2C Write 发送该 53 字节帧。
3. 等待 60 ms（DSP 内部处理）。
4. Master → I2C Write 发送 Read Request (len=0x00) 并 Read 4 字节确认 bit23=0（就绪）。

示例帧（伪示例）:

I2C Write: 0x02 | 0x10 | Cmd_L | Cmd_M | Cmd_H | [48 bytes data] | Chk

其中 `Cmd` 对应 `0x8000B2 | (packet_number << 18)`（分包时 packet_number 用于指定包序）。

### 示例：修改单一参数（步骤要点）

- 读取当前包（Read Request len=0x10 → I2C Read 52B）并解析目标字段偏移。
- 修改字段值并填充至 48B 数据段（其余字节保留原值或填 0）。
- 发送 Advanced Write（53B），等待 60ms，轮询 Read Request 确认。

### 校验与注意事项

- 写操作完成后若需要持久化，按相关模块要求再发送对应的 Commit/保存命令（例如 Device Config 之类的后续 Simple Write）。
- 对于跨包的参数（参数长度 >48B），务必按 packet 顺序逐包写入并使用 PKTNUM (Command 中的包号) 与分包机制。
- 修改 DSP 参数前建议先读取当前 Program RAM 包以避免意外覆盖。

---

（本节为 DSP 部分初稿，后续可补充具体 Program RAM 字段表、WDRC/DFBC/ENR 的逐字节布局与典型数值示例）

| 读 | 写 | 类型 |
|----|----|------|
| `0x8002A2` | `0x8012A2` | Advanced |

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | selection | uint24 | 0=Disable, 1=Enable | |
| 3-5 | adaption_time | frac24 | [600, 1677721600] | Data = fn_dec2hex(600/value, 'frac24') |
| 6-8 | current_acclim_level | frac24 | [0, 100] | Data = input / 100 |
| 9-47 | 0x00 | — | — | 填充 |

---

### 2.19 DFBC (Digital Feedback Cancellation, 反馈消除)

| 对象 | 读 | 写 | 类型 |
|------|----|----|------|
| Configure | — | `0x800052` | Advanced |
| Read Taps | `0x810052` | — | Advanced |

**Configure 数据段:**

| Byte | 字段 | 类型 | 范围 |
|------|------|------|------|
| 0-2 | dfbc_mode | uint24 | 0=Disable, 0x0F=FastStrong, 0x07=SlowStrong, 0x0B=FastWeak, 0x03=SlowWeak, 0x09=FastFBC, 0x01=SlowFBC |
| 3-5 | delay_n_sample | uint24 | max 524, round(bulk_delay_us/1e6/(1/16000)) |
| 6-47 | 0x00 | — | 填充 |

**Read Taps:** 96 bytes (0x60 = 32 taps × 3 bytes), 类型 frac24_t。

---

### 2.20 ISS (Impulsive Sound Suppression, 冲击噪声抑制)

| 对象 | 读 | 写 | 类型 |
|------|----|----|------|
| Configure | — | `0x8001B2` | Advanced |
| Display | `0x8101B2` | — | Advanced |

**Configure 数据段 (48 bytes):**

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | selection | uint24 | 0=Disable, 1=Enable | |
| 3-5 | threshold_low | frac48(L) | [50, 110] dB | `Threshold(Low) = Data & 0xFFFFFF` |
| 6-8 | threshold_high | frac48(H) | [50, 110] dB | `Threshold(High) = (Data >> 24) & 0xFFFFFF` |
| 9-47 | `0x00` | — | — | 零填充 |

> threshold_low 和 threshold_high 拼成一个 48-bit **frac48_t** 值 `Data`，转换公式按输入模式不同：

**mic1.cal.value 计算 (各输入模式共用):**

```c
mic1.cal.value = round( avg( 32 bands of (output_cal - gain_cal) ) )
```
即对 32 个频段的 `mic1_band_x` 取平均，**四舍五入取整**（v3.4 修正：原文档写 round_down，但交叉验证证实芯片实际用 round to nearest。`sum=4724` → `4724/32=147.625` → chip 用 `148` 非 `147`）。

**Threshold 转换公式 — 按输入模式:**

| 输入模式 | 公式 |
|----------|------|
| Mic (single/dual/directional) | `Data = 1 / 10^((-3 - threshold + mic1.cal.value) / 10)` |
| DAI / Telecoil / MM Plus | `Data = 1 / 10^((-3 - threshold + mic1.cal.value + input_gain_diff_dB) / 10)` |

> `threshold` = 目标阈值 dB SPL [50, 110]
> `input_gain_diff_dB` = `DAI_Telecoil_gain_diff / 10`，其中 `DAI_Telecoil_gain_diff` 为校准模块的 int16_t 原始值（`cal_tc_gd` 或 `cal_dai_gd`），单位 0.1 dB。Mic 输入时为 0。
> 完整影响范围及符号约定见 §2.22 Telecoil/DAI。公式编码实现见 `encode_param_iss()`。编码时注意: `input_gain_diff` 参数为 raw int16，需先 `/10` 转为 dB 再代入公式。
> **v3.4 舍入规则:** `frac48` 转换时须用 `round(frac_val × 2⁴⁷)` 而非 `int()` 截断。例: `0.1 × 2⁴⁷ = 14073748835532.8` → chip 用 `14073748835533 (=0xCCCCCCCCCCD)`，`int()` 截断会少 1 LSB。两处 round（mic1_cal 平均值 + frac48 转换）缺一不可，交叉验证已确认。

**Display (读 `0x8101B2`):**

| Byte | 字段 | 说明 |
|------|------|------|
| 0-2 | iss_gain | int24, 自上次读取以来的最大增益衰减量 (dB), 也可理解为冲击幅度 |
| 3-8 | — | TBD (保留) |
| 9-47 | `0x00` | 零填充 |

---

### 2.21 Volume, Beep and Input (音量/蜂鸣/输入)

| 写命令 | 类型 |
|--------|------|
| `0x800081` | Advanced |

**数据段 (48 bytes):**

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | beep_level | frac24 | [20, 140]dB | Data=1/10^((outCal-BeepLevel)/20) |
| 3-5 | beep_frequency | uint24 | 0x01-0x18 | 250-6000Hz (见频率表) |
| 6-8 | min_volume | int24 | [-48, 18]dB | Data = Vol×65536/6.02 |
| 9-11 | max_volume | int24 | [-48, 18]dB | `= Vol × 65536 / 6.02` |
| 12-14 | input_selection | uint24 | 0=FrontMic, 1=Telecoil, 2=DAI, 3=RearMic, 4=DualMic/DDM2, 5=MMPlusTelecoil, 6=MMPlusDAI |
| 15-17 | batt_flat_beep_freq | uint24 | 同 beep_frequency |
| 18-20 | batt_flat_beep_level | frac24 | 同 beep_level |
| 21-47 | 0x00 | — | 填充 |

---

### 2.22 Inputs (输入源)

| 对象 | 读 | 写 | 类型 |
|------|----|----|------|
| MM Plus | — | `0x800062` | Advanced |
| Telecoil/DAI | — | `0x804272` | Advanced |
| DDM2 | — | `0x800022` | Advanced |
| ADM Read Weighting | `0x803022` | — | Advanced |
| ADM Release | — | `0x802022` | Simple |
| ADM Freeze | — | `0x801022` | Simple |

#### MM Plus (0x800062)

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | selection | uint24 | 0=Disable, 1=Enable |
| 3-5 | mic_mixing_ratio | uint24 | 0=Disable, else: Data=524288×10^((MixRatio-inputGainDiff)/20) |
| 6-47 | 0x00 | — | 填充 |

#### Telecoil/DAI (0x804272)

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | gain_difference | int24 | Data=(GainDiff_dB × 2) × 65536 / 6.02，其中 GainDiff_dB = gain_difference_raw / 10 |
| 3-47 | 0x00 | — | 填充 |

> **数据来源:** `gain_difference_raw` 来自校准模块的 `cal_tc_gd`（Telecoil）或 `cal_dai_gd`（DAI），见 §2.3 校准数据内容。类型 int16_t，范围 [-50.0, 50.0] dB，LSB = 0.1 dB。encoder 侧 `gain_difference_raw` 即 `telecoil_gain_diff` 或 `dai_gain_diff` 字段的原始值。

##### DAI/Telecoil Gain Difference 全局影响

此值不仅用于 Telecoil/DAI Param 命令本身，还影响以下模块的编码公式（仅当输入类型为 Telecoil/DAI/MM Plus 时生效，Mic 输入时值 = 0）：

| 模块 | 命令字 | 公式中的用法 | 符号 | 说明 |
|------|--------|-------------|------|------|
| WDRC Knee Point Threshold | `0x8020B2` | `data = 60 + th - avg(mic1_band[fidx], mic1_band[fidx+1]) - input_gain_diff_dB` | **减** | §2.34 |
| WDRC Bin Gain | `0x8060B2` | `data = bin_gain - gain_cal + input_gain_diff_dB` | **加** | §2.34 |
| ISS Configure | `0x8001B2` | `data = 1/10^((-3-th+mic1.cal+input_gain_diff_dB)/10)` | **加** | §2.20 |
| MM Plus | `0x800062` | `data = 524288×10^((MixRatio-input_gain_diff_dB)/20)` | **减**（混音比内部） | §2.22 |

> `input_gain_diff_dB = gain_difference_raw / 10`。注意 KP Threshold 和 Bin Gain 的符号**相反**（KP 减，Bin Gain 加），原始文档 §2.34.4 和 §2.34.8 分别明确此差异。

> Limiter Threshold (`0x8070B2`) 不受 DAI/Telecoil gain difference 影响，公式仅用 `outCal`。

##### Param 数据段字节排列约定 (Byte-Packing Rule)

**核心规则**: Param I2C 命令的 Data Section (48 bytes) 中，不同数据类型有不同的排列方式：

| 数据类型 | 排列方式 | 每 word (3B) 存几个值 | 示例 |
|----------|----------|----------------------|------|
| **int8 / uint8** | **字节连续排列 (byte-packed)** | 3 个值跨 1 个 word，不满则跨 word 边界 | KP Threshold 2KP: 32B 连续填充, byte0=KP1_CH1, byte1=KP2_CH1, byte2=KP1_CH2... |
| int12 / uint12 | word 对齐，2 个值/word | 值 0 占 bits[11:0], 值 1 占 bits[23:12] | ENR SNR Threshold |
| frac24 / int24 / uint24 | word 对齐，1 个值/word | 每个值独占一个 word | AGCO threshold |
| frac48 | 跨 2 个 word | 低 24-bit 在前, 高 24-bit 在后 | ISS threshold |
| uint6 | word 对齐，4 个值/word | ch1[23:18], ch2[17:12], ch3[11:6], ch4[5:0] | Freq Spacing |

> **关键认知**: int8/uint8 字段**不使用** word 对齐 (≠ 2 per word, 第三个字节填 0)。数据是连续字节流，word 边界会在值中间穿过。
>
> **混淆来源**: Attack/Release/Ratio 的 2KP 模式下, 每 channel 恰好 3 个字节 (EPD+KP1+KP2), 16ch × 3 = 48 bytes, **刚好填满全部 48 字节**, 此时 byte-packing 和 word-packing 的输出**碰巧相同**。但 1KP 模式 (16ch × 2 = 32 bytes) 或 Limiter (16ch × 1 = 16 bytes) 下, 差异明显:
>
> ```
> 1KP Attack Time, 16ch: 共 32 个 uint8 值 [EPD_CH1, KP1_CH1, EPD_CH2, KP1_CH2, ...]
>
> 正确 (byte-packed):   byte0=EPD_CH1  byte1=KP1_CH1  byte2=EPD_CH2  byte3=KP1_CH2 ...
>                        word0 = {EPD_CH1, KP1_CH1, EPD_CH2}
>
> 错误 (word-aligned):  byte0=EPD_CH1  byte1=KP1_CH1  byte2=0x00      byte3=EPD_CH2 ...
>                        word0 = {EPD_CH1, KP1_CH1, 0x00}
> ```
>
> **代码实现**: 直接用 `data[offset + i] = value` 连续赋值，**不要**用 `_pack_uint8_2pw()` 或类似的 2-per-word 函数。
>
> **验证方法**: 芯片读回数据 (如 `param_commands_0.json`) 中, 对于 int8/uint8 字段, word 的 3 个字节**都应该有非零数据**, 而不是每隔 2 个字节出现一个 0x00。

#### DDM2 (0x800022)

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | selection | uint24 | 0=Disable, 1=Enable |
| 3-5 | open_ear_mode | uint24 | 0=Disable, 1=Enable |
| 6-8 | fixed_polar_pattern | frac24 | 0=Bi, 0x2=Hyper, 0x3=Super, 0x4=Cardioid, 0x7F=Omni |
| 9-11 | adm_fdm_sel | uint24 | 0=FDM, 1=ADM |
| 12-14 | mic2_dly_data | frac24 | =0.008×mic2_delay_us |
| 15-17 | mic2_cal_data | frac24 | =(10^(0.05×x))×0.5 |
| 18-20 | omni_threshold(H) | frac48 | [40,100]dB, data=2^47/10^(0.10001×(cal-val)-1.20412) |
| 21-23 | omni_threshold(L) | frac48 | `= 2^47 / 10^(0.10001×(cal-val)-1.20412)` |
| 24-26 | fixed 0x7F8000 | — | 必须写入 |
| 27-29 | fixed 0x7801FE | — | 必须写入 |
| 30-32 | flt_coef_b1 | frac24 | 高通滤波系数 |
| 33-35 | flt_coef_a1 | frac24 | |
| 36-38 | flt_coef_b0 | frac24 | |
| 39-47 | 0x00 | — | 填充 |

**fixed_polar_pattern 实际查表 (经验证, 2026-06-03):**

Flash 编码 (`bi[2:0]`) 为连续值，手册列出的 `0/2/3/4/0x7F` 有误:

| Flash uint3 | Mode | Param frac24 |
|-------------|------|-------------|
| 0b000 (0) | Bi-directional | `0x000000` |
| 0b001 (1) | Hyper-cardioid | `0x200000` |
| 0b010 (2) | Super-cardioid | `0x300000` |
| 0b011 (3) | Cardioid | `0x400000` |
| 0b100 (4) | Omni-directional | `0x7FFFFF` |
| ADM 模式 | (强制 Omni) | `0x7FFFFF` |

**omni_threshold 公式要点 (经验证, 2026-06-03):**

- `cal = avg(mic1_band[1:31])` (不是 `avg_output`)
- `value` = MT 格式: `(I2C_value - 2) / 4 + 40`
- `data = 2^47 / 10^(0.10001 * (avg_mic1 - value_mt) - 1.20412)` (手册标注 NOT VERY EXACT)
- 交叉验证数据: `data/ddm2_validation.json`

---

### 2.23 AGCO (Automatic Gain Control Output, 自动增益控制输出)

| 写命令 | 类型 |
|--------|------|
| `0x800382` | Advanced |

| Byte | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0-2 | agco_sel | uint24 | 0=Disable, 1=Enable | |
| 3-5 | agco_threshold | int24 | [0, -30]dB | Data=0xFA0000-abs(Threshold)×65536/6.02 |
| 6-8 | agco_attack_time | frac24 | [1, 2500] | data=1-exp(-16/(val/10000×16000)), **val 单位 0.1ms** |
| 9-11 | agco_release_time | frac24 | [1, 2500] | 同上，**val 单位 0.1ms** |
| 12-47 | 0x00 | — | 填充 |

---

### 2.24 Key Lock (按键锁)

| 命令 | 类型 |
|------|------|
| `0x801020` (Lock) | Simple |
| `0x800020` (Unlock) | Simple |

---

### 2.25 WNR (Wind Noise Reduction, 风噪抑制)

| 对象 | 读 | 写 | 类型 |
|------|----|----|------|
| WNR_1 (General) | — | `0x8001C2` | Advanced |
| WNR_2 (Bands 0-15) | — | `0x8011C2` | Advanced |
| WNR_3 (Bands 16-31) | — | `0x8411C2` | Advanced |
| Single mic detection | — | `0x8021C2` | Advanced |
| Display | `0x8101C2` | — | Advanced |

#### WNR_1 General Setup (0x8001C2)

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | selection | uint24 | bit0=enable, bit1=dual-mic(1=dual,0=single) |
| 3-5 | detection_threshold | int24 | =round(75-sum(outCal-gainCal)/32)×65536/6.02/8 |
| 6-8 | mic2_cal_data | frac24 | =1/10^(-x/20) |
| 9-11 | suppression_preset | uint24 | 0x03=preset1-4, 0x06=preset5 |
| 12-14 | fixed 0x001543 | — | |
| 15-17 | fixed 0x2AAAAB | — | |
| 18-20 | fixed 0x200000 | — | |
| 21-47 | 0x00 | — | 填充 |

#### WNR_2 / WNR_3 Band Data

每 band 3 bytes (int24_t)。WNR_2 = Bands 0-15, WNR_3 = Bands 16-31。

**Front Mic 模式:**
```c
band_N_WNR_data = 0x2A9764 - ((outCal-gainCal) × 2 - WNR_offset) × (65536 / 6.02)
```

**DAI / Telecoil 模式:**
```c
band_N_WNR_data = 0x2A9764 - ((outCal-gainCal + input_gain_diff_dB) × 2 - WNR_offset) × (65536 / 6.02)
```

> **单位转换**: `outCal-gainCal` = `output_calibration[band] - gain_calibration[band]` = `mic1_calibration[band]`（校准数据 uint8，直接相减即 dB 单位）。`input_gain_diff_dB` = 校准值 `telecoil_gain_diff` 或 `dai_gain_diff` ÷ 10（校准存储为 0.1 dB 单位，公式需 dB）。`WNR_offset` 来自 SSP 级别表，单位 dB。乘法因子 `65536/6.02` 将 dB 转为 int24 定点值。

#### WNR Single Mic Detection

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | band0_threshold | int24 | band0: 公式同 Band Data, 常量=3292041, offset=data2offset[0] |
| 3-5 | band1_threshold | int24 | band1: 同上, offset=data2offset[1] |
| 6-8 | band2_threshold | int24 | band2: 同上, offset=data2offset[2] |
| 9-47 | 0x00 | — | 填充 |

**计算公式 (仅 band 0-2):**

**Front Mic 模式:**
```c
data2 = 3292041 - ((outCal-gainCal) × 2 - data2offset) × (65536 / 6.02)
```

**DAI / Telecoil 模式:**
```c
data2 = 3292041 - ((outCal-gainCal + input_gain_diff_dB) × 2 - data2offset) × (65536 / 6.02)
```

> 双麦模式下不写此字段。`data2offset` 取值见下方 Data2Offset 参考表。`input_gain_diff_dB` 与 Band Data 公式中含义相同 (0.1 dB → dB)。

#### WNR Display

| Byte | 字段 | 说明 |
|------|------|------|
| 0-2 | wind_noise_detected | 0x01=检测到风噪, 0x00=未检测到 |
| 3-47 | 0x00 | 填充 |

#### WNR SSP Level / Data2Offset 参考表

| Band | ssp1 | ssp2 | ssp3 | ssp4 | ssp5 |
|------|------|------|------|------|------|
| 0 | 0 | -16 | -32 | -48 | -48 |
| 1 | 0 | -16 | -32 | -48 | -48 |
| 2 | 0 | -16 | -32 | -48 | -48 |
| 3 | 10 | -6 | -22 | -36 | -48 |
| 4 | 10 | -6 | -22 | -36 | -48 |
| 5 | 10 | -6 | -22 | -36 | -48 |
| 6-14 | -10 | -26 | -42 | -42 | -42 |
| 15-31 | -20 | -36 | -52 | -52 | -52 |

**Data2Offset 参考表 (仅 band 0-2，用于 Single Mic Detection):**

| Band | ssp1 | ssp2 | ssp3 | ssp4 | ssp5 |
|------|------|------|------|------|------|
| 0 | 0 | -8 | -16 | -26 | -34 |
| 1 | -8 | -16 | -24 | -32 | -40 |
| 2 | -40 | -50 | -58 | -68 | -78 |

> **SSP 级别映射**: ssp1=Minimal(1), ssp2=Low(2), ssp3=Medium(3), ssp4=Strong(4), ssp5=Maximum(5)。
> 注意: 括号内为 SSP 级别编号 (1-5)，**不等于** Flash Program Burn 路径的 WNR preset 字节值。Flash 路径的 preset 值映射见 §2.35 WNR 模块数据节。

---

### 2.26 Noise Gen 2 (噪声发生器 2)

| 写命令 | 类型 |
|--------|------|
| `0x800172` | Advanced |

**数据段 (48 bytes):**

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | feature_selection | uint24 | bit0=noiseGen2, bit1=muteExtAudio |
| 3-5 | nf1_b0 | frac24 | 滤波器 1 系数 b0 |
| 6-8 | nf1_b1 | frac24 | |
| 9-11 | nf1_b2 | frac24 | |
| 12-14 | nf1_a1 | frac24 | |
| 15-17 | nf1_a2 | frac24 | |
| 18-20 | fixed 0xFFFFFF | — | |
| 21-23 | nf2_b0 | frac24 | 滤波器 2 系数 b0 |
| 24-26 | nf2_b1 | frac24 | |
| 27-29 | nf2_b2 | frac24 | |
| 30-32 | nf2_a1 | frac24 | |
| 33-35 | nf2_a2 | frac24 | |
| 36-38 | fixed 0xFFFFFF | — | |
| 39-47 | 0x00 | — | 填充 |

---

### 2.27 ENR (Enhanced Noise Reduction, 增强降噪)

10 个子命令，全部 Advanced Write：

| 对象 | 命令字 | 说明 |
|------|--------|------|
| General Setup | `0x8000C2` | ENR 使能/通道数 |
| Frequency Spacing | `0x8010C2` | 每通道子带数 (6bit × 4CH/word) |
| SNR Threshold | `0x8020C2` | 每通道 SNR 阈值 (12bit × 2CH/word) |
| Max Attenuation Rate | `0x8030C2` | 每通道最大衰减率 (12bit × 2CH/word) |
| Noise Threshold | `0x8040C2` | 每通道下噪声阈值 (12bit × 2CH/word) |
| Upper Noise Threshold | `0x8050C2` | 每通道上噪声阈值 (12bit × 2CH/word) |
| Smoothing Factors | `0x8060C2` | 固定值 + 平滑因子 |
| Expansion Transition Ratio | `0x8070C2` | 每通道扩展过渡比 (frac24) |
| Noise Reduction Ratio | `0x8080C2` | 每通道降噪比 (frac24) |
| Speech Adaptive Smoothing | `0x8090C2` | 每通道语音自适应平滑 (frac24) |

**General Setup (0x8000C2):**

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | ENR selection | uint24 | 0=Disable, 1=Enable |
| 3-5 | total_channels | uint24 | [1, 16] |
| 6-8 | single_band_chs | uint24 | 频率 ≤ 125Hz 的 channel 数（即 freq_hz ≤ 125 的通道数） |
| 9-11 | multi_band_chs | uint24 | = total - single_band_chs |
| 12-47 | 0x00 | — | 填充 |

**Frequency Spacing (0x8010C2):** 每 word 打包 4 个 FS_CHx (各 6bit, uint6)。FS_CHx = channel x 的子带数（**直接存储，不-1**，与 WDRC MBC_CHx = bin_count-1 不同）。所有 FS_CHx 之和 = 32。

**v3.4 计算规则:** FS_CHx = 相邻 ENR channel 频率之间的校准 band 数。
- 对于非末尾 channel i: `FS_CHx = freq_to_index(ch[i+1].freq_hz) - freq_to_index(ch[i].freq_hz)`
- 对于末尾 channel: `FS_CHx = 32 - freq_to_index(ch[last].freq_hz)`

**v3.4 交叉验证示例** (当前芯片, 16ch):
```
ENR freqs: [0, 125, 375, 875, 1375, 1875, 2375, 2875, 3375, 3875, 4375, 4875, 5375, 5875, 6375, 6875] Hz
Band idxs: [0,   1,   2,   4,    6,    8,   10,   12,   14,   16,   18,   20,   22,   24,   26,   28]
FS_CHx  : [1,   1,   2,   2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    4]
Sum = 1+1+13×2+4 = 32 ✓
```
- 未使用的 slot 填 `0`（非 WDRC 的 `0b000001`），多余 word 填 `0x000000`

**word 内 bit 布局 (FS_CH1 在高位):**
```
word = {FS_CH1[23:18], FS_CH2[17:12], FS_CH3[11:6], FS_CH4[5:0]}
```
即字节 0[7:2]=FS_CH1, 字节 0[1:0]+字节 1[7:4]=FS_CH2, 字节 1[3:0]+字节 2[7:6]=FS_CH3, 字节 2[5:0]=FS_CH4。

**SNR Threshold (0x8020C2):** 每 word 打包 2 个通道 (各 12bit, int12)。范围 [4, 30]dB。`SNRT_CHx = floor(32/6.02 × value)`。

**Max Attenuation Rate (0x8030C2):** 每 word 打包 2 个通道 (各 12bit, int12)。范围 [0, 30]dB。`MAR_CHx = floor((max_att/SNR_threshold) × 256)`。

**Noise Threshold (0x8040C2):** 每 word 打包 2 个通道 (各 12bit, int12)。范围 [10, 72]dB SPL。
```
NT_CHx = round(5.307 * (x + 130 - mic1Cal - input_gain_diff_dB) - 371.2)
```
其中 `x = noise_th_db`（value_in_MT），`input_gain_diff_dB` 由输入类型决定。

**`mic1Cal` 计算方式 (v3.6):** 从 ENR 频段划分数据推算 `SNR_Frequency_Spacing`：
- `start_idx = freq_to_index(ch[i].freq_hz)` — ENR 频率对应的校准 band 索引
- `cnt = FS_CHx[i]` — ENR Frequency Spacing 中定义的 band 数
- `mic1Cal = (sum(mic1_cal[start_idx .. start_idx+cnt-1]) * 10 // cnt) // 10` — 整数截断（C 整数运算链语义）

即 ENR 频段划分命令 (0x8010C2) 中的 `FS_CHx` 同时也是 NT 计算中 `SNR_Frequency_Spacing` 的 `cnt` 参数。两者共用同一套 band 分区。

> **v3.6 已知问题**: 按上述方式（ENR 频段划分 band 范围平均 mic1 校准值）仅 **6/16** channel 匹配芯片，具体见 `ENR_NT_Analysis.md`。根因是芯片内部的 `SNR_Frequency_Spacing` 起始索引与 ENR 频段划分的 band start 不完全一致。

**Upper Noise Threshold (0x8050C2):** 每 word 打包 2 个通道 (各 12bit, int12)。范围 [40, 102]dB SPL。公式同 NT_CHx。**v3.6 已知问题同 NT_CHx。**

**Smoothing Factors (0x8060C2):**

| Byte | 字段 | 类型 | 范围 |
|------|------|------|------|
| 0-2 | fixed 0x200000 | — | |
| 3-5 | fixed 0x600000 | — | |
| 6-8 | fixed 0x100000 | — | |
| 9-11 | fixed 0x700000 | — | |
| 12-14 | fixed 0x020000 | — | |
| 15-17 | fixed 0x7E0000 | — | |
| 18-20 | noise_hold_sf1 | uint24 | [1,16] |
| 21-23 | noise_hold_sf2 | uint24 | |
| 24-26 | noise_falling_sf1 | uint24 | [1,16] |
| 27-29 | noise_falling_sf2 | uint24 | |
| 30-32 | noise_normal_sf1 | uint24 | [1,16] |
| 33-35 | noise_normal_sf2 | uint24 | |
| 36-38 | fixed 0x000400 | — | |
| 39-41 | speech_nonadap_sf1 | uint24 | [1,16] |
| 42-44 | speech_nonadap_sf2 | uint24 | |
| 45-47 | fixed 0x000000 | — | |

平滑因子计算: `data1 = 1 << (23 - value)` (value≥2), `data2 = 0x7FFFFF - data1 + 1`。

**Expansion Transition Ratio (0x8070C2):** 每通道 1 word (frac24_t)。16 通道 = 16 words，word 0-15 依次对应 CH0-CH15。
```c
ETR_CHx = frac24(6.02 / 32 × (1 - 1 / ratio) / MaxAttenuation)
```
> `ratio` = `exp_trans_ratio`（value_in_MT 字段，如 0.7）。`MaxAttenuation` = `max_att_db`（value_in_MT 字段，如 9）。比值无单位，结果 ≤0 (扩展过渡比为负值)。`frac24(val) = int(val × 0x7FFFFF) & 0xFFFFFF`。

**Noise Reduction Ratio (0x8080C2):** 每通道 1 word (frac24_t)。16 通道 = 16 words。
```c
NRR_CHx = frac24(6.02 / 32 × ratio / MaxAttenuation)
```
> `ratio` = `noise_red_ratio`（value_in_MT 字段，如 0.3）。结果为正小数。

**Speech Adaptive Smoothing (0x8090C2):** 每通道 1 word (frac24_t)。16 通道 = 16 words。存平滑因子 frac24 值。

---

### 2.28 WDRC Acclimatization (WDRC 适应)

| 对象 | 写命令 | 类型 |
|------|--------|------|
| General Setup | `0x8022A2` | Advanced |
| Max Gain Deltas pkt0 (SB0-15) | `0x8032A2` | Advanced |
| Max Gain Deltas pkt1 (SB16-31) | `0x8432A2` | Advanced |
| Compression Ratio Deltas | `0x8042A2` | Advanced |

**General Setup (0x8022A2):**

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | selection | uint24 | 0=Disable, 1=Enable |
| 3-5 | fixed_mode_sel | uint24 | 0=Disable, 1=Enable |
| 6-8 | fixed_mode_pct | frac24 | [0,100], Data=value/100 |
| 9-11 | cr_to_acclimatize | uint24 | 0=CR0, 1=CR1, 2=CR2 |
| 12-47 | 0x00 | — | 填充 |

**Max Gain Deltas:** 每 subband 3 bytes (int24)，范围 [-30, 30]，`Data = round(value × 65536/6.02)`。Packet0 = SB0-15, Packet1 = SB16-31。

**Compression Ratio Deltas:** 每 channel 3 bytes (frac24)，`CRD_CHx = 1/8 × value/(value+2) × 2^23`。

---

### 2.29 Startup Delay (开机延时)

| 写命令 | 类型 |
|--------|------|
| `0x800061` | Advanced |

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-5 | 0x000000 | — | 保留 |
| 6-8 | startup_delay | uint24 | [0, 999]s, Data=value×100, 1lsb=10ms |
| 9-47 | 0x00 | — | 填充 |

---

### 2.30 Global Profile (全局配置文件)

| 读命令 | 类型 |
|--------|------|
| `0x800071` | Advanced |

| Byte | 字段 | 范围 |
|------|------|------|
| 0-2 | program_active | bit0-3=prog0-3, 1=enable |
| 3-5 | auto_switch_program | 0-based index |
| 6-8 | startup_delay | uint24 | [0, 999]s, Data=value×100, 1lsb=10ms |
| 9-47 | 0x00 | 填充 |

---

---

### 2.34 WDRC (宽动态范围压缩)

| 对象 | 命令字 | 类型 |
|------|--------|------|
| General Setup | `0x8000B2` | Advanced Write |
| Frequency Spacing | `0x8010B2` | Advanced Write |
| Knee Point Threshold | `0x8020B2` | Advanced Write |
| Knee Point Attack Time | `0x8030B2` | Advanced Write |
| Knee Point Release Time | `0x8040B2` | Advanced Write |
| Ratio | `0x8050B2` | Advanced Write |
| Bin Gain | `0x8060B2` | Advanced Write |
| Limiter Threshold | `0x8070B2` | Advanced Write |
| Limiter Attack Time | `0x8080B2` | Advanced Write |
| Limiter Release Time | `0x8090B2` | Advanced Write |
| Limiter Ratio | `0x80A0B2` | Advanced Write |
| Input Display | `0x8100B2` | Advanced Read |
| 2cc Gain Display | `0x8110B2` | Advanced Read |
| Output Display | `0x8120B2` | Advanced Read |
| Channel I/O Display | `0x8130B2` | Advanced Read |

#### General Setup (0x8000B2)

| Byte | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0-2 | selection | uint24 | 固定 0x000001 |
| 3-5 | total_channels | uint24 | [1, 16] |
| 6-8 | NSBC (single-band) | uint24 | |
| 9-11 | NMBC (multi-band) | uint24 | = total - NSBC |
| 12-14 | knee_points_per_ch | uint24 | 2=1KP, 3=2KP |
| 15-17 | output_limiting_sel | uint24 | 0=Disable, 1=Enable |
| 18-47 | 0x00 | — | 填充 |

#### Frequency Spacing (0x8010B2)

**MBC_CHx 计算规则:**
1. 由 NMBC (multi-band channel 数量) 决定使用几个 MBC_CHx
2. x 越小对应低频 channel，x 越大对应高频 channel
3. `MBC_CHx = (该 multi-band channel 内的 bin 数) - 1`，bin 数至少 2，所以 MBC_CHx ≥ 1
4. 未使用的 slot（含单带 channel NOBC_CHx 及超出 NMBC 的空位）填充 `0b000001` (= 1)
5. 16 个 slot 全部显式填充后，超出 4 个 word 的部分（word 4-15）填 `0x041041`

**打包格式:** 每 word 4 个 MBC_CHx (各 6bit)，word 内 ch1 在 MSB (bits 23:18)，ch4 在 LSB (bits 5:0):
- Word 0 (bytes 0-2): MBC_CH1-4
- Word 1 (bytes 3-5): MBC_CH5-8
- Word 2 (bytes 6-8): MBC_CH9-12
- Word 3 (bytes 9-11): MBC_CH13-16
- Word 4-15 (bytes 12-47): `0x041041` (全 16 slot 已填满，剩余 word 固定值)

**示例:** NMBC=6, 每 channel 4 bin → `mbc_counts = [4,4,4,4,4,4]`
- 16 个 6-bit slot: `[3,3,3,3,3,3, 1,1,1,1,1,1,1,1,1,1]` (前 6 个 = 4-1=3, 后 10 个 = 0b000001)
- Word 0 = `0x0C30C3` (ch1-4: 3,3,3,3)
- Word 1 = `0xC3041` (ch5-6: 3,3; ch7-8 NOBC: 1,1)
- Word 2-15 = `0x041041`

#### Knee Point Threshold (0x8020B2)

**1KP 模式:** 16 channel 各 1 byte int8，范围 [0, 127] dB SPL。**byte-packed**: 16 bytes 连续填充 (byte0=CH1, byte1=CH2, ..., byte15=CH16), bytes 16-47 填 0x00。
**2KP 模式:** 32 bytes 交替 KP1/KP2 × 16 channel。**byte-packed**: 32 bytes 连续填充 (byte0=KP1_CH1, byte1=KP2_CH1, byte2=KP1_CH2, ..., byte31=KP2_CH16), bytes 32-47 填 0x00。

**计算公式 (v3.6 修正 — 芯片运算类型)**:

芯片内部流程 (厂商确认):
```
1. data = 60 + threshold – average of (output.cal.value – gain.cal.value);
   对 Mic 输入: 计算结束
   对 DAI/Telecoil/MM Plus 输入: data -= DAI/telecoil gain difference
   运算类型: float_32, float_64 或 int_16t (芯片可选用任一种)

2. saturate data into the int8_t range

3. 1 lsb = 1 dB SPL
```

其中 `output.cal.value – gain.cal.value` 即 `mic1_band`（校准表已存储此差值）。WDRC 每个 channel 覆盖 2 个校准 band，故取两个 band 的整数平均。`fidx` 通过 `_freq_to_index(freq_hz)` 查表获取。

`input_gain_diff_dB = gain_difference_raw / 10`（gain_difference_raw 为 Telecoil/DAI 模块的 int16 原始值，单位 0.1 dB）。

Python 端简化为:
```
data = 60 + threshold - avg(mic1_band[fidx], mic1_band[fidx+1]) - input_gain_diff_dB
avg = (mic1_band[fidx] + mic1_band[fidx+1]) // 2  (floor)
```

**±1 容忍规则 (v3.6):** 芯片使用 float_32/int_16t 运算后 saturate 到 int8_t，不同运算类型会导致 byte 级 ±1 差异。已验证 float32/float64/int16 三种精度各有不同的取整行为，无一能完全匹配 Python 整数 floor。**byte 级差异 ≤ ±1 视为 rounding tolerance，不阻塞验证通过。**

**v3.4 校准补偿 (cal_offset) — 已废弃，由 ±1 容忍规则替代:**
- 此前 cal_offset[3]=1, cal_offset[6]=1 (P0 TC) 可以消除差异，但 P1 FM 需要不同的 offset
- v3.6 将此归类为 rounding tolerance，不再使用 per-channel cal_offset
- 若需强制 byte-exact，仍可使用 cal_offset 参数

#### Attack / Release Times (0x8030B2 / 0x8040B2)

每个 channel 存 1 byte uint8 索引值，通过 Table 2-2 查找对应时间。索引范围 [0, 121]，共 122 个条目。

**Table 2-2: Attack/Release Time Lookup (data → time)**

| 数据 | 时间(ms) | 数据 | 时间(ms) | 数据 | 时间(ms) |
|------|---------|------|---------|------|---------|
| 0 | 0 | 41 | 80 | 82 | 1200 |
| 1 | 1 | 42 | 85 | 83 | 1300 |
| 2 | 2 | 43 | 90 | 84 | 1400 |
| 3 | 3 | 44 | 95 | 85 | 1500 |
| 4 | 4 | 45 | 100 | 86 | 1600 |
| 5 | 5 | 46 | 110 | 87 | 1700 |
| 6 | 6 | 47 | 120 | 88 | 1800 |
| 7 | 7 | 48 | 130 | 89 | 1900 |
| 8 | 8 | 49 | 140 | 90 | 2000 |
| 9 | 9 | 50 | 150 | 91 | 2200 |
| 10 | 10 | 51 | 160 | 92 | 2500 |
| 11 | 11 | 52 | 170 | 93 | 2600 |
| 12 | 12 | 53 | 180 | 94 | 2800 |
| 13 | 13 | 54 | 190 | 95 | 3000 |
| 14 | 14 | 55 | 200 | 96 | 3200 |
| 15 | 15 | 56 | 220 | 97 | 3400 |
| 16 | 16 | 57 | 240 | 98 | 3600 |
| 17 | 17 | 58 | 260 | 99 | 3800 |
| 18 | 18 | 59 | 280 | 100 | 4000 |
| 19 | 19 | 60 | 300 | 101 | 4200 |
| 20 | 20 | 61 | 320 | 102 | 4400 |
| 21 | 22 | 62 | 340 | 103 | 4600 |
| 22 | 24 | 63 | 360 | 104 | 4800 |
| 23 | 26 | 64 | 380 | 105 | 5000 |
| 24 | 28 | 65 | 400 | 106 | 5500 |
| 25 | 30 | 66 | 420 | 107 | 6000 |
| 26 | 32 | 67 | 440 | 108 | 6500 |
| 27 | 34 | 68 | 460 | 109 | 7000 |
| 28 | 36 | 69 | 480 | 110 | 7500 |
| 29 | 38 | 70 | 500 | 111 | 8000 |
| 30 | 40 | 71 | 550 | 112 | 8500 |
| 31 | 42 | 72 | 600 | 113 | 9000 |
| 32 | 44 | 73 | 650 | 114 | 9500 |
| 33 | 46 | 74 | 700 | 115 | 10000 |
| 34 | 48 | 75 | 750 | 116 | 11000 |
| 35 | 50 | 76 | 800 | 117 | 12000 |
| 36 | 55 | 77 | 850 | 118 | 13000 |
| 37 | 60 | 78 | 900 | 119 | 14000 |
| 38 | 65 | 79 | 950 | 120 | 15000 |
| 39 | 70 | 80 | 1000 | 121 | 16000 |
| 40 | 75 | 81 | 1100 | — | — |

**数据段布局 (1KP 模式, 32 bytes):**

| Byte | 字段 | 说明 |
|------|------|------|
| 0 | EPDRT_CH1 | WDRC expander release time, channel 1 |
| 1 | KP1RT_CH1 | WDRC kneepoint 1 release time, channel 1 |
| 2 | EPDRT_CH2 | channel 2 |
| 3 | KP1RT_CH2 | |
| ... | ... | (CH1–CH16 交替) |
| 30 | EPDRT_CH16 | channel 16 |
| 31 | KP1RT_CH16 | |
| 32-47 | 0x00 | 填充 |

> **byte-packed**: 上表是**连续字节排列**，不是 word 对齐。word0 的 3 个字节 = {EPDRT_CH1, KP1RT_CH1, EPDRT_CH2}，word 边界从值中间穿过。2KP 模式下 16ch×3=48B 刚好填满，word 边界自然对齐。

**数据段布局 (2KP 模式, 48 bytes):**

| Byte | 字段 | 说明 |
|------|------|------|
| 0 | EPDRT_CH1 | WDRC expander release time, channel 1 |
| 1 | KP1RT_CH1 | WDRC kneepoint 1 release time, channel 1 |
| 2 | KP2RT_CH1 | WDRC kneepoint 2 release time, channel 1 |
| 3 | EPDRT_CH2 | channel 2 |
| 4 | KP1RT_CH2 | |
| 5 | KP2RT_CH2 | |
| ... | ... | (CH1–CH16 交替) |
| 45 | EPDRT_CH16 | channel 16 |
| 46 | KP1RT_CH16 | |
| 47 | KP2RT_CH16 | |

> **Attack Time (0x8030B2):** 字段名 `EPDAT_CHx`、`KP1AT_CHx`、`KP2AT_CHx`，数据段布局同上，同样查 Table 2-2。

#### Ratio (0x8050B2)

使用预定义比率查找表 (Table 2-3) uint8，共 **128 条目** (index 0-127)，完整数据来自 `BS300 Communication Protocol Handbook.doc`:

**Table 2-3: WDRC Ratio Lookup (data → ratio)**

| data | ratio | data | ratio | data | ratio | data | ratio |
|------|-------|------|-------|------|-------|------|-------|
| 0 | 0.000 | 32 | 1.000 | 64 | 1.471 | 96 | 2.778 |
| 1 | 0.220 | 33 | 1.010 | 65 | 1.493 | 97 | 2.857 |
| 2 | 0.250 | 34 | 1.020 | 66 | 1.515 | 98 | 2.941 |
| 3 | 0.280 | 35 | 1.031 | 67 | 1.538 | 99 | 3.030 |
| 4 | 0.300 | 36 | 1.042 | 68 | 1.563 | 100 | 3.125 |
| 5 | 0.320 | 37 | 1.053 | 69 | 1.587 | 101 | 3.226 |
| 6 | 0.350 | 38 | 1.064 | 70 | 1.613 | 102 | 3.333 |
| 7 | 0.380 | 39 | 1.075 | 71 | 1.639 | 103 | 3.448 |
| 8 | 0.400 | 40 | 1.087 | 72 | 1.667 | 104 | 3.571 |
| 9 | 0.430 | 41 | 1.099 | 73 | 1.695 | 105 | 3.704 |
| 10 | 0.450 | 42 | 1.111 | 74 | 1.724 | 106 | 3.846 |
| 11 | 0.470 | 43 | 1.124 | 75 | 1.754 | 107 | 4.000 |
| 12 | 0.500 | 44 | 1.136 | 76 | 1.786 | 108 | 4.167 |
| 13 | 0.520 | 45 | 1.149 | 77 | 1.818 | 109 | 4.348 |
| 14 | 0.550 | 46 | 1.163 | 78 | 1.852 | 110 | 4.545 |
| 15 | 0.570 | 47 | 1.176 | 79 | 1.887 | 111 | 4.762 |
| 16 | 0.600 | 48 | 1.190 | 80 | 1.923 | 112 | 5.000 |
| 17 | 0.630 | 49 | 1.205 | 81 | 1.961 | 113 | 5.260 |
| 18 | 0.650 | 50 | 1.220 | 82 | 2.000 | 114 | 5.560 |
| 19 | 0.680 | 51 | 1.235 | 83 | 2.041 | 115 | 5.880 |
| 20 | 0.700 | 52 | 1.250 | 84 | 2.083 | 116 | 6.250 |
| 21 | 0.730 | 53 | 1.266 | 85 | 2.128 | 117 | 6.670 |
| 22 | 0.750 | 54 | 1.282 | 86 | 2.174 | 118 | 7.140 |
| 23 | 0.770 | 55 | 1.299 | 87 | 2.222 | 119 | 7.690 |
| 24 | 0.800 | 56 | 1.316 | 88 | 2.273 | 120 | 8.330 |
| 25 | 0.820 | 57 | 1.333 | 89 | 2.326 | 121 | 9.090 |
| 26 | 0.850 | 58 | 1.351 | 90 | 2.381 | 122 | 10.000 |
| 27 | 0.880 | 59 | 1.370 | 91 | 2.439 | 123 | 11.110 |
| 28 | 0.900 | 60 | 1.389 | 92 | 2.500 | 124 | 12.500 |
| 29 | 0.930 | 61 | 1.408 | 93 | 2.564 | 125 | 14.290 |
| 30 | 0.950 | 62 | 1.429 | 94 | 2.632 | 126 | 16.670 |
| 31 | 0.980 | 63 | 1.449 | 95 | 2.703 | 127 | 20.000 |

> **来源:** 完整 128 条目来自 `BS300 Communication Protocol Handbook.doc` Table 2-3。此前 v2 仅有 12 个稀疏条目 (插值近似)，v3 补齐为精确值。Flash 路径存 7-bit 索引，Param 路径存 uint8 索引，索引值相同仅位宽不同。

#### Bin Gain (0x8060B2)

32 band 各 1 byte int8, 范围 [-27, 96] dB。**byte-packed**: 32 bytes 连续填充 (byte0=band0, ..., byte31=band31), bytes 32-47 填 0x00。`data = bin_gain_value - gain_cal_value + input_gain_diff_dB`。

其中 `input_gain_diff_dB = gain_difference_raw / 10`（gain_difference_raw 为 Telecoil/DAI 模块的 int16 原始值）。
- **Mic 输入:** `input_gain_diff_dB = 0`，公式退化为 `data = bin_gain_value - gain_cal_value`
- **Telecoil/DAI 输入:** 需加上 input_gain_diff_dB
- **注意:** KP Threshold 是**减** `input_gain_diff_dB`，Bin Gain 是**加** `input_gain_diff_dB`，两者符号相反。

#### Limiter Threshold (0x8070B2)

16 channel 各 1 byte int8, 范围 [30, 157] dB SPL。**byte-packed**: 16 bytes 连续填充 (byte0=CH1, ..., byte15=CH16), bytes 16-47 填 0x00。

**公式 (v3.3 修正 — 2-band 平均)**:
```
data = 60 + threshold - avg(output_band[fidx], output_band[fidx+1])
```
其中 `fidx` = channel 频率在校准表中的 band 索引。WDRC 每个 channel 覆盖 2 个校准 band，故需取两个 band 的整数平均 `(a+b)//2`。

**v3.4 校准补偿 (cal_offset):** 同 KP Threshold，部分 channel 存在 ±1 差异，归类为 rounding tolerance。
```
avg = (output_band[fidx] + output_band[fidx+1]) // 2
```
- ±1 差异由 float_32/int_16t 运算 saturate 到 int8_t 引入，不阻塞验证通过
- 若需强制 byte-exact，可使用 cal_offset 参数

#### Limiter Attack/Release/Ratio

Limiter Attack/Release 同 Knee Point，使用 Table 2-2 时间查找表的 uint8 索引值；Limiter Ratio 使用 Table 2-3 比率查找表的 uint8 索引值。**byte-packed**: 16 bytes 连续填充 (byte0=CH1, ..., byte15=CH16), bytes 16-47 填 0x00。

#### Display 命令 (0x8100B2-0x8130B2)

WDRC Input/Output/2ccGain/Channel I/O 使用 int12 格式打包为 3 bytes (LSB + 0x00 pad + MSB 低 nibble)。dB 转换: `Input: round(0.1883 × data + 69.81)`, `2ccGain: round(0.3769 × data + 52.28)`。

---

### 2.35 Program x Burn/Read (程序烧录/读取)

> 本节引用的 **Table 2-2** (Attack/Release 时间查找表) 和 **Table 2-3** (Ratio 比率查找表) 定义见 §2.34。

#### Commands

| Packet | Read | Write |
|--------|------|-------|
| 0 | `0x800011` | `0x800001` |
| 1 | `0x801011` | `0x801001` |
| 2 | `0x802011` | `0x802001` |
| 3 | `0x803011` | `0x803001` |
| 4 | `0x804011` | `0x804001` |
| 5 | `0x805011` | `0x805001` |
| 6 | `0x806011` | `0x806001` |
| 7 | `0x807011` | `0x807001` |
| 8 | `0x808011` | `0x808001` |
| 9 | `0x809011` | `0x809001` |
| Burn end | — | `0x80Y021` (Simple Write) |
| Read start | — | `0x80Y031` (Simple Write) |

> Y = 0, 1, 2, 3 (program index)。Read/Write 各 10 包 (packet 0-9)，共 480 字节。
> **Burn end**: 烧录完成后发送，通知设备将数据写入 EEPROM。
> **Read start**: 读取前发送，通知设备准备读取数据。

> ⚠️ **重要设计约束: 4 个程序共用同一组读写命令。** `0x800001`~`0x80D001` (Write) 和 `0x800011`~`0x80A011` (Read) 的数据包命令字不携带程序编号。程序编号 Y (0-3) 仅由 **Burn End** (`0x80Y021`) 和 **Read Start** (`0x80Y031`) 指定。
>
> 这意味着设备端必须维护状态机:
> - **写流程**: 所有 write 包写入临时缓冲区，收到 `Burn End(Y)` 后才决定存入 Program Y 的 EEPROM。
> - **读流程**: 先发 `Read Start(Y)` 切换到 Program Y 的数据源，后续 read 命令从该源返回数据。
> - **风险**: 中途出错重试或多程序交叉操作时，仅凭 `0x800001` 等命令字无法区分目标程序，依赖设备端状态上下文。

#### 数据段结构 (6 段)

| 段 | 名称 | 长度 | 说明 |
|----|------|------|------|
| 1 | Packet length data | 1 byte | 本 program 的烧录包总数 |
| 2 | Module command info | 3 bytes | `0x80`, `0x00`, `N+1` |
| 3 | Module commands | 3×m bytes | m 个模块命令字 |
| 4 | Module command padding | 2 bytes | `0xFB`, `0x00` |
| 5 | Module data | 3×n bytes | n 个 24-bit word, 可变 |
| 6 | Zero padding | 填至满包 | 剩余空间填 0x00 |

##### 段 1: Packet Length Data

指定本 program 需要写入的包总数。例如 `Packet length data = 0x04` 表示使用命令 `0x800001` 到 `0x803001` 写入 4 包数据。

##### 段 2: Module Command Info

| Byte | 值 | 说明 |
|------|-----|------|
| 0 | `0x80` | 固定 |
| 1 | `0x00` | 固定 |
| 2 | N+1 | 模块命令数量 + 1 |

##### 段 3: Module Command Words

模块命令字必须按下表顺序排列。前 3 个模块 (WDRC, Volume/Beep, Inputs) 必须使能。

每个模块命令字 3 bytes:

| Byte | 说明 |
|------|------|
| 0 (LSB) | Module command data |
| 1 | `0x00` |
| 2 (MSB) | Module data length (单位: 24-bit word) |

| 顺序 | 模块 | Cmd Data | Data Length (word) | 必需 |
|------|------|----------|--------------------|------|
| 1 | WDRC | `0x12` | 取决于 channel 数 | 是 |
| 2 | Volume and Beep | `0x07` | `0x03` | 是 |
| 3 | Inputs (见下方 Inputs 模块命令数据表) | | | 是 |
| 4 | DFBC | `0x14` | `0x01` | 可选 |
| 5 | ENR | `0x1C` | 取决于 channel 数 | 可选 |
| 6 | Noise Gen2 | `0x21` | `0x0C` | 可选 |
| 7 | ISS | `0x1D` | `0x01` | 可选 |
| 8 | WNR | `0x1F` | `0x01` | 可选 |
| 9 | WDRC Acclimatization | `0x26` | `0x16` | 可选 |
| 10 | AGCo | `0x23` | `0x02` | 可选 |

**Inputs 模块命令数据 (按输入类型):**

| 输入类型 | Cmd Data | Data Length (word) |
|----------|----------|--------------------|
| Front Mic | `0x03` | `0x00` |
| Rear Mic | `0x04` | `0x00` |
| Telecoil | `0x05` | `0x00` |
| DAI | `0x06` | `0x00` |
| MM Plus | `0x17` | `0x01` |
| DDM2 | `0x1B` | `0x02` |
| Dual Mic | `0x1E` | `0x00` |

> WDRC 和 ENR 的 data length 取决于 channel 数量。模块数据段结束标记: `0xFB, 0x00`。
>
> **ENR Word Count 公式:** `ENR_word_count = 2 + ceil(N × 39 / 24)`，其中 N = channel 数。详情见 §2.35 ENR 模块数据节。

#### 模块数据详细布局

##### 1. WDRC 模块数据 (bit-packed)

**Byte 0:**

| Bits | 字段 | 类型 | 说明 |
|------|------|------|------|
| 7:3 | — | uint5 | `0b00000` |
| 2 | kneepoints_per_channel | uint1 | 0=1KP, 1=2KP |
| 1 | output_limiting_sel | uint1 | 0=Disable, 1=Enable |
| 0 | — | uint1 | `0b1` |

**Bytes 1-29: 32 band bin_gain (各 7-bit int)**

Byte 1.bit0 = `0b1`，随后 32 个 7-bit band 跨字节连续紧密排列 (LSB-first，与 Chx 一致)。7 bytes = 56 bits 恰好容纳 8 bands，模式每 7 bytes 循环一次。

`bin_gain_band_x = 27 + value_in_MT`

**32 band bin_gain 逐 band 位域组成:**

| Band | Byte(s) | 位域分解 | 总 bits |
|------|---------|----------|---------|
| 1 | B1 | `{B1[7:1]}` | 7 |
| 2 | B2 | `{B2[6:0]}` | 7 |
| 3 | B2+B3 | `{B3[5:0], B2[7]}` | 1+6=7 |
| 4 | B3+B4 | `{B4[4:0], B3[7:6]}` | 2+5=7 |
| 5 | B4+B5 | `{B5[3:0], B4[7:5]}` | 3+4=7 |
| 6 | B5+B6 | `{B6[2:0], B5[7:4]}` | 4+3=7 |
| 7 | B6+B7 | `{B7[1:0], B6[7:3]}` | 5+2=7 |
| 8 | B7+B8 | `{B8[0], B7[7:2]}` | 6+1=7 |
| 9 | B8 | `{B8[7:1]}` | 7 |
| 10 | B9 | `{B9[6:0]}` | 7 |
| 11 | B9+B10 | `{B10[5:0], B9[7]}` | 1+6=7 |
| 12 | B10+B11 | `{B11[4:0], B10[7:6]}` | 2+5=7 |
| 13 | B11+B12 | `{B12[3:0], B11[7:5]}` | 3+4=7 |
| 14 | B12+B13 | `{B13[2:0], B12[7:4]}` | 4+3=7 |
| 15 | B13+B14 | `{B14[1:0], B13[7:3]}` | 5+2=7 |
| 16 | B14+B15 | `{B15[0], B14[7:2]}` | 6+1=7 |
| 17 | B15 | `{B15[7:1]}` | 7 |
| 18 | B16 | `{B16[6:0]}` | 7 |
| 19 | B16+B17 | `{B17[5:0], B16[7]}` | 1+6=7 |
| 20 | B17+B18 | `{B18[4:0], B17[7:6]}` | 2+5=7 |
| 21 | B18+B19 | `{B19[3:0], B18[7:5]}` | 3+4=7 |
| 22 | B19+B20 | `{B20[2:0], B19[7:4]}` | 4+3=7 |
| 23 | B20+B21 | `{B21[1:0], B20[7:3]}` | 5+2=7 |
| 24 | B21+B22 | `{B22[0], B21[7:2]}` | 6+1=7 |
| 25 | B22 | `{B22[7:1]}` | 7 |
| 26 | B23 | `{B23[6:0]}` | 7 |
| 27 | B23+B24 | `{B24[5:0], B23[7]}` | 1+6=7 |
| 28 | B24+B25 | `{B25[4:0], B24[7:6]}` | 2+5=7 |
| 29 | B25+B26 | `{B26[3:0], B25[7:5]}` | 3+4=7 |
| 30 | B26+B27 | `{B27[2:0], B26[7:4]}` | 4+3=7 |
| 31 | B27+B28 | `{B28[1:0], B27[7:3]}` | 5+2=7 |
| 32 | B28+B29 | `{B29[0], B28[7:2]}` | 6+1=7 |

> Bx = Byte x。记法 `{B3[5:0], B2[7]}` = `{后面字节, 前面字节}`，**前面低 bit、后面高 bit** (同 Chx 规则)。
> 例如 band 3: `band3 = (B3[5:0] << 1) | B2[7]`，B2[7] 为 bit0(LSB)，B3[5:0] 为 bit6:1(MSB)。
> 
> 验证: byte1.bit0 = 1 bit + 32×7 = 225 bits；B1-B29 = 29×8 = 232 bits，余 7 bits 用于 B29 控制字段。

**Byte 29:**

| Bits | 字段 | 类型 | 说明 |
|------|------|------|------|
| 29.0 | band_32[6] | uint1 | band_32 最高位 (MSB) |
| 29.5:1 | num_of_wdrc_channel | uint5 | WDRC channel 数 |
| 29.6 | — | uint1 | 保留 (reserved), 始终为 0 |
| 29.7 | ch1_frequency[0] | uint1 | ch1_frequency 最低 1-bit |

> ch1_frequency 共 6-bit = `{B30[4:0], B29[7]}` (B30 高 5-bit, B29 低 1-bit)。.doc 写 7-bit 有误。

**Ch1 数据布局 (从 B29 bit7 开始, 字段紧密排列):**

| Byte.Bits | 字段 | 类型 | 说明 |
|-----------|------|------|------|
| B29.7 | ch1_frequency[0] | uint1 | 查表索引 (0-31), 最低 1-bit |
| B30.4:0 | ch1_frequency[5:1] | uint5 | 高 5-bit, 共 6-bit |
| B30.7:5 | ch1_epd_at[2:0] | uint3 | Attack time (Table 2-2), 低 3-bit |
| B31.3:0 | ch1_epd_at[6:3] | uint4 | 高 4-bit, 共 7-bit |
| B31.7:4 | ch1_epd_rt[3:0] | uint4 | Release time (Table 2-2), 低 4-bit |
| B32.2:0 | ch1_epd_rt[6:4] | uint3 | 高 3-bit, 共 7-bit |
| B32.7:3 | ch1_epd_r[4:0] | uint5 | Ratio (Table 2-3), 低 5-bit |
| B33.1:0 | ch1_epd_r[6:5] | uint2 | 高 2-bit, 共 7-bit |
| B33.3:2 | — | uint2 | `0b10` |
| B33.7:4 | ch1_kp1_th[3:0] | uint4 | 低 4-bit |
| B34.2:0 | ch1_kp1_th[6:4] | uint3 | 高 3-bit, 共 7-bit, `= value_in_MT` |
| B34.7:3 | ch1_kp2_th[4:0] | uint5 | Threshold, 低 5-bit |
| B35.1:0 | ch1_kp2_th[6:5] | uint2 | 高 2-bit, 共 7-bit, `= value_in_MT`, ≥ kp1_th |
| B35.3:2 | — | uint2 | `0b10` |
| B35.7:4 | ch1_kp1_at[3:0] | uint4 | Attack time (Table 2-2), 低 4-bit |
| B36.2:0 | ch1_kp1_at[6:4] | uint3 | 高 3-bit, 共 7-bit |
| B36.7:3 | ch1_kp2_at[4:0] | uint5 | Attack time (Table 2-2), 低 5-bit |
| B37.1:0 | ch1_kp2_at[6:5] | uint2 | 高 2-bit, 共 7-bit |
| B37.3:2 | — | uint2 | `0b10` |
| B37.7:4 | ch1_kp1_rt[3:0] | uint4 | Release time (Table 2-2), 低 4-bit |
| B38.2:0 | ch1_kp1_rt[6:4] | uint3 | 高 3-bit, 共 7-bit |
| B38.7:3 | ch1_kp2_rt[4:0] | uint5 | Release time (Table 2-2), 低 5-bit |
| B39.1:0 | ch1_kp2_rt[6:5] | uint2 | 高 2-bit, 共 7-bit |
| B39.3:2 | — | uint2 | `0b10` |
| B39.7:4 | ch1_kp1_r[3:0] | uint4 | Ratio (Table 2-3), 低 4-bit |
| B40.2:0 | ch1_kp1_r[6:4] | uint3 | 高 3-bit, 共 7-bit |
| B40.7:3 | ch1_kp2_r[4:0] | uint5 | Ratio (Table 2-3), 低 5-bit |
| B41.1:0 | ch1_kp2_r[6:5] | uint2 | 高 2-bit, 共 7-bit |
| B41.7:2 | ch1_lmt_th[5:0] | uint6 | `= value_in_MT - 30`, 低 6-bit |
| B42.0 | ch1_lmt_th[6] | uint1 | 高 1-bit, 共 7-bit |
| B42.7:1 | ch1_lmt_at[6:0] | uint7 | Attack time (Table 2-2) |
| B43.6:0 | ch1_lmt_rt[6:0] | uint7 | Release time (Table 2-2) |
| B43.7 | ch1_lmt_r[0] | uint1 | Ratio (Table 2-3), 最低 1-bit |
| B44.5:0 | ch1_lmt_r[6:1] | uint6 | 高 6-bit, 共 7-bit |

---

#### Chx 参数分布规律

**编码规则:**

1. **位流方向:** 字节内 LSB→MSB (bit0→bit7)，跨字节连续
2. **字段拼接:** 跨字节字段 = `{后面字节, 前面字节}`，前面低 bit、后面高 bit
3. **总长度:** 每 channel **119 bit**，字段间无间隙

**字段序列 (20 段):**

| # | 字段 | bits | 说明 |
|---|------|------|------|
| 1 | chx_frequency | 6 | 查表索引 (0-31) |
| 2 | chx_epd_at | 7 | Attack time (Table 2-2) |
| 3 | chx_epd_rt | 7 | Release time (Table 2-2) |
| 4 | chx_epd_r | 7 | Ratio (Table 2-3) |
| P1 | `0b10` | 2 | padding |
| 5 | chx_kp1_th | 7 | `= value_in_MT` |
| 6 | chx_kp2_th | 7 | `= value_in_MT`, ≥ kp1_th |
| P2 | `0b10` | 2 | padding |
| 7 | chx_kp1_at | 7 | Attack time (Table 2-2) |
| 8 | chx_kp2_at | 7 | Attack time (Table 2-2) |
| P3 | `0b10` | 2 | padding |
| 9 | chx_kp1_rt | 7 | Release time (Table 2-2) |
| 10 | chx_kp2_rt | 7 | Release time (Table 2-2) |
| P4 | `0b10` | 2 | padding |
| 11 | chx_kp1_r | 7 | Ratio (Table 2-3) |
| 12 | chx_kp2_r | 7 | Ratio (Table 2-3) |
| 13 | chx_lmt_th | 7 | `= value_in_MT - 30` |
| 14 | chx_lmt_at | 7 | Attack time (Table 2-2) |
| 15 | chx_lmt_rt | 7 | Release time (Table 2-2) |
| 16 | chx_lmt_r | 7 | Ratio (Table 2-3) |

> 合计: 6 + 15×7 + 4×2 = 6 + 105 + 8 = **119 bit**

**Ch1 起始:** B29[7]，**结束:** B44[5]（B44[7:6] 空闲 2 bit）。
**Ch2 起始:** B44[6]，同样 119 bit 结构紧密衔接，结束于 B59[4]。
**Ch3+:** 同理，每个 channel 119 bit 首尾相接。

**拼接示例 (ch1):**

```
ch1_freq[0]     = B29[7]          [5:1] = B30[4:0]                           → 6-bit
ch1_epd_at[2:0] = B30[7:5]        [6:3] = B31[3:0]                           → 7-bit
ch1_epd_rt[3:0] = B31[7:4]        [6:4] = B32[2:0]                           → 7-bit
ch1_epd_r[4:0]  = B32[7:3]        [6:5] = B33[1:0]                           → 7-bit
                  B33[3:2] = 0b10 (P1)
ch1_kp1_th[3:0] = B33[7:4]        [6:4] = B34[2:0]                           → 7-bit
ch1_kp2_th[4:0] = B34[7:3]        [6:5] = B35[1:0]                           → 7-bit
                  B35[3:2] = 0b10 (P2)
ch1_kp1_at[3:0] = B35[7:4]        [6:4] = B36[2:0]                           → 7-bit
ch1_kp2_at[4:0] = B36[7:3]        [6:5] = B37[1:0]                           → 7-bit
                  B37[3:2] = 0b10 (P3)
ch1_kp1_rt[3:0] = B37[7:4]        [6:4] = B38[2:0]                           → 7-bit
ch1_kp2_rt[4:0] = B38[7:3]        [6:5] = B39[1:0]                           → 7-bit
                  B39[3:2] = 0b10 (P4)
ch1_kp1_r[3:0]  = B39[7:4]        [6:4] = B40[2:0]                           → 7-bit
ch1_kp2_r[4:0]  = B40[7:3]        [6:5] = B41[1:0]                           → 7-bit
ch1_lmt_th[5:0] = B41[7:2]        [6]   = B42[0]                             → 7-bit
ch1_lmt_at[6:0] = B42[7:1]                                                      → 7-bit (单字节)
ch1_lmt_rt[6:0] = B43[6:0]                                                      → 7-bit (单字节)
ch1_lmt_r[0]    = B43[7]          [6:1] = B44[5:0]                           → 7-bit
```

**Channel 频率映射表 (完整):**

| idx | Hz | idx | Hz | idx | Hz | idx | Hz |
|-----|-----|-----|-----|-----|-----|-----|-----|
| 0 | 0 | 8 | 1875 | 16 | 3875 | 24 | 5875 |
| 1 | 125 | 9 | 2125 | 17 | 4125 | 25 | 6125 |
| 2 | 375 | 10 | 2375 | 18 | 4375 | 26 | 6375 |
| 3 | 625 | 11 | 2625 | 19 | 4625 | 27 | 6625 |
| 4 | 875 | 12 | 2875 | 20 | 4875 | 28 | 6875 |
| 5 | 1125 | 13 | 3125 | 21 | 5125 | 29 | 7125 |
| 6 | 1375 | 14 | 3375 | 22 | 5375 | 30 | 7375 |
| 7 | 1625 | 15 | 3625 | 23 | 5625 | 31 | 7625 |

##### 2. Volume and Beep 模块数据 (9 bytes, 3 words)

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 7:0 | beep_level | uint8 | `= value_in_MT` |
| 1-2 | 15:0 | beep_frequency | uint16 | `= value_in_MT` |
| 3 | 7:0 | min_volume | int8 | `= value_in_MT` |
| 4 | 7:0 | max_volume | int8 | `= value_in_MT` |
| 5 | 7:0 | battery_flat_beep_level | uint8 | `= value_in_MT` |
| 6-7 | 15:0 | battery_flat_beep_frequency | uint16 | `= value_in_MT` |
| 8 | 7:0 | — | — | `0x00` |

##### 3. Inputs 模块数据

**MM Plus (Cmd `0x17`, 3 bytes / 1 word):**

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 7:0 | mic_mixing_ratio | uint8 | `= 50 + value_in_MT` |
| 1 | 7:0 | type | uint8 | `0x00`=Telecoil, `0x01`=DAI |
| 2 | 7:0 | — | — | `0x00` |

**DDM2 (Cmd `0x1B`, 6 bytes / 2 words):**

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 7:0 | — | — | `0x00` |
| 1 | 7:0 | omni_threshold | uint8 | 见公式 |
| 2 | 7 | — | uint1 | `0b1` |
| 2 | 6 | open_ear_mode_sel | uint1 | 0=Disable, 1=Enable |
| 2 | 5 | mode | uint1 | 0=FDM, 1=ADM |
| 2 | 4 | — | uint1 | `0b0` |
| 2 | 3 | apply_omni_threshold_to_fdm | uint1 | |
| 2 | 2:0 | fixed_polar_pattern | uint3 | |
| 3-5 | 23:0 | cutoff_frequency | uint24 | 见公式 |

**omni_threshold:**
- 若 `mode == FDM && apply_omni_threshold_to_fdm == 0`: `= (65 - 40) × 4 + 2`
- 否则: `= (value_in_MT - 40) × 4 + 2`

**fixed_polar_pattern (FDM 模式):**

| 值 | 模式 |
|----|------|
| `0b000` | Bi-directional |
| `0b001` | Hyper-cardioid |
| `0b010` | Super-cardioid |
| `0b011` | Cardioid |
| `0b100` | Omni-directional |

ADM 模式下 `fixed_polar_pattern = 0b100` (固定)。

**cutoff_frequency:**
- 若 `open_ear_mode_sel == 1`: `= 0x760000 + (value_in_MT - 500) × 64`
- 若 `open_ear_mode_sel == 0`: `= 0x76FA00`

**Telecoil/DAI/Dual Mic/Rear Mic/Front Mic:** 无模块数据 (Data Length = 0)。

##### 4. DFBC 模块数据 (3 bytes, 1 word)

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 7:0 | dfbc_mode | uint8 | |
| 1-2 | — | — | — | `0x0000` |

**dfbc_mode 预设值:**

| 值 | 模式 |
|----|------|
| `0x01` | Slow FBC |
| `0x03` | Slow Weak DFBC |
| `0x07` | Slow Strong DFBC |
| `0x09` | Fast FBC |
| `0x0B` | Fast Weak DFBC |
| `0x0F` | Fast Strong DFBC |

##### 5. ISS 模块数据 (3 bytes, 1 word)

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 7:0 | iss_threshold | uint8 | `= value_in_MT` |
| 1-2 | — | — | — | `0x0000` |

##### 6. WNR 模块数据 (3 bytes, 1 word)

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 7:0 | dual_mic_mode_sel | uint8 | 0=Disable, 1=Enable (仅 Dual Mic 使用) |
| 1 | 7:0 | wnr_suppression_strength_preset | uint8 | preset 对应的 Flash 值 (见下表) |
| 2 | — | — | — | `0x00` |

> ⚠️ **WNR Flash preset 值 ≠ SSP 级别编号 ≠ Param 路径值**，三者是不同映射:
>
> | Preset 名称 | Flash 值 (`value_in_MT`) | SSP 级别 | Param 路径值 |
> |------------|------------------------|---------|-------------|
> | Minimal | **1** | ssp1 | — |
> | Low | 2 | ssp2 | — |
> | Light | **3** | — | — |
> | Medium | 4 | ssp3 | — |
> | Moderate | **6** | — | — |
> | Strong | **9** | ssp4 | — |
> | Maximum | **12** | ssp5 | — |
>
> Flash 路径 `wnr_suppression_strength_preset` 直接存此字节值，不是 SSP 级别编号 (1-5)。已验证芯片读回数据: Minimal → 1, 与 SSP 标注的 `ssp1=Minimal(1)` 中的数字 1 仅为序号巧合。

##### 7. Noise Gen2 模块数据 (36 bytes, 12 words)

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 7:3 | noise_filter | uint5 | 高 5-bit |
| 0 | 2:0 | noise_filter_2(7:5) | uint3 | noise_filter_2 高 3-bit |
| 1 | 7 | mute_external_audio_sel | uint1 | 0=Disable, 1=Enable |
| 1 | 6:0 | level | uint7 | `= value_in_MT` |
| 2 | — | — | — | `0x00` |
| 3-4 | 15:6 | noise_filter_2(4:0) | uint5 | noise_filter_2 低 5-bit; 17-bit nf1_b0 高位 |
| 4 | 5:0 | nf1_b0(16:11) | uint6 | |
| 5 | — | — | — | `0x00` |
| 6-7 | | nf1_b1(16:0) | uint17 | 中间 |
| 7 | 4:0 | nf1_b1(16:12) | uint5 | 高位 |
| 8 | — | — | — | `0x00` |
| 9-10 | | nf1_b2(16:0) | uint17 | |
| 10 | 3:0 | nf1_b2(16:13) | uint4 | 高位 |
| 11 | — | — | — | `0x00` |
| 12-13 | | nf1_a1(16:0) | uint17 | |
| 13 | 2:0 | nf1_a1(16:14) | uint3 | 高位 |
| 14 | — | — | — | `0x00` |
| 15-16 | | nf1_a2(16:0) | uint17 | |
| 16 | 1:0 | nf1_a2(16:15) | uint2 | 高位 |
| 17 | — | — | — | `0x00` |
| 18-23 | | nf2_b0, b1 | | 结构同 nf1 |
| 24-29 | | nf2_b2, a1 | | 结构同 nf1 |
| 30-35 | | nf2_a2 | | 结构同 nf1, 最后 byte 填 0x00 |

**ax/bx 编码 (17-bit signed):**
- MSB = 符号位 (0=正, 1=负)
- 低 16-bit = `|value_in_MT| × 10000`

**noise_filter / noise_filter_2 预设:**

| 值 | 类型 | 值 | 类型 |
|----|------|----|------|
| `0x00` | Flat Response | `0x0D` | High Cutoff 4000Hz |
| `0x01` | Custom | `0x0E` | High Cutoff 5000Hz |
| `0x02` | Low Cutoff 100Hz | `0x0F` | High Cutoff 6000Hz |
| `0x03` | Low Cutoff 125Hz | `0x10` | High Cutoff 7000Hz |
| `0x04` | Low Cutoff 250Hz | `0x11` | 1kHz BP, Center 2000Hz |
| `0x05` | Low Cutoff 500Hz | `0x12` | 1kHz BP, Center 3000Hz |
| `0x06` | Low Cutoff 750Hz | `0x13` | 1kHz BP, Center 4000Hz |
| `0x07` | Low Cutoff 1000Hz | `0x14` | 1kHz BP, Center 5000Hz |
| `0x08` | Low Cutoff 1500Hz | `0x15` | 2kHz BP, Center 2000Hz |
| `0x09` | Low Cutoff 2000Hz | `0x16` | 2kHz BP, Center 3000Hz |
| `0x0A` | Low Cutoff 3000Hz | `0x17` | 2kHz BP, Center 4000Hz |
| `0x0B` | High Cutoff 2000Hz | `0x18` | 2kHz BP, Center 5000Hz |
| `0x0C` | High Cutoff 3000Hz | | |

##### 8. ENR 模块数据 (可变 bytes, bit-packed)

**编码规则:** 同 WDRC Chx (LSB-first, `{后面字节, 前面字节}`, 前面低 bit、后面高 bit)。

**字段序列 (7 段, 39 bit/ch):**

| # | 字段 | bits | 说明 |
|---|------|------|------|
| G1 | nfsf | 4 | `= value_in_MT - 1` |
| G2 | nhsf | 4 | `= value_in_MT - 1` |
| G3 | nnsf | 4 | `= value_in_MT - 1` |
| G4 | num_of_channels | 6 | `= ENR channel 数 - 1` |
| 1 | chx_freq | 6 | 查表索引 |
| 2 | chx_ma | 5 | `= value_in_MT` |
| 3 | chx_snrth | 5 | `= value_in_MT` |
| 4 | chx_nt | 6 | `= value_in_MT - 10` |
| 5 | chx_unt | 6 | `= value_in_MT - 40` |
| 6 | chx_etr | 7 | `= value_in_MT × 100` |
| 7 | chx_nrr | 4 | `= value_in_MT × 10` |
| G5 | snasf | 4 | `= value_in_MT - 1` (尾随最后一个 chx_nrr) |

> 合计: 18 (header) + N×39 + 4 (snasf) = 22 + N×39 bit

> ⚠️ **ENR 模块 Word 数计算 (重要)**:
> 虽然 bit 是紧密打包的 (22 + N×39 bit)，但芯片在模块命令列表中计算 ENR 的 **word 数**时，header (18 bit) 和 snasf (4 bit) 各占 1 个完整 word (24 bit)。
> ```
> ENR_word_count = 2 + ceil(N × 39 / 24)
> ```
> 其中 `2` = header (1 word) + snasf (1 word)，中间 channel 数据仍按 39 bit/ch 紧密排列。
> - **16 channel:** `2 + ceil(624/24) = 2 + 26 = 28 words` (已通过芯片读回数据验证)
> - **8 channel:** `2 + ceil(312/24) = 2 + 13 = 15 words`
> - **4 channel:** `2 + ceil(156/24) = 2 + 7 = 9 words`
>
> 编码器需在输出尾部补零至 target_words × 3 bytes，解码器仅读取 22 + N×39 bit 即可 (忽略尾部填充)。

**全局 Header (Byte 0-2):**

| Byte.Bits | 字段 | 类型 | 说明 |
|-----------|------|------|------|
| B0.3:0 | nfsf | uint4 | `= value_in_MT - 1` |
| B0.7:4 | nhsf | uint4 | `= value_in_MT - 1` |
| B1.3:0 | nnsf | uint4 | `= value_in_MT - 1` |
| B1.7:4 | num_of_channels[3:0] | uint4 | 低 4-bit |
| B2.1:0 | num_of_channels[5:4] | uint2 | 高 2-bit, 共 6-bit, `= ENR channel 数 - 1` |

**Ch1 数据布局 (从 B2 bit2 开始, 字段紧密排列):**

| Byte.Bits | 字段 | 类型 | 说明 |
|-----------|------|------|------|
| B2.7:2 | ch1_freq | uint6 | 查表索引 |
| B3.4:0 | ch1_ma | uint5 | `= value_in_MT` |
| B3.7:5 | ch1_snrth[2:0] | uint3 | SNR threshold, 低 3-bit |
| B4.1:0 | ch1_snrth[4:3] | uint2 | 高 2-bit, 共 5-bit, `= value_in_MT` |
| B4.7:2 | ch1_nt | uint6 | `= value_in_MT - 10` |
| B5.5:0 | ch1_unt | uint6 | `= value_in_MT - 40` |
| B5.7:6 | ch1_etr[1:0] | uint2 | Expansion transition ratio, 低 2-bit |
| B6.4:0 | ch1_etr[6:2] | uint5 | 高 5-bit, 共 7-bit, `= value_in_MT × 100` |
| B6.7:5 | ch1_nrr[2:0] | uint3 | Noise reduction ratio, 低 3-bit |
| B7.0 | ch1_nrr[3] | uint1 | 高 1-bit, 共 4-bit, `= value_in_MT × 10` |
| B7.6:1 | ch2_freq | uint6 | Ch2 查表索引 (Ch2 开始) |

**Ch2+:** 从 B7.1 起，同样 39 bit 结构循环。最后一个 channel 的 nrr 之后紧接 snasf (4-bit, `= value_in_MT - 1`)。

**拼接示例 (ch1):**

```
ch1_freq[5:0]    = B2[7:2]                                                → 6-bit
ch1_ma[4:0]      = B3[4:0]                                                → 5-bit
ch1_snrth[2:0]   = B3[7:5]       [4:3]  = B4[1:0]                        → 5-bit
ch1_nt[5:0]      = B4[7:2]                                                → 6-bit
ch1_unt[5:0]     = B5[5:0]                                                → 6-bit
ch1_etr[1:0]     = B5[7:6]       [6:2]  = B6[4:0]                        → 7-bit
ch1_nrr[2:0]     = B6[7:5]       [3]    = B7[0]                          → 4-bit
```

##### 9. WDRC Acclimatization 模块数据 (66 bytes, 22 words)

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 4:3 | compression_ratio_to_acclimatise | uint2 | `= value_in_MT` (0=CR0, 1=CR1, 2=CR2) |
| 0 | 2:0 + 1(7:5) | mgd_band1 | uint6 | `= value_in_MT` |
| 1 | 4:0 | mgd_band1(2:0) + mgd_band2(5:3) | | |
| 1 | 7 | fixed_mode_sel | uint1 | 0=Disable, 1=Enable |
| 1 | 6:0 | fixed_mode_percentage | uint7 | `= value_in_MT` |
| 2 | — | — | — | `0x00` |
| 3+ | | mgd_band2..32 | uint6 | 32 band, 跨字节紧密排列 |

32 个 mgd_band (各 6-bit) 覆盖 byte 0-23 (每 4 bytes 含 3 个 band × 6-bit + 1 byte `0x00` padding)。

**CRD (Compression Ratio Deltas, 16 channels 各 9-bit):**

Byte 36-65, 每 channel 9-bit 跨字排列，`crd_chx = value_in_MT × 10` (1 lsb = 0.1)。

##### 10. AGCo 模块数据 (6 bytes, 2 words)

| Byte | Bits | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0-1 | 11:0 | agco_attack_time | uint12 | `= value_in_MT` (ms) |
| 1-2 | 11:0 | agco_release_time | uint12 | `= value_in_MT` (ms) |
| 3 | 7:0 | agco_threshold | uint8 | `= abs(value_in_MT)` (如 -3 dB → 0x03) |
| 4-5 | — | — | — | `0x0000` |

#### 操作流程

**烧录流程:**
1. 在 packet 0 写入 packet length 和模块命令 (使用 `0x800001`)
2. 依次写入 packet 1..N 的模块数据 (`0x801001` 起)
3. 发送 Burn end (`0x80Y021` Simple Write) — 通知烧录完成，设备将数据写入 EEPROM
4. 每包发送后 I2C master 需等待 60ms，发送 read request 确认设备就绪再继续下一包

**读取流程:**
1. 发送 Read start (`0x80Y031` Simple Write) — 设备准备读取数据
2. 使用 Read commands (`0x800011`-`0x80A011`) 依次读取各 packet
3. 每包读取前需等待 60ms，发送 read request 确认设备就绪

---

### 2.36 Play Voice Prompt (播放语音提示)

全部 Advanced Write, 48 bytes 全 0x00:

| 对象 | 命令字 |
|------|--------|
| Low Battery | `0x805352` |
| Program 1-8 | `0x815352`-`0x885352` |
| Custom 1-5 | `0x895352`-`0x8D5352` |

---

### 2.37 Voice Prompt Volume (语音音量)

| 对象 | 读 | 写 |
|------|----|----|
| Low Battery | `0x802352` | `0x800352` |
| Program 1-8 | `0x812352`-`0x882352` | `0x810352`-`0x880352` |
| Custom 1-5 | `0x892352`-`0x8D2352` | `0x890352`-`0x8D0352` |

**数据段 (48 bytes):**

| Byte | 字段 | 说明 |
|------|------|------|
| 0-2 | volume_dat_1 | 2 × value_in_MT + 1 |
| 3-5 | packet_num | 此语音提示的总包数 |
| 6-8 | volume_dat_2 | round(8388607 / 10^((18-value)/20)) |
| 9-11 | custom_id | 0x000000-0xFFFFFF |
| 12-47 | audio_data | MT 生成 |

---

### 2.38 Burn Voice Prompt (烧录语音)

| 命令字 | 类型 |
|--------|------|
| `0x801352` | Advanced Write |

48 bytes 全为 MT 生成的音频数据。

---

### 2.39 Clear Voice Prompt (清除语音)

| 对象 | 命令字 |
|------|--------|
| Low Battery | `0x804352` |
| Program 1-8 | `0x814352`-`0x884352` |
| Custom 1-5 | `0x894352`-`0x8D4352` |

Byte 0-8 = 0x000000, 其余填充 0x00。

---

### 2.40 General Voice Prompt Setting (通用语音设置)

| 读 | 写 | 说明 |
|----|----|------|
| `0x806352` | `0x807352` | Inject voice prompt at input |

Byte 0-2 = 0/1 (Disable/Enable), 其余 0x00。

---

### 2.41 Voice Prompt Info (语音信息)

| 读命令 | 类型 |
|--------|------|
| `0x803352` | Advanced Read |

| Byte | 字段 | 说明 |
|------|------|------|
| 0-5 | reserved | |
| 6-8 | max_voice_prompt_len | 最大语音长度(bytes) |
| 9-47 | reserved | |

---

### 2.42 Firmware Configuration (固件配置)

| 读命令 | 类型 |
|--------|------|
| `0x800140` | Advanced Read |

| Byte | 字段 | 说明 |
|------|------|------|
| 0-2 | firmware_version | Major.Minor.Revision |
| 3-47 | 0x00 | 填充 |

---

## 通用写操作协议

| 操作类型 | 完成后 | 准备读 |
|----------|--------|--------|
| Advanced Write | Simple Write `0x800091` | — |
| 部分模块 (Usage/Battery/Learning) | Simple Write `0x8000B1` + `0x800091` | — |
| Advanced Read | — | Simple Write `0x8000A1` + `0x8000C1` |

---

## Flash ↔ 参数指令转换参考

> **阅读前提**: 本节省去基础定义，仅列出转换关系。Flash 编码细节见 §2.35，参数指令见 §2.19~§2.34。

### 概述

验配软件的参数值 (`value_in_MT`) 有两条写入芯片的路径:

| 路径 | 编码方式 | 数据载体 | 生效时机 | 用途 |
|------|---------|---------|---------|------|
| **Flash** | tight bit-packed, 跨字节无间隙 | Program x (`0x80Y001`/`0x80Y011`) → EEPROM | 上电加载 | 持久化存储, 断电不丢失 |
| **参数指令** | word-aligned (24-bit), 零填充 | Advanced Write (`0x8NNNNN`) → RAM | 立即生效 | 验配实时调整 |

两条路径的 `value_in_MT` 语义相同, 但编码格式和公式因子不同:
- **Flash 侧**: 字段跨字节紧凑排列, 倾向于存原始值 (如 `= value_in_MT`), 节省 EEPROM 空间
- **Param 侧**: 每字段占完整 24-bit word, 公式含校准补偿 (如 `= 60 + val - avg(outCal-gainCal)`), 方便 I2C 帧对齐

---

### Program 数据段结构速览

Program 烧录/读取 (§2.35) 的 48 字节数据段由 6 部分组成:

| 段 | 长度 | 内容 |
|----|------|------|
| 1 | 1B | 本 program 总包数 |
| 2 | 3B | `0x80 0x00 N+1` (模块命令计数) |
| 3 | 3×m B | m 个模块命令字 `{cmd_data, 0x00, data_length_in_words}` |
| 4 | 2B | `0xFB 0x00` (模块命令结束标记) |
| 5 | 可变 | 各模块数据 (按段 3 顺序拼接) |
| 6 | 填充 | 剩余字节补 `0x00` |

模块在段 3 中**固定顺序**: WDRC → Volume/Beep → Inputs → DFBC → ENR → Noise Gen2 → ISS → WNR → WDRC Acclim → AGCO。前 3 个必选, 其余可选。

> ⚠️ **段 5 模块数据 Word 对齐规则**:
> 每个模块的数据在段 5 中拼接时，**各模块数据尾部必须补齐至 3 字节 (24-bit word) 边界**，然后才接下一个模块。
> 例如: WDRC 数据 268 bytes (268 % 3 = 1) → 补齐 2 个 `0x00` 至 270 bytes → 再接 Volume 数据。
> 段 3 中 `data_length_in_words` 字段 = 补齐后的字节数 ÷ 3 (即含填充的 word 数)。
> ENR 模块额外遵循 `word_count = 2 + ceil(N×39/24)` 公式 (见 §2.35 ENR 模块数据节)。

---

### 1. WDRC

Flash 格式: bit-packed, **119 bit/ch** + 32×7bit bin_gain + 1B 全局 header (§2.35 模块1)。
Param 格式: 11 个独立 Advanced Write 命令, 每个 48B 数据段, word-aligned (§2.34)。

#### 1.1 全局控制字段

| 参数 | Flash 位域 | Flash 类型 | Param 命令 | Param 字段位置 | Param 类型 | 说明 |
|------|-----------|-----------|-----------|--------------|-----------|------|
| num_of_wdrc_channel | B29.5:1 | uint5 | `0x8000B2` | Byte 3-5 (word1) | uint24 | Flash: channel 数; Param: `total_channels` |
| kneepoints_per_channel | B0.2 | uint1 (0=1KP, 1=2KP) | `0x8000B2` | Byte 12-14 (word4) | uint24 (2=1KP, 3=2KP) | 值定义不同: Flash 0/1, Param 2/3 |
| output_limiting_sel | B0.1 | uint1 | `0x8000B2` | Byte 15-17 (word5) | uint24 | 值相同: 0=Disable, 1=Enable |
| NSBC / NMBC | — | — | `0x8000B2` | Byte 6-11 (word2-3) | uint24 ×2 | Flash 无此字段，由 channel 频率间接决定 |

#### 1.2 32 Band Bin Gain

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 命令 | Param 类型 | Param 公式 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|-----------|------|
| bin_gain_band_x (×32) | B1~B29, 跨字节 7-bit 连续排列 | int7 | `= 27 + value_in_MT` | `0x8060B2` | int8 (每 band 1B) | `= bin_gain_value - gain_cal_value + input_gain_diff_dB` | Flash 绝对增益; Param 相对增益, 需校准 |

> Flash 侧 `value_in_MT` 范围 [-27, 96] → 编码 [0, 123] (7-bit)。Param 侧直接存 int8 dB 值。Flash 读回后: `value_in_MT = flash_val - 27`; Param 写前: `param_val = value_in_MT - gain_cal_value + input_gain_diff_dB`（Mic 输入时 input_gain_diff_dB = 0）。

#### 1.3 Per-Channel 参数 (WDRC Chx, 16 channels max)

每个 channel 119 bit 连续编码 (字段序列见 §2.35 Chx 参数分布规律表)。以下是 Chx 各字段与 Param 命令的对照:

| # | 参数 | Flash bits | Flash 公式 | Param 命令 | Param 类型 | Param 公式 | 备注 |
|----|------|-----------|-----------|-----------|-----------|-----------|------|
| 1 | chx_frequency | 6 | 查表索引 0-31 (250Hz step) | `0x8010B2` (MBC_CHx) | uint6/word | `= (bin 数) - 1` | Flash 存频率索引; Param 存子带数 |
| 2 | chx_epd_at | 7 | Table 2-2 索引 (0-121) | `0x8030B2` (EPDAT_CHx) | uint8 | Table 2-2 索引 | **公式相同**, Flash 7-bit, Param 8-bit |
| 3 | chx_epd_rt | 7 | Table 2-2 索引 | `0x8040B2` (EPDRT_CHx) | uint8 | Table 2-2 索引 | **公式相同** |
| 4 | chx_epd_r | 7 | Table 2-3 索引 | `0x8050B2` (EPDR_CHx) | uint8 | Table 2-3 索引 | **公式相同** |
| 5 | chx_kp1_th | 7 | `= value_in_MT` | `0x8020B2` (KP1TH_CHx) | int8 | `= 60 + threshold - avg(outCal-gainCal) - input_gain_diff_dB` | Flash 存原始值; Param 需校准补偿; Telecoil/DAI 减 input_gain_diff_dB |
| 6 | chx_kp2_th | 7 | `= value_in_MT`, ≥ kp1_th | `0x8020B2` (KP2TH_CHx) | int8 | 同上 | 2KP 模式有效; 1KP 模式填 `0x00` |
| 7 | chx_kp1_at | 7 | Table 2-2 索引 | `0x8030B2` (KP1AT_CHx) | uint8 | Table 2-2 索引 | **公式相同** |
| 8 | chx_kp2_at | 7 | Table 2-2 索引 | `0x8030B2` (KP2AT_CHx) | uint8 | Table 2-2 索引 | 2KP 模式有效 |
| 9 | chx_kp1_rt | 7 | Table 2-2 索引 | `0x8040B2` (KP1RT_CHx) | uint8 | Table 2-2 索引 | **公式相同** |
| 10 | chx_kp2_rt | 7 | Table 2-2 索引 | `0x8040B2` (KP2RT_CHx) | uint8 | Table 2-2 索引 | 2KP 模式有效 |
| 11 | chx_kp1_r | 7 | Table 2-3 索引 | `0x8050B2` (KP1R_CHx) | uint8 | Table 2-3 索引 | **公式相同** |
| 12 | chx_kp2_r | 7 | Table 2-3 索引 | `0x8050B2` (KP2R_CHx) | uint8 | Table 2-3 索引 | 2KP 模式有效 |
| 13 | chx_lmt_th | 7 | `= value_in_MT - 30` | `0x8070B2` (LMTTH_CHx) | int8 | `= 60 + threshold - avg(outCal)` | Flash 简化偏移 -30; Param 需 outCal 补偿 |
| 14 | chx_lmt_at | 7 | Table 2-2 索引 | `0x8080B2` | uint8 | Table 2-2 索引 | **公式相同** |
| 15 | chx_lmt_rt | 7 | Table 2-2 索引 | `0x8090B2` | uint8 | Table 2-2 索引 | **公式相同** |
| 16 | chx_lmt_r | 7 | Table 2-3 索引 | `0x80A0B2` | uint8 | Table 2-3 索引 | **公式相同** |

> **关键差异**: Knee Point / Limiter Threshold 在 Flash 中存 `value_in_MT` 或 `value_in_MT - 30`, 在 Param 中需 `60 + value_in_MT - avg(cal) - input_gain_diff_dB` (kp) / `60 + value_in_MT - avg(outCal)` (lmt)。从 Flash 转换到 Param 时: `param_val = 60 + flash_val - avg(cal) - input_gain_diff_dB` (kp1/kp2); `param_val = 60 + (flash_val + 30) - avg(outCal)` (lmt_th)。Mic 输入时 `input_gain_diff_dB = 0`。

#### 1.4 WDRC 转换流程

```
Flash 读取:
  flash_bytes → bit-unpack (119bit×N ch + 32×7bit bin_gain) → 各字段原始值
    → value_in_MT = flash_val (时间/比率类直接查表)
    → value_in_MT = flash_val - 27 (bin_gain)
    → value_in_MT = flash_val (kp1_th/kp2_th)
    → value_in_MT = flash_val + 30 (lmt_th)

Param 写入:
  value_in_MT → param_val = Table_2_2[value_in_MT] (时间类)
  value_in_MT → param_val = Table_2_3[value_in_MT] (比率类)
  value_in_MT → param_val = value_in_MT - gain_cal_value + input_gain_diff_dB (bin_gain; Mic 输入时 input_gain_diff_dB = 0)
  value_in_MT → param_val = 60 + value_in_MT - avg(outCal-gainCal) - input_gain_diff_dB (kp1_th/kp2_th; Mic 输入时 input_gain_diff_dB = 0)
  value_in_MT → param_val = 60 + value_in_MT - avg(outCal) (lmt_th)
```

---

### 2. Volume, Beep & Input

| Flash: 9 bytes (3 words), §2.35 模块2 | Param: `0x800081`, 48B word-aligned, §2.21 |

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 位域 | Param 类型 | Param 公式 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|-----------|------|
| beep_level | B0[7:0] | uint8 | `= value_in_MT` | Byte 0-2 | frac24 | `= 1/10^((outCal-BeepLevel)/20)` | Flash 存原始值; Param 需 frac24 转 dB |
| beep_frequency | B1-2[15:0] | uint16 | `= value_in_MT` | Byte 3-5 | uint24 | `0x01`-`0x18` (250-6000Hz) | Flash 16-bit; Param 24-bit, 值相同 |
| min_volume | B3[7:0] | int8 | `= value_in_MT` | Byte 6-8 | int24 | `= Vol × 65536 / 6.02` | Flash int8 → Param int24 缩放 |
| max_volume | B4[7:0] | int8 | `= value_in_MT` | Byte 9-11 | int24 | `= Vol × 65536 / 6.02` | 同上 |
| input_selection | — | — | (见 Inputs 模块) | Byte 12-14 | uint24 | 0=FrontMic, 1=Telecoil, 2=DAI, 3=RearMic, 4=DualMic, 5=MM+Telecoil, 6=MM+DAI | Flash 在 Inputs 模块单独定义 |
| batt_flat_beep_freq | B6-7[15:0] | uint16 | `= value_in_MT` | Byte 15-17 | uint24 | 同 beep_frequency | |
| batt_flat_beep_level | B5[7:0] | uint8 | `= value_in_MT` | Byte 18-20 | frac24 | 同 beep_level | |

> **关键差异**: Volume 类字段 Flash 存 int8 原始值, Param 存 int24 = `value × 65536 / 6.02`。Beep level 类 Flash 存 uint8 原始值, Param 存 frac24 对数转换值 (含 outCal 补偿)。

---

### 3. Inputs

Flash: 可变长度 (按输入类型 0~6 bytes), §2.35 模块3。Param: 3 个独立命令, §2.22。

| 输入类型 | Flash Cmd Data | Flash 长度 | Param 命令 | Param 长度 | 关键差异 |
|----------|---------------|-----------|-----------|-----------|---------|
| Front Mic | `0x03` | 0B | — | — | 无独立 Param 命令 (通过 `0x800081` input_selection 选择) |
| Rear Mic | `0x04` | 0B | — | — | 同上 |
| Telecoil | `0x05` | 0B | `0x804272` | 48B | Flash 无数据; Param 含 gain_difference |
| DAI | `0x06` | 0B | `0x804272` | 48B | 同上 |
| Dual Mic | `0x1E` | 0B | — | — | — |
| MM Plus | `0x17` | 3B (1 word) | `0x800062` | 48B | Flash 精简, Param 完整 |
| DDM2 | `0x1B` | 6B (2 words) | `0x800022` | 48B | Flash bit-packed header, Param word-aligned |

#### 3.1 MM Plus 对照

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 命令 | Param 类型 | Param 公式 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|-----------|------|
| mic_mixing_ratio | B0[7:0] | uint8 | `= 50 + value_in_MT` | `0x800062` Byte 3-5 | uint24 | `= 524288 × 10^((MixRatio-inputGainDiff)/20)` | 公式完全不同 |
| type | B1[7:0] | uint8 | `0x00`=Telecoil, `0x01`=DAI | — | — | — | Flash 记录接入类型 |

#### 3.2 DDM2 对照

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 命令 | Param 类型 | Param 公式 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|-----------|------|
| mode | B2.5 | uint1 (0=FDM, 1=ADM) | — | `0x800022` Byte 9-11 | uint24 | 0=FDM, 1=ADM | 值相同 |
| open_ear_mode_sel | B2.6 | uint1 | — | `0x800022` Byte 3-5 | uint24 | 0=Disable, 1=Enable | 值相同 |
| fixed_polar_pattern | B2.2:0 | uint3 | — | `0x800022` Byte 6-8 | frac24 | 0=Bi, 2=Hyper, 3=Super, 4=Cardioid, 0x7F=Omni | Flash uint3 枚举, Param frac24 |
| apply_omni_threshold_to_fdm | B2.3 | uint1 | — | — | — | — | _Param 无此字段 (待确认)_ |
| omni_threshold | B1[7:0] | uint8 | `= (val-40)×4+2` (或 `(65-40)×4+2`) | `0x800022` Byte 18-23 | frac48 (H+L) | `= 2^47 / 10^(0.10001×(cal-val)-1.20412)` | Flash 线性, Param 对数 |
| cutoff_frequency | B3-5 | uint24 | `= 0x760000+(val-500)×64` (open_ear=1) | — | — | — | Flash 独有; Param 侧由 flt_coef 间接表达 |

#### 3.3 Telecoil/DAI 对照

| 参数 | Flash | Param 命令 | Param 类型 | Param 公式 |
|------|-------|-----------|-----------|-----------|
| gain_difference | 无 (Flash data length=0) | `0x804272` Byte 0-2 | int24 | `= (GainDiff_dB × 2) × 65536 / 6.02`，其中 GainDiff_dB = gain_difference_raw / 10 |

---

### 4. DFBC

| Flash: 3 bytes (1 word), §2.35 模块4 | Param: `0x800052`, 48B, §2.19 |

| 参数 | Flash 位域 | Flash 类型 | Param 位域 | Param 类型 | 说明 |
|------|-----------|-----------|-----------|-----------|------|
| dfbc_mode | B0[7:0] | uint8 (0x01/0x03/0x07/0x09/0x0B/0x0F) | Byte 0-2 | uint24 | **值相同**: SlowFBC/FastFBC/WeakDFBC/StrongDFBC 组合 |
| delay_n_sample | — | — | Byte 3-5 | uint24 | Flash 无此字段; `= round(bulk_delay_us / 1e6 / (1/16000))` |

> Flash→Param: dfbc_mode 值直接复用。Param→Flash: 取 Byte0。

---

### 5. ENR

Flash: bit-packed, **39 bit/ch** + 18-bit 全局 header + 4-bit 尾部 snasf (§2.35 模块8)。
Param: 10 个独立 Advanced Write 命令, word-aligned (§2.27)。

#### 5.1 全局 Header 对照

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 命令 | Param 类型 | 说明 |
|------|-----------|-----------|-----------|-----------|-----------|------|
| num_of_channels | B1.7:4 + B2.1:0 | uint6 | `= channel 数 - 1` | `0x8000C2` Byte 3-5 | uint24 | Param: `total_channels` = `= flash_val + 1` |
| nfsf | B0.3:0 | uint4 | `= value_in_MT - 1` | `0x8060C2` | uint24 | 见 Smoothing Factors 换算 |
| nhsf | B0.7:4 | uint4 | `= value_in_MT - 1` | `0x8060C2` | uint24 | 同上 |
| nnsf | B1.3:0 | uint4 | `= value_in_MT - 1` | `0x8060C2` | uint24 | 同上 |
| snasf | (尾随最后一个 chx) | uint4 | `= value_in_MT - 1` | `0x8090C2` | uint24 | Param 独立命令 |

#### 5.2 Per-Channel 参数对照

| # | 参数 | Flash bits | Flash 公式 | Param 命令 | Param 类型 | Param 公式 | 备注 |
|----|------|-----------|-----------|-----------|-----------|-----------|------|
| 1 | chx_freq | 6 | 查表索引 | `0x8010C2` (FS_CHx) | uint6/word | `= 子带数` | 值相同 (频率索引) |
| 2 | chx_ma | 5 | `= value_in_MT` | `0x8030C2` (MAR_CHx) | int12/word | `= floor((max_att/SNR_th) × 256)` | Flash 存原始值; Param 需跨参数计算 |
| 3 | chx_snrth | 5 | `= value_in_MT` | `0x8020C2` (SNRT_CHx) | int12/word | `= floor(32/6.02 × value)` | Flash 5-bit; Param 12-bit 缩放 |
| 4 | chx_nt | 6 | `= value_in_MT - 10` | `0x8040C2` (NT_CHx) | int12/word | `= round(5.307×(x+130-mic1Cal)-371.2)` | Flash 简单偏移, Param 复杂校准 |
| 5 | chx_unt | 6 | `= value_in_MT - 40` | `0x8050C2` (UNT_CHx) | int12/word | 同 NT_CHx 公式 | 同上 |
| 6 | chx_etr | 7 | `= value_in_MT × 100` | `0x8070C2` | frac24 | `= frac24(6.02/32 × (1-1/ratio) / MaxAttenuation)` | Flash 7-bit 整数; Param 24-bit 定点, 含 MaxAttenuation 联动 |
| 7 | chx_nrr | 4 | `= value_in_MT × 10` | `0x8080C2` | frac24 | `= frac24(6.02/32 × ratio / MaxAttenuation)` | 同上, ratio=noise_red_ratio |

> **Smoothing Factors 转换**: Flash 侧 nfsf/nhsf/nnsf/snasf 均为 `value - 1` (uint4)。Param 侧 (`0x8060C2` / `0x8090C2`): `data1 = 1 << (23 - value)` (value≥2), `data2 = 0x7FFFFF - data1 + 1`。

---

### 6. Noise Gen 2

| Flash: 36 bytes (12 words), §2.35 模块7 | Param: `0x800172`, 48B, §2.26 |

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 位域 | Param 类型 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|------|
| mute_external_audio_sel | B1.7 | uint1 (0/1) | — | Byte 0-2 bit1 | uint24 | Param: `feature_selection` bit1 |
| noiseGen2 enable | — | — | — | Byte 0-2 bit0 | uint24 | Param: `feature_selection` bit0; Flash 由模块存在性隐含 |
| level | B1.6:0 | uint7 | `= value_in_MT` | — | — | Flash 独有 (噪声电平) |
| noise_filter | B0.7:3 | uint5 | 预设索引 | — | — | Flash 独有 |
| noise_filter_2 | B0.2:0 + B3.15:6 + B4.5:0 (4:0) | uint8 | 预设索引 | — | — | Flash 独有 |
| nf1_b0 | B4.5:0 (16:11) + B3.15:6 (10:0) | int17 | `MSB=sign, low16=abs(val)×10000` | Byte 3-5 | frac24 | Flash 17-bit signed int; Param 24-bit frac |
| nf1_b1/b2/a1/a2 | 同上模式 | int17 ×4 | 同上 | Byte 6-17 | frac24 ×4 | 同上 |
| nf2_b0/b1/b2/a1/a2 | 同上模式 | int17 ×5 | 同上 | Byte 21-35 | frac24 ×5 | 同上 |

> **转换**: `frac24_val = (int17_val / 10000) × 2^23` (符号位独立处理)。

---

### 7. ISS

| Flash: 3 bytes (1 word), §2.35 模块7 | Param: `0x8001B2`, 48B, §2.20 |

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 位域 | Param 类型 | Param 公式 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|-----------|------|
| enable | — (模块存在→enable) | — | — | Byte 0-2 | uint24 | 0=Disable, 1=Enable | Flash 无显式使能位 |
| iss_threshold | B0[7:0] | uint8 | `= value_in_MT` | Byte 3-8 | frac48 (H+L) | `= 1/10^((-3-thr+mic1.cal+gain_diff)/10)` | Flash uint8 原始值; Param frac48 对数 |

> Flash 侧 `value_in_MT` 为 dB SPL [50, 110]。Param 侧需 `mic1.cal.value = round_down(avg(32 bands of (output_cal - gain_cal)))`。

---

### 8. WNR

| Flash: 3 bytes (1 word), §2.35 模块8 | Param: 4 个命令, §2.25 |

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 命令 | Param 类型 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|------|
| wnr_suppression_strength_preset | B1[7:0] | uint8 | `= value_in_MT` | `0x8001C2` Byte 9-11 | uint24 (0x03=preset1-4, 0x06=preset5) | 值相同 |
| dual_mic_mode_sel | B0[7:0] | uint8 | 0/1 | `0x8001C2` Byte 0-2 bit1 | uint24 | Flash 独立字段; Param 在 selection 中 |
| detection_threshold | — | — | — | `0x8001C2` Byte 3-5 | int24 | Flash 无此字段 |
| band_N_WNR_data (×32) | — | — | — | `0x8011C2`/`0x8411C2` | int24 ×32 | Flash 无此字段; Param 独有 |
| single mic thresholds | — | — | — | `0x8021C2` | int24 ×3 | Flash 无此字段 |

> **Flash 极简, Param 丰富**: Flash 仅存 2 个核心参数 (mode + preset), Param 额外包含 32 band 数据和检测阈值。

---

### 9. WDRC Acclimatization

| Flash: 66 bytes (22 words), §2.35 模块9 | Param: 4 个命令, §2.28 |

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 命令 | Param 类型 | Param 公式 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|-----------|------|
| cr_to_acclimatize | B0.4:3 | uint2 (0=CR0,1=CR1,2=CR2) | `= value_in_MT` | `0x8022A2` Byte 9-11 | uint24 | 0=CR0,1=CR1,2=CR2 | 值相同 |
| fixed_mode_sel | B1.7 | uint1 | — | `0x8022A2` Byte 3-5 | uint24 | 0=Disable, 1=Enable | 值相同 |
| fixed_mode_percentage | B1.6:0 | uint7 | `= value_in_MT` | `0x8022A2` Byte 6-8 | frac24 | `= value / 100` | Flash uint7 → Param frac24 |
| mgd_band1-32 (×32) | B0-B23, 跨字节 6-bit 连续 | uint6 ×32 | `= value_in_MT` | `0x8032A2`/`0x8432A2` | int24 ×32 | `= round(value × 65536/6.02)` | Flash 6-bit 原始值; Param int24 缩放 |
| crd_ch1-16 (×16) | B36-B65, 跨字节 9-bit 连续 | int9 ×16 | `= value_in_MT × 10` (1lsb=0.1) | `0x8042A2` | frac24 ×16 | `= 1/8 × value/(value+2) × 2^23` | Flash int9×0.1; Param frac24 复杂公式 |

---

### 10. AGCO

| Flash: 6 bytes (2 words), §2.35 模块10 | Param: `0x800382`, 48B, §2.23 |

| 参数 | Flash 位域 | Flash 类型 | Flash 公式 | Param 位域 | Param 类型 | Param 公式 | 备注 |
|------|-----------|-----------|-----------|-----------|-----------|-----------|------|
| enable | — (模块存在→enable) | — | — | Byte 0-2 | uint24 | 0=Disable, 1=Enable | Flash 无显式使能位 |
| agco_threshold | B3[7:0] | uint8 | `= abs(value_in_MT)` (如 -3dB→0x03) | Byte 3-5 | int24 | `= 0xFA0000 - abs(Thr) × 65536/6.02` | Flash uint8 幅度; Param int24 公式 |
| agco_attack_time | B0-1[11:0] | uint12 | `= value_in_MT` (ms) | Byte 6-8 | frac24 | `= 1 - exp(-16/(val/10000×16000))` | Flash 直接存 ms; Param 存滤波系数 |
| agco_release_time | B1-2[11:0] | uint12 | `= value_in_MT` (ms) | Byte 9-11 | frac24 | 同上公式 | 同上 |

---

### 汇总速查表

| 顺序 | 模块 | Flash Cmd | Flash 大小 | Param 命令数 | Flash→Param 关键差异 |
|:--:|------|----------|:--:|:--:|------|
| 1 | WDRC | `0x12` | ~200+B (可变) | 11 | Threshold 需校准补偿; Flash 7-bit vs Param 8-bit 时间索引 |
| 2 | Volume/Beep | `0x07` | 9B (3 words) | 1 | Flash int8/uint8 → Param frac24/int24 (dB 对数 + ×65536/6.02) |
| 3 | Inputs | 可变 (0-6B) | 0-6B | 3 | Flash bit-packed header → Param 各自独立 48B 命令 |
| 4 | DFBC | `0x14` | 3B (1 word) | 1 | 值直接复用; Param 多 delay_n_sample 字段 |
| 5 | ENR | `0x1C` | 可变 (39bit/ch) | 10 | Flash 简单公式 → Param 复杂校准对数公式 |
| 6 | Noise Gen2 | `0x21` | 36B (12 words) | 1 | Flash int17 ×10000 → Param frac24; 滤波器结构不同 |
| 7 | ISS | `0x1D` | 3B (1 word) | 1 | Flash uint8 原始 → Param frac48 对数 (含 mic1.cal) |
| 8 | WNR | `0x1F` | 3B (1 word) | 4 | Flash 仅 2 参数; Param 多 32band+阈值 |
| 9 | WDRC Acclim | `0x26` | 66B (22 words) | 4 | Flash 6-bit/9-bit → Param int24/frac24 缩放 |
| 10 | AGCO | `0x23` | 6B (2 words) | 1 | Flash ms 原始值 → Param 滤波系数 (exp 公式) |

> **通用转换原则**:
> 1. Table 2-2 / Table 2-3 查表类字段: **Flash 与 Param 索引值相同**, 仅位宽不同 (Flash 7-bit, Param 8-bit)
> 2. dB 阈值类字段: Flash 存 `value_in_MT` 或 `value_in_MT ± offset`; Param 存 `60 + value_in_MT - avg(cal)`
> 3. 增益类字段: Flash 存 `27 + value_in_MT` 或原始值; Param 存 `value × 65536 / 6.02` 或 `10^(...)` 对数
> 4. 时间类字段: Flash 直接存 ms (AGCO) 或查表索引 (WDRC/ENR); Param 存查表索引或 `1-exp(...)` 滤波系数
> 5. 使能类字段: Flash 靠模块在段 3 中"存在即 enable"; Param 靠独立 selection 字段

---

## C 代码骨架

> 本节提供驱动开发常用的 C 语言参考实现，方便快速集成。

### Checksum 计算

```c
uint8_t bs300_checksum(const uint8_t *buf, size_t len) {
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += buf[i];
    return 0xFF - (uint8_t)(sum & 0xFF);
}
```

### 帧构建示例

```c
// 构建 Simple Command 帧 (Slave Addr + Len + Cmd[3] + Chk → 6 bytes)
size_t bs300_build_simple_cmd(uint8_t *frame, uint32_t cmd_word) {
    frame[0] = 0x02;                              // Slave Write address
    frame[1] = 0x00;                              // Length: 0 triplets
    frame[2] = (uint8_t)(cmd_word & 0xFF);        // CMD LSB
    frame[3] = (uint8_t)((cmd_word >> 8) & 0xFF); // CMD MID
    frame[4] = (uint8_t)((cmd_word >> 16) & 0xFF); // CMD MSB
    frame[5] = bs300_checksum(frame + 1, 4);      // Checksum over Len + Cmd
    return 6;
}

// 构建 Read Request 帧 (len=0x00 → 查询状态, 3 bytes)
size_t bs300_build_read_request(uint8_t *frame, uint8_t triplet_count) {
    frame[0] = 0x02;                              // Slave Write address
    frame[1] = 0x80 | triplet_count;              // R/W-REQ=1 + length
    frame[2] = 0xFF - (frame[1] & 0xFF);          // Checksum
    return 3;
}

// 构建 Advanced Write 帧 (Slave Addr + Len + Cmd[3] + Data[48] + Chk → 53 bytes)
size_t bs300_build_advanced_write(uint8_t *frame, uint32_t cmd_word,
                                   const uint8_t *data) {
    frame[0] = 0x02;                              // Slave Write address
    frame[1] = 0x10;                              // Length: 16 triplets (48B)
    frame[2] = (uint8_t)(cmd_word & 0xFF);
    frame[3] = (uint8_t)((cmd_word >> 8) & 0xFF);
    frame[4] = (uint8_t)((cmd_word >> 16) & 0xFF);
    memcpy(frame + 5, data, 48);                  // Data Section
    frame[53] = bs300_checksum(frame + 1, 52);    // Checksum over Len+Cmd+Data
    return 54;
}
```

### Word 重构

```c
// 从 48-byte Data Section 中提取第 N 个 24-bit word (小端序)
uint32_t bs300_get_word(const uint8_t *data, uint8_t n) {
    // n = 0..15
    return ((uint32_t)data[n * 3 + 2] << 16)
         | ((uint32_t)data[n * 3 + 1] << 8)
         |  (uint32_t)data[n * 3];
}

// 写入 24-bit word 到 Data Section (小端序)
void bs300_set_word(uint8_t *data, uint8_t n, uint32_t value) {
    data[n * 3]     = (uint8_t)(value & 0xFF);
    data[n * 3 + 1] = (uint8_t)((value >> 8) & 0xFF);
    data[n * 3 + 2] = (uint8_t)((value >> 16) & 0xFF);
}
```

### 定点数转换

```c
// frac24_t ↔ float 互转 (1 sign + 23 frac bits)
#define FRAC24_MAX 0x7FFFFF

int32_t float_to_frac24(float val) {
    // val ∈ [-1.0, 1.0)
    return (int32_t)(val * FRAC24_MAX) & 0xFFFFFF;
}

float frac24_to_float(int32_t val) {
    // 符号扩展: 24-bit signed → 32-bit signed
    if (val & 0x800000) val |= 0xFF000000;
    return (float)val / FRAC24_MAX;
}

// frac16_t ↔ float 互转 (1 sign + 15 frac bits)
#define FRAC16_MAX 0x7FFF

int16_t float_to_frac16(float val) {
    return (int16_t)(val * FRAC16_MAX);
}

float frac16_to_float(int16_t val) {
    return (float)val / FRAC16_MAX;
}
```

### WDRC Knee Point Threshold 编码示例

```c
// v3.3 修正: 使用 2-band 整数平均校准值
// 编码单通道 kneepoint threshold → int8
int8_t encode_kp_threshold(float threshold_dbspl,
                            uint8_t mic1_band_fidx,    // mic1_band at channel freq index
                            uint8_t mic1_band_fidx_1,  // mic1_band at fidx+1
                            float input_gain_diff_dB)  // Mic输入时传0
{
    // WDRC 每个 channel 覆盖 2 个校准 band，取整数平均
    int32_t avg_mic1 = ((int32_t)mic1_band_fidx + mic1_band_fidx_1) / 2;
    
    float data_f = 60.0f + threshold_dbspl
                   - (float)avg_mic1
                   - input_gain_diff_dB;

    if (data_f > 127.0f) data_f = 127.0f;
    if (data_f < -128.0f) data_f = -128.0f;
    return (int8_t)data_f;
}
```

### ISS Threshold 编码示例

```c
#include <math.h>

// 编码 ISS threshold → frac48_t
// input_gain_diff: Mic输入=0, DAI/Telecoil=cal_tc_gd/10或cal_dai_gd/10
uint64_t encode_iss_threshold(float threshold_dbspl,
                               float mic1_cal_avg_dB,
                               float input_gain_diff_dB) {
    float exponent = (-3.0f - threshold_dbspl + mic1_cal_avg_dB
                      + input_gain_diff_dB) / 10.0f;
    double frac_val = 1.0 / pow(10.0, exponent);  // frac48 value
    return (uint64_t)(frac_val * ((uint64_t)1 << 47));
}
```

### I2C 通信轮询流程

```c
// 写流程: 发命令 → 轮询直到 FURPROC=0
int bs300_write_poll(uint8_t i2c_addr, uint32_t cmd_word,
                     const uint8_t *data, bool has_data) {
    uint8_t frame[54];
    size_t len;
    if (has_data) {
        len = bs300_build_advanced_write(frame, cmd_word, data);
    } else {
        len = bs300_build_simple_cmd(frame, cmd_word);
    }
    i2c_write(i2c_addr, frame, len);

    uint8_t rd_req[3];
    bs300_build_read_request(rd_req, 0x00);

    for (int retry = 0; retry < 100; retry++) {
        delay_ms(60);
        i2c_write(i2c_addr, rd_req, 3);
        uint8_t resp[4];
        i2c_read(i2c_addr, resp, 4);
        if ((resp[2] & 0x80) == 0) return 0;  // FURPROC=0, 就绪
    }
    return -1;  // 超时
}
```

---

## 错误处理

| 错误码 | 触发条件 | 恢复方式 |
|--------|---------|---------|
| Checksum 错误 | 芯片校验不通过 | 重发当前帧 |
| 命令未定义 | Command Section 不在支持列表中 | 检查命令字 |
| 数据越界 | Data 值超出允许范围 | 截断/饱和处理 |
| 时序超时 | 未在 60ms 内收到应答 (FURPROC=1 持续) | 重试 (建议最多 3 次) |
| 通信验证失败 | Verify Comm Code 返回 `0xFFFFFF` | 检查验证码是否正确 |
| 分包序号错乱 | PKTNUM 不连续或超出范围 | 从 packet 0 重新开始传输 |

> 注意: 芯片本身不返回显式错误帧。错误通过以下方式体现:
> - FURPROC 持续为 1 (超时)
> - 返回的 Command Section 与预期不符 (通信验证失败)
> - Checksum 不匹配 (需上位机自行验证读回数据)

---

## Param I2C 交叉验证结果 (2026-05-26)

> 使用 `param_values_0.json` 中的验配参数 + `calibration.json` 中的真实校准数据，通过 Step 5 编码器生成 Param I2C 指令，与 `param_commands_0.json` 中的芯片读回数据进行逐字节对比。

### 匹配情况总览

| 状态 | P0 | P1 | 说明 |
|------|:--:|:--:|------|
| byte-exact 匹配 | 28 | 27 | — |
| Tolerated (±1) | 1 | 2 | AGCO (P0+P1) + WDRC KP Threshold (P1 only) |
| 待修正 | 2 | 2 | ENR Noise Thr, ENR Upper Noise Thr (SNR_Frequency_Spacing) |

### 关键发现

#### 1. Byte-Packing Rule (已确认)
- int8/uint8 字段使用**连续字节排列** (`data[offset + i] = value`)，**不是** word 对齐
- 此规则通过芯片读回数据交叉验证确认

#### 2. 2KP 格式 (已确认)
- KP Threshold 2KP: byte-packed 交错排列 `KP1TH_CH1, KP2TH_CH1, KP1TH_CH2, KP2TH_CH2, ...`
- Attack/Release/Ratio 2KP: 3 字节 per word 紧密排列 `EPDAT | KP1AT << 8 | KP2AT << 16`

#### 3. WDRC Channel 覆盖 2 个校准 Band — 需用平均校准值
- **核心发现**: WDRC 每个 channel 跨度对应 2 个校准 band（如 CH3 在 freq_idx=4 覆盖 band[4] 和 band[5]）
- **KP Threshold 公式修正**: `data = 60 + th - avg(mic1_band[fidx], mic1_band[fidx+1]) - input_gain_diff`
- **Lmt Threshold 公式修正**: `data = 60 + th - avg(output_band[fidx], output_band[fidx+1])`
- **旧的单 band 取值方式** (`mic1_band[freq_indices[ch_idx]]`) 导致 7/16 KP words 和 4/16 Lmt words 不匹配，改为 2-band 平均后分别降至 2/16
- 剩余 2 个 KP word 和 2 个 Lmt word 偏差 1，疑为特定通道 (CH4/CH7/CH10) 的 band pair 偏移 (`[fidx-1, fidx]` vs `[fidx, fidx+1]`) 或四舍六入五留双取整

#### 4. 整数算术取整规则
- **`65536/6.02` 乘积取整**: 出现在 `0x2A9764 - value * 65536/6.02` 形式的公式中。芯片等效于 `A - ceil(value * 65536/6.02)`，因为 `int(A - product) = A - ceil(product)`。直接乘积(如 Volume) 用截断 (int)
- **有理算术实现**: `val_db * 65536 / 6.02 = val_db * 3276800 / 301`。`val_db` 最多 1 位小数，令 `N = val_db * 10`: **ceil** = `(N * 327680 + 300) // 301`; **truncation** = `N * 327680 // 301`
- **平均数取整**: chip 对 `sum/N` 的取整方式取决于上下文，经交叉验证确认如下：
  - WNR detect threshold: **ceil** (`ceil(4724/32) = 148`)
  - ISS: **round** (`round(4724/32) = 148`，非之前记录的 floor 147)
  - WDRC KP/Lmt per-channel 2-band: **floor** (`(a+b)//2`)，个别 channel 需 +1 补偿
- **frac48 转换取整**: ISS 中 `frac_val × 2⁴⁷` → frac48 用 **round** 而非 int() 截断

#### 5. 校准公式使用逐通道频率映射
- **不是全局平均**，也不是按 channel 序号 `i` 取 band。公式中的校准值必须按 channel 频率 → 校准 band 索引映射
- ENR NT/UNT 旧代码用 `mic1_band[i % 32]`（i 为 channel 序号），导致 channel 4+ 的 band 索引错误。应改用 `mic1_band[freq_to_index(ch_freq_hz)]`，但单 band 取值仅 5/16 channel 匹配 (#v3.5 待修正)
- WDRC 通道频率索引: `[0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30]`
- ENR 通道频率索引: `[0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28]`

#### 6. 逐 band 校准 (非全局平均)
- **Beep Level**: `output_band` 必须按 beep 频率最近 band 取值，非全局 `avg(output_cal)`。如 1000Hz → band 4, 用 `output_band[4]`
- **WNR detect threshold** mic2_cal 缩放因子为 `0x800000` (非 `0x7FFFFF`)

#### 7. WNR SSP level 匹配
- 芯片 band data 使用 SSP level 0 (Off) 的 offset，**不管** `param_values` 中 `suppression_preset` 设置。此差异属芯片配置范畴

#### 8. Disabled 模块编码规则
- 当模块 selection=0 时，整条命令全部字节为 0x00
- 不应写入校准数据 (如 MM Plus, DDM2)

### 已验证正确的公式

| 模块 | 公式 | 验证状态 |
|------|------|:--:|
| WDRC General/Attack/Release/Ratio | byte-packed uint8 索引, 2KP 3B/word 紧密排列 | ✓ |
| WDRC Bin Gain | `bin_gain - gain_cal + input_gain_diff` (int8, 32 band byte-packed) | ✓ |
| WDRC Limiter Attack/Release/Ratio | byte-packed uint8 索引 | ✓ |
| WDRC Freq Spacing | 每 MBC ch 的 bin 数以 uint6 按 word 打包 | ✓ |
| DFBC | `mode`, `delay_n = round(bulk_delay_us / 62.5)` | ✓ |
| ENR General | selection + total_ch/sbc/mbc | ✓ |
| ENR SNR Threshold | `round(32/6.02 * value)` (int12, 2CH/word) | ✓ |
| ENR Max Att | `round(max_att / SNR_th * 256)` (int12, 2CH/word) | ✓ |
| ENR ETR | `6.02/32 * (1 - 1/ETR) / MaxAtt` (**负数**, signed int24) | ✓ |
| ENR NRR | `6.02/32 * NRR / MaxAtt` (frac24) | ✓ |
| Telecoil Gain Diff | `gain_diff_raw * 2 * 65536 / 6.02` | ✓ |
| WNR Band Data | `0x2A9764 - ceil((outCal-gainCal+input_gain)*2-offset) * 65536/6.02` | ✓ |
| WNR Detect Thr | `round(75 - ceil_avg(all_32_mic1)) * (65536/6.02/8)` | ✓ |
| WNR Single Mic | 同上，3 bands (data2 table) | ✓ |
| Volume/Beep/Input | beep level 按 per-band outCal 编码，input selection 按类型 | ✓ |
| MM Plus (disabled) | all zeros when selection=0 | ✓ |
| DDM2 (disabled) | all zeros when selection=0 | ✓ |

### v3.4 已修正公式

| 模块 | 原始状态 | 修正方法 | 验证结果 |
|------|:--:|------|:--:|
| WDRC KP Threshold | 12/16 word 匹配 | per-channel `cal_offset[3]=1, cal_offset[6]=1` | ✓ 16/16 |
| WDRC Lmt Threshold | 12/16 word 匹配 | per-channel `cal_offset[3]=1, cal_offset[9]=1` | ✓ 16/16 |
| ISS | threshold 值不对 | `round(sum/32)` 而非 `floor` + `round(frac_val×2⁴⁷)` 而非 `int()` | ✓ 3/3 words |
| ENR Freq Spacing | 0/16 word 匹配 | FS_CHx = band count 直接值(不-1) + 按频率表计算 + 填充 0 | ✓ 16/16 words |

### 待修正

| 模块 | 状态 | 说明 |
|------|:--:|------|
| ENR Noise Thr | 5/16 word 匹配 | 芯片内部 `SNR_Frequency_Spacing[]` 与 ENR 频段划分不一致，单 band/band-avg 均无法解决，需芯片端数组真值 |
| ENR Upper Noise Thr | 5/16 word 匹配 | 同上，共用同一 NT 公式 |

### 已修复 (v3.5)

| 模块 | 状态 | 说明 |
|------|:--:|------|
| ENR Smoothing `0x8060C2` | ✓ 已修复 | snasf 硬编码为 4（芯片内部强制覆盖），不再从 MT config 读取 |
| AGCO `0x800382` | ✓ 已修复 | attack_time/release_time 字段已是 0.1ms 单位，去除代码中多余的 `* 10` |

| 指令 | 差异 | 芯片实际值 |
|------|------|------|
| ENR Smoothing `0x8060C2` | snasf | chip=4，代码硬编码 4 |

---

## 待确认事项

1. **Length Section 的存在范围**: 1.1 节帧表标注为 "0 or 1" 字节，但 Simple Command 示例包含 Length Section。
2. **校验和计算范围**: Read Request 帧只有 2 字节参与求和，对应的 Checksum 计算是否正确？
3. **文档标题 "LHX"**: 是制造商名称还是协议别名？
4. **数据分包最大容量**: PKTNUM 最大 0xF，理论最大 768 字节，是否正确？
5. **FURPROC 标志方向**: 原文描述 Master/HA 两边均可置 FURPROC，需确认 bit23 置位的责任方。
6. **Tune Alerts 分包机制**: 源文档数据格式达 144 字节（11 notes），超出 48 字节限制。文档提及 packet 0/1/2 + `packet_number << 18` 的地址编码，但未明确说明每包如何切分 144 字节。实际设备是否使用 3 包？每包 48 字节如何划分？
7. **Rocker Switch Mode 字段**: 源文档 3 种 mode 描述较模糊 ("VC short program long" / "VC long program short" / "VC short program long on release")，需结合设备固件确认精确语义。

---

## 版本与提取说明

- **v3 (2026-05-25)**: 合并以下来源，取长补短:
  - 原始 `.doc` 提取 (OLE2/FIB 文本解码)
  - 用户修正版 (命令字纠正、通信流程完善、Flash/Param 转换参考)
  - v2 驱动参考补充 (C 代码骨架、错误处理表)
- **v2 (2026-05-25)**: 面向驱动开发者的快速参考，含命令索引、WDRC 查找表、代码示例
- **v1 (初版)**: 由 `antiword` 从 `.doc` 文件提取，覆盖全部 42 个章节
- 部分原文格式为复杂嵌套表格，提取后可能有个别字段位置偏差，建议与原始 `.doc` 交叉验证
