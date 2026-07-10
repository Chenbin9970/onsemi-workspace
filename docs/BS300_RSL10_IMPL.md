# BS300 RSL10 实现文档

> 2026-07-10 | 基于 `peripheral_server_sleep` 项目

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
│   ├── bs300_storage.c/h        # Main Flash 存储层 (5 扇区独立擦写 + Settings 掉电记忆)
│   ├── bs300_ram_sync.c         # 同步编排器 (startup→31cmds→active) + 提示音 + Settings 持久化
│   ├── bs300_driver.c/h         # 驱动层 (init流程/按需加载/校准读取/刷RAM)
│   └── bs300_test.c/h           # 集成测试 (打印全部数据 + 刷RAM)
└── include/
    ├── bs300_program_read.h     # 解码结构体定义 + 常量
    ├── bs300_calib.h            # 校准结构体 + 解析 API
    ├── bs300_param_encode.h     # 编码函数声明
    ├── bs300_param_tables.h     # WNR 查找表声明
    ├── bs300_ram_sync.h         # 同步 API 声明 + 提示音 + Settings 恢复
    └── bs300_encode_tables.h    # beep frac24 查找表（codegen 生成）
```

## 2. 启动 + 刷 RAM 完整流程

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
       bs300_read_calibration()                     ← I2C 读校准 (144B)
  ├─ bs300_cache_prog_inputs()                     ← 缓存 4 个 Program 的 inputs/modules/enr
  ├─ bs300_settings_load() → bs300_restore_settings() ← 恢复掉电保存的模式 + 音量
  └─ MUTE → bs300_sync_program(active_prog)        ← 覆写恢复的音量 → 编码 → 31 条 I2C → ACTIVE
```

**首次启动**: ~40 条 I2C (~2-3s) + 刷 RAM 31 条 (~3-4s) + 5 个 Main Flash 扇区擦写 (~0.5s) ≈ 5-7s  
**后续启动**: 0 条 I2C (Main Flash 缓存) + I2C 读校准 (3 条) + 刷 RAM 31 条 (~3-4s) ≈ 3-4s

## 3. Main Flash 存储布局

全部在主 Flash HIGH 区（`0x0015C800` 起），不占用 NVR3。

### 3.1 Program 扇区（每 Program 独立 2KB）

| 地址 | 大小 | 内容 |
|------|------|------|
| `0x0015D000` | 2KB | Program 0 raw data (480B) + trailer (8B) |
| `0x0015D800` | 2KB | Program 1 raw data (480B) + trailer (8B) |
| `0x0015E000` | 2KB | Program 2 raw data (480B) + trailer (8B) |
| `0x0015E800` | 2KB | Program 3 raw data (480B) + trailer (8B) |

每个扇区 trailer（offset 480）:
| 偏移 | 大小 | 内容 |
|------|------|------|
| 480 | 4B | Magic `"BSPG"` |
| 484 | 2B | Version |
| 486 | 2B | CRC16 XMODEM over 480B payload |

### 3.2 Settings 扇区（2KB，掉电记忆）

基址 `0x0015C800`。

| 偏移 | 大小 | 内容 |
|------|------|------|
| 0 | 1B | active_prog (0-3) |
| 1 | 4B | volume[0..3] (0-9 per program) |
| 5 | 3B | reserved |
| 8 | 4B | Magic `"BSST"` |
| 12 | 2B | CRC16 XMODEM |
| 14 | 2B | Version |

### 3.3 写保护

Main Flash HIGH 区写入前需解锁: `FLASH->MAIN_CTRL = MAIN_HIGH_W_ENABLE` → `FLASH->MAIN_WRITE_UNLOCK = FLASH_MAIN_KEY`。

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
bool bs300_driver_init(void);                               // I2C + Main Flash 缓存 + 同步 active_prog
bool bs300_driver_refresh(void);                            // 强制从 BS300 重读全部

/* 数据读取 */
const bs300_program_data_t *bs300_driver_get_program(uint8_t idx);  // 按需读 Main Flash → 解析
const bs300_prog_struct_t   *bs300_driver_get_struct(uint8_t idx);  // 按需读 Main Flash → 结构化
const uint8_t               *bs300_driver_get_calibration(void);    // 校准数据（deprecated）
const bs300_calib_t         *bs300_driver_get_calib(void);          // 结构化校准数据
bool bs300_driver_is_cached(void);                                  // Main Flash 缓存是否有效
```

### 5.1 运行时 API（`bs300_ram_sync.h`）

```c
/* 同步 — 阻塞 */
int bs300_sync_program(bs300_prog_struct_t *prog);          // 全量 31 条 I2C
int bs300_switch_program(uint8_t new_prog_idx);             // 增量 diff 切换（自动掉电保存）

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
void bs300_play_prompt_tone(uint8_t program, uint8_t volume);  // 检测变化并播提示音
uint8_t bs300_get_active_prog(void);                            // 获取当前 active program
uint8_t bs300_get_module_volume(uint8_t prog_idx);              // 获取缓存的音量

