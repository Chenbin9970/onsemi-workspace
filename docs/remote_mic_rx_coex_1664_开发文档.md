# remote_mic_rx_coex_1664 开发文档

## 1. 工程概述

`remote_mic_rx_coex_1664` 由 `remote_mic_rx_coex_1654` 复制而来，是 RSL10 远端麦克风接收机
（RM receiver，BLE + RM 共存）的 1664 机型分支。音频出口继承 1654 的 **OD 直驱**方案
（RSL10 片上 LPDSP32 解码 + ASRC 重采样 → 内置 Output Driver，DIO0 = OD_P / DIO1 = OD_N 差分
直接驱动受话器），但**当前已关闭**（`OUTPUT_INTRF = NO_TX_OUTPUT`），用以腾出 DIO0/DIO1
给 7100 I2C，见 §3.4 与 §6。

与 1654 的差异（见 §3）：设备名改 `Smart1664`、**删除按键**、**删除电池 AD 采样**、
**打印口由 DIO5 改到 DIO12**、**音频输出改为无输出**。

> **7100 移植状态**：**阶段一（通讯层）已完成** —— Ezairo 7100 I2C 协议已移植进来并**整体取代了
> 原 BS300 子系统**（BS300 文件已删）。含上电握手、106 步引导、4 程序读回、flash 缓存、5s 心跳。
> **阶段二（写路径 + Rempro 验配命令重映射）未开始**，详见 §17。
> 引脚按「音频输出关闭 + I2C 用 DIO0/DIO1」定案（见 §5）。

参考工程：`remote_mic_rx_coex_1654`（本工程直接来源）、`peripheral_server_sleep`（OD 输出路径
+ BS300 通讯来源）、`remote_mic_rx_coex`（7100 I2C 协议来源）。

## 2. 来源与 git 基线

| commit | 说明 |
|--------|------|
| `d9ddf9b` | 1664 工程基线：由 1654 复制（61 文件），设备名改 Smart1664/1664FOTA |
| `0ad0541` | 删除按键 / 删除 AD 采样 / 打印口改 DIO12 |
| （未提交） | 音频输出改为无输出（`NO_TX_OUTPUT`），腾出 DIO0/DIO1 给 7100 I2C；移植 7100 通讯层、删除 BS300；RM 调试 IO 关闭；移除 flash overlay；引脚对齐 7160test |

1664 与 1654 的源码差异仅有以上两笔提交的内容；`code/` 下其余文件与 1654 逐字节一致
（仅行尾符差异）。

## 3. 相对 1654 的改动总览

| 改动 | 涉及文件 | 说明 |
|------|----------|------|
| 设备名 `Smart1654` → `Smart1664`（含 FOTA 变体） | include/ble_std.h、include/app.h（`VER_ID`） | 广播名区分机型 |
| 工程名 / rteconfig / .cproject 改名 | .project、.cproject*、*.rteconfig、RTE/RTE_Components.h | 工程标识 |
| **删除按键** | app.c、code/app_init.c、include/app.h | 见 §5 |
| **删除电池 AD 采样** | code/app_init.c、code/app_process.c、code/ble_rempro_cmd.c、include/app.h、include/ble_rempro_cmd.h | 见 §5 |
| **打印口 DIO5 → DIO12** | code/app_init.c、include/app.h | 见 §5、§10 |
| **音频输出 → 无输出** | include/app.h | `OUTPUT_INTRF = NO_TX_OUTPUT`，腾出 DIO0/DIO1 给 7100 I2C，见 §3.4 |
| **关闭 RM 调试 IO** | code/rm_app.c、code/app_init.c、include/app.h | `debug_dio_num=0xff`，DIO11 让给 7100 握手，见 §3.5 |
| **移除 Flash overlay + loop cache** | code/app_init.c | 否则 7100 I2C 读回全 0，见 §3.6 |
| **移植 7100 通讯层、删除 BS300** | 新增 8 文件 / 删 19 文件 / 改 9 文件 | 见 §7、§7b |
| **引脚对齐 7160test** | include/app.h | `SAMPL_CLK`→DIO3、`RECOVERY_DIO`→DIO7，见 §5 |

### 3.1 删除按键（DIO12）

| 删除内容 | 位置 |
|----------|------|
| `Button_Process()` 整个函数（短按音量 +1 / 长按切程序 0→1→2→0） | app.c 原 29–100 行 |
| 主循环调用 `Button_Process()` | app.c |
| 按键按住时跳过 `SYS_WAIT_FOR_EVENT` 的特例（长按计时依赖主循环迭代频率） | app.c |
| DIO12 上拉输入配置 | code/app_init.c |
| `BTN_DIO` / `BTN_LONG_MS` 宏 | include/app.h |

副作用：`rempro_push_volume_change()` 失去唯一调用者（API 与声明保留，与 sleep verbatim 一致）。
`rempro_push_scene_change()` 原用于 RM 场景上报，**随 BS300 删除一并移除**（RM 流窗口的 DSP 侧动作待阶段二接 7100）。

### 3.2 删除电池 AD 采样（DIO3）

