# rx_rawtest PCM + ASRC 当前配置（最接近正常出声）

> 文档版本：2026-08-26
> 工程：`remote_mic_rx_rawtest`（RM 接收 → G722 解码 → ASRC → PCM 从机输出）
> 状态：已能出声音，**最接近正常**的一套配置（测试输入为 DSP0 中断注入的 1kHz 正弦）
> 相关文档：[pcm_config_final.md](pcm_config_final.md)（早期最终配置）、[rm_rx_pcm_dataflow.md](rm_rx_pcm_dataflow.md)

## 1. 数据流总览

```
RM 连上 → DSP0 中断注入 1kHz 正弦 → Buffer.output (16k)
  → ch3 DMA → ASRC (DEC_MODE2 16k→12k) → 120 样品/10ms
  → ch4 DMA (ASRC->OUT → pcm_tx_buf)
  → ch5 DMA (pcm_tx_buf → PCM->TX_DATA)
  → PCM 从机 SERO(DIO14) → 外部主机 12k
```

- **测试输入**：DSP0 中断里把 1kHz 正弦写进 `Buffer.output`（覆盖解码数据），走真实 ASRC→PCM 链路
- **输出**：PCM 从机，主机提供 BCLK 384k / FS 12k

## 2. PCM 配置

### 2.1 引脚（app.h）

| 信号 | DIO | 方向 | 说明 |
|------|-----|------|------|
| BCLK | DIO2 | 输入 | 外部主机 384 kHz |
| FS/LRCK | DIO3 | 输入 | 外部主机 12 kHz |
| SERI | DIO4 | 输入 | 未用（从机不回传） |
| **SERO** | **DIO14** | 输出 | 音频数据 |

> `RECOVERY_DIO` = DIO12（DIO14 让给 PCM SERO）。

### 2.2 `PCM_CFG_TX`（app.h，当前值）

```c
#define PCM_CFG_TX  (PCM_BIT_ORDER_MSB_FIRST | \
                     PCM_TX_ALIGN_LSB        | \
                     PCM_WORD_SIZE_32        | \
                     PCM_FRAME_ALIGN_FIRST   | \
                     PCM_FRAME_WIDTH_LONG    | \
                     PCM_MULTIWORD_2         | \
                     PCM_SUBFRAME_ENABLE     | \
                     PCM_CONTROLLER_DMA      | \
                     PCM_DISABLE             | \
                     PCM_SELECT_SLAVE)
```

| 字段 | 值 | 说明 |
|------|----|------|
| BIT_ORDER | MSB_FIRST | 线上 MSB 先移出 |
| TX_ALIGN | LSB | 用 TX_DATA 低 16 位 |
| WORD_SIZE | **32** | 每帧一个 32-bit 字 |
| FRAME_ALIGN | FIRST | 数据对齐帧第一位 |
| FRAME_WIDTH | LONG | FS 高半帧 |
| MULTIWORD | 2 | 每帧 2 字（2×16bit 语义） |
| SUBFRAME | ENABLE | 每字一个 FS（LRCK 方波） |
| CONTROLLER | DMA | DMA 写 TX_DATA |
| SLAVE | SLAVE | 外部主机提供时钟 |

帧结构：MULTIWORD_2 × WORD_SIZE_32 = 32 BCLK/帧 → FS = 384k/32 = **12 kHz**。

### 2.3 输出 DMA（ch5，`PCM_DMA_NUM=5`）

```c
#define RX_DMA_PCM_STEREO  (DMA_DEST_PCM | DMA_TRANSFER_M_TO_P | DMA_LITTLE_ENDIAN | \
                            DMA_COMPLETE_INT_ENABLE | DMA_COUNTER_INT_DISABLE | \
                            DMA_DEST_WORD_SIZE_32 | DMA_SRC_WORD_SIZE_32 | \
                            DMA_SRC_ADDR_INC | DMA_DEST_ADDR_STATIC | \
                            DMA_ADDR_LIN | DMA_DISABLE)
```

- 缓冲：`uint32_t pcm_tx_buf[PCM_FRAME_WORDS]`，`PCM_FRAME_WORDS = 3*FRAME_LENGTH/4 = 120`
- **LIN + 完成中断重武装**（文档记录 CIRC 循环边界欠载，用 LIN）
- ch5 完成中断：完整 `Sys_DMA_ChannelConfig` 重装计数器 → 重武装

### 2.4 初始化与启动

