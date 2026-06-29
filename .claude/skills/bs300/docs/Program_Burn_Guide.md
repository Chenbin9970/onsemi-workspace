# Program Burn Flash 读写指南

Flash 路径的完整操作：芯片回读 → bit-packed 解码，以及 value_in_MT → bit-packed 编码 → I2C 写入。

**前置依赖**: Step 0 (协议基础), Step 1 (校准数据解析)。
**参考**: 模块表、公式速查、查找表枚举见 `../../bs300.md` §6/§7/§9。字段布局以 `BS300 Protocol Handbook v3.md` 为权威来源。

---

## Part A: 芯片回读与解码

### A.0 输入格式确认（前置必做）

解析前必须向用户确认数据格式。画出一帧的字节布局，标注偏移量和字段含义：

```
示例 — 完整 I2C Read 响应（53 字节）:
  [Addr 1B] [Cmd_L 1B] [Cmd_M 1B] [Cmd_H 1B] [Payload 48B] [Chk 1B]
  byte 0     byte 1      byte 2      byte 3      byte 4-51      byte 52

示例 — 去掉 Addr 的响应（52 字节）:
  [Cmd_L 1B] [Cmd_M 1B] [Cmd_H 1B] [Payload 48B] [Chk 1B]
  byte 0      byte 1      byte 2      byte 3-50      byte 51

示例 — 裸 payload（48 字节）:
  [Payload 48B]
  byte 0-47
```

确认清单：
```
□ 每帧多少字节？（53 / 54 / 52 / 48 / 其他）
□ 帧头有什么？（Addr 0x02 + Cmd 3B / 仅 Cmd 3B / 无帧头）
□ 帧尾有没有校验和？（有 Chk 1B / 无）
□ 分包序号是否连续？有无丢包或乱序？
□ 数据来源是芯片 I2C 直读还是经过中间层（固件 log / 外部工具）处理过？
```

如果用户不确定格式：不要猜测。先读数据文件，搜索特征字节（`0x02` 帧头、`0xFB 0x00` 模块目录结束标记、尝试不同偏移的校验和），把分析结果作为假设提出，等用户确认后再解析。

### A.1 读取流程

```
1. I2C Write: Simple Command 0x80Y031 (Y=程序编号 0-3, Read Start)
2. 等待 60ms → Read Request(len=0x00) → I2C Read 4B → 检查 FURPROC=0
3. 就绪后等待 60ms
4. 循环 0~10 包: I2C Write Read Request(len=0x10) → I2C Read 52B
   命令字: 0x80X011 (X=包序号 0-10)
```

### A.2 数据段结构

```
段1 (1B):    本 program 总包数
段2 (3B):    0x80 0x00 (N+1), N=模块数
段3 (3×N B): N 个模块命令字 {cmd_data, 0x00, data_length_in_words}
段4 (2B):    0xFB 0x00 — 结束标记
段5 (可变):  各模块数据按顺序拼接
段6 (填充):  剩余 bytes 补 0x00
```

模块顺序和命令字见 `../../bs300.md` §6 模块速查表。

### A.3 BitReader (LSB-first 位流读取)

数据按 **LSB-first** 跨字节读取：字节内 bit 0→bit 7，字节间低地址先。

```python
class BitReader:
    def __init__(self, data: bytes, byte_offset: int = 0, bit_offset: int = 0)
    def read(self, n: int) -> int     # 读取 n 位 LSB-first
    def skip(self, n: int)            # 跳过 n 位
```

### A.4 WDRC Bit-Packed 布局 (119 bit/channel)

**Byte 0 header**: bit0=1, bit1=output_limiting_sel, bit2=kneepoints_per_channel, bits7:3=保留

**32 band bin_gain** (bytes 1-29, 各 7-bit int7): `bin_gain_band_x = 27 + value_in_MT`

**Byte 29 bits 5:1**: num_channels (5-bit)

**Per-channel** (119 bit):
```
chx_freq(6) | chx_epd_at(7) | chx_epd_rt(7) | chx_epd_r(7) | 0b10 |
chx_kp1_th(7) | chx_kp2_th(7) | 0b10 |
chx_kp1_at(7) | chx_kp2_at(7) | 0b10 |
chx_kp1_rt(7) | chx_kp2_rt(7) | 0b10 |
chx_kp1_r(7) | chx_kp2_r(7) |
chx_lmt_th(7) | chx_lmt_at(7) | chx_lmt_rt(7) | chx_lmt_r(7)
```

4 个 `0b10` = padding marker，用于字段分组校验。`lmt_th` 存储为 `value_in_MT - 30`。

### A.5 ENR Bit-Packed 布局 (39 bit/channel)

**Header (18-bit)**: nfsf(4) + nhsf(4) + nnsf(4) + num_channels-1(6)

**Per-channel** (39 bit):
```
chx_freq(6) | chx_ma(5) | chx_snrth(5) | chx_nt(6) | chx_unt(6) | chx_etr(7) | chx_nrr(4)
```

**尾部 snasf(4)** = `value_in_MT - 1`

### A.6 其他模块 (byte-aligned)

| 模块 | 数据长度 | 布局 |
|------|---------|------|
| Volume/Beep | 9B (3 words) | beep_level(1)+freq(2)+vol(2)+batt(3)+0x00 |
| DFBC | 3B (1 word) | mode(uint8)+0x0000 |
| ISS | 3B (1 word) | threshold(uint8)+0x0000 |
| WNR | 3B (1 word) | mode(uint8)+suppression(uint8)+0x00 |
| AGCO | 6B (2 words) | atk(uint12)+rel(uint12)+thr(uint8)+0x0000 |
| Inputs | 0-6B | MM Plus: ratio(uint8)+type(uint8)+0x00; 其他类型无数据 |