| 删除内容 | 位置 |
|----------|------|
| DIO3 电池 ADC 初始化（`ADC_NORMAL \| ADC_PRESCALE_1280H`、`ADC_POS_INPUT_DIO3`） | code/app_init.c |
| `APP_Timer` 内 200ms 周期采样 + 16 次平均 → `app_env.batt_lvl` | code/app_process.c |
| `read_battery_raw()`（每次读前重配 ADC）与 `cmd_getbatteryinfo()` | code/ble_rempro_cmd.c |
| `BAT_ADC_DIO` / `BAT_ADC_CHANNEL` / `BAT_ADC_MIN` / `BAT_ADC_MAX` / `BAT_LVL_MAX` 宏 | include/app.h |
| `app_env` 的 `batt_lvl` / `sum_batt_lvl` / `num_batt_read` / `send_batt_ntf` 字段 | include/app.h |
| `read_battery_raw()` 声明 | include/ble_rempro_cmd.h |

**Rempro `GetBatteryInfo`（ID:4）行为变化**：1654 为「读取实测百分比，最低报 1%」；
1664 改为**直接回 `flag=1`（不支持）**，不再触碰 ADC。手机端需相应处理该失败响应。

随之失效并被清理的 include：`code/app_process.c` 的 `ble_rempro_cmd.h` 与 `<printf.h>`。

### 3.3 打印口改到 DIO12

`printf_init()` 内部按 pack 的 `printf.c` 把 UART TX 配到 **DIO5**（该文件为机器级共享，
硬编码 `#define UART_TX 5`）。1664 在 `printf_init()` 之后**就地覆写**，只影响本工程：

```c
/* code/app_init.c，printf_init() 之后 */
Sys_UART_DIOConfig(DIO_6X_DRIVE | DIO_WEAK_PULL_UP | DIO_LPF_ENABLE,
                   PRINT_TX_DIO, PRINT_RX_DIO);   /* 12, 6 */
Sys_DIO_Config(5, DIO_MODE_DISABLE);              /* 释放 DIO5 */
```

`Sys_UART_DIOConfig` 是 syslib 的 `__STATIC_INLINE`，把 TX/RX 脚配成 UART 模式
（TX 靠 pad 的 IO_MODE 路由，`DIO->UART_SRC` 只有 RX 字段），因此覆写后 DIO5 真正被释放。
新增宏 `PRINT_TX_DIO(12)` / `PRINT_RX_DIO(6)` 在 include/app.h。

> ⚠ pack 的 `printf.c` 未改动，1654 / 7160test 等其它工程的打印口仍为 DIO5。

### 3.4 音频输出改为无输出（腾出 DIO0/DIO1）

1664 的 OD 直驱占用 **DIO0(OD_P) / DIO1(OD_N)**，与待移植的 7100 I2C（rx_coex 用 DIO0=SCL /
DIO1=SDA）冲突。定案：**先关闭音频输出，I2C 用 DIO0/DIO1**。

改动只有一处宏（include/app.h）：

```c
#define OUTPUT_INTRF    NO_TX_OUTPUT    /* 原 OD_OUTPUT */
```

因 `OUTPUT_DECODE_PATH = (OUTPUT_INTRF==SPI_TX_RAW_OUTPUT || ==OD_OUTPUT)`，该宏一变即连带关闭：

| 被关闭的内容 | 位置 |
|---|---|
| DSP 固件 Flash_Copy、DSS reset、codec message 设置 | code/app_init.c（`#if OUTPUT_DECODE_PATH`） |
| ASRC 输入 DMA、DSP1 / AUDIOSINK IRQ 使能 | code/app_init.c |
| OD sink 初始化：`Sys_Clocks_SystemClkPrescale1`、`Sys_Audio_Set_Config`、`AUDIO->OD_CFG/SDM_CFG/OD_GAIN`、**`Sys_DIO_Config(OD_P_DIO,…)`**、ch5(OD)/ch4(ASRC OUT) DMA | code/app_init.c（`#elif OUTPUT_INTRF == OD_OUTPUT`） |
| 全部解码/ASRC 处理（`Rendering_func`、`DspDec_isr`、`Ascc_*_isr` 等） | code/app_func.c（整个 `#if OUTPUT_DECODE_PATH` 段） |
| RM 收包后的渲染调用 | code/rm_app.c |
| RM 断链时的 OD DMA 停止 | code/rm_app.c |

**保留不变**：audiosink 计数器与 DIO7 采样钟输入（无条件配置）、RM 收发本身（仍建链/收包，
只是不渲染为音频）、BLE / Rempro / BS300 全部照常。

**验证**（已做，未上板）：`arm-none-eabi-gcc -fsyntax-only` 全量编译 0 错误、告警数与改动前一致；
预处理核对 `App_Initialize` 编译后只剩打印口 DIO12、`Sys_DIO_Config(5,DIO_MODE_DISABLE)`、
DEBUG DIO15/11、DIO_SYNC_PULSE 8 —— **DIO0/DIO1 已无任何配置**，可交 7100 I2C 使用。

