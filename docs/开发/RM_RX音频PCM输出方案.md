# RM RX 音频 PCM 输出方案

## 背景

当前 RM（远程麦克风）音频接收链路，G.722 解码后的 PCM 通过 OD（Output Driver）直接驱动助听器喇叭（模拟输出）。现需改为通过 **PCM 数字接口发送给主机**（助听器 DSP / 上位机），由主机自行处理音频。

## 目标

把 G.722 解码输出的 PCM，经 ASRC 重采样后，通过 RSL10 的 PCM 接口（SPI0 PCM 模式）以 slave 身份发送给主机。

## 主机 PCM 接口规格

| 参数 | 值 | 依据 |
|------|-----|------|
| BCLK | 384 kHz | 主机提供（clock master） |
| LRCK / FS | 12 kHz，**50% 占空比方波** | 逻辑分析仪实测 |
| 采样率 Fs | 12 kHz（每声道） | LRCK 频率 = Fs |
| 位宽 | 16 bit | 384k ÷ 12k ÷ 2 声道 = 16 |
| 声道 | 立体声（左右） | 50% 方波 = 左右声道切换 |
| 格式 | **I2S**（大概率） | 50% 方波 LRCK 是 I2S 标准特征 |

> 结论：主机是 **16-bit 立体声 I2S @ 12kHz**，每帧 32 BCLK（384/12）。

### 关键约束

- **SDO 引脚硬件封装，点不到**，无法用逻辑分析仪抓数据线实测对齐格式 → 格式只能盲试，靠主机端能否正确解析验证。
- **时钟由主机提供**（BCLK 384k + LRCK 12k），RSL10 做 PCM **slave**，被动跟随主机时钟，本身不需要"知道"采样率。
- **采样率不匹配**：G.722 解码输出是 16 kHz，主机是 12 kHz，需 ASRC 做 4:3 下采样。代价是丢失 6–8 kHz 高频（12k 奈奎斯特 = 6kHz），语音主体不受影响。

## 数据流对比

### 当前（OD 模拟输出）

```
RM Radio → G.722 decode (LPDSP32) → 16kHz PCM (Dsp2CmBuff)
  → ASRC (时钟参考 DIO7 AUDIOSINK) → BufferOut
  → OD (DMA_DEST_OD) → 喇叭
```

### 目标（PCM 数字输出）

```
RM Radio → G.722 decode (LPDSP32) → 16kHz PCM (Dsp2CmBuff)
  → ASRC 下采样 16k→12k (时钟参考 主机 LRCK) → BufferOut
  → 单声道复制为左右立体声
  → SPI0 PCM slave TX (DMA_TRG_PCM, PCM_SER_DO) → 主机
```

## 技术方案（三个模块）

### 模块 1：ASRC 重采样 16k→12k

**现状**（[app_func.c](../../peripheral_server_sleep/code/app_func.c) `Asrc_reconfig`）：

```c
Cr = FRAME_LENGTH << SHIFT_BIT;          // 160 << 20，参考帧
Ck = audio_sink_cnt;                      // 音频时钟测量值（DIO7 AUDIOSINK）
asrc_inc_carrier = ((((Cr - Ck) << 29) / Ck) << 0);
Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_DEC_MODE1);
```

**改造点**：

1. 时钟参考从 DIO7 的 AUDIOSINK 改为主机的 LRCK（12 kHz）——`AUDIOSINK` 输入时钟源改为 PCM frame sync。
2. `asrc_inc_carrier` 配成 4:3 下采样（输出 12k = 输入 16k × 3/4）。
3. ASRC 输出 DMA 的数据量相应变化：每帧输出样本数 = `FRAME_LENGTH × 3/4 = 120` 样本。

> ⚠️ 具体 ASRC 参数（`asrc_inc_carrier` 计算、`WIDE_BAND` 模式是否适用）需实现时对照 RSL10 ASRC 文档验证，不凭记忆写公式。

### 模块 2：单声道 → 立体声

RM 是单耳（`RM_LEFT` 或 `RM_RIGHT`），主机要左右两声道。`BufferOut` 目前是一路 16-bit PCM，需拆成左右交织。

两种填法（**待确认**）：

| 方式 | 布局 | 适用 |
|------|------|------|
| 复制到左右 | `[L, L, R(=L), R(=L), ...]` | 主机只要单声道内容，双耳同声 |
| 左填右补零 | `[L, 0, L, 0, ...]` | 主机明确需要区分左右声道 |

### 模块 3：PCM slave TX 输出

**PCM 配置**（16-bit I2S 立体声 slave，参照 TX 端 `PCM_CFG_RX` 改 word size）：

```c
#define PCM_CFG_TX  (PCM_BIT_ORDER_MSB_FIRST | \
                     PCM_TX_ALIGN_LSB        | \
                     PCM_WORD_SIZE_16        | \
                     PCM_FRAME_ALIGN_LAST    | \
                     PCM_FRAME_WIDTH_LONG    | \
                     PCM_MULTIWORD_2         | \
                     PCM_SUBFRAME_DISABLE    | \
                     PCM_CONTROLLER_DMA      | \
                     PCM_SELECT_SLAVE)
```

**DIO 引脚**（参照 TX 端 `app.h` PCM 定义）：

```c
#define PCM_SER_DO     1     // 数据输出 → 主机 SDI
#define PCM_CLK_DO     3     // 时钟输入 ← 主机 BCLK
#define PCM_FRAME_SYNC 0     // 帧同步输入 ← 主机 LRCK
```

**DMA 改造**：输出 DMA 从 `RX_DMA_OD`（`DMA_DEST_OD`）改为 PCM DMA（`DMA_TRG_PCM`，`PCM_SER_DO`），传输目标从 `AUDIO->OD_DATA` 改为 PCM 数据寄存器。

## PCM 格式盲试矩阵

主机解析不出正确数据时，按顺序切换 `PCM_CTRL` 字段（每次改 1 个宏，重编译测试）：

| 序 | FRAME_ALIGN | FRAME_WIDTH | 时钟极性 | 对应 |
|----|------------|-------------|---------|------|
| 0 | LAST | LONG | 正常 | **I2S（默认，先试）** |
| 1 | FIRST | LONG | 正常 | 数据紧跟 LRCK 边沿（LJ 方向） |
| 2 | LAST | SHORT | 正常 | 半帧宽（PCM 短帧方向） |
| 3 | LAST | LONG | 反转 | BCLK 采样沿反了 |

> RSL10 SAI 驱动声明支持 I2S 与 PCM short/long，不显式支持 LJ/RJ；但底层 `PCM_CTRL` 寄存器用上述字段组合可覆盖对齐变化。

## 待确认项

| # | 问题 | 状态 |
|---|------|------|
| 1 | 单声道填左右声道：复制 or 左填右补零 | **待确认** |
| 2 | 先按 I2S 实现，盲试矩阵作为调试开关 | 待确认 |
| 3 | ASRC 4:3 下采样具体参数 | 实现时验证 |

## 实施步骤与检查点

1. **模块 3**：PCM slave TX 配置 + DIO + DMA 改道（先把通路打通，用固定测试数据验证主机能收到正确位流）。
2. **模块 2**：`BufferOut` 改立体声交织布局。
3. **模块 1**：ASRC 时钟参考改主机 LRCK + 4:3 下采样。
4. **格式盲试**：主机解析异常时按矩阵切换。
5. **验证**：主机端能正确还原 G.722 解码后的音频内容，采样率 12 kHz、16-bit、立体声。