### A.7 数据结构

```python
@dataclass
class WdrcChannelFlash:
    frequency_idx: int   # 6-bit
    epd_at: int; epd_rt: int; epd_r: int
    kp1_th: int; kp2_th: int
    kp1_at: int; kp2_at: int
    kp1_rt: int; kp2_rt: int
    kp1_r: int; kp2_r: int
    lmt_th: int; lmt_at: int; lmt_rt: int; lmt_r: int

@dataclass
class EnrChannelFlash:
    frequency_idx: int
    ma: int; snrth: int; nt: int; unt: int; etr: int; nrr: int

@dataclass
class ProgramData:
    wdrc: WdrcFlash
    volume: VolumeFlash
    inputs: InputsFlash
    dfbc: DfbcFlash | None = None
    enr: EnrFlash | None = None
    iss: IssFlash | None = None
    wnr: WnrFlash | None = None
    agco: AgcoFlash | None = None
```

### A.8 解码验证检查点

```
2.1 Minimal Program (WDRC 2ch + Volume + FrontMic)    ✓
2.2 WDRC: kp_mode/limiter/num_ch/bin_gain/ch fields    ✓
2.3 Volume: beep_level/min_vol/max_vol                 ✓
2.4 Inputs: input_type                                  ✓
2.5 WDRC channel roundtrip: 12 fields all match         ✓
2.6 ENR bit-packed roundtrip: header + snasf            ✓
2.7 Optional modules (DFBC/ISS/WNR/AGCO)                ✓
2.8 MM Plus input: mixing_ratio                         ✓
```

---

## Part B: 编码与 I2C 写入

### B.1 写入流程

```
param_values_*.json
       │
       ▼
1. 各模块 Flash 编码  (value_in_MT → bit-packed bytes)
       │
       ▼
2. Program 数据段组装 (header + 模块命令列表 + 模块数据)
       │
       ▼
3. 分包 (每包 48 字节, 不足补 0x00)
       │
       ▼
4. I2C Advanced Write  (0x80X001, X=包序号 0-13)
   每包后 delay 60ms → 轮询就绪(FURPROC=0)
       │
       ▼
5. Burn End Simple Write (0x80Y021, Y=程序编号 0-3)
   设备将数据写入 EEPROM
```

### B.2 Flash 编码要点

**核心原则: Flash 编码不做校准补偿**，所有值直接等于 `value_in_MT` 或简单偏移。与 Param 路径完全不同（Param 含 CalibData 补偿）。

**WDRC 编码**:
- Byte 0: bit0=`0b1`, bit1=`output_limiting_sel`, bit2=`kneepoints_per_channel`
- 32 bin_gain (各 7-bit): `= 27 + value_in_MT` (LSB-first 跨字节)
- num_channels (5-bit) 在 Byte 29 bits 5:1
- Per-channel 119 bit 用 BitWriter (LSB-first 反操作), 4 个 `0b10` marker
- Time/Ratio 字段存表索引，不是 ms/float 值

**ENR 编码**:
- Header 18-bit: nfsf/nhsf/nnsf = `value_in_MT - 1`, num_channels = `value_in_MT` (6-bit)
- Per-channel 39 bit: nt/unt 偏移 (`nt = value_in_MT - 10`, `unt = value_in_MT - 40`)
- snasf(4) = `value_in_MT - 1`

**Volume**: 所有字段 = `value_in_MT`, byte 8 = `0x00`

**DFBC/ISS/WNR**: 单 byte + `0x0000` 填充。枚举值见 `../../bs300.md` §9。

**AGCO**: atk/rel 为 uint12 ms 值, thr = `abs(value_in_MT)` (uint8), 末尾 `0x0000`

**Inputs**: MM Plus `ratio = 50 + value_in_MT`, 其他类型无数据体

### B.3 I2C 帧格式

**Advanced Write** (54 bytes): `[02] [10] [cmd_L] [cmd_M] [cmd_H] [48B data] [chk]`
**Burn End** (6 bytes): `[02] [00] [21] [Y0] [80] [chk]`

校验和: `Chk = 0xFF - sum(Len + Cmd + Data 各字节) & 0xFF`

### B.4 编码验证检查点

**交叉验证** (readback → param 比对, 8 项):
```
3.1 WDRC: header(kp/limiter/ch), bin_gain×32, 16ch 全部字段    ✓
3.2 Volume: beep_level/freq, min/max_vol                        ✓
3.3 Inputs: 输入类型                                             ✓
3.4 DFBC: 模式值匹配                                             ✓
3.5 ENR: header(nfsf/nhsf/nnsf/ch), snasf, 16ch 全部字段         ✓
3.6 ISS: threshold                                               ✓
3.7 WNR: dual_mic, preset                                        ✓
3.8 AGCO: atk/rel/thr                                            ✓
```

**自洽验证** (encode → decode → 比对): WDRC/Volume/DFBC/ISS/WNR/AGCO/ENR 全部字段 roundtrip + I2C 帧校验和。

### B.5 运行

```bash
py -X utf8 bs300_codegen.py                     # 全部测试 + 交叉验证
py -X utf8 crossval_program_vs_mt.py            # Flash vs MT 全字段对比
```

输出: `program_burn_write_0.json` — 可直接用于 I2C 烧录的帧序列。