**恢复方法**：把 `OUTPUT_INTRF` 改回 `OD_OUTPUT` 即可（OD 相关代码全在，仅被宏关掉）。
注意届时需重新解决 DIO0/DIO1 与 7100 I2C 的冲突（改 OD 脚位或改 I2C 脚位）。

### 3.5 关闭 RM 调试 IO

RM 库通过 `app_env.rm_param.debug_dio_num[]` 在运行时翻转调试脚（原 `[0]=DIO15, [1]=DIO11`），
与 7100 握手抢 **DIO11**。已全部关闭：

- `code/rm_app.c`：`debug_dio_num[0..3] = 0xff`（无效，RM 不再碰任何调试脚）
- `code/app_init.c`：删 DIO15/DIO11 的 `Sys_DIO_Config(GPIO_OUT_0)`
- `include/app.h`：删 `DEBUG_DIO_FIRST(15)` / `DEBUG_DIO_SECOND(11)` 死宏

### 3.6 移除 Flash overlay + loop cache

`App_Initialize()` 末尾原有的：

```c
memcpy((uint8_t *)PRAM0_BASE, (uint8_t *)FLASH_MAIN_BASE, PRAM0_SIZE);  /* ×4 */
SYSCTRL->FLASH_OVERLAY_CFG  = 0xf;
SYSCTRL->CSS_LOOP_CACHE_CFG = CSS_LOOP_CACHE_ENABLE;
```

**已整体删除**（与 `remote_mic_rx_coex` 一致）。overlay 打开后 CPU 从 PRAM0..3 取指/取数，
会覆盖紧跟其后的 7100 I2C 路径 → **读回全 0**。本工程非 FOTA，不需要 overlay。

## 4. 构建与总开关

- IDE：ON Semi Eclipse（GNU ARM，arm-none-eabi-gcc，`-mcpu=cortex-m3`）。
- `.cproject` sourceEntries 按目录整收：**新增到 `code/` 的 .c、`include/` 的 .h 自动参与编译**，
  无需改工程文件。
- 总开关（include/app.h）：
  - `OUTPUT_INTRF = NO_TX_OUTPUT`（**当前：无音频输出**，见 §3.4）；可改 `OD_OUTPUT`（解码直出 OD）
    / `SPI_TX_CODED_OUTPUT` / `SPI_TX_RAW_OUTPUT`。
  - （原 `BS300_ENABLE` 已随 BS300 删除；7100 子系统无总开关，始终编译）
  - `CFG_FOTA`：FOTA 开关，默认注释（关），见 §16。
  - `OUTPUT_INTERFACE`（在 pack 的 printf.h，未在本工程覆盖 → 默认 UART）：打印出口选择。

## 5. 引脚分配

