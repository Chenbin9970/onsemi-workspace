# BS300 RSL10 实现文档

> 2026-07-08 | 基于 `peripheral_server_sleep` 项目

## 1. 文件结构

```
peripheral_server_sleep/
├── code/
│   ├── bs300_hal.c/h            # I2C GPIO 模拟 (SCL=DIO8, SDA=DIO7, 10kHz)
│   ├── bs300_startup.c/h        # I2C 帧构建/校验/FURPROC轮询/startup序列
│   ├── bs300_program_read.c/h   # Flash 读取(10×48B) + BitReader 解码
│   ├── bs300_storage.c/h        # NVR3 存储层 (擦除/写入/CRC16校验)
│   ├── bs300_driver.c/h         # 驱动层 (init流程/按需加载/校准读取)
│   ├── bs300_test.c/h           # 集成测试 (打印全部数据)
│   └── bs300_encode_tables.h    # encode 查表数据 (待引入)
└── include/
    └── bs300_program_read.h     # 解码结构体定义 + 常量
```

## 2. 启动流程

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
```

**首次启动**: ~40 条 I2C 指令 (~2-3s)  
**后续启动**: 0 条 I2C 指令 (~0s, NVR3 memory-mapped read)

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

**写入流程**: `erase()` → `write_program(0..3)` → `finalize()`（计算 CRC 并写 header）  
**验证**: magic 匹配 + CRC16 校验  
**失效**: `invalidate()` 写零覆盖 magic 区域（Flash 1→0 特性）

## 4. 驱动层 API

```c
bool bs300_driver_init(void);                          // 初始化 (I2C + NVR)
const bs300_program_data_t *bs300_driver_get_program(uint8_t idx);  // 按需读 NVR → 解析
const uint8_t *bs300_driver_get_calibration(void);     // 校准数据 (仅首次启动有)
bool bs300_driver_refresh(void);                       // 强制从 BS300 重新读取
```

`get_program()` 返回静态 buffer 指针，下次调用覆盖。每次调用从 NVR3 memory-mapped 读取 480B → 解析 → 返回，无 I2C 开销。

## 5. 解析后数据结构

```c
bs300_program_data_t:        // ~456B per program
  ├─ wdrc (292B)             // 16ch × (freq_idx + kp1/2_th + epd/kp/lmt at/rt/r)
  │   + bin_gain[32]
  ├─ volume (~12B)           // beep_level/freq, min/max_vol, batt_beep
  ├─ inputs (~24B)           // 6种输入: front/rear_mic, telecoil, dai, mm_plus, ddm2
  ├─ dfbc (1B)               // mode
  ├─ enr (~120B)             // 16ch × (freq_idx, ma, snrth, nt, unt, etr, nrr)
  ├─ iss (1B)                // threshold
  ├─ wnr (2B)                // dual_mic_mode, strength_preset
  └─ agco (6B)               // attack/release_time, threshold

calibration:                 // 144B (3×48B packets)
  mic1_band[32], output_band[32], mic2_gain_diff,
  mic_delay, telecoil_gd, dai_gd, fbc_bulk_delay
```

## 6. 内存占用

| 区域 | 使用 | 总量 | 占比 |
|------|------|------|------|
| Flash | 179.3 KB | 380 KB | 47% |
| RAM (.data+.bss) | 18.4 KB | 24 KB | **77%** |

**BS300 模块 .bss 明细**:

| 变量 | 大小 | 说明 |
|------|------|------|
| `s_raw_buf[480]` | 480 B | 单 program raw 缓冲 (per-call 复用) |
| `s_prog_buf` | 456 B | 单 program 解析结果 (per-call 复用) |
| `s_calibration[144]` | 144 B | 校准数据 (仅首次启动加载) |
| `buf[120]` (storage) | 480 B | NVR Flash 写入缓冲 |
| `raw[480]` (program_read) | 480 B | 旧解析函数缓冲 (预留后续使用) |
| 小变量 | ~22 B | 标志位 |
| **合计** | **~2,062 B** | |

## 7. I2C 协议摘要

| 帧类型 | 字节数 | 格式 |
|--------|--------|------|
| Simple Command | 5 | `{Len(0x00), Cmd[3], Chk}` |
| Advanced Write | 53 | `{Len(0x10), Cmd[3], Data[48], Chk}` |
| Read Request | 2 | `{0x80\|0x90, Chk}` |
| Read Response | 52 | `{Cmd[3], Data[48], Chk}` |

- 从机地址: `0x02` (写) / `0x03` (读)
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
| 驱动初始化 + 缓存 | ✓ | bs300_driver.c |
| 集成测试 | ✓ | bs300_test.c |
| Param I2C encode (验配参数) | ✗ | 待实现 |
| Flash Write (Program Burn) | ✗ | 待实现 |
| VM 存储层 (AC897N) | ✗ | 非 RSL10 平台 |
| 增量切换 (switch_diff_*) | ✗ | 待实现 |
| 非阻塞状态机 | ✗ | 待实现 |

## 9. 关键设计决策

- **RAW 存储**: 存 480B raw 而非 decoded struct，格式稳定，解析函数已有且可靠
- **校准不存 NVR**: 144B 每次从 BS300 重读 (3 包 ~200ms)，避免 NVR4 扇区冲突
- **按需加载**: `get_program()` 每次从 NVR3 memory-mapped 读 480B 并解析，省 ~3.7KB RAM
- **CRC16**: XMODEM 多项式 0x1021，防止半写入/flash 损坏被误用
- **NVR3 解锁**: `FLASH->NVR_CTRL = NVR3_WRITE_ENABLE` → `FLASH->NVR_WRITE_UNLOCK = FLASH_NVR_KEY`
- **Flash ROM API**: 使用 `rsl10_flash_rom.h` (ROM 向量)，无需链接 flash library
- **不依赖 malloc**: 全部静态分配
