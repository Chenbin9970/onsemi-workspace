# rx_raw PCM 输出实现

## 目标

在 `remote_mic_rx_raw` 工程中，为接收端添加 PCM 音频**输出**接口，把通过 RM 无线链路收到的音频（G722 解码）经 ASRC 重采样后，从 RSL10 的 PCM 接口发送给外部设备。参考 `remote_mic_tx_coex` 的 PCM 输入实现，做对称的输出路径。

## 关键设计决策

| 决策点 | 结论 | 依据 |
|--------|------|------|
| 时钟模式 | **RSL10 做 PCM 从机（slave）** | 实测外部设备已产生 BCLK=384kHz、FS=12kHz，说明外部设备才是时钟主机 |
| 数据源 | ASRC 输出（`ASRC->OUT`） | 复用现有 ASRC 采样率适配（16kHz 解码 → 12kHz 输出），改动最小 |
| PCM 格式 | 16-bit × 2 字（立体声）、短帧 | 384k/12k = 32 BCLK/帧 = 2×16-bit，与实测一致 |
| 全双工 | 仅输出（SERO 发数据）；SERI 未使用 | 当前无消费外部回传数据的场景，硬件支持随时可加 |

## 硬件连接

| PCM 信号 | DIO 引脚 | 方向 | 说明 |
|---------|---------|------|------|
| CLK (BCLK) | DIO2 | 输入 | 外部主机提供 384kHz 位时钟 |
| FRAME (FS) | DIO3 | 输入 | 外部主机提供 12kHz 帧同步 |
| SERO（数据输出） | DIO14 | 输出 | 音频数据发给外部设备 |
| SERI（数据输入） | DIO4 | 输入 | 未使用（外部不回传），弱上拉占位 |

> 引脚调整：`RECOVERY_DIO` 从 DIO13 移到 **DIO12**（原 DIO13 让给 PCM 数据输出）；`PCM_SER_DO` 最终定在 DIO14。

## PCM 配置（`PCM_CFG_TX`）

```c
#define PCM_CFG_TX  (PCM_BIT_ORDER_MSB_FIRST | \
                     PCM_TX_ALIGN_LSB |        \
                     PCM_WORD_SIZE_16 |        \
                     PCM_FRAME_ALIGN_LAST |    \
                     PCM_FRAME_WIDTH_LONG |    \
                     PCM_MULTIWORD_2 |         \
                     PCM_SUBFRAME_DISABLE |    \
                     PCM_CONTROLLER_DMA |      \
                     PCM_DISABLE |             \
                     PCM_SELECT_SLAVE)
```

- 帧长 = `MULTIWORD_2 × WORD_SIZE_16` = 32 BCLK/帧 → FS = 384k/32 = **12kHz** ✓
- `PCM_FRAME_WIDTH` 只影响 WS 占空比（LONG = 半帧高 / SHORT = 1 BCLK 脉冲），不影响帧长/采样率
- `PCM_SELECT_SLAVE`：CLK/FRAME 作输入，不产生时钟

## 音频数据路径

```
RM 无线接收
   → G722 解码（16kHz, 160 样本 / 10ms 帧）
   → Buffer.output（共享内存）
   → DMA ch3 (ASRC_IN_IDX): Buffer.output → ASRC->IN   （RX_DMA_ASRC_IN）
   → ASRC 重采样（16kHz → 12kHz，由 ASCC 驱动）
   → DMA ch4 (ASRC_OUT_IDX): ASRC->OUT → PCM->TX_DATA  （RX_DMA_ASRC_OUT）
   → PCM 接口 SERO(DIO14) 按时钟移出 → 外部设备
```

### ASCC 采样率跟踪

- `SAMPLING_CLK_SRC`：PCM 模式下采样 `SAMPL_CLK = PCM_FRAME_SYNC(DIO3)` 的 12kHz FS
- `ASRC_Reconfig()`（app_func.c）根据 ASCC 测得的 `audio_sink_cnt` 计算 ASRC 相位增量，使 ASRC 输出速率对齐 12kHz 从机
- ASRC 配置沿用 `WIDE_BAND | ASRC_DEC_MODE1`