| 功能 | 引脚 | 说明 |
|------|------|------|
| 7100 I2C SCL / SDA（addr 0x02） | **DIO0 / DIO1** | 原 OD_P / OD_N；音频输出关闭后腾出（§3.4）。见 [i2c_7100_hal.h:28-29](remote_mic_rx_coex_1664/include/i2c_7100_hal.h#L28-L29) |
| 7100 ready 输入（握手） | **DIO13** | 7100 上电拉低 → RSL10 等低 → DIO11 低脉冲 → 等高。见 [app.c](remote_mic_rx_coex_1664/app.c) |
| 7100 握手输出 | **DIO11** | RSL10 → 7100 应答脉冲（原 `DEBUG_DIO_SECOND`，已让出） |
| 7100 观察输入 | DIO9 / DIO10 | 仅置输入打印电平变化 |
| 采样 / audiosink 时钟输入 | **DIO3** | `SAMPL_CLK = PCM_FRAME_SYNC`，`Sys_Audiosink_InputClock()` 无条件配置；**与 7160test 一致**（原 DIO7） |
| 上电暂停 / 恢复(recovery) | **DIO7** | 接地暂停便于重刷；**与 7160test 一致**（原 DIO13 → 曾暂定 DIO2） |
| 调试 UART TX / RX | **DIO12** / DIO6 | 115200；在 `printf_init()` 后覆写（原 DIO5） |
| DIO_SYNC_PULSE | DIO8 | GPIO 默认输出（原 BS300 SCL，BS300 已删） |
| 已释放 | DIO5 | 原打印 TX，`DIO_MODE_DISABLE` |
| 空闲 / 预留 | DIO2 / DIO4 / DIO14 / DIO15 | `DEBUG_DIO_*` 宏已删（RM 调试 IO 关闭、DIO11 让给握手） |

> **与 7160test 对齐的两处**：`SAMPL_CLK` 用 DIO3、`RECOVERY_DIO` 用 DIO7。
> ⚠ 这假定 1664 硬件的采样钟实际接在 DIO3 —— 若板上仍接 DIO7，需改回。
> （当前 `NO_TX_OUTPUT` 下 audiosink 链路空转，暂不影响功能。）
>
> **恢复 OD 直驱会与 7100 I2C 冲突**（DIO0/DIO1），届时须改脚位。

## 6. 音频通路（OD 直驱 —— **当前已关闭**）

> ⚠ 本工程当前 `OUTPUT_INTRF = NO_TX_OUTPUT`，**整条链路被宏关闭**（见 §3.4）：
> DSP 解码、ASRC、OD 输出均不初始化，RM 仍收包但不渲染音频。
> 下列内容为**关闭前的设计**，恢复 `OUTPUT_INTRF = OD_OUTPUT` 即生效。

接收链路（RX）：

```
RM 射频包 → RM_Callback_TRX(RM_RX_TRANSFER_GOODPKT)
         → Rendering_func(outTempBuff)      [app_func.c]
         → Start_Dec_Lpdsp32 → LPDSP32 解码 (DspDec_isr)
         → ch3 DMA: Dsp2CmBuff0dec → ASRC->IN
         → ASRC 重采样（锁定 DIO7 采样钟，Ascc_phase/period_isr）
         → ch4 DMA: ASRC->OUT → BufferOut  (OD_RX_DMA_ASRC_OUT, circ)
         → ch5 DMA: BufferOut → AUDIO->OD_DATA (RX_DMA_OD, OD_DMA_NUM=5)
         → OD 输出 DIO0/DIO1
```

关键配置（照 peripheral_server_sleep `Audio_Init` 末尾）：

- `Sys_Clocks_SystemClkPrescale1(AUDIOCLK_PRESCALE_5)`
- `Sys_Audio_Set_Config(AUDIO_CONFIG)`；`AUDIO->OD_CFG/SDM_CFG/OD_GAIN`
- `Sys_DIO_Config(OD_P_DIO(=0), …DIO_MODE_OD_P)`
- `BufferOut[2*FRAME_LENGTH]`（code/app_init.c 全局）
- OD DMA 在 RM 建链/断链时由 `rm_app.c` 的 `RM_Callback_StatusUpdate` 启停（断链下溢保护静音）

门控宏：`OUTPUT_DECODE_PATH = (OUTPUT_INTRF==SPI_TX_RAW_OUTPUT || ==OD_OUTPUT)`，
用于 app.h / app_init.c / app_func.c / rm_app.c 中所有「解码 + ASRC 初始化」的 `#if`。

## 7. 7100 通讯子系统 & 读回缓存

**状态**：阶段一（通讯层）已完成，取代原 BS300 子系统。

### 7.1 文件

| 文件 | 作用 |
|------|------|
| code/i2c_7100_hal.c / include/i2c_7100_hal.h | 硬件 I2C0 主机（中断驱动，DIO0/1，addr 0x02，~410kHz），`i2c_7100_read/write` 支持 >255B 单事务 |
| code/dsp_7100_init.c / include/dsp_7100_init.h | 开机引导 + 4 程序×(WDRC/DFBC/降噪) 读回；读回结果 getter |
| code/dsp_7100_init_tables.c | 生成的引导步骤表（106 步，`scripts/gen_dsp_7100_init.py`） |
| code/dsp_7100_rb_tables.c | 生成的读回命令表（28 条，`scripts/gen_dsp_7100_rb.py`） |
| code/dsp_7100_storage.c / include/dsp_7100_storage.h | 读回结果 Main Flash 缓存 |
| app.c | 上电握手（DIO13/DIO11）+ `dsp_7100_boot_init()` + `dsp_7100_cache_try_load()`；主循环 `dsp_7100_process_deferred()` |
| code/app_process.c | `APP_7100_HB_Handler`：200ms tick 推读回 + 每 5s 心跳 `{0x88,0x01}` |
| code/ble_std.c | GAPM_RESET 后挂 `APP_7100_HB_TIMER` |
| code/ble_custom.c | Rempro ROLE 写 `0xFE` → 失效缓存 + 重启重读 |

移植时**去掉了 rx_coex 的死代码**：`dsp_7100_parm_seq_tick` / `dsp_7100_a7_seq_tick` /
`dsp_7100_a7_arm/poll` / `dsp_connect_replay`（全部无调用者）。**写路径未移植**（阶段二）。

### 7.2 读回结果（RAM + Flash 缓存）

每程序 3 块 payload：WDRC 375B / DFBC 306B / 降噪 174B（不含 `46 <lo> <hi>` 头）。

**Flash 缓存**（复用 BS300 原程序区，该区已空出）：

| 地址 | 内容 |
|------|------|
| `0x0015D000` / `0x0015D800` / `0x0015E000` / `0x0015E800` | Program 0..3，各 2KB sector |

**只存解析后的参数，不存原始 block**（原始 855B → 参数 51B，省 ~94%）。

| 偏移 | 长度 | 内容 |
|------|------|------|
| `[0]` | 1 | `denoise_en` |
| `[1]` | 1 | `denoise_lvl`（0..4） |
| `[2]` | 1 | `dfbc_en` |
| `[3..18]` | 16 | `wdrc_ll[16]` |
| `[19..34]` | 16 | `wdrc_hl[16]` |
| `[35..50]` | 16 | `wdrc_ol[16]` |
| `[51..54]` | 4 | magic `"D71P"` |
| `[55]` | 1 | version（**v3**） |
| `[56]` | 1 | valid `0xA5` |
| `[57..58]` | 2 | CRC16-XMODEM（覆盖 `[0..50]`） |

槽固定 64B（16 word），整扇区擦除后重写。RAM 侧同样只保留解析结果
（`dsp_7100_rb_bufs_t` = 4 × `dsp_7100_prog_t`），原始块读到即弃 —— RAM 也从 3.4KB 降到 204B。

> **有损存储**：WDRC 的 UT / AGCO / 压缩比等未存字段，以及 DFBC / 降噪的其余字节均已丢弃。
> 阶段二 Rempro `GetFittingData` 若需要这些，须重新从 7100 读（0xFE 失效缓存后重启）。

#### 参数解析规则

| 参数 | 来源 | 规则 |
|------|------|------|
| `denoise_en` | 降噪 0xAE `payload[0]` | bit7 |
| `denoise_lvl` | 降噪 0xAE `payload[0]` | `((payload[0]>>3)&0xF)/3 - 1`，值 3/6/9/12/15 → 档 0..4 |
| `dfbc_en` | DFBC 0x32 `payload[0]` | bit7（其余 306B 抓包间恒同） |
| `wdrc_ll[ch]` | WDRC 0x177 | 通道单元 `+0`，7bit 无符号 |
| `wdrc_hl[ch]` | WDRC 0x177 | 通道单元 `+14`，8bit 有符号 |
| `wdrc_ol[ch]` | WDRC 0x177 | 通道单元 `+22`，8bit 有符号 |

WDRC 通道单元 147 bit，起点 `bit = 414 + ch×147`（ch 从 0 起，代码 payload 坐标系，
比文档坐标系小 32 bit = 地址字节 1B + `46 77 01` 头 3B）。

已用 `docs/7100协议/WDRC/7100_WDRC读取.md` §9 三个测试向量核验 OutputLimit 偏移：

| 通道 | 文档 bit | 代码 bit | 字节 | 取值 |
|---|---|---|---|---|
| ch1 | 321 | 289 | 36,37 = `7D 74` | -6 ✓ |
| ch2 | 468 | 436 | 54,55 = `9F AE` | -6 ✓ |
| ch9 | 1497 | 1465 | 183,184 = `FD 73` | -6 ✓ |

> **顺带修正**：HighLevelGain 由旧的 `+15 / 7bit` 改为文档验证过的 **`+14 / 8bit 有符号`**
> （旧读法漏掉 bit14；实测数据下两者数值相同，见文档 §5/§9）。
> UpperThreshold 按要求**不解析**。
> OutputLimit 的 `-6` 是程序 0 全通道常量，位置来自周期性扫描间接定位，
> 文档 §7 标注**尚未用「改值后读回」正面验证** —— 上板时留意。

### 7.3 开机流程（读缓存优先）

```
App_Initialize()
  → 上电握手：DIO13 输入等低 → DIO11 低脉冲 → 等 DIO13 高
  → dsp_7100_boot_init()          106 步引导（同步、喂狗）
  → dsp_7100_cache_try_load()     ← flash 缓存命中？
       命中 → s_rb_needed=0，跳过 I2C 读回
       未中 → 走 I2C 读回
主循环
  → dsp_7100_process_deferred()   读回一轮完成 → 落盘（flash 擦写在主循环，不在定时器）
200ms tick（APP_7100_HB_Handler）
  → dsp_7100_rb_seq_tick()        每次推进一步（仅 s_rb_needed 时）
  → 每 5s 发心跳 {0x88,0x01}
```

**`0xFE`**（ble_custom.c）：`dsp_7100_cache_invalidate()`（擦 4 sector）+ `NVIC_SystemReset()`，
重启后缓存不命中 → 重新从 7100 读并落盘。

> ⚠ 握手 `while(DIO_DATA->ALIAS[13] == 1)` **无超时**（照 rx_coex）。板上无 7100 时卡在开机。

## 7b. BS300 子系统（已删除）

原 BS300 子系统（9 .c + 10 .h，7.1k 行）已**整体移除**，由 §7 的 7100 通讯子系统取代。
被删文件：`code/bs300_{calib,driver,hal,param_encode,param_tables,program_read,ram_sync,startup,storage}.c`
+ `include/bs300_*.h`。

调用点改动：

| 文件 | 改动 |
|------|------|
| app.c | `bs300_driver_init()` → 7100 握手 + `dsp_7100_boot_init()`；`bs300_process_deferred()` → `dsp_7100_process_deferred()` |
| code/app_process.c | `BS300_SyncTimer` → `APP_7100_HB_Handler` |
| code/rm_app.c | 删 5 处 BS300 调用；`audio_streaming` 标志保留（BLE 互斥仍生效），仅去掉 DSP 侧动作 |
| code/ble_custom.c | `0xFE` 改为 `dsp_7100_cache_invalidate()` + 重启 |
| code/ble_rempro_cmd.c | 1069 → 550 行；15 个依赖 BS300 的命令改为回 `flag=1`（不支持），**待阶段二接 7100 恢复** |
| include/app.h | 删 `BS300_ENABLE` / `BS300_SYNC_TIMER` |

> 阶段二需把 `ble_rempro_cmd.c` 的验配命令重新落到 7100 的 A7 参数块（WDRC 0x177 / DFBC 0x132 /
> 降噪 0x00AE / EQ / AGCO）。参考配方见 `docs/7100_A7协议*.md`、`docs/7100协议/`。

## 8. 启动流程

`App_Initialize()`（code/app_init.c）：

1. 关中断、禁 JTAG DATA/TRST（释放 DIO）、等待 DIO13 释放
2. 48MHz 时钟 / RF / **（1664 已删电池 ADC）**
3. audiosink 计数 + 采样钟输入（DIO7）（无条件）
4. `#if OUTPUT_DECODE_PATH`：DSP 固件 Flash_Copy、DSS reset、设 codec message；
   `#if OD`：ASRC/OD/DMA 初始化（DIO0 OD_P、ch3/4/5）……
   —— **当前 `NO_TX_OUTPUT` 下整段落空**（不执行）
5. 10k 喂狗延时 → `BLE_Initialize()` → `App_Env_Initialize()` → `printf_init()`
   → **覆写打印口到 DIO12、释放 DIO5** → `APP_RM_Init(ear_side)`
6. `RF_SwitchToCPMode(); RM_Enable(1000);`（对齐 sleep：开机即进 RM）
7. DEBUG DIO（DIO15/DIO11）配置
8. Flash overlay + loop cache、`DEBUG_UART_LOG`（默认关）
9. 使能中断

`main()`（app.c）：

```
App_Initialize() → 打印 started → bs300_driver_init()
→ while(1){ Kernel_Schedule(); rempro_tx_poll(); rempro_cmd_process()（非流中）;
            RM_StatusHandler(); bs300_process_deferred(); 喂狗; SYS_WAIT_FOR_EVENT; }
```

## 9. RM 无线电配置（对齐 sleep）

- `RM_HOPLIST = { 3, 9, 15, 21, 24, 33, 36 }`（include/app.h）
- `accessword = 0x00cde629 | (0xf2<<24)` = `0xf2cde629`（rm_app.c）
- 其余 rm_param（interval 10000 / retrans 5000 / audio_rate 48 / radio_rate 2000 / scan 6500 /
  preamble 0x55 / renderDelay 200 / preFetch 1300(RM_APP_REQUEST) / pkt / 搜索阈值）与 sleep 一致。
- ⚠ 这些是收发对端配对参数：发射机与 1664 接收机必须一致才能建链。
- `APP_RM_AUDIO_CHANNEL = RM_RIGHT`（通道切右）。

## 10. 打印 / 调试

- 出口：pack `printf.h` 默认 `OUTPUT_UART` → pack `printf.c`（本机已改成 UART TX=DIO5 / RX=DIO6）。
  `printf_init()` 在 `App_Initialize()` 中调用，**随后被覆写为 TX=DIO12 / RX=DIO6**（见 §3.3）。
- bs300 内部日志：bs300_*.c 已在各自 `#ifndef PRINTF` 前 `#include <printf.h>`，`[BS300] …` 会输出；
  若想静音删除这几行 include。
- Rempro 日志：`ble_rempro*.c` 含 `<printf.h>`，可见 `[REMPRO RX/TX frame/chunk/push]` 等。
- 想看 RTT：工程已链 SEGGER_RTT，把 `OUTPUT_INTERFACE` 定义为 `1`（RTT）即可
  （需在每处 include printf.h 之前生效，一般放 app.h 顶部用数值 `1`）。
- ⚠ pack printf.c/h 是共享文件，改引脚/出口会影响所有 RSL10 工程。1664 的策略是**不改 pack、就地在 app_init 覆写**。

## 11. 关键文件清单

相对基线 `d9ddf9b` 的改动（均**未提交**）：

**新增**
- `code/i2c_7100_hal.c` / `include/i2c_7100_hal.h`
- `code/dsp_7100_init.c` / `include/dsp_7100_init.h`
- `code/dsp_7100_init_tables.c`、`code/dsp_7100_rb_tables.c`（Python 生成）
- `code/dsp_7100_storage.c` / `include/dsp_7100_storage.h`

**修改**
- app.c、code/app_init.c、code/app_process.c、code/ble_custom.c、code/ble_std.c、
  code/rm_app.c、code/ble_rempro_cmd.c、include/app.h、include/ble_rempro_cmd.h

**删除**
- `code/bs300_*.c`（9）、`include/bs300_*.h`（10）

## 12. 已知问题 / 待办

1. **阶段二未开始**：7100 写路径（动态 A7 编码 / 会话状态机）+ Rempro 15 个验配命令重映射，见 §17。
2. **上电握手无超时**（照 rx_coex）：板上无 7100 时卡在 `while(DIO_DATA->ALIAS[13] == 1)`，不退出。
3. `rempro_push_volume_change()` 无调用者（按键删除的副作用）。保留与否待定。
4. **采样钟脚位待硬件确认**：`SAMPL_CLK` 已按 7160test 改为 DIO3，需确认 1664 板实际接法（§5）。
5. **RM 调试 IO 已全部关闭**（`debug_dio_num[0..3]=0xff`）：DIO15 也不再翻转，RM 调试波形没了。
6. OD 引脚 DIO0/1 让给 7100 I2C；恢复 OD 直驱需重新选脚位（如 7160test 的 DIO12 单端方案）。
7. 读回缓存首次写入后即命中，**除非 0xFE 失效否则不会重读**；芯片侧参数变了需主动 0xFE。

## 13. 验证步骤

1. 编译 `remote_mic_rx_coex_1664` Debug，确认链接通过。
   （已用命令行 `arm-none-eabi-gcc -fsyntax-only` 全量核查：0 错误，仅 2 条既有告警。）
2. 烧录后用 **UART DIO12(115200)** 观察开机序列：
   `started` → `[IO] wait DIO13 low …` → `[IO] DIO13 high, run 7100 init`
   → `[7100-init] n/106 TX/RX …`（每步含收发字节）→ `[7100-init] sync pre-A7 done`
   → `[7100-cache] hit/miss`。
3. **首次开机**应见 `[7100-cache] miss` → `[RB] 1/28 …` 逐条读回 → `[RB] round done, parse`
   → `[7100-cache] saved to flash`。**再次开机**应见 `[7100-cache] hit`，且无 `[RB]` 读回。
4. 主循环应见 DIO9/10/13 电平变化打印：`[IO] D9=… D10=… D13=…`（照 rx_coex）。
5. 与已配对发射机建链：串口出现 `RM_LINK_ESTABLISHED/DISCONNECTED`。
   **当前无音频输出**（`NO_TX_OUTPUT`），不应期待听到声音。
6. 手机扫描应看到 **Smart1664** 广播；Rempro `GetBatteryInfo` 回 `flag=1`。
7. 示波器：**DIO0/DIO1 应有 I2C 波形**（SCL/SDA）；DIO12 打印 TX；DIO11 握手脉冲；DIO13 ready。
8. 发 `0xFE`：应见 `[7100] 0xFE: invalidate cache + reset to reload`，重启后重新走读回。

## 14. BLE 配置（参考 sleep：单设备连接）

- **单设备连接**：沿用 peripheral 单连接（`APP_IDX_MAX = 1`）；未移植 sleep 的左右耳 peer /
  BLE Central / 双耳 GATT 同步；Rempro 已单独移植（见 §15）。
- **设备名**：`APP_DFLT_DEVICE_NAME = "Smart1664"`（include/ble_std.h）；
  FOTA 开启时为 `Smart1664FOTA`。
- **地址配置**：`BD_ADDRESS_TYPE = BD_TYPE_PUBLIC`；`PRIVATE_BDADDR`、`APP_PUBLIC_BDADDR`、
  `RADIO_CLOCK_ACCURACY(500)` 照 sleep。
- **广播**：ADV 放设备名，可发现模式 `GAP_GEN_DISCOVERABLE`，广播间隔 160×0.625ms≈100ms；
  公司厂商段 18B 用 sleep 的 `APP_COMPANY_ID_DATA`，把「耳侧 + 设备 MAC」编入 company data。
  因名字 Smart1664 为 9 字符、ADV 放不下 18B 厂商段，MAC/耳侧数据改放 **scan response**。
- **bdaddr 修正**（code/ble_std.c）：PUBLIC 分支读到公共地址后不再被 `PRIVATE_BDADDR` 覆盖，
  读不到才回退 `co_default_bdaddr`。

## 15. Rempro 服务（手机验配）

**GATT 服务：只保留 Rempro**（Battery / Custom 已移除）

- `SERVICE_ADD_FUNCTION_LIST` 只剩 `RemproService_ServiceAdd`；`SERVICE_ENABLE_FUNCTION_LIST` 为 NULL。
- Rempro 的 ATT 读写路由复用 `ble_custom.c` 的 GATTC 处理器（按 `rempro_env.start_hdl` 区间分流）。
- **1664 起电池相关已全部移除**（Battery 不注册、无 ADC 采样、GetBatteryInfo 回不支持）。

| 项 | UUID |
|---|---|
| 服务 | `F36F8680-ABEC-11F1-8F9E-7265746F6E65` |
| 特征 ROLE（手机→设备 命令，Read/Write） | `F36F8681-ABEC-11F1-8F9E-7265746F6E65` |
| 特征 ONOFF（设备→手机 Notify） | `F36F8683-ABEC-11F1-8F9E-7265746F6E65` |

- 文件：`code/ble_rempro.c`（服务 DB）、`code/ble_rempro_cmd.c`（HDLC 验配协议：
  SetVolume/Scene/Gain/MPO/EQ/Denoise/Feedback/Audiometry/GetFitting/电池等，分块 Notify 发送）。
- 应用胶水：boot `RemproService_Env_Initialize`；主循环 `rempro_tx_poll()`（`Kernel_Schedule` 后）
  与连接态 `rempro_cmd_process()`；断链 `rempro_reasm_reset()`。
- **命令实现依赖 BS300**（见 §7）：移植 7100 时这部分是主要改动面。

## 16. FOTA 空中升级（照 sleep，CFG_FOTA 开关）

**机制**（同 peripheral_server_sleep）：FOTA ON 时 app 重定位到 `0x00130800`（前部预留给
boot + `fota.bin` 子镜像），启动向量 7/8 = `Sys_Boot_app_version` / `image_descriptor`，
Reset_Handler 在 SystemInit 后调用 `SystemFotaInit()`；BLE 侧在 Rempro ROLE 写入收到首字节
`0xFD` 时 `Sys_Fota_StartDfu(1)` 进入升级（`0xFD` 非 HDLC 帧头 `0x7E`，安全保留）。

**开关**：`include/app.h` 顶部 `//#define CFG_FOTA`（注释=关/开）；开启同时需替换 RTE 变体。
FOTA 开启时 BLE 广播名自动带标识 `Smart1664FOTA`（ble_std.h 按 `CFG_FOTA` 分支）。

**文件**（remote_mic_rx_coex_1664/ 下）：

- 代码：`code/fota_system.c`（SystemFotaInit→fota_init + weak Device_Param_Prepare）、
  `include/fota_system.h`；`ble_std.c` 里 `SYS_FOTA_VERSION(VER_ID,…)`（CFG_FOTA 时）生成版本符号；
  `ble_custom.c` Rempro ROLE 写 0xFD 触发。
- RTE/Device/RSL10：`startup_rsl10_fota.S` / `_nofota.S`、`sections_fota.ld` / `_nofota.ld`。
- 工程变体：`remote_mic_rx_coex_1664_fota.rteconfig` / `_nofota.rteconfig`、`.cproject_fota` / `.cproject_nofota`。

**切换方法**

- FOTA ON：
  ```
  cp RTE/Device/RSL10/startup_rsl10_fota.S  RTE/Device/RSL10/startup_rsl10.S
  cp RTE/Device/RSL10/sections_fota.ld      RTE/Device/RSL10/sections.ld
  cp remote_mic_rx_coex_1664_fota.rteconfig remote_mic_rx_coex_1664.rteconfig
  cp .cproject_fota .cproject
  # app.h 取消注释 #define CFG_FOTA
  ```
- FOTA OFF：把上面 4 个文件换回 `_nofota`（或 git 还原），并注释 `CFG_FOTA`。
- OFF 构建不依赖 FOTA 库/头，行为与普通固件一致（fota_system.c 由 `--gc-sections` 剥掉）。
- **默认状态 = OFF**。

## 17. 待办：7100 阶段二（写路径 + 验配命令）

**阶段一已完成**（§7）：通讯层、读回、flash 缓存、心跳。

**阶段二目标**：把 Rempro 的验配命令重新落到 7100 的 A7 参数块上，恢复被临时禁用的 15 个命令。

**待实现**

1. **动态 A7 写编码**：把「模块 + 通道 + 值」编码成 A7 写序列（选程序 → 选模块 → 准备 → 写值 →
   confirm → unmute → commit）。参考 `scripts/gen_dsp_7100_set.py` 的会话骨架与
   `docs/7100协议/` 下各模块文档。
2. **写会话状态机**：mute → select → 写 ×N → confirm → unmute → commit，异步推进 + deferred。
3. **Rempro 命令重映射**（`ble_rempro_cmd.c`，现均为 `flag=1`）：

   | 命令 | 需落到 7100 |
   |------|------|
   | SetVolume | WDRC bin_gain / 音量寄存器 |
   | SetEqualizer (EQ) | EQ 模块（`docs/7100协议/EQ/`） |
   | SetGain / SetMPO / SetCompressRatio | WDRC bin_gain / lmt / kp（`docs/7100协议/WDRC/`） |
   | SetDenoise | 降噪 0x00AE（`docs/7100协议/降噪/`） |
   | SetFeedbackOnOff | DFBC 0x132（`docs/7100协议/DFBC/`） |
   | SetCurrentScene / GetCurrentScene | 选程序 `A7 02 …12 <P>` + 读回解析 |
   | SetPlayVoice / SetStopVoice / SetAudiometryStatus | 需确认 7100 侧对应命令 |
   | GetFittingData | 读回解析（缓存已就绪，见 §7.2） |

4. **写后回写缓存**：写路径改完参数后应同步更新 flash 缓存，避免下次开机读回被旧值覆盖。

**参考（rx_coex 侧，未移植的部分）**

| 文件 | 作用 |
|------|------|
| code/dsp_7100_set_tables.c + scripts/gen_dsp_7100_set.py | 写会话样例（P01 WDRC LL 全通道置 0），38 条命令 |
| code/dsp_7100_parm_tables.c + scripts/gen_dsp_7100_parm.py | parm1604 命令表（105 条 A7） |
| code/dsp_connect_replay.c + `_tables.c` | 连接回放（rx_coex 里也被 .cproject 排除，未接线） |

> 阶段一已**剔除**上述死代码；阶段二按需重新引入。
