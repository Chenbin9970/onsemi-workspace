# remote_mic_rx_coex_1664 开发文档

## 1. 工程概述

`remote_mic_rx_coex_1664` 由 `remote_mic_rx_coex_1654` 复制而来，是 RSL10 远端麦克风接收机
（RM receiver，BLE + RM 共存）的 1664 机型分支。音频出口继承 1654 的 OD 直驱方案，但因 7100 I2C
占用 DIO0/DIO1，**已改为 PCM 从机输出**（`OUTPUT_INTRF = PCM_SLAVE_OUTPUT`）：7100 做时钟主机
提供 BCLK/FS，RSL10 从机在 SERO 移位输出，见 §3.4 与 §6。

与 1654 的差异（见 §3）：设备名改 `Smart1664`、**删除按键**、**删除电池 AD 采样**、
**打印口由 DIO5 改到 DIO12**、**音频输出改为 PCM 从机**。

> **7100 移植状态**：**阶段一（通讯层）已完成** —— Ezairo 7100 I2C 协议已移植进来并**整体取代了
> 原 BS300 子系统**（BS300 文件已删）。含上电握手、106 步引导、4 程序读回、flash 缓存、5s 心跳。
> **阶段二（写路径 + Rempro 验配命令重映射）进行中**：写会话状态机、写后回写缓存、
> 降噪 / DFBC / EQ / **WDRC（增益/MPO/HighLevel）** 及其读回、**纯音测听（CMD 40/13/14/21）** 均已实现。
> **纯音测听已上板验证通过**（App 侧尚在完善）；**WDRC 与 EQ 尚未上板**，详见 §7.4.3 / §7.4.4 与 §17。
> 引脚按「音频输出关闭 + I2C 用 DIO0/DIO1」定案（见 §5）。

参考工程：`remote_mic_rx_coex_1654`（本工程直接来源）、`peripheral_server_sleep`（OD 输出路径
+ BS300 通讯来源）、`remote_mic_rx_coex`（7100 I2C 协议来源）。

## 2. 来源与 git 基线

| commit | 说明 |
|--------|------|
| `d9ddf9b` | 1664 工程基线：由 1654 复制（61 文件），设备名改 Smart1664/1664FOTA |
| `0ad0541` | 删除按键 / 删除 AD 采样 / 打印口改 DIO12 |
| `95896c1` | 移植 7100 通讯层取代 BS300 + 读回参数 flash 缓存 |
| `b6365f2` | Rempro 切模式 / 调音量接 7100 运行时命令 |
| （未提交） | 降噪 / DFBC 写入（tick 模型）+ GetBatteryInfo 回 100% + I2C 收发日志 |
| （未提交） | **音频出口改 PCM 从机**（DIO2/3/4/14，24k），见 §3.4 / §6 |

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
1664 无 ADC 采样，现**固定回 `100/100`（flag=0）** —— 回 `flag=1` 会导致 App 连不上。

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

### 3.4 音频输出改为 PCM 从机（DIO0/DIO1 留给 7100 I2C）

1664 的 OD 直驱占用 **DIO0(OD_P) / DIO1(OD_N)**，与 7100 I2C（DIO0=SCL / DIO1=SDA）冲突。
定案：**音频出口改走 PCM 从机**（参照 `peripheral_server_sleep7160test` 已验证实现）——
7100 做时钟主机提供 BCLK/FS，RSL10 只在 SERO 移位输出，因此**用不到 DIO0/DIO1**，I2C 独占之。

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
ch4/ch5 长度为 `PCM_FRAME_WORDS(120)`、**DIO0/DIO1 上无任何配置**（I2C 独占）。

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
| code/dsp_7100_cmd.c / include/dsp_7100_cmd.h | 运行时命令：切程序 / 音量（同步）+ 降噪 / DFBC / EQ / WDRC（tick 异步），见 §7.4 |
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

#### 7.4.1 切程序 / 调音量（同步阻塞）

```
写帧  A2 00 <reg> <val>        reg: 0x16=程序, 0x12=音量
读回  43 03 00 00 <reg> <val>  （从机确认）
结束  82
```

| 命令 | 序列 | 参数 |
|------|------|------|
| `dsp_7100_set_volume(L)` | 写 →2ms→ 读6B →1ms→ `82` | L = 1..6，值 `{0x11,0x21,0x32,0x43,0x53,0x64}` |
| `dsp_7100_switch_program(P)` | 写 →80ms→ 读6B →1ms→ `82` →1ms→ 读6B →1ms→ `82` | P = 1..4 |

