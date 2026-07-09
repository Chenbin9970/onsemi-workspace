# BS300 RSL10 实现文档

> 2026-07-08 | 基于 `peripheral_server_sleep` 项目

## 1. 文件结构

```
peripheral_server_sleep/
├── code/
│   ├── bs300_hal.c/h            # I2C GPIO 模拟 (SCL=DIO8, SDA=DIO7, 10kHz)
│   ├── bs300_startup.c/h        # I2C 帧构建/校验/FURPROC轮询/startup序列
│   ├── bs300_program_read.c/h   # Flash 读取(10×48B) + BitReader 解码
│   ├── bs300_calib.c            # 校准数据解析 (144B raw → struct)
│   ├── bs300_param_tables.c     # 静态查找表 (WNR SSP offset)
│   ├── bs300_param_encode.c     # 31 条 Param I2C 编码函数 + 数学辅助
│   ├── bs300_storage.c/h        # NVR3 存储层 (擦除/写入/CRC16校验)
│   ├── bs300_ram_sync.c         # 同步编排器 (startup→31cmds→active)
│   ├── bs300_driver.c/h         # 驱动层 (init流程/按需加载/校准读取/刷RAM)
│   └── bs300_test.c/h           # 集成测试 (打印全部数据 + 刷RAM)
└── include/
    ├── bs300_program_read.h     # 解码结构体定义 + 常量
    ├── bs300_calib.h            # 校准结构体 + 解析 API
    ├── bs300_param_encode.h     # 编码函数声明
    ├── bs300_param_tables.h     # WNR 查找表声明
    ├── bs300_ram_sync.h         # 同步 API 声明
    └── bs300_encode_tables.h    # beep frac24 查找表（codegen 生成）
```

## 2. 启动 + 刷 RAM 完整流程

```
bs300_driver_init()
  ├─ bs300_hal_init() + delay(800ms)          ← I2C 初始 + DSP 供电稳定
  ├─ bs300_storage_is_valid() ?
  │   YES → 直接返回 (零 I2C, NVR3 memory-mapped)
  │   NO  ↓
  └─ read_and_save_all():
       bs300_startup()                         ← MUTE → KEY_LOCK → VERIFY_COMM
       bs300_storage_erase()                   ← 擦除 NVR3 扇区 (2KB)
       for prog 0..3:
         bs300_program_read(i)                 ← I2C 读 480B
         bs300_storage_write_program(i, data)  ← 写入 NVR3
       bs300_storage_finalize()                ← 写 CRC16 + magic header
       bs300_read_calibration()                ← I2C 读校准 (144B)

bs300_driver_sync_ram(prog_idx)
  ├─ bs300_driver_get_program(idx)             ← 从 NVR3 加载 + 解析 480B
  ├─ bs300_startup()                           ← MUTE → KEY_LOCK → VERIFY_COMM
  ├─ 校准数据:
  │   ├─ bs300_driver_get_calibration()        ← 缓存命中直接用
  │   └─ (miss) bs300_read_calibration()       ← 从芯片重读 144B
  ├─ bs300_calib_parse()                       ← 解析校准结构体
  ├─ 逐条编码 + 发送 31 条 Param I2C:
  │   DDM2 → MM+ → DFBC → ENR×8 → TC/DAI → ISS →
  │   WNR×4 → AGCO → Vol/Beep → WDRC×11
  └─ bs300_send_simple_cmd(0x800010)           ← ACTIVE 启动 DSP
```

**首次启动**: ~40 条 I2C 指令 (~2-3s) + 刷 RAM 31 条 (~3-4s) ≈ 5-7s  
**后续启动**: 0 条 I2C (NVR3 缓存) + 刷 RAM 34 条 (~3-4s) ≈ 3-4s

## 3. NVR3 存储布局

基址 `0x00081000`, 2KB。

| 偏移 | 大小 | 内容 |
|------|------|------|
| 0 | 480B | Program 0 raw data |
| 480 | 480B | Program 1 raw data |
| 960 | 480B | Program 2 raw data |
| 1440 | 480B | Program 3 raw data |
| 1920 | 4B | Magic `"BS30"` (0x42,0x53,0x33,0x30) |
| 1924 | 2B | Version + flags |
| 1926 | 2B | CRC16 XMODEM (多项式 `0x1021`) over bytes 0-1919 |
| 1928 | 120B | Reserved |

## 4. Param I2C 编码架构

### 4.1 命令发送顺序（31 条）

```
DDM2(0x800022) → MM+(0x800062) → DFBC(0x800052)
→ ENR×8 (0x8000C2-0x8080C2，不含 0x8090C2 SASF)
→ TC/DAI(0x804272) → ISS(0x8001B2)
→ WNR×4 (0x8001C2/0x8011C2/0x8411C2/0x8021C2)
→ AGCO(0x800382) → Vol/Beep(0x800081)
→ WDRC×11 (0x8000B2-0x80A0B2)
```

### 4.2 编码函数分类

**A 类 — 简单字节/索引打包**（无需校准数据）：
WDRC general, freq_spacing, attack/release/ratio, lmt_attack/release/ratio
ENR general, freq_spacing, snr_th, max_att, smoothing, etr, nrr

**B 类 — 依赖校准数据**：
WDRC kp_threshold, bin_gain, lmt_threshold
Volume/Beep, DFBC, ISS, WNR×4
ENR noise_th, upper_noise_th
AGCO, DDM2, MM+, TC/DAI

### 4.3 关键公式速查

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

### 4.4 Flash 值 → value_in_MT 转换

RSL10 结构体存储 flash 原始值，编码时需转换：

