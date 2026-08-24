# PCM 配置重测记录（进行中）

> 文档版本：2026-08-24
> 状态：**未完成** —— 换电脑继续调试，下一步见文末。
> 起因：怀疑之前 `WORD_SIZE_32 + SUBFRAME_ENABLE` 的 PCM 配置是"绕弯"的，改为从 SVD 手册和官方 SAI 参考重新验证一个干净配置。

## 1. 背景

之前 [pcm_output_config.md](pcm_output_config.md) 的结论是"PCM TX 必须 `WORD_SIZE_32 + SUBFRAME_ENABLE + FRAME_ALIGN_FIRST`"。但仔细核对 SVD 后发现这个组合是在用「32-bit 字 + 每字一个 FS」来伪装「2×16-bit 立体声」，`TX_ALIGN_LSB` 在 `WORD_SIZE_32` 下还是死字段。于是决定重新测一个更标准的配置。

目标仍是：**单声道 16-bit，帧长 32 BCLK，FS = 12kHz（384k/32）**。

## 2. 硬件权威语义（SVD rsl10.svd）

| 字段 | 位 | 语义 |
|------|----|------|
| `WORD_SIZE` | [10:9] | 线上每字位数（8/16/24/32） |
| `FRAME_LENGTH`(MULTIWORD) | [6:4] | 每帧字数（2/4/6/…/16） |
| `FRAME_SUBFRAMES`(SUBFRAME) | [3] | **ENABLE=每字一个 FS；DISABLE=每帧一个 FS** |
| `FRAME_WIDTH` | [7] | **LONG=FS 高电平持续半帧；SHORT=高 1 BCLK** |
| `FRAME_ALIGN` | [8] | FIRST=FS 对齐帧第一位；LAST=对齐帧最后一位 |
| `BIT_ORDER` | [12] | MSB_FIRST=数据从 MSB 移出 |
| `TX_ALIGN` | [11] | **仅 word_size < 32 时生效**：LSB=用 TX_DATA 低 16 位 |

## 3. 官方参考（SAI_RSLxx.c / SAI_RSLxx.h）

官方 CMSIS SAI 驱动的权威配置：

- **`SAI_CFG_INI`**（DMA 模式）= `PCM_TX_ALIGN_LSB | PCM_CONTROLLER_DMA | PCM_DISABLE`（`TX_ALIGN` 用 **LSB**）
- **I2S / PCM long 帧**：`PCM_FRAME_ALIGN_LAST | PCM_FRAME_WIDTH_LONG`（**FRAME_ALIGN 用 LAST，不是 FIRST**）
- **word_size=16**：`PCM_WORD_SIZE_16`，且 `dmaCfgT.dst_word_size = DMA_WORD_SIZE_16`
- **I2S / slot=32 + word=16**：`PCM_SUBFRAME_DISABLE | PCM_MULTIWORD_2`

官方 I2S 16-bit 完整组合：

```c
BIT_ORDER_MSB_FIRST | TX_ALIGN_LSB | WORD_SIZE_16 | FRAME_ALIGN_LAST |
FRAME_WIDTH_LONG | MULTIWORD_2 | SUBFRAME_DISABLE | CONTROLLER_DMA
```

## 4. 测试时间线（逻辑分析仪 + 诊断 pattern `0x8000`）

诊断 pattern：`TX_DATA` 只含一个 1（`0x8000`），用于精确定位位序、字宽、数据落位。

| # | 配置 | 结果 | 结论 |
|---|------|------|------|
| 1（baseline） | `WORD_SIZE_32 + SUBFRAME_ENABLE + FRAME_ALIGN_FIRST`，打包 `0x8000<<16`，DMA dst=32 | `1` 在 FS 高电平第 1 个 BCLK，间隔 31 | 32-bit 字结构确认，MSB-first + 数据在 word0 正确 |
| 2 | `WORD_SIZE_16 + SUBFRAME_DISABLE`，打包 `0x8000`(低16)，DMA dst=32 | 高、低电平段都出现 `1` | 数据错位，DMA/PCM 宽度不匹配 |
| 3 | 同上 + DMA dst=16 | `1` 只在低电平段 | **word0=TX_DATA 高16位，word1=低16位**（关键事实）；DMA dst=16 只写低16 → word0 恒 0 |
| 4 | 打包 `0x8000<<16`(高16)，DMA dst=32（改回） | **没有高电平了** | FS 恒低：`FRAME_ALIGN_FIRST` 在 `SUBFRAME_DISABLE` 下失效 |
| 5（当前） | 同上 + `FRAME_ALIGN_LAST` | **未测** | 换电脑继续 |

