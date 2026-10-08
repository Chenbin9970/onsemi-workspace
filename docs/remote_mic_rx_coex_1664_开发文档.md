# remote_mic_rx_coex_1664 开发文档

## 1. 工程概述

`remote_mic_rx_coex_1664` 由 `remote_mic_rx_coex_1644` 复制而来，是 RSL10 远端麦克风接收机
（RM receiver，BLE + RM 共存）的 1664 机型分支。音频出口继承 1644 的 OD 直驱方案，但因 7100 I2C
占用 DIO0/DIO1，**已改为 PCM 从机输出**（`OUTPUT_INTRF = PCM_SLAVE_OUTPUT`）：7100 做时钟主机
提供 BCLK/FS，RSL10 从机在 SERO 移位输出，见 §3.4 与 §6。

与 1644 的差异（见 §3）：设备名改 `Smart1664`、**删除按键**、**电池 AD 采样从 DIO3 改到 DIO0
（与 7100 I2C 分时复用，§3.2）**、**打印口由 DIO5 改到 DIO12**、**音频输出改为 PCM 从机**。

> **7100 移植状态**：**阶段一（通讯层）已完成** —— Ezairo 7100 I2C 协议已移植进来并**整体取代了
> 原 BS300 子系统**（BS300 文件已删）。含上电握手、106 步引导、4 程序读回、flash 缓存、5s 心跳。
> **阶段二（写路径 + Rempro 验配命令重映射）进行中**：写会话状态机、写后回写缓存、
> 降噪 / DFBC / EQ / **WDRC（增益/MPO/HighLevel）** 及其读回、**纯音测听（CMD 40/13/14/21）** 均已实现。
> **切程序 / 调音量 / 测听（进入·出音·停音·退出）已上板验证通过**（见 §22.6）；
> App 侧尚在完善；**WDRC 与 EQ 尚未上板**，详见 §7.4.3 / §7.4.4 与 §17。
> 引脚按「音频输出关闭 + I2C 用 DIO0/DIO1」定案（见 §5）。

参考工程：`remote_mic_rx_coex_1644`（本工程直接来源）、`peripheral_server_sleep`（OD 输出路径
+ BS300 通讯来源）、`remote_mic_rx_coex`（7100 I2C 协议来源）。

## 2. 来源与 git 基线

| commit | 说明 |
|--------|------|
| `d9ddf9b` | 1664 工程基线：由 1644 复制（61 文件），设备名改 Smart1664/1664FOTA |
| `0ad0541` | 删除按键 / 删除 AD 采样 / 打印口改 DIO12 |
| `95896c1` | 移植 7100 通讯层取代 BS300 + 读回参数 flash 缓存 |
| `b6365f2` | Rempro 切模式 / 调音量接 7100 运行时命令 |
| （未提交） | 降噪 / DFBC 写入（tick 模型）+ GetBatteryInfo 回 100% + I2C 收发日志 |
| （未提交） | **音频出口改 PCM 从机**（DIO2/3/4/14，24k），见 §3.4 / §6 |

1664 与 1644 的源码差异仅有以上两笔提交的内容；`code/` 下其余文件与 1644 逐字节一致
（仅行尾符差异）。

## 3. 相对 1644 的改动总览

| 改动 | 涉及文件 | 说明 |
|------|----------|------|
| 设备名 `Smart1644` → `Smart1664`（含 FOTA 变体） | include/ble_std.h、include/app.h（`VER_ID`） | 广播名区分机型 |
| 工程名 / rteconfig / .cproject 改名 | .project、.cproject*、*.rteconfig、RTE/RTE_Components.h | 工程标识 |
| **删除按键** | app.c、code/app_init.c、include/app.h | 见 §5 |
| ~~**删除电池 AD 采样**~~ → **改为 DIO0 分时复用** | code/app_init.c、code/app_process.c、code/ble_rempro_cmd.c、include/app.h、include/ble_rempro_cmd.h、code/i2c_7100_hal.c/.h | 见 §3.2、§5 |
| **打印口 DIO5 → DIO12** | code/app_init.c、include/app.h | 见 §5、§10 |
| **音频输出 → PCM 从机** | include/app.h、code/app_init.c、code/app_func.c、code/rm_app.c | `OUTPUT_INTRF = PCM_SLAVE_OUTPUT`；DIO0/DIO1 留给 7100 I2C，见 §3.4、§6 |
| **RM 流中断静音 + PLC** | code/rm_app.c、code/app_func.c、include/app.h | 坏包/丢包重复上一帧好数据；连丢 2 包立即静音（不等 2s 后的 `LINK_DISCONNECTED`），见 §6.7 |
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

### 3.2 电池 AD 采样：DIO3 版已删 → 改为 DIO0 分时复用（2026-09-21）

**历史**：1664 复制自 1644 时删掉了 1644 的 **DIO3** 电池 AD 采样（`ADC_POS_INPUT_DIO3`、
`APP_Timer` 内 200ms 采样 + 16 次平均、`read_battery_raw()`、`BAT_ADC_*` 宏、`app_env.batt_lvl`
等字段），`GetBatteryInfo` 一度**固定回 `100/100`**。
**原因**：1664 的 DIO3 被 PCM FS 占用（§5），不能再做 ADC 输入 —— **不能照搬 1644 的 DIO3 方案**。

**现状（已实现，未标定）**：电池采样改到 **DIO0**，与 7100 I2C 的 SCL **分时复用**。

| 内容 | 位置 |
|------|------|
| `BAT_ADC_ENABLE`（0=关/1=开）、`BAT_ADC_CHANNEL/MIN/MAX`、`BAT_LVL_MAX` | include/app.h |
| `read_battery_raw()`：交接引脚 → 重配 ADC → 读 `DATA_TRIM_CH` → 交还 I2C | code/ble_rempro_cmd.c |
| `read_battery_raw()` 声明 | include/ble_rempro_cmd.h |
| `i2c_7100_pin_release_for_adc()` / `_restore_after_adc()`（DIO0 交接；`hw_apply_config()` 拆出） | code/i2c_7100_hal.c/.h |
| 低电量提示 **TODO**（1664 无 BS300，待定 7100 提示音命令） | code/app_process.c `APP_Timer` |

**`GetBatteryInfo`（ID:4）**：`BAT_ADC_ENABLE=1` 时**实采 DIO0**（低于阈值最低报 1%，不再固定 100%）；
`=0` 时维持固定 `100/100`（flag=0 —— 回 `flag=1` 会导致 App 连不上）。

**交接的两个关键点**：采样前 `Sys_I2C_Reset()` + `DIO_NO_PULL` 关掉 **RSL10 内部强上拉** ——
否则上拉把 1M+360k 的弱分压拉满，ADC 只会读到 ≈VDD；采完 `hw_apply_config()` 整体重配回 I2C
（引脚 + CTRL0 + 中断）。上层调用保证采样时无 I2C 传输在跑（HAL 读写都是阻塞式，同一任务上下文）。

> ⚠ **当前实测不可用（见 §12.15）**：1664 板上 DIO0 还挂着 **7100 侧内置的 10k I2C 上拉**，
> 而 1M+360k 分压的戴维南等效只有 265 kΩ，被 10k 上拉压住 —— 实测 raw 与电池电压不成比例。

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

> ⚠ pack 的 `printf.c` 未改动，1644 / 7160test 等其它工程的打印口仍为 DIO5。

### 3.4 音频输出改为 PCM 从机（DIO0/DIO1 留给 7100 I2C）

1664 的 OD 直驱占用 **DIO0(OD_P) / DIO1(OD_N)**，与 7100 I2C（DIO0=SCL / DIO1=SDA）冲突。
定案：**音频出口改走 PCM 从机**（参照 `peripheral_server_sleep7160test` 已验证实现）——
7100 做时钟主机提供 BCLK/FS，RSL10 只在 SERO 移位输出，因此**用不到 DIO0/DIO1**，I2C 独占之
（2026-09 起 DIO0 另与电池 AD 采样分时复用，见 §3.2）。

改动（include/app.h）：

```c
#define PCM_SLAVE_OUTPUT   6    /* 新增接口值 */
#define OUTPUT_INTRF       PCM_SLAVE_OUTPUT   /* 原 NO_TX_OUTPUT / OD_OUTPUT */
```

`OUTPUT_DECODE_PATH` 并入 `PCM_SLAVE_OUTPUT`，于是被打通：

| 打通的内容 | 位置 |
|---|---|
| DSP 固件 Flash_Copy、DSS reset、codec message 设置 | code/app_init.c（`#if OUTPUT_DECODE_PATH`） |
| ASRC 输入 DMA(ch3)、DSP1 / AUDIOSINK IRQ 使能 | code/app_init.c |
| PCM sink 初始化：`Sys_Clocks_SystemClkPrescale1`、`Sys_Audio_Set_Config(AUDIO_CONFIG_PCM)`、`Sys_PCM_ConfigClk`、ch5(PCM)/ch4(ASRC OUT) DMA | code/app_init.c（`#elif OUTPUT_INTRF == PCM_SLAVE_OUTPUT`） |
| 全部解码/ASRC 处理（`Rendering_func`、`DspDec_isr`、`Ascc_*_isr` 等） | code/app_func.c（整个 `#if OUTPUT_DECODE_PATH` 段） |
| RM 收包后的渲染调用 | code/rm_app.c |
| RM 建链/断链时的 PCM DMA 重武装 / 停止 | code/rm_app.c |

**保留不变**：audiosink 计数器与 DIO3 采样钟输入、RM 收发本身、BLE / Rempro / 7100 通讯全部照常。

**验证**（编译级，已做）：工程自带 makefile 全量构建通过（0 错误，告警数与改动前一致）；
预处理核对 `App_Initialize` 中 `Sys_PCM_ConfigClk(SLAVE,…)` 的参数为 2/3/4/14、
ch4/ch5 长度为 `PCM_FRAME_WORDS(120)`、**DIO0/DIO1 上无任何配置**（I2C 独占；DIO0 另有电池
AD 采样分时复用，见 §3.2）。

**回退方法**：把 `OUTPUT_INTRF` 改回 `OD_OUTPUT` 即可（OD 相关代码全在，仅被宏关掉）。
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
  - `OUTPUT_INTRF = PCM_SLAVE_OUTPUT`（**当前：PCM 从机输出**，见 §3.4、§6）；可改
    `OD_OUTPUT`（解码直出 OD，须重解 DIO0/DIO1 冲突）/ `SPI_TX_CODED_OUTPUT` / `SPI_TX_RAW_OUTPUT`
    / `NO_TX_OUTPUT`（无音频输出）。
  - （原 `BS300_ENABLE` 已随 BS300 删除；7100 子系统无总开关，始终编译）
  - `CFG_FOTA`：FOTA 开关，默认注释（关），见 §16。
  - `OUTPUT_INTERFACE`（在 pack 的 printf.h，未在本工程覆盖 → 默认 UART）：打印出口选择。

## 5. 引脚分配

