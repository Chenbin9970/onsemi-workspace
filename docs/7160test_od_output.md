# 7160test — OD 输出实现文档

> 工程：`peripheral_server_sleep7160test`
> 目标：调试 **RSL10 内部输出驱动（OD）单端输出**，**DIO12 做 OD_P**（参照 `peripheral_server_sleep` 的 OD 路径移植）
> 状态：**2026-08-28 完成，实机验证正常**；与 PCM 输出互斥（编译期宏切换，均验证通过）
> 前提：RM 接收 + G722 解码链路已通（与 PCM 模式共用）。

## 0. 输出规格

| 参数 | 值 |
|------|-----|
| 输出 | RSL10 内部输出驱动（OD），sigma-delta |
| 引脚 | **DIO12 = OD_P（单端，OD_N 不接）** |
| 采样钟 | **内部 DMIC/OD 时钟**（`AUDIOSINK_CLK_SRC_DMIC_OD`，不依赖外部引脚） |
| ASRC | `DEC_MODE1` + `<<29` |
| 采样率 | 由内部 OD 时钟决定，ASRC 动态锁速 |

## 1. 数据流

> RM 接收 → G722 解码(16k) → `Dsp2CmBuff0dec` → DMA ch3 → **ASRC(DEC_MODE1)** → ch4 → `BufferOut`(CIRC) → ch5(OD DMA) → `AUDIO->OD_DATA` → **DIO12**

- ch4：`ASRC->OUT` → `BufferOut`（`RX_DMA_ASRC_OUT`，DEST32/SRC16/**CIRC**，无完成中断，连续填充）
- ch5：`BufferOut` → `AUDIO->OD_DATA`（`RX_DMA_OD`，16-bit/**CIRC**，`DMA_ENABLE` 随配置即启动）

## 2. 与 PCM 互斥（编译期宏）

`app.h` 里两个宏控制：

| 宏 | OD 模式 | PCM 模式 |
|----|---------|----------|
| `OD_DIO12_OUTPUT` | **开** | 关 |
| `DEBUG_UART_ENABLE` | 关（DIO12 让给 OD_P） | **开** |
| 音频输出 | DIO12（OD_P） | DIO14（PCM SERO） |
| DIO12 用途 | OD_P | UART 打印口 |
| 采样钟 | 内部 DMIC/OD | DIO3 12k FS |
| ASRC | `DEC_MODE1` + `<<29` | `DEC_MODE2` + `<<28` |
| ASRC OUT DMA | CIRC 无中断 → BufferOut | LIN 完成中断 → pcm_tx_buf |

## 3. 引脚冲突与处理

| DIO | 原用途 | OD 模式用途 | 处理 |
|-----|--------|-------------|------|
| 12 | UART 打印口(TX) | **OD_P** | 打印关闭（`DEBUG_UART_ENABLE` 关 / `OUTPUT_INTERFACE=OUTPUT_DISABLED`） |
| 0 / 1 | I2C SCL/SDA | 不变 | — |
| 2 / 3 | PCM BCLK/FS | 空闲（PCM 关） | — |
| 4 | PCM SERI | 空闲 | — |
| 14 | PCM SERO | 空闲（PCM 关） | — |
| 7 | 恢复按钮 | 不变 | — |

## 4. 改动文件

### 4.1 `include/app.h`

1. `//#define OD_DIO12_OUTPUT`（测 OD 取消注释）——打开后走 OD 分支、关闭 PCM。
2. `DEBUG_UART_ENABLE`（OD 模式注释掉）——关闭打印。
3. `SAMPLING_CLK_SRC`：OD=`AUDIOSINK_CLK_SRC_DMIC_OD`；PCM=`SAMPL_CLK<<…`（DIO3 FS）。
4. `AUDIO_CONFIG`：OD 分支加 `OD_ENABLE`。
5. `OD_P_DIO 12`、`OD_DMA_NUM 5`、`RX_DMA_OD`、`RX_DMA_ASRC_OUT`（OD 版 CIRC）。

### 4.2 `code/app_init.c`

- `Audio_Init`/`Audio_Resume` 加 OD 分支：`Sys_DIO_Config(12, DIO_6X_DRIVE|DIO_LPF_DISABLE|DIO_NO_PULL|DIO_MODE_OD_P)` + OD DMA(`BufferOut→OD_DATA`) + `ASRC->OUT→BufferOut`。
- `Initialize_Raw_PCM_Output_Type()` 包 `#ifndef OD_DIO12_OUTPUT`（它引用只在 PCM 分支定义的 `RX_DMA_PCM_STEREO`，不包会编译错）。

### 4.3 `code/app_func.c`

- `Asrc_reconfig`：OD 分支 `DEC_MODE1`+`<<29`；PCM 分支 `DEC_MODE2`+`<<28`。
- PCM ch4/ch5 ISR + 别名包 `#ifndef OD_DIO12_OUTPUT`（OD 模式 ch4/ch5 用 CIRC 无完成中断，不触发）。

## 5. 关键实现点 / 踩坑

1. **采样钟必须用内部时钟**（最大坑）：先照搬 sleep 的 `SAMPL_CLK=7`（DIO7 外部时钟），但 7160 测试时 DIO7 无时钟 → ASRC 锁不上速 → **一顿一顿**。改 `AUDIOSINK_CLK_SRC_DMIC_OD`（remote_mic_rx_raw 的 OD 做法）解决——ASRC 输出速率直接锁定内部 OD 速率，不依赖外部引脚。
2. **打印必须关**：DIO12 原是 UART 打印口。打印开关重构：`printf.h` 改为 app.h **无条件包含**（`PRINTF` 恒有定义），`OUTPUT_INTERFACE=OUTPUT_DISABLED` 由 `OD_DIO12_OUTPUT 或 关闭 DEBUG_UART_ENABLE` 触发。这样 `DEBUG_UART_ENABLE` 成为真正的打印总开关，且注释掉它不会因未包宏的 `PRINTF`（ble_rempro_cmd.c 等 47 处）编译失败。
3. **单端只配 OD_P**：sleep 也只 `Sys_DIO_Config(OD_P_DIO,…|DIO_MODE_OD_P)`，OD_N 不配不接。
4. `Initialize_Raw_PCM_Output_Type()` 必须 `#ifndef OD_DIO12_OUTPUT` 包裹，否则 OD 模式编译错。

## 6. 验证记录（2026-08-28）

- OD 实机**出声正常**（DIO12）。
- PCM 基线（提交 `2aaf771`）回测正常；之前 PCM"一顿一顿"是**测试环境问题，非代码回归**。
- OD / PCM 两种模式切换（`-fsyntax-only` 均通过，实机均正常）。
- 往返用 `git stash` 暂存/恢复过 OD 改动，无丢失。

## 参考

- OD 完整实现基准：`peripheral_server_sleep`（`Audio_Init` 的 OD 段）
- 内部时钟做法：`remote_mic_rx_raw`（`AUDIOSINK_CLK_SRC_DMIC_OD`）
- PCM 输出文档：`docs/7160test_pcm_output.md`
- IO 分配 / 调试总览：`docs/7160调试过程.md`
