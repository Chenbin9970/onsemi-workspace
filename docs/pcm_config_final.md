# RSL10 PCM 输出配置（重测最终结论）

> 文档版本：2026-08-25
> 本文档记录 `remote_mic_rx_raw` 工程 PCM slave 输出的**最终正确配置**，取代此前
> [pcm_output_config.md](pcm_output_config.md) 的 `WORD_SIZE_32` 结论，并纠正
> [pcm_config_retest.md](pcm_config_retest.md) 重测过程中走错的方向。

## 1. 最终配置

```c
#define PCM_CFG_TX  (PCM_BIT_ORDER_MSB_FIRST | \
                     PCM_TX_ALIGN_LSB        | \
                     PCM_WORD_SIZE_16        | \
                     PCM_FRAME_ALIGN_FIRST   | \
                     PCM_FRAME_WIDTH_LONG    | \
                     PCM_MULTIWORD_2         | \
                     PCM_SUBFRAME_ENABLE     | \
                     PCM_CONTROLLER_DMA      | \
                     PCM_DISABLE             | \
                     PCM_SELECT_SLAVE)
```

引脚（[app.h](../../remote_mic_rx_raw/include/app.h)）：

| 信号 | DIO | 方向 | 说明 |
|------|-----|------|------|
| BCLK | DIO2 | 输入 | 外部主机 384 kHz |
| FS / LRCK | DIO3 | 输入 | 外部主机 12 kHz，50% 方波 |
| SERO | DIO14 | 输出 | 数据（16-bit 立体声，单声道复制） |

## 2. 主机接口规格

| 参数 | 值 |
|------|-----|
| BCLK | 384 kHz（主机提供） |
| LRCK / FS | 12 kHz，**50% 占空比方波** |
| 采样率 | 12 kHz/声道 |
| 位宽 | 16 bit × 2 声道 |
| 帧结构 | 32 BCLK/帧（384k ÷ 12k） |
| 格式 | I2S 族（LRCK 方波） |

## 3. 字段选择依据

| 字段 | 值 | 依据 |
|------|-----|------|
| `WORD_SIZE` | **16** | 真实数据 16-bit。之前 `WORD_SIZE_32` 是把 2×16 塞进一个 32-bit word，语义脏 |
| `SUBFRAME` | **ENABLE** | **关键**。主机 FS 是 12k 方波（LRCK），每个边沿对应一个 word。SVD 语义：`ENABLE`=每 word 一个 frame signal，`DISABLE`=每帧一个。LRCK 方波必须用 ENABLE |
| `FRAME_ALIGN` | **FIRST** | 数据对齐 word 第一位（LRCK 边沿处） |
| `FRAME_WIDTH` | LONG | FS 高半帧（对应 50% 方波） |
| `MULTIWORD` | 2 | 立体声，每帧 2 word |
| `TX_ALIGN` | **LSB** | SVD 语义：`word_size < 32` 时用 TX_DATA 低 16 位。两个 word 都取低 16 位 → 单声道自动复制到左右声道 |
| `BIT_ORDER` | MSB_FIRST | 线上 MSB 先移出 |
| `SLAVE` | SLAVE | 主机提供 BCLK/FS，RSL10 被动跟随 |

## 4. 打包格式（关键）

数据放 **TX_DATA 低 16 位**：

```c
uint32_t s = (uint16_t)pcm_raw_buf[i];
pcm_tx_buf[i] = s;          // 低 16 = s，高 16 = 0（不是 s << 16）
```

`TX_ALIGN_LSB` 让 PCM 的两个 word（左右声道）都取 TX_DATA 低 16 位 = `s`，实现**单声道自动复制到左右立体声**。

> 之前错误地打包成 `s << 16`（高 16 位），但 `TX_ALIGN_LSB` 取低 16 位 → 移出全 0，SERO 恒低。

## 5. 调试过程（错误 → 正确）

| 阶段 | 配置 | 结果 | 结论 |
|------|------|------|------|
| 早期（旧文档） | `WORD_SIZE_32 + SUBFRAME_ENABLE + FRAME_ALIGN_FIRST` | 能出声 | 能工作但语义脏（2×16 当 1×32） |
| 重测尝试 | `WORD_SIZE_16 + SUBFRAME_DISABLE + FRAME_ALIGN_LAST` | SERO 恒低 / 数据错位 | **方向错**：DISABLE 是给窄脉冲 FS 的，跟 LRCK 方波对不上 |
| **最终** | `WORD_SIZE_16 + SUBFRAME_ENABLE + FRAME_ALIGN_FIRST` | 正确移出 | LRCK 方波每 word 一个边沿 → 必须 ENABLE |

### 根因

`pcm_config_retest.md` 把 `SUBFRAME_DISABLE` 当成了"16-bit 立体声"的标准配置，但：

- `SUBFRAME_DISABLE` = "每帧一个 frame signal" → 对应**窄脉冲 FS**（DSP mode）
- `SUBFRAME_ENABLE` = "每 word 一个 frame signal" → 对应**LRCK 方波**（每个边沿一个 word）

主机 FS 是 12kHz 50% 方波（LRCK），所以必须用 `SUBFRAME_ENABLE`。

## 6. 验证结果（诊断 pattern）

| 测试数据 | 结果 | 结论 |
|---------|------|------|
| `0x8000`（低 16） | 每 16 BCLK 一个 "1"，在 LRCK 边沿 | word 边界正确，bit15 在 word 第一个 BCLK |
| `0xAAAA5555`（高 16=0xAAAA，低 16=0x5555） | 两段都是 `0101…`（0x5555） | **两个 word 都取低 16 位**，高 16 位被忽略 → 单声道自动复制 |

## 7. 音频链路（PCM 输出段）

```
G722 解码(16kHz) → ASRC 16k→12k(DEC_MODE2) → pcm_raw_buf(16-bit)
  → 打包(低16位) → pcm_tx_buf(32-bit) → ch5 DMA → PCM slave → 主机
```

- ASRC 用 `DEC_MODE2`（0.75 下采样），因为 12k ≠ 16k 必须实质重采样
- 已知伪影：ASRC `DEC_MODE2` 大比率下采样有轻微"蚊蚊"（高频抖动）；软件插值方案（线性/三次）因无抗混叠，效果更差，已回退