| 字段 | Flash 存储值 | → 编码用 value_in_MT |
|------|-------------|---------------------|
| `kp1_th`, `kp2_th` | value_in_MT（原始值） | 直接使用 |
| `lmt_th` | value_in_MT - 30 | `+ 30` |
| `bin_gain[i]` | 27 + value_in_MT | `- 27`（有符号） |
| `nt`, `unt` | value_in_MT - 10 | `+ 10` |

### 4.5 内存策略

- 逐条编码 + 发送，每次栈上仅分配 48B buffer
- 校准结构体 `bs300_calib_data_t` ≈ 80B，栈上分配
- 查找表在 .rodata，不占 RAM
- `bs300_encode_tables.h` (beep frac24 表) 396 条 uint32 ≈ 1.6KB .rodata

## 5. 驱动层 API

```c
/* 初始化 */
bool bs300_driver_init(void);                               // I2C + NVR 缓存
bool bs300_driver_sync_ram(uint8_t prog_idx);               // 刷 RAM（31 条 Param I2C）
bool bs300_driver_refresh(void);                            // 强制从 BS300 重读

/* 数据读取 */
const bs300_program_data_t *bs300_driver_get_program(uint8_t idx);  // 按需读 NVR → 解析
const uint8_t *bs300_driver_get_calibration(void);                  // 校准数据（首次/refresh 后有效）
```

## 6. 内存占用

| 区域 | 使用 | 总量 | 占比 |
|------|------|------|------|
| Flash (text) | 188.6 KB | 380 KB | 49.6% |
| RAM (.bss) | 18.6 KB | 24 KB | 77.5% |

**BS300 模块 .text 明细**：

| 文件 | text | 说明 |
|------|------|------|
| `bs300_program_read.o` | ~3 KB | Flash 读取 + BitReader 解码 |
| `bs300_startup.o` | ~0.7 KB | 帧构建/校验/轮询 |
| `bs300_hal.o` | ~0.6 KB | GPIO 模拟 I2C |
| `bs300_driver.o` | ~0.5 KB | 驱动初始化流程 |
| `bs300_storage.o` | ~0.3 KB | NVR3 存储层 |
| `bs300_calib.o` | ~0.3 KB | 校准解析 |
| `bs300_param_tables.o` | ~1.2 KB | WNR SSP offset 表 |
| `bs300_param_encode.o` | ~6 KB | 31 条编码函数 + 数学辅助 |
| `bs300_ram_sync.o` | ~1 KB | 同步编排器 |
| `bs300_test.o` | ~1 KB | 集成测试打印 |
| **BS300 合计** | **~14.6 KB** | |

**BS300 模块 .bss 明细**：

| 变量 | 大小 | 说明 |
|------|------|------|
| `s_raw_buf[480]` | 480 B | 单 program raw 缓冲 (per-call 复用) |
| `s_prog_buf` | 456 B | 单 program 解析结果 (per-call 复用) |
| `s_calibration[144]` | 144 B | 校准数据缓存 (首次启动/refresh 后有效) |
| `buf[120]` (storage) | 480 B | NVR Flash 写入缓冲 |
| `raw[480]` (program_read) | 480 B | 旧解析函数缓冲 |
| 小变量 | ~36 B | 标志位 |
| **合计** | **~2,076 B** | |

## 7. I2C 协议摘要

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

## 8. 已实现 vs 待实现

| 模块 | 状态 | 文件 |
|------|:--:|------|
| I2C HAL (GPIO 模拟) | ✓ | bs300_hal.c |
| 帧构建/校验/轮询 | ✓ | bs300_startup.c |
| Flash Read + 解码 | ✓ | bs300_program_read.c |
| NVR3 存储 | ✓ | bs300_storage.c |
| 校准解析 | ✓ | bs300_calib.c |
| Param I2C 编码 (31 条) | ✓ | bs300_param_encode.c |
| 刷 RAM 同步器 | ✓ | bs300_ram_sync.c |
| 驱动初始化 + 缓存 | ✓ | bs300_driver.c |
| 集成测试 | ✓ | bs300_test.c |
| Flash Write (Program Burn) | ✗ | 待实现 |
| DDM2/MM+ enable 模式 | ✗ | 当前仅 disabled |
| 增量切换 (switch_diff_*) | ✗ | 待实现 |
| 非阻塞状态机 | ✗ | 待实现 |

## 9. 已知差异（不阻塞）

| 模块 | 差异 | 根因 |
|------|------|------|
| ENR NT/UNT | ~3 unit 偏差 | 芯片 mic1Cal 数组与 ENR 频段不一致 |
| AGCO | byte-level ±1 | rounding tolerance (float_32 vs int_16t) |
| WDRC KP Th (P1) | byte-level ±1 | rounding tolerance |
| DDM2/MM+ | 仅支持 disabled (全零) | enable 模式编码待补 |

## 10. 关键设计决策

- **RAW 存储**: 存 480B raw 而非 decoded struct，格式稳定
- **校准不存 NVR**: 144B 每次从 BS300 重读 (3 包 ~200ms)，避免 NVR4 冲突
- **按需加载**: `get_program()` 每次从 NVR3 memory-mapped 读 480B 并解析，省 ~3.7KB RAM
- **逐条编码**: 每次栈上 48B buffer，不缓存全部 31 条 payload，省 ~1.5KB RAM
- **PRINTF fallback**: 所有文件 `#ifndef PRINTF` → `#define PRINTF(...) ((void)0)`，关 `DEBUG_UART_ENABLE` 不崩
- **CRC16**: XMODEM 多项式 0x1021，防止半写入/flash 损坏
- **不依赖 malloc**: 全部静态分配
- **C 编码函数 = Python 逐行翻译**: 遵循 Rule 16，不"理解后重写"