| 功能 | 引脚 | 说明 |
|------|------|------|
| 7100 I2C SCL / SDA（addr 0x02） | **DIO0 / DIO1** | 原 OD_P / OD_N；音频改走 PCM 后腾出（§3.4）。见 [i2c_7100_hal.h:28-29](remote_mic_rx_coex_1664/include/i2c_7100_hal.h#L28-L29) |
| **电池 AD 采样（与上面 DIO0 分时复用）** | **DIO0** | `BAT_ADC_ENABLE` 控制；采样瞬间切成 ADC 输入，读完交还 I2C。⚠ DIO0 上另有 7100 侧 10k 上拉，当前读数不可用（§3.2、§12.15） |
| 7100 ready 输入（握手） | **DIO13** | 7100 上电拉低 → RSL10 等低 → DIO11 低脉冲 → 等高。见 [app.c](remote_mic_rx_coex_1664/app.c) |
| 7100 握手输出 | **DIO11** | RSL10 → 7100 应答脉冲（原 `DEBUG_DIO_SECOND`，已让出） |
| 7100 观察输入 | DIO9 / DIO10 | 仅置输入打印电平变化 |
| 采样 / audiosink 时钟输入 | **DIO3** | `SAMPL_CLK = PCM_FRAME_SYNC`，`Sys_Audiosink_InputClock()` 无条件配置；**与 7160test 一致**（原 DIO7） |
| PCM BCLK 输入 | **DIO2** | `PCM_CLK_DO`，7100 提供 384 kHz（§6） |
| PCM FS 输入 | **DIO3** | `PCM_FRAME_SYNC`，7100 提供 12 kHz；与 audiosink 采样钟同脚 |
| PCM SERI 输入 | **DIO4** | `PCM_SER_DI`，从机不回传，未用 |
| PCM SERO 输出 | **DIO14** | `PCM_SER_DO`；JTAG 已在 `App_Initialize` 运行时关（`CM3_JTAG_DATA/TRST` DISABLED）释放该脚 |
| 上电暂停 / 恢复(recovery) | **DIO7** | 接地暂停便于重刷；**与 7160test 一致**（原 DIO13 → 曾暂定 DIO2） |
| 调试 UART TX / RX | **DIO12** / DIO6 | 115200；在 `printf_init()` 后覆写（原 DIO5） |
| DIO_SYNC_PULSE | DIO8 | GPIO 默认输出（原 BS300 SCL，BS300 已删） |
| 已释放 | DIO5 | 原打印 TX，`DIO_MODE_DISABLE` |
| 空闲 / 预留 | DIO15 | `DEBUG_DIO_*` 宏已删（RM 调试 IO 关闭、DIO11 让给握手） |

> PCM 四脚（2/3/4/14）与 7100 I2C（DIO0/DIO1）**不重叠**，也与打印口（12/6）、
> 握手（11/13）、观察（9/10）无冲突。

> **与 7160test 完全对齐**：`SAMPL_CLK` 用 DIO3、`RECOVERY_DIO` 用 DIO7，PCM 四脚同为 2/3/4/14。
> ⚠ 这假定 1664 硬件的采样钟与 7100 的 BCLK/FS 实际接在 DIO3/DIO2 —— 若板上走线不同，需改宏。
>
> **恢复 OD 直驱会与 7100 I2C 冲突**（DIO0/DIO1），届时须改脚位。

## 6. 音频通路（PCM 从机输出）

> 本工程当前 `OUTPUT_INTRF = PCM_SLAVE_OUTPUT`（见 §3.4）。实现参照 `peripheral_server_sleep7160test`
> 已验证的 PCM 从机方案，文档见 `docs/pcm/7160test_pcm_output.md` + `docs/pcm/7160test_pcm_24k.md`。

### 6.1 主机接口规格

| 参数 | 值 |
|------|-----|
| 时钟角色 | **7100 是 clock master**，RSL10 做 PCM slave |
| BCLK | 384 kHz（DIO2 输入，7100 提供） |
| FS | 12 kHz（DIO3 输入，50% 占空比） |
| **有效采样率** | **24k** —— `WORD_SIZE_16 + MULTIWORD_2` → 每 FS 帧 2×16-bit = 32 BCLK（384k÷12k） |
| 数据 | 16-bit；每 32-bit 字**低 16 位**为采样（高 16 补零），7100 读 word1 |
| 输出脚 | SERO = DIO14 |

### 6.2 接收链路（RX）

```
RM 射频包 → RM_Callback_TRX(RM_RX_TRANSFER_GOODPKT)
         → Rendering_func(outTempBuff)      [app_func.c]
         → Start_Dec_Lpdsp32 → LPDSP32 G722 解码(16k) (DspDec_isr)
         → ch3 DMA: Dsp2CmBuff0dec → ASRC->IN
         → ASRC 重采样（INT_MODE 16k→24k 闭环，锁定 DIO3 的 12k FS，Ascc_phase/period_isr）
         → ch4 DMA: ASRC->OUT → pcm_tx_buf[pcm_fill]  (PCM_RX_DMA_ASRC_OUT, LIN)
         → ch5 DMA: pcm_tx_buf[pcm_ready] → PCM->TX_DATA (RX_DMA_PCM_STEREO, PCM_DMA_NUM=5)
         → SERO(DIO14) → 7100
```

> 包类型分发（`RM_Callback_TRX`）：GOODPKT 喂解码器并存入 `rm_last_good`；**坏包/丢包改喂
> `rm_last_good` 做 PLC**（损坏 payload 绝不进解码器）；连续丢 2 包立即静音 —— 见 §6.7。

### 6.3 双缓冲握手（ch4 / ch5 ISR）

`pcm_fill` = ch4 正在填的 buf；`pcm_ready` = ch5 待流的 buf（`0xFF` = 无）；`pcm_waiting` = ch5 空闲。

```
初始化    pcm_fill=0, pcm_ready=0xFF, pcm_waiting=1；武装 ch4（填 buf0）；使能 ch4 ISR；ch5 只配不使能
ch4 完成  pcm_ready=pcm_fill; pcm_fill=1-pcm_fill; if (pcm_fade_in) 该块就地淡入; 重武装 ch4（填新 buf）
          if (pcm_waiting) { pcm_waiting=0; 武装 ch5(pcm_ready); 使能 ch5; pcm_ready=0xFF; }
ch5 完成  if (pcm_break) 续流 pcm_zero_buf（静音，§6.7）;
          else if (pcm_ready != 0xFF) { 武装 ch5(pcm_ready); 使能 ch5; pcm_ready=0xFF; }
          else pcm_waiting = 1;        /* 等 ch4 完成中断来启动 */
```

**ch5 不在初始化时使能**，必须由 ch4 完成中断在 `pcm_waiting` 时启动 —— 避免首帧竞争。

`pcm_break`（流中断静音）/ `pcm_fade_in`（恢复淡入）是在这套握手上加的两个旁路，
由 RM 丢包判定驱动，见 §6.7。

### 6.4 逐文件改动

| 文件 | 改动 |
|------|------|
| include/app.h | 新增 `PCM_SLAVE_OUTPUT(6)` 并设为 `OUTPUT_INTRF`；`OUTPUT_DECODE_PATH` 并入该值；PCM 四脚宏改为 2/3/4/14；`PCM_CFG_TX`；`PCM_DMA_NUM(5)`、`PCM_FRAME_WORDS(3*FRAME_LENGTH/4=120)`、`PCM_DOUBLE_BUFFER`；`PCM_RX_DMA_ASRC_OUT` / `RX_DMA_PCM_STEREO`；`AUDIO_CONFIG_PCM`（去掉 `OD_ENABLE`）；`pcm_tx_buf` 与 `pcm_fill/ready/waiting` 的 extern；流中断静音的 `pcm_break` + `Pcm_Stream_Break/Resume` 声明（§6.7） |
| code/app_init.c | `pcm_tx_buf[2][PCM_FRAME_WORDS]` 定义；`Initialize_Raw_PCM_Output_Type()`（`Sys_PCM_ConfigClk` + `Sys_PCM_Config` + ch5 DMA 配置）；`App_Initialize` 加 `#elif (OUTPUT_INTRF == PCM_SLAVE_OUTPUT)` 分支；`BBIF->CTRL` 稳态改 `BB_DEEP_SLEEP`（§18） |
| code/app_func.c | `pcm_fill/ready/waiting` 定义；`Asrc_reconfig` 加 PCM 分支（`INT_MODE` + 闭环 `2Ck`）；`Pcm_asrc_out_dma_isr()` / `Pcm_tx_dma_isr()`；`DMA4/DMA5_IRQHandler` 别名；流中断静音 `pcm_ramp_block` / `Pcm_Stream_Break` / `Pcm_Stream_Resume`（§6.7） |
| code/rm_app.c | `LINK_ESTABLISHED` 重武装 ch4/ch5 + `Sys_PCM_Enable()`（对应 7160test 的 `Audio_Resume`）；`RM_Callback_TRX` 按 `type` 分发（`rm_last_good` + PLC + 连续丢包判定静音）；`LINK_DISCONNECTED` 兜底静音后再停 ch5（§6.7） |

> DIO14 的 JTAG 释放在 `App_Initialize` 开头已有（`CM3_JTAG_DATA/TRST` DISABLED），无需新增。

### 6.5 关键宏

| 宏 | 值/说明 |
|----|---------|
| `PCM_CFG_TX` | MSB_FIRST \| TX_ALIGN_LSB \| **WORD_SIZE_16** \| FRAME_ALIGN_FIRST \| FRAME_WIDTH_LONG \| **MULTIWORD_2** \| SUBFRAME_ENABLE \| CONTROLLER_DMA \| DISABLE \| SELECT_SLAVE |
| `PCM_DMA_NUM` | 5（与 `OD_DMA_NUM` 同值，两模式互斥） |
| `PCM_FRAME_WORDS` | `3 * FRAME_LENGTH / 4` = 120 字 = 120 采样 = 5ms/缓冲 |
| `PCM_RX_DMA_ASRC_OUT` | `SRC_ASRC` / `P_TO_M` / `SRC16` / **`DEST16`** / `LIN` / 完成中断 |
| `RX_DMA_PCM_STEREO` | `DEST_PCM` / `M_TO_P` / 32→32 / `LIN` / 完成中断 |
| `AUDIO_CONFIG_PCM` | 同 `AUDIO_CONFIG` 但**无 `OD_ENABLE`** |

### 6.6 易错点

- **ASRC 必须 `INT_MODE` + 闭环 `2Ck`**（`inc = (Cr - 2Ck)<<29 / 2Ck`，Ck 异常回退 `0xF5555556`）：
  硬编码名义 2:3 会因 7100 时钟偏差造成周期性欠载/溢出爆音。这是与 OD 分支（`DEC_MODE1`）最大的差异。
- **`PCM_SER_DO` 绝不能用 DIO1** —— 那是 7100 I2C 的 SDA（原模板值恰为 1，已改 14）。
- **DMA 用 LIN 不用 CIRC**：ch5 用 CIRC 会在回绕边界欠载。
- **`AUDIO_CONFIG_PCM` 必须去掉 `OD_ENABLE`**，否则 OD 输出与 DIO0/DIO1 的 I2C 打架。
- **音频收尾不能挂在 ch4 完成中断上**：ASRC 停/输入枯竭时 ch4 不再完成，挂在上面的淡出
  **永远不会触发**；必须由 ch5（7100 外部时钟驱动，必然完成）来驱动，见 §6.7。
- **坏包 payload 不能喂解码器，但也不能不管**：RM 库对 `BADCRCPKT`/`NOPKT` 也带**非 0** 的
  `packet_length`，按 `*length == 0` 判「无包」永远不成立；损坏 payload 会被 G.722 解成爆音。
  正确做法是**按 `type` 分发**：好帧存 `rm_last_good` 供 PLC，坏包/丢包重复它，见 §6.7。

门控宏：`OUTPUT_DECODE_PATH = (OUTPUT_INTRF==SPI_TX_RAW_OUTPUT || ==OD_OUTPUT || ==PCM_SLAVE_OUTPUT)`，
用于 app.h / app_init.c / app_func.c / rm_app.c 中所有「解码 + ASRC 初始化」的 `#if`。

> **OD 直驱仍保留在代码里**（`#elif (OUTPUT_INTRF == OD_OUTPUT)`）：数据流为
> ASRC(**DEC_MODE1**, 锁定 DIO7 采样钟) → ch4 → BufferOut(CIRC) → ch5 → `AUDIO->OD_DATA` → DIO0/DIO1。
> 切回需 `OUTPUT_INTRF = OD_OUTPUT`，并重解 DIO0/DIO1 与 I2C 的冲突。

### 6.7 RM 流中断静音（断开杂音修复，2026-09 已上板）

**现象**：TX **持续推流中被硬断电**（掉电/出范围），RX 出一段 1~2 秒的断续杂音（听感「咔/啪」）。
TX 端只是把音量调小（流没断）时不出现。

**根因**：TX 消失后不再有新解码数据，但整条 PCM 流水照跑 —— ASRC 输入枯竭后输出极限环/残留
（与 rawtest1 那个「静态蚊蚊」同源），被 ch4 → ch5 一路送到 7100；而停机挂在 `LINK_DISCONNECTED`
上，RM 库要**丢满 `pktLostHighThrshld = 200` 包（≈2s）** 才判掉线，这 2 秒没人管。

**机制**（触发点见 code/rm_app.c 的 `RM_Callback_TRX` / 两个状态回调）：

| 触发 | 动作 |
|------|------|
| GOODPKT | 存 `rm_last_good`（供 PLC 用）→ 喂解码器 → `rm_stream_good()`：解除静音 + 下一块淡入 |
| 坏包 / 丢包（BADCRC / NOPKT） | **PLC：重复 `rm_last_good`** → 喂解码器（已静音时不喂）；`rm_stream_loss()` 计数 |
| 连续 `RM_STREAM_BREAK_LOSS_N`(=2) 个非好包 | `Pcm_Stream_Break()` 静音 |
| `LINK_DISCONNECTED` | 兜底再 `Pcm_Stream_Break()`，然后停 ch5 |

> PLC 取舍：**单包丢失用「重复最后一帧好数据」把听感接上**（即库注释 "repeat previous packet"
> 的意图），所以只有连丢 ≥2 包才静音 —— 既避免单包丢失的顿挫，也避免长时间重复同一帧变成卡带音。
> `rm_last_good` 长度为 `sizeof(outTempBuff)`（与既有的裸 `memcpy(..., *length)` 暴露面一致）。

`Pcm_Stream_Break()`（app_func.c）：停采 ASRC（关 ch4 + 其 NVIC）→ 就地淡出两块 `pcm_tx_buf`
（`pcm_ramp_block`）→ ch5 **直接改流全 0 的 `pcm_zero_buf`**。这样到 `LINK_DISCONNECTED` 停 ch5 时，
移位器残留字已经是 0，不再留台阶。（`Pcm_Stream_Resume` 反向：重采 ASRC、回双缓冲、`pcm_fade_in`
让恢复后的第一块淡入。）

⚠ **为什么必须由 ch5 驱动，不能挂在 ch4 上**：ch4 的完成中断依赖 ASRC 还在产出。ASRC 一旦停/
枯竭，ch4 就不再完成，任何挂在上面的淡出**永远不会触发**（本问题第一次尝试正是这么失败的：
`LINK_DISCONNECTED` 里置标志、等 ch4 来淡出）。ch5 由 7100 的 BCLK/FS 外部驱动，只要 PCM
使能就必然完成。

⚠ **坏包 payload 绝不能直接喂解码器**：RM 库对 `RM_RX_TRANSFER_BADCRCPKT` 和 `NOPKT` 也带**非 0**
的 `packet_length`（`rm_pkt_hdl.c:812-826` 三种类型都传 `&rm_env.packet_length`），所以
`RM_Callback_TRX` 里 `if ((*length) == 0)` 那条「无包」分支**永远不会走** —— 损坏 payload 会被
G.722 解成满量级爆音。现按 `type` 分发：好帧存进 `rm_last_good` 再喂；坏包/丢包改喂
`rm_last_good`（PLC 重复），损坏数据一字节都不进解码器。

**已知取舍**（可接受，出问题从这里查）：
- `DMA_CTRL1[]` 只有**编程长度**、没有剩余传输计数 → **读不到 ch5 正播到缓冲哪个位置**，只能整块
  先淡、再切 0。切换点恰在缓冲边界时，断开瞬间仍可能有**一声轻「咔」**。
- **连续丢 2 包即静音**：弱信号下偶发连丢会带来一次「静音 → 淡入」的短暂下沉。**回归重点** ——
  必须有 GOODPKT 把它拉回来，否则会永久静音。
- 多占 **480B RAM**（`pcm_zero_buf`）。RAM 紧时可改成借用 `pcm_tx_buf` 某一块清 0，代价是淡出质量。

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
| code/dsp_7100_cmd.c / include/dsp_7100_cmd.h | 运行时命令：切程序 / 音量 / 测听 / 纯音（异步，只发写帧+82，§7.4.1 / §22）+ 降噪 / DFBC / EQ / WDRC（异步命令表），见 §7.4 |
| app.c | 上电握手（DIO13/DIO11）+ `dsp_7100_boot_init()` + `dsp_7100_cache_try_load()`；主循环 `dsp_7100_process_deferred()` |
| code/app_process.c | `APP_7100_HB_Handler`：200ms tick —— 会话中推会话，否则推读回 + 每 5s 心跳 `{0x88,0x01}` |
| code/ble_std.c | GAPM_RESET 后挂 `APP_7100_HB_TIMER` |
| code/ble_custom.c | Rempro ROLE 写 `0xFE` → 失效缓存 + 重启重读 |

移植时**去掉了 rx_coex 的死代码**：`dsp_7100_parm_seq_tick` / `dsp_7100_a7_seq_tick` /
`dsp_7100_a7_arm/poll` / `dsp_connect_replay`（全部无调用者）。
写路径参考其在 `dsp_7100_init.c` 的写会话骨架（见 §7.4.2）。

### 7.2 读回结果（RAM + Flash 缓存）

每程序 3 块 payload：WDRC 375B / DFBC 306B / 降噪 174B（不含 `46 <lo> <hi>` 头）。

**Flash 缓存**（复用 BS300 原程序区，该区已空出）：

| 地址 | 内容 |
|------|------|
| `0x0015D000` / `0x0015D800` / `0x0015E000` / `0x0015E800` | Program 0..3，各 2KB sector |

**只存解析后的参数，不存原始 block**（原始 855B → 参数 54B，省 ~94%）。

| 偏移 | 长度 | 内容 |
|------|------|------|
| `[0]` | 1 | `denoise_en` |
| `[1]` | 1 | `denoise_lvl`（0..4） |
| `[2]` | 1 | `dfbc_en` |
| `[3..5]` | 3 | `eq_low` / `eq_mid` / `eq_high`（int8，App **上次下发的绝对值** ±dB） |
| `[6..21]` | 16 | `wdrc_ll[16]`（**设备当前值**，已含 EQ） |
| `[22..37]` | 16 | `wdrc_hl[16]`（**设备当前值**，已含 EQ） |
| `[38..53]` | 16 | `wdrc_ol[16]` |
| `[54..57]` | 4 | magic `"D71P"` |
| `[58]` | 1 | version（**v5**） |
| `[59]` | 1 | valid `0xA5` |
| `[60..61]` | 2 | CRC16-XMODEM（覆盖 `[0..53]`） |

槽固定 64B（16 word），整扇区擦除后重写。RAM 侧同样只保留解析结果
（`dsp_7100_rb_bufs_t` = 4 × `dsp_7100_prog_t`），原始块读到即弃 —— RAM 也从 3.4KB 降到 204B。

> **v5 语义变更**：`wdrc_ll/hl` 由「不含 EQ 的**基准**」改为「**设备当前值**（含 EQ）」。
> 缓存里存的就是 7100 里的值，所以读回不会污染、`GetGainData` / `GetHighLevelGainData`
> 报出的数字与实际听到的一致（**所见即所得**）。`eq_*` 保留 App 上次下发的**绝对值**，
> 仅用于算下一次的差值（见 §7.4.2）。v4 及更早的槽与本版不兼容，自动失效重读。

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

### 7.4 运行时命令（切程序 / 音量 / 降噪 / DFBC / WDRC / 纯音测听）

移植自 `peripheral_server_sleep7160test` + `remote_mic_rx_coex` 已验证写法。

#### 7.4.1 切程序 / 调音量（异步会话，**只发写帧 + 82**）

抓包波形（`peripheral_server_sleep7160test` 的 program1-4.csv / volume.csv）里原本是：

```
写帧  A2 00 <reg> <val>        reg: 0x16=程序, 0x12=音量
读回  43 03 00 00 <reg> <val>  （从机确认）
结束  82
```

**2026-09-30 起实际只发写帧 + 82**，读确认 / 读状态都不发（原因与实测证据见 §22）：

| 命令 | 线上序列（2 帧） | 参数 |
|------|------------------|------|
| `dsp_7100_set_volume(L)` | `04 A2 00 12 <v>` → `04 82` | L = 1..6，v = `{0x11,0x21,0x32,0x43,0x53,0x64}` |
| `dsp_7100_switch_program(P)` | `04 A2 00 16 <P>` → `04 82` | P = 1..4 |

线上首字节 `04` = 7 位地址 `0x02` 左移补的，由 `i2c_7100_write` 内部加，日志里不显示。

异步会话 —— 与读回同一套推进模型：主循环 `dsp_7100_cmd_poll()` 在 DIO13 边沿上推进，
200ms tick 只做兜底；返回 `true` 仅表示**已受理**，不等于完成（看日志
`[7100] --- session done ok=? ---`）。同一时刻只允许一个会话。

> ⚠ **`ok=1` 只代表 I2C 写成功，不代表 7100 照做了** —— 简化后命令不再被校验。
> 副作用：不再阻塞主循环 80ms，`dsp_7100_cmd_busy()` 窗口从 ~90ms 缩到 ~10–20ms。

#### 7.4.2 降噪 / DFBC / 均衡器（异步，命令表 + 200ms tick）

> **验证状态**：降噪 / DFBC **已上板验证通过**；
> **均衡器（EQ）尚未上板测试** —— 通道映射、±10dB 钳位、Value 语义均为待验证假设，见下。

⚠ **必须照 `remote_mic_rx_coex` 已验证的 tick 模型**，不要自创：

- **无任何 0x03 状态读**（rx_coex 实测通过的会话表里一条都没有；加了会导致写入不生效）
- 应答**固定读 3B**（`46 00 00`）
- 一条命令一个 tick：**发命令 → 下个 tick 读应答 + 写 `82` → 进下一条**
- 命令数：降噪/DFBC = 8（16 tick ≈ **3.2s**）；EQ 低/中/高**都是 14**（≈ **5.6s**）

会话骨架（与 rx_coex 写会话逐条一致）：

```
静音      A7 01 00 00 00 25
选程序    A7 02 00 00 00 12 <P>          P = 程序号+1
写块准备   A7 05 00 00 00 05 <blk> <P> <a> <b>
写块      降噪: A7 27 <300B>
          DFBC: A7 04 00 00 00 08 00 00 <X>   X = 01开/00关
confirm   A7 02 00 00 00 10 <P>
解除静音   A7 01 00 00 00 26
选回程序0  A7 02 00 00 00 12 01          （恒为 01，与程序无关）
commit    A7 01 00 00 00 0C
```

| 块 | blk | a | b |
|----|-----|---|---|
| 降噪 | `09` | `00` | `01` |
| DFBC | `0A` | `00` | `00` |

**降噪 300B 写块按公式生成**（不占表空间，`build` 见 `a7_add_noise_block`）：

| 段 | 字节 | 内容 |
|----|------|------|
| 头 | `[0..7]` | `A7 27 01 00 00 08 00 00` |
| 档位值 | `[8..152]` | 49 × `XX`（stride 3，中间补 0），`XX = 3×level + 3` |
| 三元组 | `[153..299]` | 49 × 档位三元组 |

| 档位 | XX | 三元组 |
|:---:|:---:|---|
| 0 | `03` | `2F 6E CD` |
| 1 | `06` | `4C 58 CB` |
| 2 | `09` | `60 D1 00` |
| 3 | `0C` | `6F 4E C9` |
| 4 | `0F` | `79 91 1C` |

> 已与 `docs/7100协议/降噪/7100_降噪设置.md` 的 5 档 hex dump **逐字节比对通过**；
> 会话事务序列与抓包 `p0dfbc0-1` / `noise0-1` **14/14 一致**。

**驱动与互斥**（`app_process.c` `APP_7100_HB_Handler`）：

```c
if (dsp_7100_cmd_busy()) {   /* 写会话中：只推会话 */
    dsp_7100_cmd_tick();
    if (!dsp_7100_cmd_busy()) {
        rempro_pending_start_next();  /* 刚跑完 → 起下一条缓存的命令 */
    }
    return;                  /* 不读回、不发心跳，避免抢 I2C */
}
dsp_7100_rb_seq_tick();
/* 每 5s 心跳 */
```

**均衡器（EQ）→ WDRC LL/HL**

App `SetEqualizer` 是 3 段（低 ≤500Hz / 中 500-2000Hz / 高 >2000Hz），
实测要求落到 WDRC 通道的低/高电平增益上，**1dB/LSB**：

| 段 | 数组下标 | 数量 | 写入参数 | 会话命令数 | 时长（静音） |
|----|------|:---:|---------|:---:|:---:|
| 低音 | **{1, 2}** | 2 | LL + HL | 14 | 5.6s |
| 中音 | **{3, 4}** | 2 | LL + HL | 14 | 5.6s |
| 高音 | **{6, 7}** | 2 | LL + HL | 14 | 5.6s |

命令数 = 通道数 × 2 参数(LL/HL) × 2 命令(准备+写) + 6（静音/选程序/confirm/解除/选回/commit）。
一条命令跨 2 个 tick（400ms），故时长 = 命令数 × 0.4s。

> ⚠ **下标 0 与 5 不参与 EQ**（2026-09-20 由 `{0,1}/{2,3,4}/{5..15}` 改为上表）。
> 三段各 2 通道，时长统一 **5.6s**；原先高音段 11 通道的 **20s 静音问题随之消失**。

> **待验证**：下标是「数组序号」，与 7100 侧通道号的对应关系（是否 1-based）**未上板确认**；
> 三段的频率边界（≤500 / 500-2000 / >2000Hz）是否真落在这些通道上也未验证。

**参数号**（`docs/7100协议/WDRC/7100_WDRC设置.md` 已验证）：

```
LowLevelGain (ch N) = 0x15 + 0x11×(N−1)
HighLevelGain(ch N) = LowLevelGain + 2
```

已核验：ch2→`0x26/0x28`、ch5→`0x59/0x5B`、ch10→`0xAE/0xB0`，与文档实测点全等。

**写入方式（增量模型，2026-09-20 定）**：App 下发的是 EQ 的**绝对值**，设备侧只加「与上次的差值」：

```
delta = 本次绝对值 − 缓存里上次保存的绝对值      /* 低频 1→2 ⇒ +1；1→−1 ⇒ −2 */
目标 LL = clamp(设备当前 LL + delta, -30, 60)   /* 1 LSB = 1 dB，8bit 补码 */
目标 HL = clamp(设备当前 HL + delta, -30, 60)
之后：缓存 wdrc_ll/hl 写成新值，eq_* 写成本次绝对值，一起落盘
```

- 缓存里的 `wdrc_ll/hl` **就是设备当前值**（含 EQ），所以 `0xFE` 重读**不会**污染 —— 读回来还是它。
- 基准由 `设备当前值 − 上次绝对值` 隐式还原，不需要单独保存「纯净基准」。

写命令格式（照 WDRC 设置文档）：

```
A7 05 00 00 00 05 07 <P> <addr_hi> <addr_lo>   ← 准备
A7 04 00 00 00 08 00 00 <val>                   ← 写值
```

单次 EQ 会话 = 静音 + 选程序 + N 通道×(LL prep/write + HL prep/write) + confirm + 解除静音 + 选回 + commit，
N = 2（低/中/高都是 2）→ **14 条**。

> App `Value` 语义：0-100 = 正 dB；`>100` → `Value-256` 得负值（即 253 → -3dB）。
> 本实现钳位 **±10 dB**（`A7_EQ_MAX_DB`，超出打印告警）。
> 依赖缓存：若该程序尚未读回（`dsp_7100_get_prog()` 返回 NULL），EQ 会话拒绝启动并打日志。

> ⚠ **可用范围受 WDRC 余量限制**：`clamp` 后若撞到 ±30/60 边界，用户再调同方向**不会有效果**，
> 且段内各通道顶死的时机不一致 → 频响会被压歪。待优化，见 §12。

**EQ 待验证项（尚未上板）**

| 项 | 当前假设 | 验证方法 |
|----|---------|---------|
| 通道下标→7100 通道号 | 下标直接就是通道号（是否 1-based **未确认**） | 改小值后读回对应通道看是否变化 |
| 参数号公式 | `0x15 + 0x11×(N−1)`（文档已验证 ch2/5/10） | 下标 6/7 的参数号未被文档覆盖，需实测 |
| 三段频率归属 | 低{1,2} / 中{3,4} / 高{6,7} 与 ≤500 / 500-2000 / >2000Hz 对应 | 分段扫频确认 |
| dB 钳位 | ±10（`A7_EQ_MAX_DB`） | 发大值看是否钳位 |
| Value 语义 | 0-100 正、>100 取负 | 手机端实际下发值 |
| LL/HL 同步平移 | 段内每通道 LL 与 HL 都加同一个 delta | 听感 + 读回 |

**命令缓存（I2C 忙时不丢命令）**

写会话要跑 3.2~5.6s，期间来的设置命令**不拒绝**（`ble_rempro_cmd.c` `s_pend[3]`）：

| 情况 | 行为 |
|------|------|
| I2C 空闲 | 立即启动会话，回 `Flag=0` |
| I2C 忙，**同类型**命令 | **覆盖**缓存里那条旧值（同类合并），回 `Flag=0` |
| I2C 忙，**异类型**命令 | 占一个空槽，回 `Flag=0` |
| 参数非法（len / prog / eq_type / 档位） | 立即回 `Flag=1`，不入缓存 |

会话结束（`APP_7100_HB_Handler` 检测到刚跑完）→ `rempro_pending_start_next()` 起下一条。
覆盖是安全的：均衡器/降噪/DFBC **都是绝对值语义**，丢中间帧不影响终态（用户拖滑块只跑最后一次）。

> `SetGain` / `SetHighLevelGainData` / `SetMPO`（§7.4.3）**不在**缓存范围，I2C 忙时仍回 `Flag=1`。

**API 语义**：`dsp_7100_set_denoise/dfbc/eq()` 返回 true = **已受理**（异步），不是已完成。
应答**立即**发（`Flag=0` 只表示受理）；是否真写下去看日志 `[7100] --- session done ok=? ---`。
完成后 `a7_session_finish()` 回写 RAM 参数并请求落盘。

#### 7.4.3 WDRC 参数写 / 读（SetGain / SetMPO / SetHighLevelGainData）

> **已实现，未上板验证。** 三条 Rempro 验配命令落到 7100 的 WDRC 参数，走 §7.4.2 同一套 tick 会话模型。

| Rempro 命令 | → 7100 WDRC | 参数号 (ch N) | 值字段 |
|---|---|---|---|
| `SetGain` (6) | **LowLevelGain** | `0x15 + 0x11×(N−1)` | 1 字节，8bit 补码 |
| `SetHighLevelGainData` (29) | **HighLevelGain** | `0x17 + 0x11×(N−1)` | 1 字节，8bit 补码 |
| `SetMPO` (7) | **OutputLimit** | `0x18 + 0x11×(N−1)` | **4 字节**，见下 |

**值换算**（App 档位 → 7100 dB，1 LSB = 1 dB，纯偏移）：

| 命令 | App 值域 | 换算 | 7100 值域 |
|---|---|---|---|
| SetGain | 0-90 | `− 30` | −30 ~ 60 |
| SetHighLevelGainData | 0-90 | `− 30` | −30 ~ 60 |
| SetMPO | 0-60 | `− 60` | −60 ~ 0 |

**索引**：payload 的 `Spectrum` / `Channel` 取 **0-15**，直接对应 7100 ch1-16（`idx + 1`）；16-31 忽略。

**多通道共用一次会话**（关键）：

```
静音 → 选程序 → [选块 + 写值] × n → confirm → 解除静音 → 选回 → commit
```

公共帧只发一次，每通道只多两条命令。命令数 = `6 + 2n`，时长 ≈ `(6+2n) × 0.4s`：
**n=1 → 3.2s；n=16 → 15.2s（全程静音）** —— 会话期间读回与心跳暂停，属已知取舍。

**API**（`include/dsp_7100_cmd.h`）：

```c
typedef struct { uint8_t ch; int8_t val; } dsp_7100_wdrc_item_t;   /* val = 7100 dB 绝对值 */

bool dsp_7100_set_low_level_gain (uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n);
bool dsp_7100_set_high_level_gain(uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n);
bool dsp_7100_set_output_limit   (uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n);
```

- `prog` 1-4，`n` 1-16；返回 true = **已受理**（异步，同 §7.4.2 的 API 语义）。
- 值在进入会话时**就钳位**（LL/HL `−30..60`、OL `−60..0`），写入与收尾回写缓存共用同一份值。
- items **拷贝进模块静态数组** —— 会话异步跑，调用方的 BLE 负载缓冲到收尾时可能已失效。
- 成功后回写 `wdrc_ll/hl/ol[]` 并请求落盘（满足 §17 第 4 条）。
- **与 `set_eq` 的交互（2026-09-20 起已自洽）**：本接口与 `set_eq` 写的都是**设备当前值的绝对值**，
  缓存 `wdrc_ll/hl` 语义统一。直接改 WDRC 后再设 EQ，EQ 会以新值为基准加差值 —— 不再有
  「差一个 eq 量」的问题。但 `eq_*` 记录的仍是 App 上次下发的值，两者独立。
- ⚠ 本接口**不进命令缓存**（§7.4.2）：I2C 忙时直接回 `Flag=1`，命令被丢弃。

**OutputLimit 的值字段是 4 字节**（LL/HL 只有 1 字节）：

```
A7 07 00 00 00 08 00 00 <b0> <b1> <b2> <b3>
b0 = OL 本身（8bit 补码）
b1 = OL + K_ch          K_ch 逐通道常量（−24…−13）
b2 / b3 = 逐通道常量，与 OL 无关
```

`b1`~`b3` 取自默认表 `s_wdrc_ol_tail[16][3]`（OL = −6 状态抓包）。
⚠ 该表只来自**一次**默认状态抓包 —— 若这些字节被其它工具改过即失准。
16 通道表与抓包验证见 `docs/7100协议/WDRC/7100_WDRC设置.md` §3.2。

**读回**（同批实现）：

| 命令 | 应答（`Flag + Channel_Number + {Index, Value} × N`） |
|---|---|
| `GetGainData` (22) | `Value = LowLevelGain + 30` |
| `GetHighLevelGainData` (30) | `Value = HighLevelGain + 30` |
| `GetMPOData` (23) | `Value = OutputLimit + 60` |

数据取读回缓存 `dsp_7100_get_prog()`；**读回未完成或程序无效时回 `Flag=1`**，App 需重试。
（Get 类应答按协议文档**不带 status**，与设置类不同。）

> 协议细节（payload 布局、SYS_ID 命名空间陷阱）：`docs/7100协议/瑞听设置指令.md`；
> 7100 侧帧格式、参数号实测、OL 默认表：`docs/7100协议/WDRC/7100_WDRC设置.md`。

#### 7.4.4 纯音测听 / 静音 / 开关机（CMD 40 / 13 / 14 / 21 / 3）

> **已上板验证通过**（App 侧尚在完善中）。7100 侧帧格式与电平模型见 `7100协议/纯音测听.md`。

**三条 7100 命令**：

| 用途 | 帧 | 机制 |
|------|-----|------|
| 出纯音 | `A7 07 00 00 00 2E 01 <freq16-BE> <level24-BE>` | 写帧 + `82`，**不读应答**（§22.7） |
| 停音 | `A7 07 00 00 00 2E 00 00 00 00 00 00` | 同上 |
| 静音/解静音 | `A7 01 00 00 00 25` / `26` | 写帧 → 读 3B → `82`（**未简化**） |
| 测听模式寄存器 | `A2 00 2E <00/58>` | 测听会话第 ② 句，写后等 2ms → `82`（§22.5） |

电平 = `round(4.39902 × 10^((db + C(freq))/20))`，`C(f)` 逐频率整数修正。
**已标定 6 个频点**：500/1000/2000/3000/4000/6000 Hz（其余 11 个返回 false → App 收 `Flag=1`）。

**BLE → 7100 流程**

```
CMD 40 {DevType, 0} 进入测听     ← 先回应答，再做 I2C；三句一个会话，都不读应答
   ├ A2 00 16 03       → 等80ms → 82   切程序 3
   ├ A2 00 2E 00       → 等 2ms → 82   测听位开
   ├ A7 01 00 00 00 26 →         82   解除静音
   └ 延 1s → 推 SYS_ID=1 CMD 6 {DevType, Initial_Status=2}

CMD 13 {DevType, Spectrum, Decibel}      出音（Spectrum 0-16 → 250…8000 Hz，dB 20-100）
CMD 14 {DevType}                         停音

CMD 40 {DevType, 1} 退出测听
   ├ A2 00 16 <原程序> → 等80ms → 82   切回进入前的程序
   ├ A2 00 2E 58       → 等 2ms → 82   测听位关
   ├ A7 01 00 00 00 26 →         82   解除静音
   └ 延 1s → 推 SYS_ID=1 CMD 6 {DevType, Initial_Status=1}

CMD 21 {DevType, Mute} 静音开关   ← ⚠ 方向与 CMD 3 相反：Mute 非 0 = 静音
CMD  3 {DevType, OnOff} 开关机    ← OnOff 非 0 = 开机 → 解除静音；0 = 关机 → 静音
```

**CMD 3 开关机**（2026-09-17 补实现）：请求 `{Device_Type, Device_OnOff}`，按接口文档 `Device_OnOff` 的
`0: Off 1: On` —— **非 0 = 开机**。7100 侧没有独立的开关机指令，按 1644 的做法（`bs300_active`/`bs300_mute`）
映射成解除静音/静音，复用上面那两条帧：

| 请求 | 动作 | 7100 帧 |
|------|------|---------|
| `OnOff = 1` 开机 | `dsp_7100_set_mute(false)` | `A7 01 00 00 00 26` |
| `OnOff = 0` 关机 | `dsp_7100_set_mute(true)` | `A7 01 00 00 00 25` |

与 CMD 21 **共用 `s_device_on`**，所以两条指令交替下发不会互相打架；`GetDeviceOnOff`(33) 读的就是它。

> **已上板验证通过**（2026-09-17）。

**实现要点**

- CMD 40 的应答**必须抢在推送前面**：BLE TX 是**单槽**（`s_tx_frame`），顺序反了推送会顶掉应答（实测日志：`push` 先于 `TX frame`）
- 推送用 `rempro_deferred_tick()`（在 `APP_7100_HB_Handler` 的 200ms tick 里计数，5 tick = 1s），**不引入新的 ke_timer**
- CMD 21 被 App 约每 6s 轮询一次 → **只在状态变化时才动 I2C**，否则只回应答，避免打断读回/会话；
  **CMD 3 同理，且与 21 共用 `s_device_on`**，所以 03/21 交替下发不会互相打架
- 纯音/静音是**单命令会话**（2 tick ≈ 0.4s），不走公共帧；收尾**不落盘**（不改任何缓存参数，否则每播一个频点擦写一次 flash）

> ⚠ **时序与抓包的偏差（未复刻，实测无影响）**：抓包里单写帧的「写→读」只有 1~8ms，
> 而单命令会话走 200ms tick，实际是 200ms；命令间抓包约 44~49ms，我们几乎无间隔。
> 上板验证功能正常，故未改。若后续测听点按感觉迟钝，可把纯音/静音改成阻塞形态（照
> `dsp_7100_switch_program` 的写法，`写 → 5ms → 读 → 1ms → 82`），单次约 6ms。
> 另：`tonestar.txt` 里切程序只用了 3 帧，本工程用的是 5 帧（§3.2），两者都可用。

#### 7.4.5 Rempro 接线

| 命令 ID | 处理 | 映射 |
|---------|------|------|
| `CMD_SETVOLUME` (2) | `cmd_setvolume_7100` | App vol 0-5 → 7100 档位 1-6（`+1`） |
| `CMD_SETCURRENTSCENE` (16) | `cmd_setcurrentscene_7100` | App scene 0-3 → 7100 程序 1-4（`+1`） |
| `CMD_SETDENOISE` (9) | `cmd_setdenoise_7100` | prog 0-3 → 程序 1-4；level 0-4（>4 钳位并告警） |
| `CMD_SETFEEDBACKONOFF` (5) | `cmd_setfeedbackonoff_7100` | prog 0-3 → 程序 1-4；onoff 即 DFBC |
| `CMD_SETEQUALIZER` (10) | `cmd_setequalizer_7100` | type 0低/1中/2高 → 该段通道的 LL+HL（±dB） |
| `CMD_SETGAIN` (6) | `cmd_setgain_7100` | Spectrum 0-15 → ch1-16；`−30` → LL（§7.4.3） |
| `CMD_SETHIGHLEVELGAINDATA` (29) | `cmd_sethighlevelgain_7100` | Channel 0-15 → ch1-16；`−30` → HL（§7.4.3） |
| `CMD_SETMPO` (7) | `cmd_setmpo_7100` | Channel 0-15 → ch1-16；`−60` → OL（§7.4.3） |
| `CMD_GETGAINDATA` (22) | `cmd_getgaindata_7100` | 读回缓存 → `+30`（§7.4.3） |
| `CMD_GETHIGHLEVELGAINDATA` (30) | `cmd_gethighlevelgain_7100` | 读回缓存 → `+30`（§7.4.3） |
| `CMD_GETMPODATA` (23) | `cmd_getmpodata_7100` | 读回缓存 → `+60`（§7.4.3） |
| `CMD_SETDEVICEONOFF` (3) | `cmd_setdeviceonoff_7100` | `OnOff` 非 0 = 开机；映射成 unmute/mute（§7.4.4） |
| `CMD_SETMUTEDATA` (21) | `cmd_setmutedata_7100` | ⚠ **方向与 3 号相反**：`Mute` 非 0 = 静音（§7.4.4） |
| `CMD_SETPLAYVOICE` (13) | `cmd_setplayvoice_7100` | Spectrum 0-16 → 250…8000 Hz；dB 20-100（§7.4.4） |
| `CMD_SETSTOPVOICE` (14) | `cmd_setstopvoice_7100` | 停音（§7.4.4） |
| `CMD_SETAUDIOMETRYSTATUS` (40) | `cmd_setaudiometrystatus` | 0=进测听 / 1=退测听（§7.4.4） |
| `CMD_GETBATTERYINFO` (4) | `cmd_getbatteryinfo_7100` | `BAT_ADC_ENABLE=1` → **实采 DIO0**（§3.2）；`=0` → 固定回 100/100（回 flag=1 会导致 App 连不上） |

`GetDeviceConfig` 改为 7100 取值：**Program_Num=4、Chip_Type=6 (E7160SL)、Volume_Number=5**（原 3 / 1 / 9）。

> 其余 4 个 Rempro 命令（SetCompressRatio (8) / GetCurrentScene (15) /
> GetFeedbackOnOff (34) / GetFittingData (17)）仍回 `flag=1`。

**RM 推流期间全部指令被丢弃**：`app.c` 主循环里 `app_env.audio_streaming` 为真时只调
`rempro_reasm_reset()`，**不调** `rempro_cmd_process()` —— 所有 BLE 指令（含 §19 的 88/89）
静默丢弃、不回响应。比 1644 §20.4 的白名单**更严**（1644 放行 26/4/15 三条只读）。
理由是推流中任何写 DSP 的命令都会打断音频，而复位类（89）更会直接掐断流。

**应答格式统一（2026-09-15）**：所有**设置类**命令的应答统一为 `Flag(1) + status(1)`
（协议文档规定；`hdlc_response_set()` 生成，成功 = `00 01`，失败/不支持 = `01 00`）。
此前 SetDenoise / SetFeedbackOnOff / SetEqualizer 及各处兜底分支只回 `Flag`，本轮按文档补齐；
**Flag 取值一律沿用原逻辑**，只补上缺失的 status 字节（SetVolume 成功分支字节不变）。
Get 类命令的应答按文档**不带 status**。

#### 7.4.6 I2C 收发日志（四条链路统一）

```c
#define I2C_DUMP_CHUNK 32      /* ⚠ 每行最多 32 字节 */
[7100] W (300B ok=1): A7 27 01 00 00 08 00 00 0C 00 00 ...   ← 首行带前缀
       00 0C 00 00 0C 00 00 ...                              ← 续行
[7100] R (3B ok=1): 46 00 00
```

- `W`/`R` = 方向；`(nB ok=x)` = 字节数 + 是否成功（读还含首字节 `0x46` 校验）
- **不含 I2C 地址字节**（HAL 在 START 后发；逻辑分析仪上对应日志未显示的 `04`/`05`）

> ⚠ **必须分块打印**：pack `printf.c` 用 `vsprintf` 写 **200B 静态缓冲（`TX_BUFFER_SIZE`）且无边界检查**。
> 降噪写块 300B → 900+ 字符一次输出会冲爆缓冲导致**死机**（曾发生）。
> 分块后单次 ≤ ~123 字符。同类隐患：`dsp_7100_init.c` 的 `dump_hex` 上限已收到 32（≤100 字符）。

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
3. audiosink 计数 + 采样钟输入（DIO3）（无条件）
4. `#if OUTPUT_DECODE_PATH`：DSP 固件 Flash_Copy、DSS reset、设 codec message；
   `#elif OUTPUT_INTRF == PCM_SLAVE_OUTPUT`（**当前生效**）：ASRC 输入 ch3、
   `Sys_PCM_ConfigClk(2/3/4/14)` + `Sys_PCM_Config(PCM_CFG_TX)`、ch4(ASRC OUT→pcm_tx_buf)、
   ch5 配置但**不使能**（等 ch4 完成中断启动）、`Sys_PCM_Enable()`
5. 10k 喂狗延时 → `BLE_Initialize()` → `App_Env_Initialize()` → `printf_init()`
   → **覆写打印口到 DIO12、释放 DIO5** → `APP_RM_Init(ear_side)`
6. `RF_SwitchToCPMode(); RM_Enable(1000);`（对齐 sleep：开机即进 RM）
7. DEBUG DIO（DIO15/DIO11）配置
8. Flash overlay + loop cache、`DEBUG_UART_LOG`（默认关）
9. 使能中断

> PCM 在 `App_Initialize` 就配置并 `Sys_PCM_Enable()`，**早于 `main()` 里的 7100 握手**。
> RSL10 是从机，7100 未提供 BCLK/FS 时不会移位；ch5 又只由 ch4 完成中断启动，
> 所以建链前不会有音频输出，无需额外门控。

`main()`（app.c）：

```
App_Initialize() → 打印 started → bs300_driver_init()
→ while(1){ Kernel_Schedule(); rempro_tx_poll(); rempro_cmd_process()（非流中）;
            RM_StatusHandler(); bs300_process_deferred(); 喂狗; SYS_WAIT_FOR_EVENT; }
```

## 9. RM 无线电配置（对齐 sleep）

- `RM_HOPLIST = { 3, 9, 15, 21, 24, 33, 36 }`（include/app.h）
- **音频流地址 = accessword 的高 24 位**，默认 `0xF2CDE6` → `accessword = 0xF2CDE629`
  （`RM_STREAM_ADDR_TO_ACCESSWORD()`，宏在 include/app.h）。**默认值可被 BLE 89 号
  SetStreamAddress 改写并落盘**，开机由 `APP_RM_Init()` 从 Settings 扇区回填 —— 见 §19。
  （原写法 `0x00cde629 | (0xf2<<24)` 与本宏展开结果**逐位相同**，非默认路径的行为不变。）
- 其余 rm_param（interval 10000 / retrans 5000 / audio_rate 48 / radio_rate 2000 / scan 6500 /
  preamble 0x55 / renderDelay 200 / preFetch 1300(RM_APP_REQUEST) / pkt / 搜索阈值）与 sleep 一致。
- **扫描驻留已改**：`waitCntGranularity` 200 → **400**（突发之间的驻留 = `retrans_time(5ms) × 该值`
  = 1s → 2s，省电向）。
  ⚠ **天花板低**：库在驻留期（`RM_READY`）照样会起一次 RX 窗口 —— `RM_ReceivePacket()`
  （`rm_pkt_hdl.c:449-659`）的 switch 只算 timeout/定时器，**之后那段收尾代码无条件执行**
  （`RF_SwitchToCPMode()` + `RF_Reg_WriteBurst(CENTER_FREQ,…)` + `FSM_MODE,0x3`）。
  实测「有效果但不明显」。
- ⚠ `scan_time = 6500` 是**死参数**：本版库 `rm_env.scan_time` 只赋值、全库无引用，改它无效。
  （旧文档里「扫描超时 6.5ms」的说法源自它，属过时表述。）
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
- `code/dsp_7100_cmd.c` / `include/dsp_7100_cmd.h`

**修改**
- app.c、code/app_init.c、code/app_process.c、code/ble_custom.c、code/ble_std.c、
  code/rm_app.c、code/ble_rempro_cmd.c、include/app.h、include/ble_rempro_cmd.h
- **PCM 输出（§3.4/§6）**：include/app.h、code/app_init.c、code/app_func.c、code/rm_app.c
  —— 无新增文件
- **BLE 88/89 流地址（§19）**：include/app.h、include/ble_rempro_cmd.h、app.c、code/rm_app.c、
  code/ble_rempro_cmd.c、code/dsp_7100_storage.c / include/dsp_7100_storage.h（settings 扇区）
  —— 无新增文件，`.cproject` 不动

**删除**
- `code/bs300_*.c`（9）、`include/bs300_*.h`（10）

## 12. 已知问题 / 待办

1. **阶段二进行中**：剩余 4 个 Rempro 命令（SetCompressRatio (8) /
   GetCurrentScene (15) / GetFeedbackOnOff (34) / GetFittingData (17)）仍回 `flag=1`，见 §17。
   已完成：切程序 / 音量 / 降噪 / DFBC / **纯音测听 + 静音 + 开关机**（**均已上板验证**）、
   EQ 与 **WDRC（SetGain / SetMPO / SetHighLevelGainData 及其读回）**（**均未上板**，见 §7.4.2 / §7.4.3）。
   纯音的 App 侧仍在完善中。
2. **上电握手无超时**（照 rx_coex）：板上无 7100 时卡在 `while(DIO_DATA->ALIAS[13] == 1)`，不退出。
3. `rempro_push_volume_change()` 无调用者（按键删除的副作用）。保留与否待定。
4. **PCM 脚位待硬件确认**：按 7160test 定为 BCLK=DIO2 / FS=DIO3 / SERO=DIO14，需确认 1664 板
   与 7100 的实际走线（§5）。若不同，改 `app.h` 的 `PCM_CLK_DO/PCM_FRAME_SYNC/PCM_SER_DO`。
5. **RM 调试 IO 已全部关闭**（`debug_dio_num[0..3]=0xff`）：DIO15 也不再翻转，RM 调试波形没了。
6. OD 引脚 DIO0/1 让给 7100 I2C；音频出口已改 **PCM 从机**（`PCM_SLAVE_OUTPUT`）。
   恢复 OD 直驱需重新选脚位（如 7160test 的 DIO12 单端方案）。
7. 读回缓存首次写入后即命中，**除非 0xFE 失效否则不会重读**；芯片侧参数变了需主动 0xFE。
8. **I2C 打印必须分块**（§7.4.6）：pack `printf.c` 的 200B 静态缓冲 + `vsprintf` 无边界检查，
   单次输出超长会死机（曾因 300B 写块一次打印而踩坑）。新增打印时务必遵守。
9. 写会话期间读回与心跳被暂停（`dsp_7100_cmd_busy()`）。会话时长：降噪/DFBC ≈3.2s；
   EQ 低/中/高**都是 5.6s**（各 2 通道；原高音段 11 通道的 20s 静音已随通道映射调整消失）。
   切程序 / 调音量 / 纯音出停音（§22）简化后只发 2 帧，会话 ~10–20ms，对读回几乎无影响；
   测听（进入 / 退出）是 3 条命令、带 80ms + 2ms 延时（§22.5），会话 ~90ms。
   期间手机多发命令**不再被拒绝** —— 按类型合并缓存，会话结束依次执行（§7.4.2「命令缓存」）。
   `SetGain` / `SetMPO` / `SetHighLevelGainData` 仍会被拒绝（回 `Flag=1`）。
10. ~~**EQ 的基准值污染风险**~~ **已解决**（2026-09-20）：缓存 `wdrc_ll/hl` 改为存**设备当前值**（含 EQ），
    `eq_*` 另存 App 上次下发的**绝对值**，计算时只加「本次 − 上次」的差值。`0xFE` 重读拿回的就是缓存里的值，
    不再污染基准，反复设置也不会累积。缓存版本随之升到 **v5**（旧槽自动失效重读）。
11. **EQ 可用范围受 WDRC 余量限制**（§7.4.2）：`clamp` 到 ±30/60 边界后同方向继续调**无效果**，
    且段内各通道顶死时机不一致会让频响失真。当前未处理。
12. **音频侧本轮改动未完全上板**（§6.7）：`Pcm_Stream_Break/Resume` 的「TX 硬断电杂音消失」
    已确认，但**弱信号（连续丢 2 包）后的恢复**、以及 **PLC（重复最后一帧）**的听感，仍需回归。
    相关计数：`app_err1`（坏包/PLC 次数）、`app_err2`（帧号跳变）、`app_err3`（通道标志不符）。
13. **FOTA 版装不下**（§16）：app 区只有 190KB，代码已用 189.9KB，加 7100 缓存 8KB 超出。
14. **`BBIF->CTRL` 稳态改 `BB_DEEP_SLEEP`（app_init.c:129）未上板验证**：对齐 sleep 工程稳态，
    目的是不再永久强制唤醒基带；改动本身实测对搜索态电流**无影响**（见 §18），保留是为将来真加
    深睡时的前提。
15. **电池 AD 采样（DIO0）当前实测不可用，且量程未标定**（§3.2）：
    - `BAT_ADC_MIN/MAX` 仍是 **1644 的占位值**（6950/9374），从未按 1664 实测标定。
    - **根因**：DIO0 上挂着 **7100 侧内置的 10k I2C 上拉**，而 1M+360k 分压的戴维南等效只有
      265 kΩ —— 电池只占节点电压约 **3.6%**。2026-09-21 实测：电池 3.2V→raw **9816**、
      4.4V→**10334**，折算节点电压 1.233V→1.312V，**与分压关系不符**（电池 1.2V 变化只对应
      节点 ~0.01V）；且 13 个采样点呈单调爬升，更像漂移。
    - **出路**：① **关掉 7100 的内置上拉**（RSL10 侧本来就用 `DIO_STRONG_PULL_UP` 撑着 I2C，
      不依赖它）—— 关掉后预期 raw **3.2V≈7296 / 4.4V≈9374**，可直接沿用 1644 常量；
      ② 若关不掉则须改硬件，把分压挪到未被 I2C 占用的脚（DIO5 已释放 / DIO15 空闲）。
    - **判定办法**：万用表直接量 DIO0 直流电压 —— 随电池成比例（3.2V≈0.85V / 4.4V≈1.17V）
      是 ADC 配置问题；基本不变（≈1.2V）即上拉主导。
16. **ADC 块启动后不会关**（§3.2）：`Sys_ADC_Set_Config(ADC_NORMAL | ADC_PRESCALE_1280H)`
    只**使能** ADC，代码里没有 `ADC_DISABLE` 收尾 —— 开过一次即长期上电。
    **测功耗前必须把 `BAT_ADC_ENABLE` 置 0**（SDK 有 `ADC_DISABLE` 可加收尾，待做）。
17. **低电量提示未实现**（§3.2）：1664 无 BS300，播不了 `0xFD12F2`。待确认 7100 有无可用
    提示音命令；位置已留在 `code/app_process.c` 的 `APP_Timer`（有 TODO 注释）。
18. **BLE 88/89 流地址未上板实测**（§19）：落地路径（Settings 扇区擦写、复位时机、
    开机回填）都只做了代码走查。唯一能证明正确性的两项 —— 「断电重启后流地址是否保持」
    与「改完地址后 TX 能否对上」—— 见 §19 验证清单。

## 13. 验证步骤

1. 编译 `remote_mic_rx_coex_1664` Debug，确认链接通过。
   （已用工程自带 makefile 全量构建：0 错误；告警数与 PCM 改动前一致，均为既有告警。）
2. 烧录后用 **UART DIO12(115200)** 观察开机序列：
   `started` → `[IO] wait DIO13 low …` → `[IO] DIO13 high, run 7100 init`
   → `[7100-init] n/106 TX/RX …`（每步含收发字节）→ `[7100-init] sync pre-A7 done`
   → `[7100-cache] hit/miss`。
3. **首次开机**应见 `[7100-cache] miss` → `[RB] 1/28 …` 逐条读回 → `[RB] round done, parse`
   → `[7100-cache] saved to flash`。**再次开机**应见 `[7100-cache] hit`，且无 `[RB]` 读回。
4. 主循环应见 DIO9/10/13 电平变化打印：`[IO] D9=… D10=… D13=…`（照 rx_coex）。
5. 与已配对发射机建链：串口出现 `RM_LINK_ESTABLISHED/DISCONNECTED`。
   **建链后开始有 PCM 音频输出**；断链应静音，重连应恢复出声（验证 `rm_app.c` 的重武装）。
6. 手机扫描应看到 **Smart1664** 广播；Rempro `GetBatteryInfo`：`BAT_ADC_ENABLE=1` 时回**实测值**
   （串口同步打 `[REMPRO] GetBatteryInfo: raw=… pct=…%`，标定用），`=0` 时回 **100%**（§3.2）。
7. 示波器：
   - **DIO0/DIO1 应有 I2C 波形**（SCL/SDA），不受 PCM 影响；
   - **DIO2 = BCLK 输入 384 kHz、DIO3 = FS 输入 12 kHz**（7100 提供）；
     **DIO14 每个 FS 周期移出 32 bit = 2 段 16-bit 连续采样**（无插零），有效 24k；
   - DIO12 打印 TX；DIO11 握手脉冲；DIO13 ready。
8. 发 `0xFE`：应见 `[7100] 0xFE: invalidate cache + reset to reload`，重启后重新走读回。
9. **WDRC 验配命令**（§7.4.3，**未上板**，需实测）：
   - App 下发 `SetGain` / `SetMPO` / `SetHighLevelGainData` → 串口应依次出现
     `[REMPRO] SetGain: dev=… prog=… n=… started=1` → `[7100] --- WDRC set … n=… ---`
     → `[7100] --- session done ok=1 ---`。
     **n=16 时约 15.2s，会话期间全程静音**（会话期间读回与心跳暂停，属已知取舍）。
   - 逻辑分析仪按 §7.4.3 的帧形状核对：`A7 05 00 00 00 05 07 <P> <addr16-BE>` + 写值帧
     （LL/HL 是 `A7 04 … <1B>`，**OL 是 `A7 07 … <4B>`**）。
   - 写完下发 `GetGainData` / `GetMPOData` / `GetHighLevelGainData` 回读比对：
     **回读值应等于写入值**（换算后：LL/HL `+30`、OL `+60`）。
     ⚠ 开机读回未完成时这三个会回 `Flag=1`。
   - 复用现成抓包做对照：`p0lowchannel2levelgainset-2`（LL = −2 → `FE`）、
     `p0channel16set0`（ch16 OL = 0，地址 `0x117`）。
10. **纯音测听 / 静音**（§7.4.4，**已上板验证通过**）：
   - 进测听：App 发 CMD 40 `Fitting_Status=0` → 串口应依次出现
     `SetAudiometryStatus: status=0` → `TX frame … 00 28 …`（**应答先出**）→
     `[7100] W (4B): A2 00 16 03` → `A2 00 2E 00` → `A7 01 00 00 00 26`
     （三句之间**不再有 `[7100] R` 读回**，见 §22.5；`session start` 那行 `cmds=3`）
     → `audiometry push scheduled (1) in 1000 ms` → 约 1s 后 `push initial status done`
     + `TX push … 01 06 …`。
   - 出音：CMD 13 → `[7100] W (12B): A7 07 00 00 00 2E 01 <freq16> <level24>`，
     例如 1000Hz 50dB → `… 03 E8 00 05 6F`（`00056F` = 1391）。
   - 停音：CMD 14 → `A7 07 00 00 00 2E 00 00 00 00 00 00`。
   - **不应出现** `[7100-cache] saved to flash` —— 纯音/静音不改缓存，不落盘（§7.4.4）。
   - 静音：CMD 21 `Mute=0` 重复下发时只有第一次（状态变化）会打 `[7100] --- unmute ---`，
     之后只回应答（`changed=0`）。
11. **RM 流中断静音 + PLC**（§6.7，**「TX 硬断电杂音消失」已上板确认**）：
    - TX 持续推流中**硬断电**（不是把音量调小）→ 应 ~20~30ms 内静音，不再有 1~2 秒断续杂音；
      断开瞬间若有**一声轻「咔」**属已知取舍（读不到 ch5 播放位置）。
    - **单包丢失**（偶发）：应靠 PLC「重复最后一帧」接上，听感是轻微一顿，**不应有爆音/长静音**。
    - **回归重点**：弱信号 / 偶发连丢 2 包 → 音频短暂下沉后**必须正常恢复出声**，不能永久静音
      （走到远处或加遮挡实测）。
    - 正常推流音质不受影响；`LINK_DISCONNECTED` 后彻底安静；重连正常。

## 14. BLE 配置（参考 sleep：单设备连接）

- **单设备连接**：沿用 peripheral 单连接（`APP_IDX_MAX = 1`）；未移植 sleep 的左右耳 peer /
  BLE Central / 双耳 GATT 同步；Rempro 已单独移植（见 §15）。
- **设备名**：`APP_DFLT_DEVICE_NAME = "Smart1664"`（include/ble_std.h）；
  FOTA 开启时为 `Smart1664FOTA`。
- **地址配置**：`BD_ADDRESS_TYPE = BD_TYPE_PUBLIC`；`PRIVATE_BDADDR`、`APP_PUBLIC_BDADDR`、
  `RADIO_CLOCK_ACCURACY(500)` 照 sleep。
- **广播**：ADV 放设备名，可发现模式 `GAP_GEN_DISCOVERABLE`，广播间隔 160×0.625ms≈100ms；
  > ⚠ **当前工作区被临时改成 5000（≈3.1s）**：`ble_std.h` 的 `APP_ADV_INT_MIN/MAX`
  > 由 160 改为 `5000//160`（原值以注释保留），是为**功耗测试**临时放宽广播间隔。
  > 正式版本应改回 **160**。
  公司厂商段 18B 用 sleep 的 `APP_COMPANY_ID_DATA`，把「耳侧 + 设备 MAC」编入 company data。
  因名字 Smart1664 为 9 字符、ADV 放不下 18B 厂商段，MAC/耳侧数据改放 **scan response**。
- **bdaddr 修正**（code/ble_std.c）：PUBLIC 分支读到公共地址后不再被 `PRIVATE_BDADDR` 覆盖，
  读不到才回退 `co_default_bdaddr`。

## 15. Rempro 服务（手机验配）

**GATT 服务：只保留 Rempro**（Battery / Custom 已移除）

- `SERVICE_ADD_FUNCTION_LIST` 只剩 `RemproService_ServiceAdd`；`SERVICE_ENABLE_FUNCTION_LIST` 为 NULL。
- Rempro 的 ATT 读写路由复用 `ble_custom.c` 的 GATTC 处理器（按 `rempro_env.start_hdl` 区间分流）。
- **Battery 服务不注册**（§3.2）；Rempro `GetBatteryInfo` 在 `BAT_ADC_ENABLE=1` 时**实采 DIO0**，
  `=0` 时固定回 100%（回不支持会让 App 连不上）。

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
  # .project 的 <link> 改回 libfota.a（见下）
  ```
- FOTA OFF：把上面 4 个文件换回 `_nofota`（或 git 还原），并注释 `CFG_FOTA`；`.project` 见下。
- OFF 构建不依赖 FOTA 库/头，行为与普通固件一致（fota_system.c 由 `--gc-sections` 剥掉）。
- **默认状态 = OFF**（当前工作区即为 OFF）。

**⚠ `cp` 之外的三个坑（2026-09-21 两次切换实测）**

1. **`.project` 也要跟着切**。它的 `<link>` 列表里 FOTA 版是 `RTE/Device/RSL10/libfota.a`，
   nofota 版是 `libblelib.a` + `libkelib.a`（与 `.cproject` 的链接库一一对应）。
   上面那 4 个 `cp` **覆盖不到它**，需手工改（或让 IDE 重建）。判据：
   `grep -o "lib[a-z]*\.a" .project` 应与 `.cproject` 的库组合一致。
2. **两份 `.cproject` 备份的 Release 配置都缺变体 exclude**。`_fota` / `_nofota` 都只在
   **Debug** 的 `sourceEntries` 里配了 `RTE/Device/RSL10/startup_rsl10_{fota,nofota}.S` /
   `sections_{fota,nofota}.ld` 的 `excluding`，**Release 没配** → 直接 `cp` 后编 Release 会因为
   `startup_rsl10.S` / `_fota.S` / `_nofota.S` 三个同时入编而 **`Reset_Handler` 重复定义，链接失败**。
   **cp 之后务必给 Release 的 `sourceEntries` 也补上前缀**。两个配置的正确状态都是：
   `startup_rsl10.S` / `sections.ld` 入编，**四个变体文件全部排除**。自检：
   ```
   grep -o 'startup_rsl10_[a-z]*\.S\|sections_[a-z]*\.ld' .cproject | sort | uniq -c   # 应各为 2
   ```
3. **`rsl10_protocol.c` 的取舍两版相反**：FOTA 版**必须排除**（`Device_Param_Read` /
   `BLE_DeviceParam_Set_*` 这些符号由 `libfota.a` 的 `fota_sym.o` 以绝对地址转发到 bootloader），
   nofota 版**必须保留**（它链接的 `libblelib.a` / `libkelib.a` 不提供这些符号）。
   自检：`grep -o 'excluding="[^"]*' .cproject | grep -c rsl10_protocol.c` —— FOTA 版应为 2
   （Debug/Release 各一），nofota 版应为 0。

### 16.1 ⚠ 当前 FOTA 版装不下（2026-09-20 实测）

切到 ON 后链接失败：`region FLASH overflowed by 4800 bytes`。**这是既有容量问题，不是切换操作错误。**

Flash 预算（RSL10 共 384KB = `0x00100000~0x00160000`）：

| 区域 | 范围 | 大小 |
|------|------|------|
| 引导分区（boot + `fota.bin`） | `0x100000 ~ 0x130800` | 194 KB（`fota.bin` 实测 157.5KB） |
| **FOTA app 可用区** | `0x130800 ~ 0x160000` | **190.0 KB** |
| 当前 `.text` | `0x130800 ~ 0x15FF88` | **189.9 KB** |
| 7100 参数缓存（4×2KB sector） | `0x15D000 ~ 0x15F000` | 8 KB |

- 光 `.text` + `.data` 初值就**超 380K 区 4800 字节**；就算把 `FLASH` 区扩到 384KB 仍差 **428 字节**。
- 更关键：**7100 参数缓存区 `0x15D000~0x15F000` 正落在 `.text` 里** —— 即便挤出空间，
  代码也会压掉缓存。要开 FOTA，**必须同时给缓存挪位置或缩容**。
- 连接器区域定义（两版 ld 相同，均为 `ORIGIN=0x00100000, LENGTH=380K`）**把缓存区也圈了进去** ——
  nofota 版现在代码只到 `~0x13F788` 没撞上，但再涨约 120KB 会静默踩掉缓存。**链接脚本本该排除这段。**

**待选方案**（需腾出约 8.5KB）：

| 方案 | 省 | 说明 |
|------|-----|------|
| A. 7100 缓存 8KB → 2KB | 6 KB | 4 程序挤进 1 个扇区（每槽 64B）。`cache_save()` 本来就整批重写，改后反而**一次擦写完成**，更快且省磨损 |
| B. 代码瘦身 | 2~5 KB | `-O2` → `-Os`；或关 SEGGER RTT / printf 输出 |
| C. 暂不切 FOTA | — | 回 nofota，app 区 372KB，余量充裕 |

> 另注：切换后若报 `undefined reference to fota_init / Sys_Fota_StartDfu`，是 `Debug/objects.mk`
> 还是旧的（指向 `libblelib.a`/`libkelib.a`）。IDE 会重新生成成 `libfota.a`，重新编译即可。

## 17. 待办：7100 阶段二（写路径 + 验配命令）

**阶段一已完成**（§7）：通讯层、读回、flash 缓存、心跳。

**阶段二目标**：把 Rempro 的验配命令重新落到 7100 的 A7 参数块上。

**已完成（2026-09-15）**

1. ~~动态 A7 写编码~~ → `a7_add_prep()` / `a7_add_wdrc_param()` / `a7_add_wdrc_ol()` /
   `a7_add_noise_block()`（`dsp_7100_cmd.c`），按「块 + 参数号 + 值」现算 A7 写帧。
2. ~~写会话状态机~~ → `a7_build_session()` + `dsp_7100_cmd_tick()`：
   静音 → 选程序 → 写 ×N → confirm → 解除静音 → 选回 → commit，命令表 + 200ms tick 异步推进。
   **多通道共用公共帧**（`6 + 2n` 条命令），见 §7.4.3。
3. ~~写后回写缓存~~ → `a7_session_finish()` 成功后改写 RAM 参数并 `dsp_7100_cache_save_request()`。
4. Rempro 命令重映射（`ble_rempro_cmd.c`）：

   | 命令 | 状态 |
   |------|------|
   | SetVolume (2) / SetCurrentScene (16) | **已完成 + 已上板** — §7.4.1 / §22 |
   | SetDenoise (9) / SetFeedbackOnOff (5) | **已完成 + 已上板** — §7.4.2 |
   | SetEqualizer (10) | **已完成，未上板** — §7.4.2（映射到 WDRC LL/HL） |
   | SetAudiometryStatus (40) / SetPlayVoice (13) / SetStopVoice (14) / SetMuteData (21) | **已完成 + 已上板** — §7.4.4 / §22（App 侧完善中）|
   | SetGain (6) / SetMPO (7) / SetHighLevelGainData (29) | **已完成，未上板** — §7.4.3 |
   | GetGainData (22) / GetMPOData (23) / GetHighLevelGainData (30) | **已完成，未上板** — §7.4.3 |
   | GetCurrentScene (15) | 待实现：选程序 `A7 02 00 00 00 12 <P>` + 读回解析 |
   | GetFittingData (17) | 待实现：读回解析（缓存已就绪，见 §7.2） |
   | SetDeviceOnOff (3) | **已完成 + 已上板** — 映射到 unmute/mute，与 21 号共用 `s_device_on`（§7.4.4）|
   | GetFeedbackOnOff (34) | 待实现 |
   | SetCompressRatio (8) | **按需求不做** |

**接下来**

1. **上板验证 WDRC（§7.4.3）与 EQ（§7.4.2）** —— 两者目前都只做了离线比对（WDRC 已与
   `p0lowchannel2levelgainset-2` / `p0channel16set0` 等抓包逐字节核对），未上板。
2. **纯音只标定了 6/17 个频点**（§7.4.4）：500/1000/2000/3000/4000/6000 之外的 11 个
   （250、1500、2500、3500、4500、5000、5500、6500、7000、7500、8000）会回 `Flag=1`。
   补法：固定一个 dB，把这 11 个频点各抓一条，反解 `C(f)` 填表（`docs/7100协议/纯音测听.md` §2/§4）。
   另：电平基常数 `K = 4.39902` 出处不明，**可能是整机校准值**，换机需重标。
3. **OutputLimit 值字段的 `b2`/`b3` 语义未明**（§7.4.3）：默认表只来自一次默认状态抓包。
4. ~~**EQ 与直接 WDRC 写的交互**：两边对"缓存基准"的假设不一致。~~
   **已解决**（2026-09-20）：缓存 `wdrc_ll/hl` 统一为「设备当前值」，两条路径自洽，见 §7.4.3。
5. **Get 类命令依赖开机读回完成**：未完成时回 `Flag=1`，必要时加"读回完成后主动推一次"。
6. `A7_WDRC_LL_MIN/MAX` 本轮由 `0/127` 改为 `-30/60`（真实范围）；`set_eq` 用同一对宏，
   其钳位行为随之变化，需一并回归。

**参考（rx_coex 侧，未移植的部分）**

| 文件 | 作用 |
|------|------|
| code/dsp_7100_set_tables.c + scripts/gen_dsp_7100_set.py | 写会话样例（P01 WDRC LL 全通道置 0），38 条命令 |
| code/dsp_7100_parm_tables.c + scripts/gen_dsp_7100_parm.py | parm1604 命令表（105 条 A7） |
| code/dsp_connect_replay.c + `_tables.c` | 连接回放（rx_coex 里也被 .cproject 排除，未接线） |

> 阶段一已**剔除**上述死代码；本次阶段二改为在 `dsp_7100_cmd.c` 内按公式现算，未重新引入大表。

## 18. 功耗：RM 搜索态现状与结论（2026-09）

**实测**（RM 开、无 TX、搜索中）：**RSL10 单独 ≈ 450µA / 整机 > 1mA**。
7100 是**固定底座**（本轮明确不调整）。对照 `peripheral_server_sleep` 当年「RM 未连接 ~300µA」。

**根因级结论：1664 这颗 RSL10 从不进低功耗模式。** 主循环只有 `SYS_WAIT_FOR_EVENT`
（`rsl10_sys_cm3.h:45` = `wfe`，纯 CPU 停顿），**全工程没有 `BLE_Power_Mode_Enter`**。
sleep 工程之所以能靠 `BB_DEEP_SLEEP` / 音频外设停机 / `DSS_LPDSP32_PAUSE` 省电，是因为它
**真的调了深睡**，那些位/停机都是配套动作。

**试过、实测无效的**：

| 改动 | 结果 |
|------|------|
| `LINK_DISCONNECTED` 加 `DSS_LPDSP32_PAUSE` | **无效**（已回退）—— RSL10 头文件里**没有 LPDSP32 时钟门控位**，PAUSE 只 halt 核、时钟照跑 |
| `BBIF->CTRL`：`BB_WAKEUP` → `BB_DEEP_SLEEP` | **无效**（改动保留，见 §12.12）—— 不进睡眠就没人门控基带时钟，该 wakeup request 位空转 |
| `waitCntGranularity` 200 → 400 | 有效果但**不明显**（§9：驻留期照样起 RX 窗口） |

**还能试的**：`searchTryCntThrshld` 20 → 8（直接砍 RF 窗口数，且能顺便量化「搜索占这 450µA 多少」）；
深睡与「RM 常搜」互斥（RM 用 SLOWCLK 域 TIMER0/1，硬件定时器未必是 deep sleep 的唤醒源），
赌注大，先别碰。

> 结论：在「RM 必须常搜 + 7100 不动」下，450µA 基本是地板；继续调扫描占空比收益在几十 µA 量级。

## 19. BLE 88/89 GetStreamAddress / SetStreamAddress（照 1644 §20.6 移植）

把 RM **音频流地址**（accessword 高 24 位）开放给 App 改，用于让 RX 的流地址与 TX 端对齐。
闭环：App 写 → 落盘 Settings 扇区 → 复位 → 开机回填。

**accessword 的构成**（宏在 include/app.h）：

| 位 | 含义 |
|---|---|
| bit31-8 | **音频流地址**（24 位，`RM_STREAM_ADDR_MASK`；默认 `0xF2CDE6`，`RM_STREAM_ADDR_DEFAULT`） |
| bit7-0 | 固定常量 `0x29`（`RM_STREAM_ACCESSWORD_FIXED_LOW`） |

> ⚠ **与 RSL10 SDK 样例方向相反**：SDK 写的是 `0x00cde629 | (xx << 24)`（低 3 字节固定、
> 高 1 字节随流变，xx = `0xF2`/`0x0D`）；本机按 App 约定改成低 1 字节固定、高 3 字节可设。
> 两种拆法都能得到出厂值 `0xF2CDE629`，但 App 能改的字节不同 —— 改动前必须与 App/TX 侧对齐，
> 否则 RM 直接搜不到。映射只在 `RM_STREAM_ADDR_TO_ACCESSWORD` / `RM_STREAM_ACCESSWORD_TO_ADDR`
> 两个宏里定义，其余代码不许重算。

**指令**

| ID | 名称 | 请求 data[] | 响应 data[] | handler |
|---|---|---|---|---|
| 88 | GetStreamAddress | Device_Type | Flag + Stream_Address(3, 小端) | `cmd_getstreamaddress` |
| 89 | SetStreamAddress | Device_Type, Stream_Address(3, 小端) | Flag + status | `cmd_setstreamaddress` |

- `Device_Type` 一律忽略（单耳设备）。
- 88 回的是 `rm_param` 的**配置值**，不是库内部 `rm_env` 的在效值 —— 两者只在「89 已写、
  复位还没发生」这一小段窗口内不同；回配置值才能让 App 看到自己刚设的值（Set→Get 往返一致）。
- 89 落盘失败 → 回 `Flag=1` 且**不复位**（不把失败伪装成成功）。

**为什么必须复位**：`RM_Configure()` 把 `param->accessword` **拷贝**进库内部 `rm_env`
（`APP_RM_Init()` 里调用，之后就与 `app_env.rm_param` 脱钩）；`RF_InitRegistersCustomMode()`
只在 RM 启动时把它写进 RF `PATTERN` 寄存器。所以运行时改 `app_env.rm_param` 对已跑起来的 RM
毫无影响，只能重启让 `APP_RM_Init()` 用新值重新 `RM_Configure()`。

**执行流程**

| # | 动作 |
|---|---|
| 1 | 89: 校验 `len>=4`；3 字节小端拼成 24 位 → `RM_STREAM_ADDR_TO_ACCESSWORD()` |
| 2 | `dsp_7100_settings_save_stream_addr()` 写 Settings 扇区；失败 → `Flag=1`，**不复位** |
| 3 | 成功 → 回 `Flag=0 + status=1`，置 `s_reset_pending` |
| 4 | `rempro_tx_poll()` 等 **ACK 最后一个分块被协议栈确认发出**（`!s_tx_in_progress && rempro_env.sentSuccess`）→ `NVIC_SystemReset()` |
| 5 | 重启后 `APP_RM_Init()` 用 `dsp_7100_settings_load_stream_addr()` 取值（无记录则用默认），**再** `RM_Configure()` |
| 6 | `app.c` 主函数打印一行 `[RM] stream addr=0x...... (from flash\|default)` —— **不能在 `APP_RM_Init()` 里打**，那里跑在 `App_Initialize()` 的 PRIMASK 屏蔽窗口内，UART PRINTF 走 DMA+中断，`tx_busy` 清不掉会死锁（§10） |

> **第 4 步为什么不能直接复位**：响应走分块 Notify，复位太早会把 ACK 掐断，App 会当成
> 「无响应」而重发。等 GATTC 完成事件是唯一可靠的信号。
> ⚠ **边角情况**：App 在 ACK 发出前断链 → `sentSuccess` 不置位 → **本次不复位**。地址已落盘、
> 下次开机自然生效，只是没立刻生效。刻意不加超时轮询（那会在断链后突然重启，行为更怪）。

**Settings 扇区**（`0x0015F000`，2KB，`code/dsp_7100_storage.c`）

1664 没有 BS300 那套设置记录（音量/EQ/降噪本就不持久化），这里**只存 3 字节流地址**：

```
0..2    stream_addr（小端 24 位）
3..6    magic "RMST"
7       version 1
8       valid   0xA5
9..10   CRC16-XMODEM（覆盖 0..7）
11..15  0xFF 填充
```

选址理由：链接脚本 `RTE/Device/RSL10/sections.ld` 的 FLASH 区 = `0x00100000 + 380K`，
上界 **`0x15F000`（不含）** → 该扇区**在链接区之外，代码涨不到它**（7100 缓存的
`0x15D000~0x15EFFF` 反而落在区内，是 §16.1 已记录的既有隐患）。属于 main flash HIGH 区，
复用 `dsp_7100_storage.c` 现成的 `main_flash_unlock()` / `crc16_xmodem()`，不新写 flash/CRC 代码。

**RM 推流期间收不到**：主循环在 `app_env.audio_streaming` 时 `rempro_reasm_reset()` 丢掉全部指令
（§7.4.5），等效于 1644 §20.4 的白名单 —— 88/89 都在此列。（89 若在推流中被收下，复位会直接
打断音频，所以这个丢弃是必要的。）

**本轮改动文件**：include/app.h（RM_STREAM 宏 + `rm_stream_addr_from_flash()` 声明）、
include/ble_rempro_cmd.h（`CMD_GETSTREAMADDRESS 88` / `CMD_SETSTREAMADDRESS 89`）、
code/ble_rempro_cmd.c（两个 handler + 分发 case + `s_reset_pending` + `rempro_tx_poll()` 延迟复位）、
code/dsp_7100_storage.c / include/dsp_7100_storage.h（Settings 扇区 save/load）、
code/rm_app.c（`APP_RM_Init()` 从 flash 取值 + `rm_stream_addr_from_flash()`）、app.c（开机日志）。
**无新增文件，`.cproject` 不动。**

**编译状态**：仅改动源码，**未编译、未上板**。

**验证清单**

1. Set：`Device_Type=1, Stream_Address = E6 CD F2`（小端 → 值 `0xF2CDE6`）→ 日志顺序应为
   `[REMPRO] SetStreamAddress: dev=1 addr=0xF2CDE6 accessword=0xF2CDE629`
   → `[7100-settings] stream addr saved (crc=....)`
   → `[REMPRO] SetStreamAddress: reset to apply` → 设备重启
   （`accessword` 末字节恒为 `0x29` —— 那是固定的低字节，不随地址变）
2. **验证持久化（核心）**：重启后开机日志应为 `[RM] stream addr=0xF2CDE6 (from flash)` ——
   且**要带 `(from flash)`**；若显示 `(default)` 说明值虽然对，但用的是出厂默认、**没存住**
   （两者取值恰好相同，只能靠这个标签区分）
3. **验证读回**：发 88 号（只带 `Device_Type`）→ 应回 3 字节 `E6 CD F2`。**Set→Get 往返应一致**
4. **验证掉电**：设一个**非默认**地址（如 `12 34 56`，小端发 `56 34 12`）→ 直接拔电再上电，
   开机日志应是 `[RM] stream addr=0x123456 (from flash)`。用非默认值才能和「默认回退」区分开
5. **验证字节序**：小端才对得上 `addr=0xF2CDE6`。若 App 发的是 `F2 CD E6`（大端写法），
   日志会变成 `addr=0xE6CDF2` —— 说明与固件的小端约定不一致
6. **失败路径**：89 字段 < 4 字节 → 失败 ACK 且**不复位**；88 无 payload → 失败 ACK
7. **回归**：SetVolume / 切程序 / 降噪 / 测听仍正常；RM 推流期间发 88/89 应被静默丢弃
8. **端到端**（唯一能证明地址真被 RF 采用的测试）：设新地址 → 重启 → TX 用同一 accessword
   则能连上，不同则搜不到

## 20. DIO11 极性 / 脉冲 / 0xFC 调试记录（2026-09-30）

> 本节是**调试记录**，不是设计。当天试的改动**已全部回退**到参考设计（`remote_mic_rx_coex`）原样，
> 只刻意留下两处：开机第二个脉冲仍 `#if 0` 关着、引导尾部补发的那条 `04 82`。
> `app.c:46` 的注释指向本节。

### 20.1 结论摘要

| 项 | 结论 | 依据 |
|---|---|---|
| 运行期改 DIO11 电平，看 DIO13 有无反应 | **DIO13 全程无反应**（电平、边沿计数都没变） | 实测，2026-09-30 |
| BLE `0xFC` 调试指令（运行期拨 DIO11） | 试了三版都没测出效果 → **已删** | 代码已回退 |
| DIO11 整体极性翻转（空闲低 / 脉冲高） | 代码写好了但**未上板**，随本轮一起回退 | 未验证 |
| 开机第二个脉冲（实为 1s） | 仍 `#if 0` 关着；DIO11 停在开机那行设的「输出高」 | 开发者选择 |
| 引导尾部补发 `04 82`（第 107 条） | **保留** | 见 20.6 |
| DIO11 上电瞬间电平 | 复位态 `DIO_CFG=0x313F` → `DIO_MODE_DISABLE` + 弱上拉 ⇒ **弱高、非悬空** | 核 SVD，非实测 |

### 20.2 涉及的两个引脚

| 引脚 | 方向 | 作用 |
|---|---|---|
| DIO13 | 7100 → RSL10 | 7100 ready：上电先拉低等 RSL10 应答；也是读回阶段「响应就绪」的边沿源 |
| DIO11 | RSL10 → 7100 | **RSL10 到 7100 唯一的一条信号线** |

DIO11 只有这一条，所以对它做任何改动（极性、脉冲宽度、驱动/释放）影响都直接落在 7100 上，
且没有第二条线能旁证 —— 这就是下面每个实验都必须上板看 7100 行为的原因。

### 20.3 开机时 DIO11 的全部写点

| 位置 | 动作 | 说明 |
|---|---|---|
| 复位态（`App_Initialize()` 期间） | 弱上拉 ⇒ 弱高 | `DIO_CFG[n]` 复位值 `0x313F` = `DIO_MODE_DISABLE` + 弱上拉；软件消不掉这段窗口 |
| `app.c:49` | `DIO_MODE_GPIO_OUT_1` | 强驱动高，此后一直被驱动 |
| `app.c:67-68` | `OUT_0` → `OUT_1` | 握手应答「低脉冲」，宽度≈0（见 20.5(a)） |
| `app.c:99+101` | `OUT_0` … 1s … `OUT_1` | 引导完的第二个脉冲；现被 `#if 0` 关掉（见 20.5(b)） |

### 20.4 试过什么

**(1) `0xFC`：BLE 运行期拨 DIO11（已删）**

演变四版，都没有实测到效果：

| 版本 | 动作 | 结果 |
|---|---|---|
| v1 | 低脉冲，宽度宏默认 1ms | 无可见效果 |
| v2 | 脉冲宽度改由指令**后一个字节**给（ms） | 无可见效果 |
| v3 | 配合极性翻转，改成**高脉冲** | 未上板（随翻转一起回退） |
| v4 | 默认宽度改 0（间隔 0） | 未上板 |
| — | **删除** | 开发者决定 |

删除后的代码状态（已逐项核对）：

- `code/ble_custom.c` 与 HEAD **逐字节相同** —— 加过的 `#include "dsp_7100_init.h"` 和 `case 0xFC`
  都去掉了，文件里现只剩原有的 `0xFD`(FOTA) / `0xFE`(清 cache+复位) 两个分支。
- `include/dsp_7100_init.h` 回到 **163 行**原样（`DSP7100_DIO11_PULSE_MS` 宏、`dsp_7100_dio11_pulse()`
  声明都已删）。`DSP7100_DIO13_IRQ_ENABLE` 仍为 `1`。
- `code/dsp_7100_init.c` 里的 `dsp_7100_dio11_pulse()` 函数体与其注释块已删。
- 全工程残留的 `0xFC` 只剩 `app.c:46` 那一条**指向本节的历史注释**。

**(2) 极性翻转（未上板，已回退）**

需求是「开机 DIO11 默认输出**低**，脉冲那边弄成**高**脉冲，宽度/间隙不变，之后持续低」——
即把 20.3 表里三处写点的 `OUT_1`/`OUT_0` 对调。代码改完了，**没上板验证**就随本轮一起回退。

**(3) DIO11 释放成输入、纳入 `[IO]` 监控（已回退）**

试过把 DIO11 从输出改成输入，好在 `[IO]` 行里看它的电平 —— 已回退。`[IO]` 监控现在只打印
DIO9/10/13（与参考设计一致）。

### 20.5 顺带查实的四件事

**(a) 第一个握手脉冲的宽度 ≈ 0**

`app.c:67-68` 是两条紧挨着的寄存器写：

```c
Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_0);
Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_1);
```

`Sys_DIO_Config` 是 `__STATIC_INLINE`，展开就是**一条** `DIO->CFG[11] = cfg`（模式和电平在同一次写里
设定）。两条之间只有取指/写寄存器的时间，所以这个「低脉冲」宽度是**亚微秒级**，不是刻意给的宽度。
若 7100 要求一个最小宽度才认，这里其实是隐患 —— 但参考设计就这么写、握手也是通的，故不动。

**(b) 第二个脉冲实测是 1 秒，不是注释写的 2ms**

`app.c:100`：

```c
Sys_Delay_ProgramROM(1000 * (SystemCoreClock / 1000));   /* 原注释写 ~2ms，有误 */
```

`Sys_Delay_ProgramROM()` 收的是 **CPU 时钟周期**，不是毫秒。`1000 × 每毫秒周期数 = 1000ms`。
串口时间戳实证：脉冲前后两条日志差 **1018ms**。这行连同错注释是从参考设计
`remote_mic_rx_coex/app.c:77` 原样抄来的 —— 参考设计里的注释也错。

> ⚠ DIO11 是 RSL10 → 7100 唯一的线，把 1s 缩短 500 倍对 7100 侧行为的影响**未知**。
> 2026-09-29 决定只记事实、**不改值**（改值必须先上板确认引导仍正常）。

**(c) DIO11 在 `app.c:49` 之前是弱高**

`DIO_CFG[n]` 复位值 `0x313F` → IO_MODE = `DIO_MODE_DISABLE`(0x3F)、配的是**弱上拉**。所以从上电到
`app.c:49` 执行 `Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_1)` 之前，DIO11 由复位态的弱上拉**微微拉高**
（不是悬空、也不是强驱动）。这段窗口在 `App_Initialize()` 里，软件消不掉。

**(d) `DIO_DATA` 读的是 pad 实数，不是输出锁存器**

已核 `rsl10.svd`：`DIO_DATA` 是**只读**的引脚实际电平。所以 `[IO]` 打出来的 D13 是线上的真实电平，
不是「我们设成了什么」。

### 20.6 引导尾部那条 `04 82`（保留）

`code/dsp_7100_init.c` 的 `dsp_7100_boot_init()` 在 106 步跑完后**额外补发**一条 `0x82`
（线上是 `04 82`，HAL 会在前面补地址字节），日志里表现为第 107 条。

**依据** —— 抓包 `start-connect-parm.txt` 里 A1 段与 A2 段形状不对称：

| 段 | 连发 | 后面跟了什么 |
|---|---|---|
| A1（Packet 89..99，11 条连发） | ✓ | **Packet 100 有一条 `82` 收尾**，距 Packet 99 **782 µs** |
| A2（Packet 110..118，9 条连发） | ✓ | **没有** —— 连发完直接跳 A7（Packet 119） |

本工程已实测 **`82` 是真握手**（漏发 → 下一条读回 `00 00 00`，补发即愈），故按 A1 段的形状给 A2 段
补上收尾；延时 `782µs` 直接取自 Packet 99→100 的实际间隔。

> ⚠ **这条抓包里不存在，是有意偏离抓包的补发**，效果以上板为准。
> 且**不是已证实的必需项**：抓包里 Packet 120 的 A7 读在 Packet 119 之后**没等 `82` 就返回了合法的
> `46` 响应**。保留它是因为「A1 段有、A2 段没有」这个不对称像是漏发，代价只有 782µs（每次开机一次）。

**实现位置**：补发写在 C 代码里，**没有**动生成的步骤表 `code/dsp_7100_init_tables.c` ——
该表的契约是「顺序/字节与抓包完全一致」（`scripts/gen_dsp_7100_init.py`），塞一条合成项就破了契约。
所以 `dsp_init_step_cnt` 仍是 106，而日志打印的是 `n + 1` = 107。

> ⚠ 抓包 Packet 109 是一条 `0x83`（不是 `0x82`）—— **至今没解释**。与本节无关，顺手记在这里。

### 20.7 如果要重开这轮调试

- **`0xFC`**：`code/ble_custom.c` 需恢复 `#include "dsp_7100_init.h"` 与 `case 0xFC` 分支
  （照同文件里 `0xFD`/`0xFE` 的写法，都挂在 `GATTC_WriteReqInd` 的 `case REMPRO_IDX_ROLE_VALUE_VAL:` 下）；
  `include/dsp_7100_init.h` 恢复宏与声明；`code/dsp_7100_init.c` 恢复 `dsp_7100_dio11_pulse()`。
- **极性翻转**：`app.c` 三处写点对调（49 行、67-68 行、引导后那对写点）。
  注意引导后那个脉冲的 `#if 0` 已在 §21 摘掉，现在**是生效的**。
- **前置条件**：动手前先弄清 7100 侧到底靠什么判 DIO11 —— 目前**没有任何观测手段**能证明运行期拨
  DIO11 有效或无效（DIO13 无反应，串口也看不到 7100 内部）。

**本轮改动文件**（全部已回退，仅此记录）：`app.c`、`code/ble_custom.c`、`code/dsp_7100_init.c`、
`include/dsp_7100_init.h`。

**编译/上板状态**：回退后**未编译、未上板**。
## 21. 读回耗时回归 8.8 s → 1.0 s：根因 = 引导后那个 DIO11 脉冲（2026-09-30）

### 21.1 现象

同一天早些时候一轮 28 步读回只要 **1.037 s**（14:16），后来变成 **8.8 s**。两份 8.8 s 日志
（13:02、13:12）**逐字节相同**（同样的 19 次 retry、同样的包头 `00 46 00`/`02 42 2F`/`71 44 14`/
`65 6D 6F`/`0D 49 A8`…、同样的 payload）—— 说明是 7100 的**确定性行为**，不是 I2C 噪声。

- 卡在**第 9 步**（`A7 03 00 00 00 37 07 02` = 切到程序 2），连续 **19 次 retry**；
- 第 2 步（`37 07 01`）、第 16 步（`37 07 03`）、第 23 步（`37 07 04`）都 ~10 ms 一次过，**只有程序 2 卡**；
- retry 期间读到的不是空包，而是**真 7100 `42` 参数块内存**、错位一个字节，里面能看到 ASCII
  `Command Send`、`Stream C(fg)`（参数名见 `docs/7100协议.md:129`）和 RM 默认流地址 `F2 CD E6`；
- 卡完之后 DIO13 上升沿要 **170–230 ms** 才来（之前 ~0 ms）→ 7100 自己也变慢了，
  后半程每一步都退化成等 200 ms tick。

耗时账：`s_rb_retry` 锁 + 200 ms tick × 19 次 ≈ 3.8 s，其余是后半程每步 200 ms。
（`s_rb_retry` 见 `code/dsp_7100_init.c`：读失败后置位，在相内不认边沿、只等 tick，
用来阻止 `0x82` 自身上升沿自激成重试风暴。）

### 21.2 定位

把 14:16 基准之后的 **52 处 1664 代码编辑**逐条比对，**还生效的只有 2 处**：

| # | 文件 | 改动 | 性质 |
|---|------|------|------|
| 2534 | `app.c` | 引导后那个 DIO11 低脉冲被包进 `#if 0` | **功能性** —— 7100 唯一能感知到的差异 |
| 1677 | `code/dsp_7100_init.c` | `RB_DUMP_BYTES` = 1（hex dump） | 仅日志：一轮多约 4 KB ≈ 0.35 s |

其余当时做过的实验（`0xFC` 分支、DIO11 探针、极性翻转、引导尾巴那条 `04 82`、cache-invalidate）
**都早已回退**，`code/ble_custom.c` 与 HEAD 逐字节相同。另外 `HDR 00 46 00` 这个包头
**只出现在 8.8 s 的那两次日志里**，1.0 s 的两轮（14:16、13:29）都没有 —— 是个可信的判别特征。

### 21.3 结论：要的是「边沿」，不是宽度，也不是那 1 秒

恢复脉冲 + `RB_DUMP_BYTES=0` 之后，一轮 28 步回到 **1.035 s**（13:29），第 9 步一次过，回归消失。

剩下要拆的是：`#if 0` 同时去掉了**两个**因素 —— **（甲）脉冲本身**，和**（乙）它带的 1 秒延时**
（`Sys_Delay_ProgramROM`，它同时也**就是**脉冲宽度）。两轮对照实验拆开：

| DIO11 低脉冲 | 1 秒延时 | 结果 | 日志 |
|---|---|---|---|
| 有（宽度 1 s） | 有 | **28/28，1.035 s** ✓ | 13:29 |
| 有（宽度 **≈0**） | **无** | **28/28，1.029 s** ✓ | 13:40 |
| **无** | 有 | 27/28 快，**第 28 步永久卡死** ✗ | 13:36 |
| **无** | 无 | 卡第 9 步，整轮 **8.8 s** ✗ | 13:02 / 13:12 |

（「宽度 ≈0」= 两条背靠背的 `DIO->CFG[11]` 写，与开机握手那个脉冲同形。）

**结论：7100 认的是 DIO11 上的那个「边沿」，电平宽度和 RSL10 多等的那 1 秒都不是必需的。**
两轮 28/28 的 cache CRC 逐字节相同（`30AD / E942 / 8CE9 / 8641`）—— 去掉延时只是变快，数据没变。

顺带了结 §20 留下的悬案：参考设计那句「~2ms 低脉冲」的注释错了 500 倍，而**实际需要的宽度是 0**。
副作用是开机少阻塞 1 秒，进 `while(1)`（→ BLE 广播）提前约 1 秒。

#### 21.3.1 失败时的机理（两轮失败共用）

读回的头整体**错位一个字节**：期望 `46 AE 00`，实到 `00 46 AE`。而代码里

```c
hlen = (uint16_t)hdr[1] + ((uint16_t)hdr[2] << 8);
rd   = (hlen > DSP_INIT_RX_BUF) ? DSP_INIT_RX_BUF : hlen;
```

- 正常：`46 AE 00` → `hlen = 0x00AE = 174` = 命令声明的长度 → PASS
- 错位：`00 46 AE` → `hlen = 0xAE46 = 44614` → 被夹到 **`DSP_INIT_RX_BUF` = 700** →
  也就是日志里的 `DATA700B`

700 字节一发，7100 的应答流就彻底错位：之后连头都读不到（`AC 76 48`、`88 E3 CC`、`BE BA B7`
一路乱码），最后 `00 00 00` 再不回话。8.8 s 那两次是同一机理（`HDR 00 46 00`、`02 42 2F`
同样是错位头 + 700 B 夹紧），只是发生得更早、后面侥幸恢复。

**推测（未验证）**：DIO11 那一下拉低可能是在**冲刷 7100 的 IPC 应答缓冲**，而不是握手。
这能解释为什么运行期再拨它对 DIO13 毫无反应（§20）—— 那个时刻它已经不是握手了。
**没有观测手段证实，姑且记着。**

### 21.4 本轮改动（已编译、已上板）

- `app.c`：删掉包住引导后脉冲的 `#if 0` / `#endif` 与调试说明（§21.1 那次回归由此修复）；
- `app.c`：**脉冲宽度 1 秒 → 1ms**（`Sys_Delay_ProgramROM(SystemCoreClock / 1000)`），
  去掉那 1 秒开机阻塞 —— 依据是上面 13:40 那一轮；
- `code/dsp_7100_init.c`：`RB_DUMP_BYTES` 1 → 0（关掉后 `RB_DUMP(...)` 展开成 `((void)0)`，
  串口输出与 14:16 逐字节相同；`dump_hex` 仍被命令 dump 用着，无 unused-function 警告）；
- `code/dsp_7100_init.c`：`s_end82` 上方那条「两处用：引导序列末尾…」的注释已失真（引导那处
  于 §20 删除），改为一行实述。

**编译/上板状态**：每一步都由开发者在 Eclipse 编译、上板实测（13:29 / 13:36 / 13:40 三轮日志即证据）。

> ⚠ **遗留**：1ms 是**安全余量**，实测 0 也通过，但目前只测了**一轮**。失败点在第 9 步和第 28 步
> 都出现过，看着有随机性 —— **建议再断电重启复测 2 次**再视为定稿。

## 22. 运行命令改「只发写帧 + 82」（切程序 / 调音量 / 测听 / 纯音，2026-09-30）

### 22.0 最终形态一览（**已上板验证**）

四条命令现在一律**只发命令、不读应答**；`82` 是每条命令的收尾（实验证过的解锁字节）。
**写帧与 `82` 之间的等待，按各自的协议事实取值**：

| 命令 | BLE | 线上序列（`04` = HAL 加的设备地址写字节） | 会话 |
|---|---|---|---|
| 切程序 | Scene (16) | `04 A2 00 16 <prog>` → 立刻 → `04 82` | 1 步 / 2 帧 |
| 调音量 | Volume (2) | `04 A2 00 12 <0x11…0x64>` → 立刻 → `04 82` | 1 步 / 2 帧 |
| 纯音出音 | PlayVoice (13) | `04 A7 07 00 00 00 2E 01 <freq16> <level24>` → 立刻 → `04 82` | 1 步 / 2 帧 |
| 纯音停音 | StopVoice (14) | `04 A7 07 00 00 00 2E 00 00 00 00 00 00` → 立刻 → `04 82` | 1 步 / 2 帧 |
| **测听进入** | Audiometry (40) = 0 | ① `04 A2 00 16 03` → **80ms** → `04 82`<br>② `04 A2 00 2E 00` → **2ms** → `04 82`<br>③ `04 A7 01 00 00 00 26` → 立刻 → `04 82` | 3 步 / 6 帧 |
| **测听退出** | Audiometry (40) = 1 | ① `04 A2 00 16 <prev>` → **80ms** → `04 82`<br>② `04 A2 00 2E 58` → **2ms** → `04 82`<br>③ `04 A7 01 00 00 00 26` → 立刻 → `04 82` | 3 步 / 6 帧 |

- 两个延时都是 HEAD 原值：80ms = `switch_program` 的程序加载窗口，2ms = `set_tone_mode` 的帧间间隔。
- 与改动前相比，砍掉的只有**读**（每句后面那一次 + 它前后的 1ms）；命令条数与延时不变。
- 日志指纹：`session start: … cmds=` → 切程序 **1** / 调音量 **1** / 纯音 **1** / 测听 **3**。
- 上板结论（§22.6）：切程序、调音量、测听整条链路（进入→出音→停音→退出）**全部正常**。

### 22.1 改之前的发送内容

| BLE 命令 | 处理函数 | 映射 |
|---|---|---|
| `SetVolume` (ID 2) | `cmd_setvolume_7100` | App vol 0-5 → 档位 1-6（`level = vol + 1`） |
| `SetCurrentScene` (ID 16) | `cmd_setcurrentscene_7100` | App scene 0-3 → 程序 1-4（`prog = scene + 1`） |

两者都落到 `code/dsp_7100_cmd.c` 的 `a7_session_start()`，会话骨架是 A2 写帧打头：

```
调音量  dsp_7100_set_volume(L)      1 步 / 3 帧
        写 A2 00 12 <v> → 2ms → 读 3B → 82

切程序  dsp_7100_switch_program(P)  2 步 / 5 帧
        写 A2 00 16 <P> → 80ms → 读 3B → 82
        纯读 6B → 82
```

`v = {0x11,0x21,0x32,0x43,0x53,0x64}`（`s_volume_value[]`）。

### 22.2 实测：A2 无回铃得到确认，但应答读不出可信内容

一轮 `[7100] W` / `R` 字节转储（切程序 prog=3，15:35:32）：

```
[32.924] [7100] W (4B ok=1): A2 00 16 03
[33.001] [7100] R (3B ok=1): 65 01 00
[33.004] [7100] W (1B ok=1): 82
[33.004] [7100-irq] DIO13 rise +1/42 fall +1/41
[33.014] [7100] R (6B ok=1): 28 65 01 00 28 43
[33.014] [7100] W (1B ok=1): 82
[33.014] [7100-irq] DIO13 rise +1/43 fall +1/42
[33.024] [7100] --- session done ok=1 ---
```

| # | 观察 | 结论 |
|---|------|------|
| 1 | 32.924 写完到 33.004 之间**零个 `[7100-irq]`** | **A2 写不产生 DIO13 回铃** —— 实测确认；80ms 固定延时是必需的（也生效：924→001 = 77ms） |
| 2 | 两条 `82` 各产生 1 升 1 降（计数 41→42→43） | 边沿驱动的节拍模型成立，严格 1:1 |
| 3 | 第一条读回 `65 01 00` | **7100 未就绪**（该签名见 `code/dsp_7100_init.c` 的 `RB_SKIP_END82_IDX` 注释：「基线偶发重试返回 65 01 00（7100 明说未就绪）」）。而写路径**完全不校验应答**，只判 I2C 传输成功 → 把它当确认，一路 `ok=1` |
| 4 | 第二条读回 `28 65 01 00 28 43` | **错位**。写路径按固定长度盲读（A2 步的 `rlen` 取的是 A7 的 `A7_RX_ACK` = 3），而读回路径是「先读 3B 头 → `hlen = hdr[1] \| (hdr[2] << 8)` → 再读 `hlen` 字节」。长度一旦与 7100 实际吐出的不符，下一次读就从错误偏移开始，且**没有事务边界可重同步** |
| 5 | 错位串末尾那个 `43` | 是期望确认 `43 03 00 00 <reg> <val>` 的**首字节** —— 说明 7100 约在 +90ms 才就绪并开始吐确认，而那时我们已经带着错位碾过去了 |

**即 80ms 不够（77ms 时仍未就绪），而且即使够，读法本身也是错的。**

### 22.3 决策：切程序 / 调音量只发写帧 + 82

| 决定 | 理由 |
|---|---|
| 砍掉读确认 / 读状态 | A2 应答格式在本工程从未验证过（来源是 `peripheral_server_sleep7160test` 的抓包）；实测既错位、又半数是 `65 01 00` 未就绪，读不出可信内容 |
| **保留 `82`** | 本工程实验证过的**解锁字节**：漏发后下一条拿不到 `46`、返回 `00 00 00`，补发即愈（见 `code/dsp_7100_init.c` 的 `RB_SKIP_END82_IDX`） |
| 顺带砍掉 80ms / 2ms 固定延时 | 既然不读，就没有什么需要等的 |
| 接受「不再校验」 | 代价明确：`ok=1` 只代表 I2C 写成功 |

**被否掉的替代方案**：改用读回路径**已验证 28/28** 的 `A7 02 00 00 00 12 <P>` 来切程序。
否掉的原因：读回只用它取参数，**它能否真的切换音频程序未经验证**。留作日后兜底。

### 22.4 改动清单

| 文件 | 改动 |
|---|---|
| `code/dsp_7100_cmd.c` | 新增 `a7_add_a2_noack(reg, val)` —— A2 写帧 + 82、`rlen` 置 0（本步不读）；内部用 `s_step_cnt` 前后比对，兜住 `a7_put()` 失败时不越界改上一步 |
| 同上 | `A7_KIND_VOL` / `A7_KIND_PROG` 两分支改用 `a7_add_a2_noack` |
| 同上 | 步骤机 gate 一行：`s->wlen > 0` → `s->wlen > 0 && s->rlen > 0`。否则 `rlen = 0` 的步仍会去等一个永远不来的上升沿，每步退化成 200ms tick 兜底 |
| `include/dsp_7100_cmd.h` | 顶部帧描述改为「抓包波形里是…」，并加 ⚠ 段说明砍读的原因与代价 |

> 本小节只记当时那一版，**后续小节已多次改写，以 §22.5 为准**：
> 切程序与测听的分支时分时合（最终是**分开**的：测听是 3 条命令的会话）；
> `A7_A2_WAIT_MS` / `A7_PROG_LOAD_MS` 当时写着「测听仍在用」，中间一度无人引用，
> **最终都回到测听在用**（80ms 给第 ① 句切程序、2ms 给第 ② 句测听位）。

### 22.5 测听（进入 / 退出）：三句都留，只砍读（2026-09-30）

这一小节来回改过三次，**最终形态在最后**，前面两步只作过程记录。

**第一步**：`A7_KIND_AUDIO_ENTER` / `A7_KIND_AUDIO_EXIT` 与 `A7_KIND_PROG`
**合并走同一个构建分支**，只发一条 `A2 00 16 <prog>` + `82`。

**第二步**（「保留之前的延时，把读都去了」「只修改测听进入和退出」）：测听从切程序分支
**再拆出来**，补回 80ms 程序加载窗口。

**第三步（最终，开发者：「我要其他两句，只是不去读」）**：把 `A2 00 2E`（测听位）与
`A7 01 00 00 00 26`（解除静音）**加回来**，与切程序编成**一个会话的 3 步**，全部不读应答：

| # | 进入（`A7_KIND_AUDIO_ENTER`） | 写后 | 退出（`A7_KIND_AUDIO_EXIT`） |
|---|---|---|---|
| ① | `04 A2 00 16 03` | 等 **80ms** → `04 82` | `04 A2 00 16 <prev>` |
| ② | `04 A2 00 2E 00` | 等 **2ms** → `04 82` | `04 A2 00 2E 58` |
| ③ | `04 A7 01 00 00 00 26` | 立刻 → `04 82` | `04 A7 01 00 00 00 26` |

- 80ms / 2ms **都是 HEAD 原值**（`switch_program` 的程序加载窗口、`set_tone_mode` 的帧间间隔），
  不是新造的数；退出那侧同值。
- **原波形里每句后面都跟着一次读 + 一次 `82`**（切程序那句原本有**两次**读、**两条** `82`），
  本次只砍读 —— 所以每句现在只配一条 `82`。
- 第 ③ 句 `A7 …26` **原来没有固定延时**（靠等 A7 回铃上升沿），按「只砍读、不另加等待」
  处理成**写完立刻 `82`**（与切程序 / 调音量 / 纯音一致）。
- **进出末尾都是 `26` 解除静音**（抓包原话「末尾恒有 26」）。
- ⚠ 三句**必须在同一会话里**发 —— 单独调 `dsp_7100_set_mute()` 凑第 ③ 句会撞
  「同一时刻只允许一个会话」，被直接忽略。

连带：`a7_put_read()` 因无调用者**已删除**；`DSP7100_REG_TONE_MODE` / `TONE_MODE_ON/OFF`
与两个延时常量 `A7_A2_WAIT_MS` / `A7_PROG_LOAD_MS` **全部回到在用状态**。
新增一个小助手 `a7_add_simple_noack(opt)`（= `a7_add_simple` + `rlen=0`），
与既有的 `a7_add_a2_noack(reg, val, wait_ms)` 同形。

**切程序 / 调音量 / 纯音不在此列** —— 按开发者的范围限定，它们仍是「写完立刻 `82`」。

**上板状态**：**已编译、已上板、测听进入 / 退出正常**（§22.6）。

### 22.6 验证状态与遗留

**已编译、已上板验证通过**（两轮）：

| 轮次 | 验证内容 | 结论 |
|---|---|---|
| 前一轮 | 切程序 / 调音量（只发写帧 + `82`） | 「切模式调音量正常」—— App 切场景声音真的变、调音量音量真的变 |
| 本轮 | **测听整条链路**：CMD 40 进入 → CMD 13 出音（听得见）→ CMD 14 停音 → CMD 40 退出 | 「测听流程正常」 |

日志指纹：`session start: … cmds=` —— 切程序 = **1**、调音量 = **1**、测听 = **3**；
测听那三条后面**不再有 `[7100] R` 读回**。

至此 **§22 的全部改动（§22.1–§22.7）均已上板验证**，无「未编译 / 未上板」遗留。

遗留：

1. **不再校验了** —— `ok=1` 只代表 I2C 写成功；命令是否真生效只能靠听。已接受此代价。
2. **`82` 紧跟写帧 / 紧跟延时** —— 已实测通过（切程序、调音量、纯音都是「写完立刻 `82`」；
   测听是「写后等 80ms / 2ms 再 `82`」）。原抓包里 `82` 在读确认**之后**，中间隔了一次读，
   所以「7100 是否真的不在意那个读」这个前提**仍未被独立证明**，只是**实测没出问题**。
   若日后出现间歇失效，兜底手段是回退读那一步，而不是加延时。
3. **A2 命令本身是否正确的嫌疑没消除** —— 它仍来自别的产品的抓包，本次只改了发送逻辑。日后若出现间歇失效，**优先怀疑命令，而不是链路**。
4. ~~测听位 / 解除静音被去掉后是否还需要~~ **已加回，三句版本上板实测正常**（§22.5 第三步）。
   ⚠ 但**「只切程序」那一版从未上过板**（改出来后没编就被换掉了），所以它**是否真的不够
   发，并没有被证伪** —— 这条假设只是被更保守的方案取代了，不是被推翻了。
   日后若想再精简测听，这仍是一个**未验证**的候选（上板时重点看出音是否照常）。
5. **步骤表的 `wait_ms` 一度成为死路径，现已复活**：`a7_put_read()` 删除后曾没有任何构建函数设非 0 的 `wait_ms`；§22.5 第二步给测听补回 80ms 后，`dsp_7100_cmd_step()` 里那个 `wait_ms > 0` 分支**重新被走到**（`A2 写帧 → 延时 → 82`），步骤机无需任何改动。`rlen = 0` 那条 gate（`s->wlen > 0 && s->rlen > 0`）仍然必要 —— 纯音是「不读也不延时」，靠它才不会去等一个不需要的上升沿。
6. **测听那两个延时（80ms / 2ms）是照抄原值，值本身未重新标定** —— 分别取自
   `dsp_7100_switch_program()` 的程序加载窗口与 `dsp_7100_set_tone_mode()` 的帧间间隔。
   本轮实测**按这个值跑通了**；但若换机 / 换 7100 固件后测听出现异常，
   **仍然是第一个该动的旋钮**。

### 22.7 纯音（出音 / 停音）同样简化（2026-09-30）

`a7_add_tone()` 末尾加一行 `s_steps[s_step_cnt - 1].rlen = 0;` —— 出音 / 停音从
**写 12B 帧 → 等上升沿 → 读 3B → `82`**（抓包 `tone*hz*db.txt` 的四步）变成 **写帧 + `82`**：

| 用途 | 线上序列 |
|---|---|
| 出音 | `04 A7 07 00 00 00 2E 01 <freq16> <level24>` → `04 82` |
| 停音 | `04 A7 07 00 00 00 2E 00 00 00 00 00 00` → `04 82` |

⚠ **与 A2 那两处有个本质区别**：A2 是「7100 不回铃，所以只能直接 `82`」；
A7 写**本来是有回铃的、等得起**。这里按「和切程序 / 调音量一样，简单优先」一并不等 ——
属于**主动放弃握手**，不是没有选择。

**上板状态**：**已编译、已上板、出音 / 停音正常**（本轮测听链路验证含在内，见 §22.6）。
即「主动放弃 A7 回铃」这一步**实测没问题**。若日后出现**出音偶发不响 / 停音不干净**，
第一手段仍是把这一行 `rlen = 0` 删掉（回到 写 → 读 → `82`），而不是先怀疑电平表或帧格式。

`A7_KIND_MUTE` **没跟着改**（仍是 写 → 读 3B → `82`）—— 本次只简化纯音。

### 22.8 执行时序与阻塞（主循环占用分析）

本节是**静态代码分析**（连百 µs 级的耗时尚**未上板实测**），回答「发这些命令时到底卡不卡主循环」。
分三个层次看，混起来会得出错误印象：

| 层次 | 阻塞？ | 说明 |
|---|---|---|
| ① BLE 命令处理函数 | **否** | `dsp_7100_switch_program` / `_set_volume` / `_audiometry` 只**建步骤表 + 置会话状态**就返回，此时一个字节都还没发出去 |
| ② 单次 I2C 事务（HAL） | **是** | `i2c_7100_write` / `_read` 是忙等轮询（上限 `I2C_7100_TIMEOUT_MAX` = 2 000 000 次循环），**循环内每次都 `Sys_Watchdog_Refresh()`**；410 kHz 下几字节 → 百 µs 量级（**估算，未实测**） |
| ③ 步骤之间 | **否** | `dsp_7100_cmd_poll()` 等不到 DIO13 边沿时**直接 return**，靠主循环反复进来推进，不是 spin（200 ms tick 只置 `s_cmd_timeout` 兜底） |

**唯一的真阻塞是 `i2c_7100_delay_ms()`** —— `dsp_7100_cmd.c:760-762` 里 `wait_ms > 0` 那一支，
内部是 `Sys_Delay_ProgramROM(ms * SystemCoreClock / 1000)`，**纯忙等且不喂狗**（`i2c_7100_hal.c:230`）。

按命令分：

| 命令 | 步骤数 | 步骤表 `wait_ms` | 忙等合计 | 主循环表现 |
|---|---|---|---|---|
| 切程序 | 1 | 0 | **0** | 只有两次百 µs 级 HAL 忙等，中间被主循环切开 |
| 调音量 | 1 | 0 | **0** | 同上 |
| 纯音 出/停 | 1 | 0 | **0** | 同上；且 A7 回铃被主动放弃（§22.7） |
| **测听 进/出** | 3 | 80 / 2 / 0 | **82 ms** | ①的 80 ms 与 ②的 2 ms **各自**烧在一次 poll 调用里，中间主循环至少转一圈 |

补充事实：

1. **测听那 80 ms 切不碎** —— 写帧 → `i2c_7100_delay_ms(80)` → `82` 全在同一次 `dsp_7100_cmd_poll()` 里完成。
   这段时间 `dsp_7100_cmd_busy()` 为真，读回 / 心跳一并停摆。
2. **第 ③ 句 A7 的回铃被丢掉** —— `rlen = 0` 让它落进 `else` 分支（`s_cmd_read_gate = 0`），
   SEND 相位不记 `s_cmd_wait_rise`，A7 特有的上升沿信息完全没用上，写完整条立刻 `82`。
   （与 §22.7 的纯音同性质：A7 等得起，这里是主动不等。）
3. **不喂狗在当前配置下不构成复位风险** —— 全工程只有 `Sys_Watchdog_Refresh()`，
   **没有 `Sys_Watchdog_Config()`**，走复位默认周期，80 ms 落在窗口内；已上板实测正常。
   ⚠ 若日后有人调短 WDT 周期，**第一处会咬到的就是这 80 ms**。

**后面若出问题的调整方向**（各自代价明确，非必要不动）：

- 测听 80 ms 想改成不阻塞：**没有边沿可等**（第 ① 句是 A2，本来就不回铃），
  只能退回 200 ms tick 兜底，粒度反而更粗 —— 不划算。
- 把 `i2c_7100_delay_ms()` 改成带喂狗的版本：改的是 HAL 公共函数，影响面超出测听，须单独评估。

## 23. DFBC 设置改「不读应答、等回铃直接 82」（2026-09-30）

沿用 §22 的取舍方向，但**只动 DFBC**。降噪 / WDRC / EQ 共用同一套骨架，本轮**未改**。

> ⚠ **结论先行（§23.6 已上板实测）**：「等回铃再发 `82`」这半截**不成立** ——
> 实测**写帧产生零边沿**，边沿只在 `82` 之后出现，于是门控永远等不到，
> 每步退到 200 ms tick 兜底 → **会话 1430 ms、静音窗口 810 ms**。
> 「等下降沿再发下一条」那半截是好的（0~2 ms）。
> **待办见 §23.7（国庆后改方案 A）。**

### 23.1 每条命令的线序变化

| | 线序 | I2C 事务/条 |
|---|---|---|
| 改前 | 写帧 → 等 DIO13 上升沿 → **读 3B** → `04 82` → 等下降沿 | 3 |
| 改后 | 写帧 → 等 DIO13 上升沿 → `04 82` → 等下降沿 | 2 |

**命令本身一字未动** —— 还是抓包 `p0dfbc0-1.csv` 里那 8 条（见 §23.2）。
骨架、条数、`82` 的解锁作用、下降沿门控全部保留，**只砍掉中间那次读**；
边沿等不到仍由 200 ms tick 兜底放行。

### 23.2 DFBC 会话的 8 条命令（未改）

| # | 写帧（线上） | 作用 |
|---|---|---|
| 1 | `04 A7 01 00 00 00 25` | 静音 |
| 2 | `04 A7 02 00 00 00 12 <prog>` | 选程序 |
| 3 | `04 A7 05 00 00 00 05 0A <prog> 00 00` | 准备 DFBC 块 |
| 4 | `04 A7 04 00 00 00 08 00 00 <01\|00>` | 写使能（01 = 开，00 = 关） |
| 5 | `04 A7 02 00 00 00 10 <prog>` | confirm |
| 6 | `04 A7 01 00 00 00 26` | 解除静音 |
| 7 | `04 A7 02 00 00 00 12 01` | 选回程序0 |
| 8 | `04 A7 01 00 00 00 0C` | commit |

8 条与抓包逐字节一致。抓包里另有的 6 条 `A7 01 00 03 00 02`（0x03 状态读）
本工程一直刻意省略，本次也没加回来。

### 23.3 实现：一个新字段 + 一个会话标志

`a7_step_t` 加 `wait_rise`：

```c
uint8_t wait_rise;   /* 1 = 不读那 3B，但仍要等 A7 回铃上升沿再发 82 */
```

步骤机 SEND 相位的门控从 `s->rlen > 0` 改成：

```c
} else if (s->wlen > 0 && (s->rlen > 0 || s->wait_rise)) {
    s_cmd_read_gate = 1;
}
```

会话级标志 `s_sess_noack` 由 `a7_put()` 消费（决定每步的 `rlen` / `wait_rise`），
在 `a7_build_session()` 顶部按 kind 置位：

```c
s_sess_noack = (kind == A7_KIND_DFBC) ? 1 : 0;
```

**状态机一行没动** —— 相位逻辑、边沿门控、tick 兜底全部复用。
DFBC 的 8 步因此自动全部变成 `rlen = 0 / wait_rise = 1`。

### 23.4 ⚠ 为什么必须用显式字段，不能靠 rlen 推断

**纯音的帧形状和 DFBC 每一步完全一样**（`wlen > 0`、`rlen = 0`、`wait_ms = 0`），
但语义**正好相反**：

| 会话 | `rlen` | `wait_rise` | 写完帧之后 |
|---|:---:|:---:|---|
| 纯音（§22.7） | 0 | **0** | **不等回铃**，立刻 `82` |
| DFBC（§23） | 0 | **1** | **等回铃上升沿**，再 `82` |

所以不能用「`rlen == 0` 就是不等回铃」来推断，只能显式区分。

另外 `a7_add_a2()` 里强制 `wait_rise = 0`：A2 写**没有** DIO13 回铃（§22.2 实测），
若哪天的会话标志不小心盖到 A2 步上，就会变成等一个永远不来的边沿 ——
一行钉死，避免以后踩。

### 23.5 收益与代价（**别期待变快**）

**会话总时长基本不变。** 一条命令省下的只是那次 3B 读（百 µs 级），
而快慢由 DIO13 边沿的间隔决定 —— 上升沿、下降沿一个都没少等。

拿到的是：

- 少一次可能读错位的读（A7 的读目前是好的，但少一次盲读就少一个出错面）
- DFBC 路径与 §22 方向一致，代码更单一

代价：

- **主动放弃一个本来读得通的握手。** 这与 §22 砍 A2 读的理由**不同** ——
  A2 是**读不出可信内容**才砍；DFBC 的读在抓包里 14/14 都是干净的 `46 00 00`。
  这次是「明知读得通，仍然不读」。
- ⚠ **前提已被实测推翻**（见 §23.6）：本设计依赖「7100 在主机不读的情况下**照样**
  拉 DIO13 上升沿」。**实测写帧产生零边沿**，边沿只在 `82` 之后出现 ——
  于是每一步都靠 200 ms tick 兜底放行，8 步 = 8 个 tick = **1430 ms**
  （**退化但没卡死** —— 下降沿门控仍在，这一半是好的）。

### 23.6 已上板实测（2026-09-30）—— **结论：设计前提不成立**

**代码按预期跑通，但"等写帧后的回铃"这一半永远等不到。**

`cmds=8` ✅ ｜ 全程**没有一行 `[7100] R`** ✅ ｜ `session done ok=1` ✅
会话 27.575 → 29.005 = **1430 ms**。

#### 8 条 `82` 的时刻（决定性数据）

| 步 | `82` | 距上一步 |
|---|---|---|
| ① 静音 | 27.603 | —— |
| ② 选程序 | 27.803 | **200 ms** |
| ③ 准备块 | 28.003 | **200 ms** |
| ④ 写使能 | 28.202 | **199 ms** |
| ⑤ confirm | 28.403 | **201 ms** |
| ⑥ 解除静音 | 28.605 | **202 ms** |
| ⑦ 选回 | 28.804 | **199 ms** |
| ⑧ commit | 29.003 | **199 ms** |

**等距 200 ms = tick 周期。8 步 = 8 个 tick，没有一步是边沿放行的。**
第①步只花 8 ms，是因为会话起点（27.575）**正好赶在 tick 网格点 27.603 前 28 ms**
—— 是运气，不是速度。

#### 边沿归属：`82` 产生一对 rise + fall，写帧产生零边沿

会话**最后一条**（commit）之后不再有任何写帧，但计数仍在涨：

```
28.804  W  82                        ← 第⑦条的 82
28.805  [irq] rise +1/72 fall +1/71
28.805  W  A7 01 00 00 00 0C         ← 第⑧条写帧（最后一条写帧）
29.003  W  82                        ← 第⑧条的 82
29.005  [irq] rise +1/73 fall +1/72  ← 之后没有任何新写帧，边沿只可能来自这条 82
29.005  --- session done ok=1 ---
```

对账：

| | 条数 | rise | fall |
|---|:---:|:---:|:---:|
| 写帧 | 8 | **0** | **0** |
| `82` | 8 | **8** | **8** |
| 日志总计 | | **8** ✅ | **8** ✅ |

且**每个边沿都落在某条 `82` 之后 0~2 ms**，8 对 8 从无例外：

| `82` | 边沿打印 | 间隔 |
|---|---|---|
| 27.603 | 27.605 | 2 ms |
| 27.803 | 27.805 | 2 ms |
| 28.003 | 28.005 | 2 ms |
| 28.202 | 28.204 | 2 ms |
| 28.403 | 28.404 | 1 ms |
| 28.605 | 28.605 | 0 ms |
| 28.804 | 28.805 | 1 ms |
| 29.003 | 29.005 | 2 ms |

⇒ **`82` → 一个 rise + 一个 fall 脉冲；写帧 → 零边沿。**

#### 为什么门控必然空转（鸡生蛋）

```
写帧 → 等上升沿 → 发 82 → 上升沿才出现
        ↑__________________________|
        等的那条边沿，要等自己发出 82 才会有
```

所以 `wait_rise` 门控**永远不可能满足**，每步都退到 200 ms tick 兜底。
**当前实际是 tick 驱动、不是边沿驱动** —— `wait_rise` 唯一的作用就是把每步拖到 200 ms。

#### 附带后果：静音窗口 810 ms

第①条**静音**（27.595），第⑥条才**解除静音**（28.404）——
中间 **~810 ms 助听器完全无声**，全是 4 个 tick 堆出来的，用户能明确感知。

#### 哪一半是好的

**"等下降沿再发下一条"这一半生效了** —— 每条 `82` 到下一个写帧只隔 **0~2 ms**，
那是 `s_cmd_send_gate` 在起作用，不是 tick。坏的只有"等写帧后的边沿再发 82"前半截。

### 23.7 待办（**国庆后回来再调**）

**方案 A（建议）**：砍掉前半截门控 —— DFBC 的 `wait_rise` 置 0，
改成「写帧 → **立刻** `82` → 等下降沿 → 下一条」，即 §22.7 纯音的形态。

| | 会话时长 | 静音窗口 |
|---|---|---|
| 现状 | 1430 ms | 810 ms |
| 方案 A | **约 40 ms** | **约 30 ms** |

改动很小：让 `s_sess_noack` 只控制 `rlen`（不控制 `wait_rise`），
或拆成两个会话级开关。

⚠ 唯一风险：**写完立刻发 `82` 比原厂抓包的 45 ms/条还快**，7100 跟不跟得上**未验证**。
若 A 测出问题，退到"固定延时 50 ms"（复用现有 `wait_ms`），会话约 400 ms ——
代价是 8×50 ms 不喂狗的忙等（同 §22.8 测听那 80 ms 的问题）。

**其它遗留**：

1. 降噪 / WDRC / EQ **仍是老行为**（读 3B）。同一个函数里跑两种节奏是**有意为之** ——
   本轮只在 DFBC 上验证，且结论是"前半截门控无意义"，所以推给它们之前
   应先决定走 A 还是保持现状。
2. **DFBC 开关本身是否真的生效未确认** —— `ok=1` 只代表 I2C 写成功。
   下次上板请**用耳朵确认开 / 关真的切换**，这是 A 方案的前置条件。
3. 与 §22.6 遗留 1 相同：不再校验，`ok=1` 只代表 I2C 写成功，不代表 7100 照做了。
4. **1430 ms / 8 tick 是"改后"的数据，"改前"（读 3B 那版）没跑过基线。**
   推断改前用的是同一个上升沿门控、同样是 tick 驱动，故时长应当相同 ——
   但**这是推断，未实测**。

> ⚠ **§23.7 的所有待办已由 §24 取代**（2026-10-08 重做）。方向与方案 A 不同 ——
> 不是"去掉读、写完立刻 82"，而是"**恢复读**，读之前等固定 50ms"。见 §24。

---

## 24. 命令会话分两条路径：配置族恢复读应答（2026-10-08）

### 24.0 结论先行

| | 路径 A 配置族 | 路径 B 交互族 |
|---|---|---|
| 谁走 | 降噪 / DFBC / EQ / WDRC | 切程序 / 调音量 / 测听 / 纯音 / 静音 |
| 线序 | 写帧 → 等 50ms → 读 3B → 校验 46 → `82` → 等下降沿 | 写帧 →（等 wait_ms）→ `82` → 等下降沿 |
| 读应答 | **读**，非 `46` 就重读（最多 3 次） | 不读 |
| 每条耗时 | ≈ 52 ms | ≈ 2 ms（测听第 ① 句 82 ms） |
| 校验 | 有（`ok=0` 会真的报失败） | 无，`ok=1` 只代表 I2C 写成功 |

**一条铁律（实测得出，两条路径都适用）**：**A7 写帧不产生任何 DIO13 边沿。**
所以「写完等上升沿再读」在本工程**永远不可能成立** —— §23 那版就是栽在这上面。
命令层因此**只认 `82` 的下降沿**，`s_cmd_read_gate` / `s_cmd_wait_rise` 已整组删除。

### 24.1 本轮实验：A7 写帧零边沿（上板实测）

> ⚠ **本节结论已被 §25.7 推翻**（2026-10-08 晚上板两轮）：A7 写帧**有**上升沿，
> 在写帧后 47~60ms、早于我们的读。本节「8/8 走到 200ms 兜底」的判据用的是
> `[7100-irq]` 打印，而那个打印点在 `W 82` **之后** —— 累计数的打印位置不等于
> 边沿的位置，早到的沿被算到了 82 头上。本节其余内容（82 的沿、会话时长）仍成立，
> 但**不要再引用「0xA7 族没有上升沿」**这一句。

为了分辨「A7 写到底有没有回铃」，在 `dsp_7100_cmd_poll()` 里加了一行**只打印不改行为**
的日志：哪个相位被门控住却靠 200ms tick 兜底才走进来。跑一次 DFBC 设置（8 条命令），
**8 步全部命中**：

```text
[09:01:11.040] [7100] W (6B ok=1): A7 01 00 00 00 25
[09:01:11.050] [7100] !step0/8 READ 兜底200ms：本条写帧上升的边沿没来 rise=41 fall=40
[09:01:11.087] [7100] W (1B ok=1): 82
[09:01:11.087] [7100-irq] DIO13 rise +1/42 fall +1/41
```

七种**不同形状**的帧都试到了，一个上升沿都没有：

| 步骤 | 帧 | 作用 |
|---|---|---|
| 0 | `A7 01 00 00 00 25` | 静音 |
| 1 | `A7 02 00 00 00 12 01` | 选程序 |
| 2 | `A7 05 00 00 00 05 0A 01 00 00` | 准备块 |
| 3 | `A7 04 00 00 00 08 00 00 01` | 写使能 |
| 4 | `A7 02 00 00 00 10 01` | confirm |
| 5 | `A7 01 00 00 00 26` | 解除静音 |
| 6 | `A7 02 00 00 00 12 01` | 选回程序 0 |
| 7 | `A7 01 00 00 00 0C` | commit |

**哪个锚点是好的**：

- `82` → rise + fall，**0~2 ms 到齐**（`11.087 W 82` → `11.087 [7100-irq] rise +1 fall +1`）。
- 「等上一条 `82` 的下降沿再发下一条」实测 **0~13 ms**，完全可用。
- 门控等不到的兜底是标准 **200 ms 栅格**：READ 落在 `11.237 / 11.437 / 11.637 /
  11.837 / 12.040 / 12.240 / 12.440`，一步一个 tick。
- 会话 `11.020 → 12.461` = **1440 ms**，静音窗口 `11.040 → 11.850` ≈ **810 ms**。

**对照组**：读回（`dsp_7100_init.c`）同一套 gate 跑 28 步只要 **1.035 s**，
说明 **`0x77` 族（`04 77 01` 之类）的写是有上升沿的**。所以不是"写都没有回铃"，
是 **`0x77` 族有、`0xA7` 族没有**。这个区别后面还要再挖（见 §24.6）。

### 24.2 参考实现是怎么读的（抓包 p0dfbc0-1.csv）

`docs/7100协议/DFBC/7100_A7协议_p0dfbc0-1.md` 里 42 条事务，
把每条 `[W]` 到紧跟的 `[R]` 的间隔算出来：

| 命令类型 | 写→读间隔 |
|---|---|
| 设值类（`25` / `12 01` / `05…` / `08 00 00 01` / `10 01` / `26` / `0C`） | **44 / 45 / 46 / 47 / 48 / 50 / 71 ms** |
| busy 轮询（`A7 01 00 03 00 02`） | **94 ~ 98 ms**（首条 250 ms 是冷启） |

**14/14 全部读到 `46`。** 关键推论：

1. **参考实现是"写完等一段固定时间再读"，不是等边沿** —— 与 §24.1 的实测完全吻合。
2. 它的设值命令周期 ≈ **50 ms**（busy 轮询那档 ≈ 100 ms，因为轮询自己占 2 格）。
3. 我们**不读** `0x03` 状态（§23.2 已定），所以只需要设值那一档 → **取 50 ms**。

> 50 ms 是参考实现实测 44~98 ms 的**下沿**。它是不是"应答最早什么时候就绪"未知 ——
> 参考实现可能只是在按自己的节拍轮询。等第一轮日志证明 50 ms 恒回 `46`，再考虑往下压。

### 24.3 设计

**路径 A（配置族）**：

```text
写帧 → i2c_7100_delay_ms(50) → 读 3B → 是 46 ?
        ├ 是 → 记下降沿 → 发 82 → 等下降沿 → 下一条
        └ 否 → 记下降沿 → 发 82（失败那步照发）→ 锁住 → 等 200ms tick → 重读
```

**路径 B（交互族）**：与 §22 完全一致，一字未改。

```text
写帧 → （测听第①句等 80ms、第②句等 2ms，其余不等）→ 发 82 → 等下降沿 → 下一条
```

**为什么能分成这样**：两族的帧形状**完全一样**（都是「写帧 + 82」），
区别只在中间有没有那一次读 —— 所以必须由**会话族**显式决定，
不能从帧形状或 `rlen` 推断（§23.4 就是这么踩坑的）。
现在 `A7_FAM_READ` / `A7_FAM_NOREAD` 由 `a7_build_session()` 按 kind 置位，
`dsp_7100_cmd_step()` 据此选执行函数。

### 24.4 重试：照读回那套

需求原话是「就像读参数一样，失败就重发」。读回（`dsp_7100_rb_seq_tick`）的失败处理是：

1. 该步的 `82` **照发**（不因为读失败就不发）；
2. 置 `s_rb_retry = 1`，本相位**不再认边沿**，只等 200 ms tick；
3. tick 到了**重读同一相位** —— **不重发命令帧**。

路径 A 原样照抄（`s_cmd_retry_lock` / `s_cmd_retry_cnt`）：

- 第 ② 条的锁**必须**有：`82` 自己带一对边沿，不锁的话重读会被自己的 `82` 自激成风暴
  （读回那边实测退化成 28~30 ms 一次，`hlen=0` 时甚至 ~1 ms 一次）。
- **唯一与读回不同的一处**：读回**不封顶**（开机一次性跑完，卡住也只是慢）；
  命令会话跑在运行期，无限重试会让 `dsp_7100_cmd_busy()` 永远为真 ——
  读回停摆、后续所有设置被忽略。所以加了 `A7_ACK_RETRY_MAX = 3`，超了
  打 `会话判失败` 并 `a7_session_finish(false)`（`ok=0`，不落盘）。

### 24.5 代码改动清单

`code/dsp_7100_cmd.c`：

| 改动 | 说明 |
|---|---|
| 删 `a7_step_t.rlen` / `.wait_rise` | 读不读由会话族定，步表只描述"发什么" |
| 删 `s_sess_noack` → 加 `s_sess_family` | `A7_FAM_READ` / `A7_FAM_NOREAD` |
| 删 `s_cmd_read_gate` / `s_cmd_wait_rise` | 命令层不再有"等上升沿"的门 |
| 加 `s_cmd_retry_lock` / `s_cmd_retry_cnt` | 重读机制 |
| 加 `A7_ACK_WAIT_MS`(50) / `A7_ACK_RETRY_MAX`(3) / `DSP7100_RSP_OK`(0x46) | |
| 删 `DSP7100_RX_LEN` | 路径 A 固定读 `A7_RX_ACK`(3)，这个上限没人用了 |
| `dsp_7100_cmd_step()` 拆成 `a7_step_read()` + `a7_step_noread()` + 一个分派 | |
| 新增 `a7_step_advance()` | 两条路径共用的收尾：记下降沿 → 发 82 → 进下一条 |
| 删 `a7_add_a2_noack()` / `a7_add_simple_noack()` | 它们只做"强制 rlen=0"，现在由族决定 |

`include/dsp_7100_cmd.h`：接口不变，注释按 §24 重写。
⚠ 顺手改正一处**已被推翻的旧注释**：WDRC 那行原写「n=16 从 15.2s 掉到几秒」——
实测那条 gate 等的是写帧上升沿，**从来没生效过**，所以 15.2s 一直成立。

### 24.6 未验证 / 待办

1. **50 ms 够不够，看第一轮日志**：路径 A 每个设值步应打
   `[7100] R (3B ok=1): 46 00 00`。若出现 `65 01 00`（未就绪）就会看到
   `ACK FAIL ... retry 0/3`，把它往上调到 100 ms 再试。
2. **`0x77` 族有边沿、`0xA7` 族没有 —— 为什么？** 这是本次最有价值的新事实，
   但机理未解。可能的解释：7100 只在"有数据等我读"时才拉 DIO13，
   `0x77` 是取数据所以拉，`0xA7` 是配置命令所以不拉。若成立，
   那 §24.1 的"写帧零边沿"是**结构性的**，不是时序问题 —— 值得单独验一次。
3. **DFBC 开关是否真的生效仍未用耳朵确认**（§23.7 遗留 2 原样保留）。
   现在读回来了 `46`，比之前多一层保证，但 `46` 只说明"命令被接下"，
   不等于"参数被应用"。
4. **路径 A 的 50 ms 是阻塞忙等、不喂狗**（同 §22.8 的测听 80 ms）。
   8 条命令 = 8 × 50 ms，每条之间主循环会喂狗，单次最长 50 ms，应当安全；
   但本工程**没有** `Sys_Watchdog_Config()`，用的是默认周期 —— 若日后调短 WDT，这里先炸。
5. **参考实现那 7 条设值命令里有 71 ms 和 98 ms 两条**，50 ms 会命中重试。
   第一轮日志重点看有没有这两条对应的步骤。

> ⚠ **§24.1 的间隔数据有错、§24.6 第 5 条作废，已被 §25.2 取代**（2026-10-08 重算）。
>   §24 其余部分（两条路径、重试语义、零边沿的依据）仍然有效。

## 25. 命令序列按抓包补齐：配置族插入 6 条 0x03 探测（2026-10-08）

### 25.0 结论先行

配置族（降噪 / DFBC / EQ / WDRC）的会话**少发了 6 条 `A7 01 00 03 00 02`**，
帧序与抓包不符 —— 抓包里这 6 条是固定骨架的一部分，不是偶发轮询。
本节按抓包补齐，并顺带纠正 §24 里读错的间隔数据。

| | §24（改前） | §25（改后） |
|---|---|---|
| DFBC 会话 | 8 条 | **14 条** |
| 帧序 | 静音→选程序→选块→写值→提交→解除静音→选回→commit | 这 8 条 **+ 6 条 0x03 探测**，位置照抓包 |
| 每步读长度 | 固定 3B | **按步**：setter 3B、探测 6B |
| DFBC 时长 | ≈ 420ms | ≈ 730ms |
| 探测的应答 | 从没读过 | 读满 6B 打进日志（只发不判，见 §25.4） |

### 25.1 依据：三份抓包同构

| 出处 | 探测位置（TX 编号） |
|---|---|
| DFBC `p0dfbc0-1` | 000 / 009 / 021 / 024 / 033 / 039 |
| 降噪 `7100_A7协议_noise0-1` | 000 / 009 / 021 / 024 / 034 / 040 |
| WDRC `7100_WDRC设置.md §4` | 以文档形式给出完整流程，位置相同 |

三份抓包的 A7 写帧都是 14 条，骨架逐条对得上：

```
预检(0x03) → 静音(25) → 选程序(12) → 等空闲(0x03) → 选块(05) → 写值(04 / 27 / 07)
→ 提交(10) → 等空闲(0x03) ×2 → 解除静音(26) → 选回程序(12) → 0x03 → commit(0C) → 0x03
```

注意那两条**连续的**探测（§24.1 里我当成"busy 轮询"的那对），
是"提交后等 7100 落完块"的一次查询 + 一次复查，位置是固定的。

### 25.2 纠正 §24.1 的间隔数据（本次逐条重算）

§24.1 写的是"设值命令写完 44~50ms 才开始读；busy 轮询那档 94~98ms"。
按 `p0dfbc0-1` **逐条**重算（写帧 → 它自己那条读应答）：

| 写帧 | 类别 | 间隔 |
|---|---|---|
| `A7 01 00 00 00 25` 静音 | setter | 48 ms |
| `A7 02 00 00 00 12 01` 选程序 | setter | **98 ms** |
| `A7 05 … 0A 01 00 00` 选块 | setter | 45 ms |
| `A7 04 … 08 00 00 01` 写值 | setter | **71 ms** |
| `A7 02 00 00 00 10 01` 提交 | setter | 50 ms |
| `A7 01 00 00 00 26` 解除静音 | setter | 46 ms |
| `A7 02 00 00 00 12 01` 选回 | setter | 47 ms |
| `A7 01 00 00 00 0C` commit | setter | 44 ms |
| `A7 01 00 03 00 02` ×6 | 探测 | 250(冷) / 96 / 98 / 97 / 96 / 94 |

**两处更正**：

1. 间隔**不是按命令族分的** —— `A7 02 … 12 01`（选程序）是地道的 setter，却是 98 ms。
   准确说法：setter 8 条中 6 条落在 44~50 ms，另有 71 ms / 98 ms 各一条。
2. §24 与代码注释里的"**取 50 对齐其下沿**"是错的：设值簇是 44~50，50 是**上沿**。

**更要紧的一点**：读→读间隔是 `51, 100, 99, 51, 75, 50, 99, 100, 50, 51, 99, 50, 100`
—— 基本是 **50 ms 台阶**。所以这份抓包量的是**参考 RSL10 自己的读节奏**，
不是 7100 的应答就绪时间。**50 ms 够不够，抓包证明不了**（推测，未验证）。
`A7_ACK_WAIT_MS` 维持 50 不动，靠 §24.4 的重试兜底。

### 25.3 读长度必须按步区分

| 帧类型 | 应答 | 长度 |
|---|---|---|
| setter（addr `0x0000`） | `46 00 00` = 头 + 地址回显，**零数据字节** | 3B |
| 0x03 探测（addr `0x0003`） | `46 03 00 <x> <busy> 1A` | 6B |

读少了会把尾巴留在 7100 里，**下一条读就错位**（同 §21.3 那个"头错位一个字节"的机理：
`00 46 AE` ≠ `46 AE 00` → `hlen` 解错 → 后续全乱）。
所以步表重新带上 `rlen`，`a7_step_read()` 读 `s->rlen` 字节。

### 25.4 忙标志是哪个字节：两份文档冲突（未解决）

| 出处 | 说法 |
|---|---|
| `7100协议/WDRC/7100_WDRC设置.md §4` | `46 03 00 <x> <busy> 1A`，**第 5 字节**是忙标志（`00` 写完 / `01` 仍忙） |
| `7100协议/DFBC/7100_DFBC设置.md §2` | 同一帧读作 `busy=01`，指的是**第 4 字节**（`<x>`） |
| `7100协议/DFBC/7100_DFBC读取.md §3` | 同一帧的**最后字节** `1A`/`4A`，bit6 = DFBC 开 / 关 |

倾向 WDRC 那份（它把 6 个字节全列了出来，编号也和读取文档的 `byte[6]` 对得上），
但**未验证**。本版**只发不判**：6 字节原样进 `[7100] R (6B ok=1): …`，
等上板看主写块期间哪个字节跳 `01` 就能定。

> 另注：`7100_A7协议.md §4` 把地址 `0x0003` 记作"7111_proto：Volume Step Set"，
> 与上面两份 DFBC 文档的说法都不同 —— 疑因那份地址名抄自 **7111** 的寄存器表，
> 未必适用于 7100。**推测，未验证**。

### 25.5 代码改动清单

| 文件 / 位置 | 改动 |
|---|---|
| `dsp_7100_cmd.c` 常量区 | 新增 `A7_RX_PROBE(6)` / `A7_RX_MAX(6)` / `A7_PROBE_ADDR(0x0003)` / `A7_PROBE_VAL(0x02)`；`A7_RX_ACK(3)` 语义改为"setter 长度" |
| `dsp_7100_cmd.c` `A7_ACK_WAIT_MS` 注释 | 按 §25.2 重写（原来那两句错话改掉；**数值不动**） |
| `dsp_7100_cmd.c` `a7_step_t` | 加回 `uint8_t rlen`（新语义：本步读几字节） |
| `dsp_7100_cmd.c` `a7_put()` | 默认 `rlen = A7_RX_ACK` |
| `dsp_7100_cmd.c` 新函数 `a7_add_probe()` | 组 `A7 01 00 03 00 02`，并把该步 `rlen` 改成 6 |
| `dsp_7100_cmd.c` `a7_build_session()` | 配置族骨架插入 6 条探测（① 预检 / ② 选程序后 / ③④ 提交后 / ⑤ 选回后 / ⑥ 收尾） |
| `dsp_7100_cmd.c` `a7_step_read()` | 读缓冲改 `A7_RX_MAX`，实际读 `s->rlen`（原来固定 `sizeof(rx)`） |
| `dsp_7100_cmd.c` 文件头 / 骨架注释 | 帧序改成 14 条版 |
| `include/dsp_7100_cmd.h` | 配置族块：帧序、每步读长度、时长（DFBC 8→14 条、EQ 14→20 条、WDRC (12+2n)） |
| **未改** | `A7_ACK_WAIT_MS` 的值、`A7_ACK_RETRY_MAX`、路径 B 的任何一行、`a7_step_noread()`、poll/tick 状态机 |

包大小核算：探测 6×6B + 骨架 3×6B + 3×7B = 75B；WDRC 16 通道 OL 最坏
16×(10+12) = 352B → 合计 427B < `A7_CMD_BUF_SZ`(640)。
命令条数：16 通道 = 32 + 12 = **44** < `A7_MAX_CMDS`(56)。

### 25.6 未验证 / 待办

1. **忙标志字节仍未定**（§25.4）。第一轮日志重点看探测那 6 条应答里
   `[7100] R (6B ok=1): 46 03 00 ?? ?? 1A` 的 4/5 字节，哪几个在写块期间跳 `01`。
2. **6 字节读会不会错位**：这是本节最大的风险点（§25.3 的机理）。
   若出现 setter 步骤读到 `00 46 xx` 这类头错位的签名，说明读长度仍不对。
3. **50 ms 是否够** —— §24.6 第 1 条仍然有效，且 §25.2 说明抓包证明不了。
4. **探测只发不判**：完全没有利用它的应答（不查忙、不查 DFBC 位）。
   按抓包位置"各发一次"是本次商定的最小改动，busy 驱动的重发留待下一轮。
5. 会话变长：DFBC 420ms → 730ms，静音窗口随之拉长（EQ 20 条 ≈ 1.04s）。
   是否影响听感需实测。
6. **DFBC 是否真的生效仍未用耳朵确认**（§24.6 第 3 条原样保留）。

### 25.7 读时机改「等 DIO13 上升沿」：删掉固定 50ms（2026-10-08 上板两轮）

> ⚠ **本节结论已被 §25.8 推翻，只作历史记录（含下面的代码片段——已不在代码里）。**
> 10:40 上板日志证明本节那版实现是坏的：读相位等的上升沿**一次都没在写帧后出现**，
> 只能靠 200ms tick 踩进来读；而本节那版失败分支**故意不补 82**，事务收不了尾，
> 于是 4 次全零、会话 `ok=0`。真正的原因是**会话第 0 步前面缺一条 82**（见 §25.8）。
>
> ⚠ 后续：§25.8 那版也已回退（见 §25.9），当前代码是 **10:03 那版固定 50ms**。

#### 25.7.0 结论先行

`A7_ACK_WAIT_MS`(50ms) 固定延时**已删**，路径 A 改成**等 DIO13 上升沿再读** ——
与读回 `RB_READ` 同一套。同时 **§24.1 的结论被推翻**：A7 写帧**有**上升沿。

#### 25.7.1 两轮实测

09:38「DFBC 关」(val=0) 与 10:03「DFBC 开」(val=1)，各 14 条命令，
两轮都是 `cmds=14`、`done ok=1`。两次的 6 条 0x03 探测应答**逐字节相同**：

| 探测 | 位置 | 09:38 关 | 10:03 开 | 参考抓包 p0dfbc0-1 |
|---|---|---|---|---|
| ① | 会话最前 | `46 03 00 00 80 1A` | `46 03 00 00 80 1A` | `46 03 00 00 00 1A` |
| ② | 选程序后 | `46 03 00 01 80 1A` | `46 03 00 01 80 1A` | `46 03 00 01 00 1A` |
| ③ | 提交后 | `46 03 00 01 81 1A` | `46 03 00 01 81 1A` | `46 03 00 01 01 1A` |
| ④ | 提交后第 2 条 | `46 03 00 01 80 1A` | `46 03 00 01 80 1A` | `46 03 00 01 00 1A` |
| ⑤ | 选回后 | `46 03 00 00 80 1A` | `46 03 00 00 80 1A` | `46 03 00 00 00 1A` |
| ⑥ | 收尾 | `46 03 00 00 80 1A` | `46 03 00 00 80 1A` | `46 03 00 00 00 1A` |

写帧确实变了（`A7 04 00 00 00 08 00 00 0X`，X 从 0 → 1），**状态位一位没动**。于是：

- **§25.4「忙标志是哪个字节」换了个答案**：第 4 字节跟着**「程序指针是否被选走」**走
  （选程序后 01、选回程序 0 后 00），③ ④ 两条连续探测都是 01 正好证明它不是
  「正在写块」——写块那时早结束了。抓包同列逐条一致（`00 01 01 01 00 00`）。
- 第 5 字节我们恒为 `80`（只有 ③ 提交后那条的 bit0 翻 1），抓包是 `00`（同一个 bit0 同步）。
  多出来的 **bit7 与 DFBC 无关** —— A/B 没动它。
- 末字节恒 `1A`（bit6=0=关）。**但这一轮证明不了写生效**：本次写的就是「关」。
  §25.6 第 6 条仍未解决，验法见 §25.7.7 第 2 条。

#### 25.7.2 关键发现：A7 写帧有上升沿，位置在写帧后 47~60ms

`app.c` 的 `[IO] D9=.. D10=.. D13=..` 一行**只在电平变化时打印**，是真实的边沿时间戳。
10:03 日志里它逐条落在写帧后 47~60ms、**在我们发起读之前**：

```text
30.803  W   A7 01 00 03 00 02
30.850  [IO] D9=0 D10=1 D13=1              ← 写帧 +47ms，沿先到
30.863  R   (6B ok=1): 46 03 00 00 80 1A   ← 之后才读，拿到 46
30.863  W   82
30.863  [7100-irq] rise +0/298 fall +1/298 ← 82 之后落
```

14 条命令**14 条都有沿**，且相关性全对：**读在沿后 → 全 `46`**（13 条）；
**读在沿前 → 只有 09:38 首条，拿到 `65 01 00`**（读 +49ms、沿 +60ms）。

**反证**（排除「沿是我们的读产生的」）：09:38 那一步的读在 +49ms 就**已完成且失败**，
沿却在 +60ms 才来 —— 沿不可能由它产生。

**固定 50ms 正好压在这个窗口的下沿**，等于每步掷一次骰子 —— 这就是 §24.6 第 1 条
「50ms 是不是够」的答案：不够；09:38 的失败与 10:03 的成功只差 11ms。

#### 25.7.3 与 §24.1 的矛盾（未解决，但风险已封顶）

§24.1（09:01 那次）的结论相反：8 步写帧后等上升沿**全部**走到 200ms tick 兜底，
由此判定「`0x77` 族有上升沿、`0xA7` 族没有」。本次 10:03 的 `[IO]` 电平行与之冲突。

方法论上的教训：那次的判据是 `[7100-irq] rise +1/..`，而这一行打在 `W 82` **之后** ——
**打印位置在 82 之后 ≠ 边沿发生在 82 之后**，计数器报的是累计值，早到的沿会被算到 82 头上。
本节改用 `[IO]` 的**电平变化时间戳**，不受此影响。

**风险已封顶**：万一沿真的不来（例如 DIO13 中断没 arm），`dsp_7100_dio13_rise_cnt()`
恒为 0 ⇒ 门永远等不到 ⇒ 自动退回 200ms tick；那时读到的仍是 `46`（7100 早已就绪），
**结果正确、只是退化成 ~200ms/条**。这与读回路径的退化方式完全一致
（`dsp_7100_init.h:67` 注释写明的行为）。所以最坏情况是慢，不是错。

#### 25.7.4 tick 会踩进等沿窗口 —— 必须留 `s_cmd_rise_seen`

tick 是**每 200ms 周期**触发的（会话期间 `app_process.c` 每 tick 调一次
`dsp_7100_cmd_tick()`），等沿窗口约 50ms ⇒ **约 1/3 的步会被 tick 踩到**，踩到就提前读。
若照普通失败处理（补发 82 + 计入重试 + 锁到 tick 节奏），每被踩一次白花 200ms，
平均一步会从实测 ~67ms 涨到 ~115ms —— 比原来的固定延时还慢。

所以加一位 `s_cmd_rise_seen`：**没等到沿就读**判为「太早」，**不补 82、不锁边沿**，
直接退回等沿相位（只赔掉一次读的时间）。这一支**绝对不能补 82** —— 82 自己就产生
一个上升沿，补了它，`s_cmd_wait_rise` 立刻变成「旧的」，下一轮 poll 会把这个沿当成
新的应答沿立刻再读 → 自激成风暴（读回 `dsp_7100_init.c:430` 踩过同一个坑，
实测退化成 28~30ms 一次、hlen=0 时甚至 ~1ms 一次）。

#### 25.7.5 时长：实测 67ms/条（原注释写 52ms）

09:38 那次 12.813 → 13.864 = **1051ms**（含一次重试）；10:03 那次 30.773 → 31.743
= **970ms**，逐条间隔 60~70ms。所以：

| 会话 | 条数 | 时长 |
|---|---|---|
| DFBC | 14 | ≈ 970ms（原注释写 730ms） |
| EQ | 20 | ≈ 1.4s |
| WDRC n=1 | 14 | ≈ 0.97s |
| WDRC n=16 | 44 | ≈ 3.0s（原注释写 2.3s） |

头文件里 52ms/条那几处已按实测改成 67ms。

#### 25.7.6 代码改动清单（`dsp_7100_cmd.c` / `dsp_7100_cmd.h`）

1. 删 `A7_ACK_WAIT_MS`。路径 A 的 SEND 分支不再阻塞 `i2c_7100_delay_ms`，改为
   **发写帧之前**记 `s_cmd_wait_rise = dsp_7100_dio13_rise_cnt()`
   （记在动作之前，同读回 `RB_SEND` 的做法）。
2. 新增状态 `s_cmd_wait_rise` / `s_cmd_rise_seen`；`a7_step_advance()` 与
   `a7_session_start()` 各复位一次 `s_cmd_rise_seen`。
3. `dsp_7100_cmd_poll()` 的 READ 相位新增第三条门：
   `s_sess_family == A7_FAM_READ && !s_cmd_rise_seen` → 等 `rise_cnt != s_cmd_wait_rise`；
   放行时记 `s_cmd_rise_seen = (沿到了) ? 1 : 0`。路径 B（A2 无沿）不进这个门。
4. 读失败分流：`!s_cmd_rise_seen` → 太早（退回等沿，不补 82）；
   `s_cmd_rise_seen` → 真失败（补 82 + 锁 tick，原逻辑不变）。
   两条都计入 `s_cmd_retry_cnt`，本步读次数仍封顶 3 + 1 次。
5. `wlen == 0` 的只读步：没有写帧就没有可等的沿 → 直接置 `s_cmd_rise_seen = 1`，
   不让它挂到 200ms tick。（当前没有这种步，但 `a7_step_t` 注释里写了
   `wlen = 0 = 不写`，顺手补齐。）
6. 注释更正：删掉所有「A7 写帧零边沿 / 只能等固定时间」的表述（`.c` 文件头、
   `a7_step_t` 块、`dsp_7100_cmd_poll`、纯音那段；`.h` 三处），改成
   「有沿，写帧后 47~60ms，等它」。

#### 25.7.7 未验证 / 待办

1. **上升沿到底来不来 —— 与 §24.1 直接冲突，需一轮确认**（见 §25.7.3）。
   上板看下一次设置的日志：若每步都出现 `未等到上升沿就读（tick 踩进来）`，
   说明沿没来、退化到 ~200ms/条（**结果仍对**）；若这条不再出现、且首条不再
   `65 01 00`，则本节成立。
2. **写是否真的生效仍未验**（§25.6 第 6 条）。硬验法：发一次**清缓存** → 复位 →
   看开机读回 `[RB] P1 降噪 en=.. | DFBC=on/off`（`dsp_7100_init.c:311` 取 DFBC 块
   `payload[0] bit7`）。注意**缓存命中时读回不跑**（`dsp_7100_init.c:351`），
   而本次会话刚落了盘，所以必须先清缓存。
3. 会话变长的听感影响（§25.6 第 5 条）：DFBC 970ms、WDRC n=16 ≈ 3.0s 全程静音。
4. 路径 B（A2 族）仍不读应答。现在知道 A7 有沿；A2 有没有沿只有 §24.1 的旧测量，
   而那次的方法本身已被质疑（§25.7.3），值得重测。

---

### 25.8 读只认 DIO13 电平 + 会话头补一条 `04 82` + 整块重来（2026-10-08）

> ⚠ **本节已回退（2026-10-08，见 §25.9）：代码回到 10:03 那版固定 50ms。**
> 本节只作历史记录 —— 那三条改动（会话头 82 / 读只认电平 / 整块重来）和
> `dsp_7100_dio13_level()` 都已从代码里撤掉，**勿据此实现**。

#### 25.8.0 结论先行

- **读的时机只认电平，不设任何 ms 延时**：写完帧后等 DIO13 = **高**再读；低就继续等，
  200ms tick 只做兜底（等不到电平就由 tick 放行）。读回 `RB_READ` 本来就是这套。
- **配置族会话（路径 A）开头单独补一条 `04 82`**。这条 82 **A7 抓包里没有，是我们自己加的**，
  不算协议帧。没有它，第 0 步就是「没有前序 82 的裸写帧」——7100 完全不响应。
- **读不到 46 就整块重来**（82 → 写帧 → 等电平 → 读），最多 3 次尝试；不再「只重读」、
  也不再有「读早了不补 82」那一支。
- 路径 B（切程序 / 音量 / 测听 / 纯音 / 静音）**不读、也不补这条 82**（用户明确）。

#### 25.8.1 10:40 那次失败的日志（`ok=0`）

```
[10:40:57.694] [7100] --- session start: DFBC prog=1 val=0 db=0 cmds=14 ---
[10:40:57.694] [7100] W (6B ok=1): A7 01 00 03 00 02      ← 第 0 步，前面一条 82 都没有
[10:40:57.783] [7100] R (6B ok=1): 00 00 00 00 00 00      ← 写后 +89ms
[10:40:57.794] step0/14 未等到上升沿就读（tick 踩进来），退回等沿 1/3
[10:40:57.983] [7100] R (6B ok=1): 00 00 00 00 00 00      ← 之后每 +200ms 一次，共 4 次全零
[10:40:58.183] [7100] R (6B ok=1): 00 00 00 00 00 00
[10:40:58.392] [7100] R (6B ok=1): 00 00 00 00 00 00
[10:40:58.393] step0/14 ACK FAIL hdr=00 00 00 (retry 3/3)
[10:40:58.393] [7100] W (1B ok=1): 82
[10:40:58.393] [7100-irq] DIO13 rise +1/51 fall +1/50   ← 82 一发完，rise/fall 同时到齐
[10:40:58.403] step0/14 读 3 次仍无 46，会话判失败
[10:40:58.403] --- session done ok=0 ---
```

判读四条：

1. **全程一条 `[IO] D13=` 都没有** —— `app.c:118` 那行只在电平变化时打印，没有它 =
   读之前电平**一次都没变过**。所以「等上升沿」等不到不是偶然。
2. **82 之后 `rise +1` 与 `fall +1` 同时到齐**（同一个 poll 内）—— 印证 §23.6：
   这对沿是 **82 自己的回声**，跟写帧无关。
3. 读到的是 **`00 00 00`（全零），不是 `65 01 00`** —— 按 §23.5 的签名判别，全零是
   **「漏发 82」**（7100 完全不响应），不是「未就绪」。
4. 失败分支**故意不补 82**（§25.7.4 那版设计），于是自愈手段被自己掐掉，
   4 次读全部零，3 次重试后判失败。

#### 25.8.2 与 10:03 那轮的对照：两个条件同时满足才读得到

| 轮次 | 读之前电平 | 读之前有没有前序 82 | 读回 |
|------|-----------|-------------------|------|
| 10:03（旧固件，固定 50ms） | 高（`[IO] D9=0 D10=1 D13=1`，写后 +47ms） | **有**（上一条的收尾 82） | `46 …` |
| 10:40（§25.7 那版） | 低（全程无 `[IO]` 行） | **没有**（会话第 0 步） | `00 00 00` ×4 |

所以 **10:03 能读、10:40 读不到**，差的是**前序 82**：82 把事务开起来，之后 7100 才把
DIO13 拉高（10:03 实测 +47ms）、应答才可读。§24.1/§23.6 那条「A7 写帧零边沿」很可能
就是在**同样缺前序 82** 的状态下测的 —— 静态的「写帧有没有沿」没有意义，**得看事务有没有开**。

结论：**会话头补一条 82（把第 0 步的事务开起来）+ 读只认电平**，两件事一起做才对。
电平给时机（快，≈47ms/条），200ms tick 给兜底（电平不来时仍然读得到，因为 82 已经把
事务开起来了）。

#### 25.8.3 代码改动清单（2026-10-08）

`code/dsp_7100_cmd.c`
- `a7_session_start()`：**路径 A 会话在第一条命令前补发一条 `04 82`**
  （记下降沿 → `dsp_7100_send_end()` → `s_cmd_send_gate = 1`）；路径 B 不补。
  `session start` 那行日志挪到发 82 **之前**，方便对日志；82 发不出去会多打一行
  `会话头 82 发送失败，会话未启动`。
- 删掉 `s_cmd_wait_rise` / `s_cmd_rise_seen` / `s_cmd_retry_lock` 三个状态位与
  「未等到上升沿就读」那一整支分支。
- `dsp_7100_cmd_poll()`：读相位的门从「等上升沿计数」改成 **`dsp_7100_dio13_level() == 0` 就等**
  （电平高了立刻读；tick 放行兜底）。
- `a7_step_read()` 失败分支改成：打 `ACK FAIL` → 记下降沿 + 补 82 → `s_cmd_retry_cnt++`
  → 到 `A7_ACK_RETRY_MAX(3)` 判失败，否则回 `CMD_ST_SEND` 整块重来。

`code/dsp_7100_init.c` / `include/dsp_7100_init.h`
- 新增 `dsp_7100_dio13_level()`：`DIO_DATA->ALIAS[13]`（pad 实数，不是输出锁存）。
  中断没开那支返回 0 ⇒ 读相位只能靠 200ms tick 兜底，与既有降级约定一致。

#### 25.8.4 未验证 / 待办

1. **本轮改动尚未上板实测**。要看的三件事：① 会话头是不是 `W (1B): 82` 打头；
   ② 每步 `R` 之前有没有 `[IO] D13=1`（有 = 电平快路径生效，会话约 0.9s；
   没有 = 每条退到 200ms，会话约 2.8s，但**仍然读得到**）；
   ③ `R` 是不是清一色 `46 …`，有没有 `65 01 00` / `00 00 00`。
2. **重试语义变了**：现在是「最多 3 次**整块**尝试」（旧版是 1 次 + 3 次重读）。
   最坏耗时 3×200ms = 600ms。
3. **写是否真的生效仍未验**（§25.6 第 6 条）—— 硬验法同 §25.7.7：清缓存 → 复位 →
   看开机读回 `[RB] P1 … | DFBC=on/off`。
4. 会话变长的听感影响：电平快路径下 DFBC ≈ 0.9s、WDRC n=16 ≈ 2.2s（全程静音）；
   退到 tick 则 8.8s。待实测确认走的是哪一档。
5. `DSP7100_DIO13_IRQ_ENABLE` 若被关掉，`dsp_7100_dio13_level()` 恒 0 ⇒ 每步 200ms，
   且 `dsp_7100_send_end()` 后的下降沿门也要靠 tick。**别关**（§21 的硬约束同源）。

#### 25.8.5 上板记录：11:04（本节那版，好使）

```
[7100] --- session start: DFBC prog=1 val=0 db=0 cmds=14 ---
[7100] W (1B ok=1): 82                       ← 会话头那条，我们自己加的
[7100-irq] DIO13 rise +1/46 fall +1/45       ← 82 自己的一对沿，同时到齐
[7100] W (6B ok=1): A7 01 00 03 00 02        ← 第 0 步写帧
[7100] R (6B ok=1): 46 03 00 00 80 1A        ← 与抓包第一条探测逐字节一致
[7100] W (1B ok=1): 82
[7100-irq] DIO13 rise +1/47 fall +1/46
```

DFBC 会话（14 条）在这版上跑通、读回全 `46`（用户确认「设置 DFBC 开关都正常回复」）。
**这一版是当前的代码状态**：会话头 82 + 读只认电平 + 整块重来。

#### 25.8.6 已回退的实验：读改等上升沿（2026-10-08 11:19，不可用）

11:19 试过把读的门从「等电平高」换成「等写帧之后的新上升沿」（发写帧前记
`dsp_7100_dio13_rise_cnt()`，照读回 `RB_READ`），上板一次后**回退**，原因：

```
47.653  W 82                       ← 会话头
47.653  [7100-irq] rise +1/46 fall +1/45   ← 82 自己那一对，<1ms 成对到齐
47.673  W A7 01 00 03 00 02
47.833  R 46 03 00 00 80 1A        ← 写帧后 160ms
47.833  W 82 → rise +1/47 fall +1/46
```

- **写帧之后 160ms 内一条 `[7100-irq]` 都没有** ⇒ DIO13 一个沿都没发生。旁证：47.833
  那次打印只多出 `+1 rise / +1 fall`，正好是那条 82 自己那一对；若写帧也贡献了沿，
  这里就该是 `+2`。
- 所以「等写帧之后的上升沿」实际等于「等 200ms tick」——读虽然仍拿到 `46`（7100 备好了），
  但节奏从 ≈30ms/条 掉到 ≈200ms/条。用户判「越改越不是我想要的」，回退到 25.8 那版。
- 遗留问题：**写帧之后到底有没有沿、沿的极性是什么，仍未量准**。现有日志看不出来
  （`[7100-irq]` 打的是累计计数，打印点又在 82 之后）。要量的话只剩两条路：
  ① 给 `print_i2c()` 的 W/R 行加 `lvl=`（只给电平，零侵入）；② 在 DIO13 中断的
  上升/下降两个分支各打一行（日志时间戳能读出**顺序**=极性，代价是 ISR 里打印会
  拖慢几百 µs~ms）。**用户尚未选。**

---

### 25.9 回退：回到 10:03 那版「固定 50ms」—— 当前基线（2026-10-08）

> 本节描述**当前代码状态**。§25.7（等上升沿）与 §25.8（读只认电平 + 会话头 82 +
> 整块重来）两次改动**都已撤掉**，那两节只作历史记录，**勿据此实现**。

#### 25.9.0 结论先行

- 用户判定：10:03 那版**起码能正确通讯**（DFBC 14 条全部拿到有效应答、`ok=1`），
  先把它定为**基线**，时序优化放到以后再做。
- 回退只涉及四个文件，**逐字节**恢复成 10:03 时的内容 —— 不是凭记忆重写，
  依据与校验见 §25.9.2。
- 撤掉三件东西：会话头那条单独加的 `04 82`、`dsp_7100_dio13_level()` 接口、
  「读相位等电平 / 等沿」的门。

#### 25.9.1 基线版的机制（与 §25.7 / §25.8 的差别）

| 环节 | 基线（当前） | §25.7 / §25.8（已撤） |
|------|-------------|----------------------|
| 一块 | 写帧 → **阻塞等 `A7_ACK_WAIT_MS = 50` ms** → 读 rlen B → 校验 46 → `04 82` | 写帧 → 等电平（§25.8）/ 等沿（§25.7）→ 读 |
| 会话头 | **没有**那条 `04 82` | 有（§25.8 加的） |
| 读失败 | 补 82 后**只重读**（不重发写帧），本相位只等 200ms tick；最多 `A7_ACK_RETRY_MAX = 3` 次 | 整块重来（§25.8）/ 失败不补 82（§25.7） |
| 辅助接口 | 无 `dsp_7100_dio13_level()` | 有（§25.8 加的） |
| 注释立场 | 「A7 写帧**没有** DIO13 回铃」（§24.1） | 「写帧后有上升沿」（§25.7.2） |

读相位**没有门**：路径 A 的 SEND 分支写完帧时已经阻塞等满 50ms，进读相位直接读；
只有**重读**那一支（`s_cmd_retry_lock`）才停下来等 200ms tick（同读回 `s_rb_retry`）。
发相位仍等上一条 82 的**下降沿**（`s_cmd_send_gate`，实测 0~2ms）。

#### 25.9.2 恢复方法（可复现）

四个文件的内容从**会话记录**重建 —— `C:\Users\ViewSSS\.claude\projects\d--projects-onsemi-workspace\33b424ca-….jsonl`，
两条独立路径互相印证：

| 文件 | 依据 | md5 |
|------|------|-----|
| `code/dsp_7100_cmd.c` | 10:23:12 的**整文件 Read** ＋ 倒放 10:03 之后的 49 条 Edit，两条路径字节一致 | `567ffaf9b6e9f749a84f78bdb7385155` |
| `include/dsp_7100_cmd.h` | 编辑前快照 `originalFile` | `a41928c9c4a5ed8c9f789291e50ee94c` |
| `code/dsp_7100_init.c` | 倒放 10:03 之后的 6 条 Edit ＋ 用当时的 Read 片段校验 | `ee6db0716a72fa99f8e3174913b24bea` |
| `include/dsp_7100_init.h` | 编辑前快照 `originalFile` | `cb5ac25316e3ea58285d5b4ba8a03dc6` |

覆盖前的四个文件（= §25.8 那版）备份在 `d:/tmp/restore_1003/before_restore/`。
恢复后校验：行尾全部 `w/lf`、末字节换行保留、大括号平衡（cmd.c 147/147）、
全工程无 `dsp_7100_dio13_level` 引用（撤掉接口不会编译报错）。
存档里 10:03 之后只动过这四个文件与本文档，其他源码没有变化。

#### 25.9.3 上板记录：09:38 那轮（基线版的完整一轮）

```
[09:38:12.833] [7100] W (6B ok=1): A7 01 00 03 00 02     ← 第 0 步，前面没有任何 82
[09:38:12.882] [7100] R (6B ok=1): 65 01 00 28 00 00     ← 写后 +49ms，读到「未就绪」
[09:38:12.883] [7100] step0/14 ACK FAIL hdr=65 01 00 (retry 0/3)
[09:38:12.893] [7100] W (1B ok=1): 82                    ← 失败分支补 82（这版会补）
[09:38:12.893] [7100-irq] DIO13 rise +1/59 fall +1/58
[09:38:12.982] [7100] R (6B ok=1): 46 03 00 00 80 1A     ← 补 82 后重读 → 46，与抓包一致
[09:38:12.983] [7100] W (1B ok=1): 82
[09:38:12.983] [7100-irq] DIO13 rise +0/59 fall +1/59
[09:38:12.983] [IO] D9=0 D10=1 D13=0                     ← 82 收尾，电平拉低
[09:38:12.983] [7100] W (6B ok=1): A7 01 00 00 00 25     ← 第 1 步（静音）
[09:38:13.041] [IO] D9=0 D10=1 D13=1                     ← 写后 +58ms，电平拉高
[09:38:13.043] [7100] R (3B ok=1): 46 00 00              ← 读落在电平拉高之后 +2ms
```

要点：

1. 第 0 步读到 `65 01 00` 是**固定 50ms 太短**，不是这版的机制错：§25.7.2 实测应答在写帧后
   44~60ms 才到，50ms 正好压在下沿。重读那支**补 82 后立刻拿到 46**，所以能自愈。
2. 后面每步 `R` 都紧跟在 `[IO] D13=1` 之后约 2ms —— 不是「等电平」，而是 50ms 延时与该
   电平上升（实测 +47~58ms）**碰巧同相**。这既解释了为什么这版看起来对，也说明
   「改成等电平/等沿」理论上能省下这 50ms，但要先量准（见 §25.9.4 第 3 条）。
3. 这轮 14 条全部拿到有效应答、`ok=1`；10:03 那轮同形（`val=1`，同样 `ok=1`）。

#### 25.9.4 未验证 / 待办（时序优化前的检查单）

1. **本轮回退未编译、未上板**。上板要对照：日志与 §25.9.3 同形 —— 每条 `R` 落在写帧后
   约 50ms、`ok=1`、6 条 0x03 探测拿到 `46 03 00 …`；第 0 步可能先来一条 `65 01 00`，
   由重读救回。
2. **DFBC 写是否真的生效仍未验**（§25.6 第 6 条）：清缓存 → 复位 → 看开机读回
   `[RB] P1 … | DFBC=on/off`。缓存命中时读回不跑（`dsp_7100_init.c:351`），必须先清缓存。
3. **时序优化的第一步是量准，不是改**：50ms 是猜的窗口。要换成沿/电平驱动，得先知道
   DIO13 的极性与写帧之后到底有没有沿 —— §25.8.6 末尾那两条诊断路（`lvl=` 字段 /
   ISR 分支打印）**用户尚未选**。
4. 第 0 步没有前序 82 时，这版读到的是 `65 01 00`（未就绪），**不是** §25.8.2 判的
   `00 00 00`（漏发 82）。两种读法的差异原因未定，留给时序优化那轮一起量。
5. 路径 B（A2 族）仍不读应答；A2 有没有沿只有 §24.1 的旧测量，方法已被质疑（§25.7.3）。