/* Settings 掉电记忆 */
void bs300_restore_settings(uint8_t active_prog, const uint8_t *volume);  // 启动时恢复
bool bs300_settings_load(uint8_t *active_prog, uint8_t *volume);          // 读 Settings 扇区
bool bs300_settings_save(uint8_t active_prog, const uint8_t *volume);     // 写 Settings 扇区

/* 存储 */
void bs300_storage_invalidate(uint8_t idx);                 // 清除单个 Program 缓存
void bs300_settings_invalidate(void);                       // 清除 Settings 缓存
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

### 7.1 I2C 速率自动切换

GPIO 模拟 I2C 速率由 `bit_delay` 循环次数控制，`bs300_mute()` / `bs300_active()` 内部自动切换，调用方无需手动管理。

| 宏 | 值 | 对应速率 | 用途 |
|---|:---:|:---:|------|
| `BS300_I2C_DELAY_NORMAL` | 500 | ~2 kHz | 关键指令 + DSP 运行时 |
| `BS300_I2C_DELAY_FAST` | 10 | ~50 kHz | DSP 停止时 (mute 成功后) |

**切换时机**（2026-07-09）：

| 时机 | bit_delay | 说明 |
|------|:---:|------|
| 初始 (hal_init) | 500 | 安全默认值 |
| MUTE 命令发送前 | 500 | 关键指令，慢速确保可靠 |
| MUTE 发送成功 | 10 | DSP 已停，后续 Param 命令全速发送 |
| ACTIVE 命令发送前 | 500 | 关键指令，慢速确保可靠 |
| ACTIVE 发送后 | 500 | DSP 运行中，保持慢速 |

`bs300_i2c_set_speed(delay)` 可运行时修改，ack_delay 自动 = 5×bit_delay。

## 8. 已实现 vs 待实现

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
| 驱动初始化 + 缓存 | ✓ | bs300_driver.c |
| 集成测试 | ✓ | bs300_test.c |
| BLE 异步切模式 (diff) | ✓ | bs300_ram_sync.c + app.c |
| BLE 异步调音量 | ✓ | bs300_ram_sync.c + app.c |
| 非阻塞状态机 (ke_timer) | ✓ | bs300_ram_sync.c + app_process.c |
| 提示音 I2C 播放 | ✓ | bs300_ram_sync.c |
| DDM2/MM+ enable 模式 | ✗ | 当前仅 disabled |
| Flash Write (Program Burn) | ✗ | 待实现 |

## 9. BLE 异步切模式

### 9.1 架构

```
app.c Main_Loop
  │
  ├─ cs_env.rx_value_changed ?
  │   └─ cmd=0x01 → bs300_switch_program_async(prog, on_done)
  │       cmd=0x02 → bs300_set_volume_async(vol, on_done)
  │                    │
  │                    ├─ bs300_switch_program_start()   ← 构建 diff 命令队列 (提示音排最前)
  │                    ├─ ke_timer_set(BS300_SYNC_TIMER) ← 启动非阻塞状态机
  │                    └─ 立即返回 0 (不阻塞主循环)
  │
  └─ Kernel_Schedule()
       └─ ke_timer 触发 → BS300_SyncTimer()
            └─ bs300_sync_timer_handler()
                 ├─ bs300_sync_tick(): SEND → raw_write_packet() → POLL → 下一条
                 ├─ 还有命令 → ke_timer_set 重新 arm (2tick SEND / 6tick POLL)
                 └─ 全部完成 → on_bs300_switch_done() / on_bs300_volume_done()
                                 └─ BLE 通知手机
```

### 9.2 定时器注册

```c
// app.h — BS300_SYNC_TIMER 常量定义
#define BS300_SYNC_TIMER  0x10

// app.h — 消息处理器注册
#define APP_MESSAGE_HANDLER_LIST \
    DEFINE_MESSAGE_HANDLER(BS300_SYNC_TIMER, BS300_SyncTimer), ...
```

`BS300_SyncTimer()` 在 `app_process.c` 中实现，是 kernel 消息分发和 `bs300_sync_timer_handler` 之间的薄适配层。

### 9.3 BLE 指令协议

手机写入 Custom Service 的 **RX Value** characteristic (UUID `6e0edc24-4003-9eca-e5a9-a300b5f393e0`)：