阻塞调用（含 ms 级延时），在 Rempro 命令处理（主循环上下文）执行。

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
| 出纯音 | `A7 07 00 00 00 2E 01 <freq16-BE> <level24-BE>` | 单写帧 |
| 停音 | `A7 07 00 00 00 2E 00 00 00 00 00 00` | 单写帧 |
| 静音/解静音 | `A7 01 00 00 00 25` / `26` | 单写帧 |
| 测听模式寄存器 | `A2 00 2E <00/58>` | `A2` 三帧序列 |

电平 = `round(4.39902 × 10^((db + C(freq))/20))`，`C(f)` 逐频率整数修正。
**已标定 6 个频点**：500/1000/2000/3000/4000/6000 Hz（其余 11 个返回 false → App 收 `Flag=1`）。

**BLE → 7100 流程**

```
CMD 40 {DevType, 0} 进入测听     ← 先回应答，再做 I2C
   ├ A2 00 16 03              切程序 3（dsp_7100_switch_program）
   ├ A2 00 2E 00              测听模式开（dsp_7100_set_tone_mode(true)）
   ├ A7 01 00 00 00 26        解除静音（dsp_7100_set_mute(false)）
   └ 延 1s → 推 SYS_ID=1 CMD 6 {DevType, Initial_Status=2}

CMD 13 {DevType, Spectrum, Decibel}      出音（Spectrum 0-16 → 250…8000 Hz，dB 20-100）
CMD 14 {DevType}                         停音

CMD 40 {DevType, 1} 退出测听
   ├ A2 00 16 <原程序>          切回进入前的程序
   ├ A2 00 2E 58              测听模式关
   ├ A7 01 00 00 00 26        解除静音
   └ 延 1s → 推 SYS_ID=1 CMD 6 {DevType, Initial_Status=1}

CMD 21 {DevType, Mute} 静音开关   ← ⚠ 方向与 CMD 3 相反：Mute 非 0 = 静音
CMD  3 {DevType, OnOff} 开关机    ← OnOff 非 0 = 开机 → 解除静音；0 = 关机 → 静音
```

**CMD 3 开关机**（2026-09-17 补实现）：请求 `{Device_Type, Device_OnOff}`，按接口文档 `Device_OnOff` 的
`0: Off 1: On` —— **非 0 = 开机**。7100 侧没有独立的开关机指令，按 1654 的做法（`bs300_active`/`bs300_mute`）
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
| `CMD_GETBATTERYINFO` (4) | `cmd_getbatteryinfo_7100` | **固定回 100/100**（回 flag=1 会导致 App 连不上） |

`GetDeviceConfig` 改为 7100 取值：**Program_Num=4、Chip_Type=6 (E7160SL)、Volume_Number=5**（原 3 / 1 / 9）。

> 其余 4 个 Rempro 命令（SetCompressRatio (8) / GetCurrentScene (15) /
> GetFeedbackOnOff (34) / GetFittingData (17)）仍回 `flag=1`。

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
- `accessword = 0x00cde629 | (0xf2<<24)` = `0xf2cde629`（rm_app.c）
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
6. 手机扫描应看到 **Smart1664** 广播；Rempro `GetBatteryInfo` 回 **100%**。
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
  公司厂商段 18B 用 sleep 的 `APP_COMPANY_ID_DATA`，把「耳侧 + 设备 MAC」编入 company data。
  因名字 Smart1664 为 9 字符、ADV 放不下 18B 厂商段，MAC/耳侧数据改放 **scan response**。
- **bdaddr 修正**（code/ble_std.c）：PUBLIC 分支读到公共地址后不再被 `PRIVATE_BDADDR` 覆盖，
  读不到才回退 `co_default_bdaddr`。

## 15. Rempro 服务（手机验配）

**GATT 服务：只保留 Rempro**（Battery / Custom 已移除）

- `SERVICE_ADD_FUNCTION_LIST` 只剩 `RemproService_ServiceAdd`；`SERVICE_ENABLE_FUNCTION_LIST` 为 NULL。
- Rempro 的 ATT 读写路由复用 `ble_custom.c` 的 GATTC 处理器（按 `rempro_env.start_hdl` 区间分流）。
- **1664 起电池相关已全部移除**（Battery 不注册、无 ADC 采样）；
  Rempro `GetBatteryInfo` 固定回 100%（回不支持会让 App 连不上）。

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
   | SetVolume (2) / SetCurrentScene (16) | **已完成 + 已上板** — §7.4.1 |
   | SetDenoise (9) / SetFeedbackOnOff (5) | **已完成 + 已上板** — §7.4.2 |
   | SetEqualizer (10) | **已完成，未上板** — §7.4.2（映射到 WDRC LL/HL） |
   | SetAudiometryStatus (40) / SetPlayVoice (13) / SetStopVoice (14) / SetMuteData (21) | **已完成 + 已上板** — §7.4.4（App 侧完善中）|
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