### ASRC 输出 DMA（PCM 变体）

```c
#define RX_DMA_ASRC_OUT  (DMA_SRC_ASRC | DMA_DEST_PCM | DMA_TRANSFER_P_TO_P | \
                          ... | DMA_SRC_WORD_SIZE_16 | DMA_DEST_WORD_SIZE_16 | \
                          DMA_SRC_ADDR_STATIC | DMA_DEST_ADDR_STATIC | \
                          DMA_ADDR_CIRC | DMA_DISABLE)
```

- 通道 4，源 `ASRC->OUT`，目标 `PCM->TX_DATA`（`AsrcOutDest` 在 `Initialize_Receiver_Audio_Output` 的 PCM 分支传入）
- 传输长度 `2 * FRAME_LENGTH = 320`，循环模式

## 代码改动清单

### `include/app.h`

1. 新增 `PCM_TX_RAW_OUTPUT(5)`，`OUTPUT_INTRF` 默认切到 PCM
2. PCM 引脚：`PCM_CLK_DO=2`、`PCM_FRAME_SYNC=3`、`PCM_SER_DI=4`、`PCM_SER_DO=14`
3. 新增 `PCM_CFG_TX`（16-bit × 2 字短帧 + slave + DMA 控制）
4. `RX_DMA_ASRC_OUT` 新增 PCM 变体（`DMA_DEST_PCM`，16-bit 直通）
5. `SAMPLING_CLK_SRC`：PCM 模式下 `SAMPL_CLK = PCM_FRAME_SYNC`（ASCC 测 DIO3）
6. `RECOVERY_DIO` 13 → 12

### `code/app_init.c`

1. 新增 `Initialize_Raw_PCM_Output_Type()`：
   - `Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP, CLK, FRAME, SERI, SERO, DIO_MODE_INPUT)`
   - `Sys_PCM_Config(PCM_CFG_TX)` + `Sys_PCM_Enable()`
2. `Initialize_Receiver_Audio_Output` 新增 PCM 分支：`AsrcOutDest = (uint32_t)&PCM->TX_DATA`
3. RECOVERY 注释更新（DIO12）

## 当前状态（2026-08-19）

- ✅ 已能出声，PCM 链路（解码 → ASRC → DMA → PCM TX）打通
- ⚠️ 声音"不大对"——具体症状待确认，排查方向见下

## 待排查 / 调试

| 症状 | 可能原因 | 建议 |
|------|---------|------|
| 音调/速度不对（变快/变慢） | ASRC 16k→12k 重采样比例未收敛，或 ASCC 对 DIO3 12kHz 测量异常 | 检查 `audio_sink_cnt` 是否≈120/帧；必要时在 `ASRC_Reconfig` 打点 |
| 含糊、破音、噪声 | 帧对齐/WS 极性不匹配 | 调 `PCM_FRAME_ALIGN_LAST→FIRST`、`PCM_FRAME_WIDTH_LONG→SHORT` |
| 断续、卡顿 | DMA/ASRC 跟不上，underrun | 检查 DMA 传输长度、ASRC 输出速率；必要时加大缓冲 |

## 相关文件

- [remote_mic_rx_raw/include/app.h](../remote_mic_rx_raw/include/app.h)
- [remote_mic_rx_raw/code/app_init.c](../remote_mic_rx_raw/code/app_init.c)
- [remote_mic_rx_raw/code/app_func.c](../remote_mic_rx_raw/code/app_func.c)（ASRC_Reconfig）
- RSL10 SDK：`C:/Users/ViewSSS/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.9.1182/`
  - `include/rsl10_sys_pcm.h` — PCM 驱动
  - `include/rsl10_hw_cid101.h` — PCM/DIO/DMA 寄存器定义
  - `source/firmware/drivers/sai_driver/source/SAI_RSLxx.c` — PCM master 模式参考