| RX 数据 | 动作 |
|---------|------|
| `[01, prog]` | 异步 diff 切换到 Program `prog` (0-3) → 通知手机 |
| `[02, vol]` | 异步设置音量 `vol` (0-9) → 掉电保存 → 通知手机 |
| `[FE, 00]` | 清除全部 Main Flash 缓存 + Settings（下次开机从 BS300 重读） |

### 9.4 提示音

切模式和调音量时自动播放 DSP 提示音。命令字：

| 场景 | I2C 命令 | 说明 |
|------|---------|------|
| 切到 Program 0 | `0xFD52F2` | 模式提示音 |
| 切到 Program 1 | `0xFD72F2` | |
| 切到 Program 2 | `0xFD92F2` | |
| 切到 Program 3 | `0xFDB2F2` | |
| 音量=0 | `0xFD12F2` | 静音提示 |
| 音量≠0 | `0xFCD2F2` | 音量提示 |

提示音在 session 开头最先发送：`切换开始 → ①提示音 → ②diff I2C 命令 → 完成`。

### 9.5 掉电记忆

- **切模式** → Settings 扇区保存 `active_prog + volume[4]`
- **调音量** → Settings 扇区保存 `active_prog + volume[4]`（同步更新 Main Flash Program 数据）
- **上电恢复** → `bs300_settings_load()` → `bs300_restore_settings()` → 覆写同步用的 struct → sync to DSP
- Settings 校验失败（首次上电）→ 默认 Program 0，音量用 Program 原始值

### 9.6 低功耗行为

关 `DEBUG_UART_ENABLE` 后，`Main_Loop` 进入 `POWER_MODE_SLEEP` + `SYS_WAIT_FOR_INTERRUPT`。`ke_timer` 使用硬件定时器，能唤醒 CPU 执行 I2C 命令，完成后回到睡眠。BLE 协议栈的定时器与 BS300 定时器互不冲突。

### 9.7 打印管理

- 所有 BS300 源文件含 `#ifndef PRINTF` → `#define PRINTF(...) ((void)0)` fallback
- `bs300_test.c` 的 11 个打印函数整体被 `#ifdef DEBUG_UART_ENABLE` 包裹，关打印时不编译
- BS300 头文件不放入 `app.h`（避免 `bs300_encode_tables.h` 静态表注入所有编译单元），各 `.c` 文件独立引用

## 10. 已知差异（不阻塞）

| 模块 | 差异 | 根因 |
|------|------|------|
| ENR NT/UNT | ~3 unit 偏差 | 芯片 mic1Cal 数组与 ENR 频段不一致 |
| AGCO | byte-level ±1 | rounding tolerance (float_32 vs int_16t) |
| WDRC KP Th (P1) | byte-level ±1 | rounding tolerance |
| DDM2/MM+ | 仅支持 disabled (全零) | enable 模式编码待补 |

## 11. 关键设计决策

- **RAW 存储**: 存 480B raw 而非 decoded struct，格式稳定
- **Main Flash HIGH 区独立扇区**: 5 个 2KB 扇区（1 Settings + 4 Program），改一个只擦写一个，互不影响
- **不占 NVR3**: NVR3 留给 BLE 栈（设备地址/IRK/CSRK），免冲突
- **校准不存 Flash**: 144B 每次从 BS300 重读 (3 包 ~200ms)
- **按需加载**: `get_program()` 每次从 Main Flash 读 480B 并解析，省 ~3.7KB RAM
- **逐条编码**: 每次栈上 48B buffer，不缓存全部 31 条 payload，省 ~1.5KB RAM
- **PRINTF fallback**: 所有文件 `#ifndef PRINTF` → `#define PRINTF(...) ((void)0)`，关 `DEBUG_UART_ENABLE` 不崩
- **测试打印编译隔离**: `bs300_test.c` 打印函数全在 `#ifdef DEBUG_UART_ENABLE` 内，关打印时完全不编译
- **BS300 头文件不入 app.h**: 避免 `bs300_encode_tables.h` (1.6KB+ 静态表) 注入所有编译单元
- **CRC16**: XMODEM 多项式 0x1021，防止半写入/flash 损坏
- **不依赖 malloc**: 全部静态分配
- **异步切模式 = diff timer → 通知**: 不 mute/active，提示音排 session 最前，主循环不阻塞
- **异步调音量 = re-encode bin_gain → timer → 通知**: 自动掉电保存
- **增量 diff 切换**: 只发送新旧 Program 间变化的模块，大幅减少 I2C 指令数
- **C 编码函数 = Python 逐行翻译**: 遵循 Rule 16，不"理解后重写"
- **掉电记忆 = Settings 扇区**: 独立 2KB 扇区存 active_prog + volume[4]，切模式/调音量自动保存，上电恢复