- `Initialize_Raw_PCM_Output_Type()`：
  `Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP, DIO2, DIO3, DIO4, DIO14, DIO_MODE_INPUT)`
  + `Sys_PCM_Config(PCM_CFG_TX)`（含 DISABLE）
  + ch5 `Sys_DMA_ChannelConfig`（不使能）
- `App_Process_Connected`（RM 连接时）：使能 ch4 → 使能 ch5 + NVIC → `Sys_PCM_Enable()`（最后开 PCM）

## 3. ASRC 配置

### 3.1 输入 DMA（ch3，`ASRC_IN_IDX=3`）

```c
#define RX_DMA_ASRC_IN  (DMA_DEST_ASRC | DMA_TRANSFER_M_TO_P | DMA_LITTLE_ENDIAN | \
                         DMA_COMPLETE_INT_ENABLE | DMA_COUNTER_INT_DISABLE | \
                         DMA_DEST_WORD_SIZE_16 | DMA_SRC_WORD_SIZE_32 | \
                         DMA_SRC_ADDR_INC | DMA_DEST_ADDR_STATIC | \
                         DMA_ADDR_LIN | DMA_DISABLE)
```

- `Buffer.output`（32-bit 字存 2 样品）→ ASRC->IN（16-bit），每子帧 8 样品
- DSP0 中断：注入 1kHz 正弦 → 清状态 → 重武装 ch3 → `ASRC_ENABLE`

### 3.2 输出 DMA（ch4，`ASRC_OUT_IDX=4`，当前值）

```c
#define RX_DMA_ASRC_OUT  (DMA_SRC_ASRC | DMA_TRANSFER_P_TO_M | DMA_LITTLE_ENDIAN | \
                          DMA_COMPLETE_INT_ENABLE | DMA_COUNTER_INT_DISABLE | \
                          DMA_DEST_WORD_SIZE_16 | DMA_SRC_WORD_SIZE_16 | \
                          DMA_SRC_ADDR_STATIC | DMA_DEST_ADDR_INC | \
                          DMA_ADDR_LIN | DMA_DISABLE)
```

- 长度 `PCM_FRAME_WORDS = 120`，目标 `pcm_tx_buf`
- ch4 完成中断：仅重武装
- 开机不使能；RM 连接时在 `App_Process_Connected` 里使能

### 3.3 `ASRC_Reconfig()`（app_func.c）

```c
int64_t Cr = FRAME_LENGTH << SHIFT_BIT;      // 160<<20 (16k)
int64_t Ck = audio_sink_cnt;                 // ASCC 实测 12k
int64_t phase = ((Cr - Ck) << 28) / Ck;
Sys_ASRC_Config(phase, WIDE_BAND | ASRC_DEC_MODE2);
```

- **DEC_MODE2：16k → 12k 下采样**（参考工程验证配置）

### 3.4 速率同步（ASCC）

- `SAMPLING_CLK_SRC = PCM_FRAME_SYNC(DIO3)` → ASCC 测 12k FS
- `Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0)` + phase/period 中断
- 同步链路：
  `ASCC 测 DIO3 12k → AUDIOSINK_PHASE_IRQHandler 更新 audio_sink_cnt + flag_ascc_phase`
  → `DSP0 中断 flag_ascc_phase 置位时调 ASRC_Reconfig()`

## 4. 当前状态与已知问题

| 项 | 状态 |
|----|------|
| 数据链路 | 能出声，最接近正常 |
| 测试输入 | DSP0 注入 1kHz 正弦（覆盖解码数据） |
| 待验证 | 具体听感/波形是否还有轻微间隙或杂音（"接近正常"而非完美） |

### 与文档最终值的差异（当前 IDE 值）
- `PCM_WORD_SIZE` = **32**（`pcm_config_final.md` 最终是 **16**）
- `RX_DMA_ASRC_OUT` = **SRC16 / DEST16**（16-bit 样品、16-bit 写）

## 5. 相关文件

- `remote_mic_rx_rawtest/include/app.h` — `PCM_CFG_TX`、`RX_DMA_PCM_STEREO`、`RX_DMA_ASRC_OUT`、引脚
- `remote_mic_rx_rawtest/code/app_init.c` — `Initialize_Raw_PCM_Output_Type`、`Initialize_ASCC`、`Initialize_ASRC`
- `remote_mic_rx_rawtest/code/app_func.c` — `ASRC_Reconfig`、ch3/ch4/ch5 DMA 处理器、`App_Process_Connected`