## 5. 关键结论（已验证的硬事实）

1. **word0 = TX_DATA 高 16 位，word1 = 低 16 位**（MSB-first 先移出高 16）。数据要进 word0（FS 前半段）必须放 TX_DATA 高 16 位，即打包 `s << 16`。
2. **DMA 必须 32-bit 写满 TX_DATA**（`DMA_DEST_WORD_SIZE_32`）。`dst=16` 只写低 16 位，word0 拿不到数据。
3. **`FRAME_ALIGN_FIRST` 只在 `WORD_SIZE_32 + SUBFRAME_ENABLE` 下能生成 FS 高电平**；换成 `WORD_SIZE_16 + SUBFRAME_DISABLE` 后 FS 恒低，必须用官方的 `FRAME_ALIGN_LAST`。
4. 官方统一用 `FRAME_ALIGN_LAST`（I2S/PCM long），`TX_ALIGN_LSB` 是对的。

## 6. 当前代码状态

`remote_mic_rx_raw` 三个文件已改（未验证）：

- **`PCM_CFG_TX`**（app.h）已改为官方 I2S 16-bit 组合：
  ```c
  BIT_ORDER_MSB_FIRST | TX_ALIGN_LSB | WORD_SIZE_16 | FRAME_ALIGN_LAST |
  FRAME_WIDTH_LONG | MULTIWORD_2 | SUBFRAME_DISABLE | CONTROLLER_DMA | DISABLE | SLAVE
  ```
- 测试音 `pcm_test_buf` 填诊断 pattern `0x8000 << 16`（高16），`RX_DMA_PCM_TEST` 保持 `DEST_WORD_SIZE_32 | SRC_WORD_SIZE_32`。
- `PCM_TEST_TONE = 1`、`PCM_TEST_ASRC = 0`（测试音路径激活）。
- 附带加了 `PCM_TEST_ASRC` 开关（ASRC 隔离测试，默认关，暂未用）。

## 7. 下一步（换电脑后）

1. 编译，抓波形，验证 **#5 配置**（`FRAME_ALIGN_LAST`）：
   - FS 有没有高电平了？
   - `1` 落在 FS 哪个电平段、间隔多少 BCLK？
   - 预期两种：A=数据落在低电平段（标准 I2S 极性，WS=0 左声道）；B=仍在高电平段。
2. 无论 A/B，只要帧结构正确（32 BCLK 帧、FS 半帧 16），就把这个干净配置**应用到真实路径**：
   - `RX_DMA_PCM_STEREO`（app.h）目前 `DEST_WORD_SIZE_32` 需确认是否需要配合调整。
   - `app_func.c` 的 `DMA_IRQ_FUNC(ASRC_OUT_IDX)` 打包 `s << 16` 与最终确定的 word 落位对齐。
   - 关闭测试音（`PCM_TEST_TONE=0`），恢复真实 Path B。
3. 完成后更新 [pcm_output_config.md](pcm_output_config.md) 的结论（当前它记录的 `WORD_SIZE_32 + SUBFRAME_ENABLE + FRAME_ALIGN_FIRST` 可能要被新的干净配置取代）。

## 相关文件

- [remote_mic_rx_raw/include/app.h](../remote_mic_rx_raw/include/app.h) — `PCM_CFG_TX`、`RX_DMA_PCM_TEST`、`PCM_TEST_*`
- [remote_mic_rx_raw/code/app_init.c](../remote_mic_rx_raw/code/app_init.c) — 测试音路径、诊断 pattern
- [remote_mic_rx_raw/code/app_func.c](../remote_mic_rx_raw/code/app_func.c) — ch4 打包、ch5 重武装
- SAI 官方参考：`C:/Users/ViewSSS/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.9.1182/source/firmware/drivers/sai_driver/source/SAI_RSLxx.c`
