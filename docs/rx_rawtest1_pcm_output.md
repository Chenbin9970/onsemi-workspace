# remote_mic_rx_rawtest1 — PCM 输出实现

> 工程：`remote_mic_rx_rawtest1`
> 目标：从干净 OD 基线逐步移植 `remote_mic_rx_rawtest` 已验证的 PCM slave 输出（**WORD_SIZE_32**）。
> 状态：**数据流已打通，能出声**（播放金属杂音待定；静音蚊蚊已用 dither 解决）
> 验证基准：`remote_mic_rx_rawtest`（WORD_SIZE_32 版"最接近正常出声"）

## 主机接口规格（唯一不变锚点）

| 参数 | 值 |
|------|-----|
| 时钟角色 | 主机是 clock master，RSL10 做 PCM slave |
| BCLK | 384 kHz（主机提供，**不可改**） |
| FS / LRCK | 12 kHz，50% 占空比方波（I2S 特征） |
| 采样率 | 12 kHz/声道 |
| 帧结构 | 32 BCLK/帧（384k ÷ 12k）= 2 × 16-bit |
| 数据 | 16-bit 立体声：word0=左、word1=右，24k 字/秒 |

### 已坐实的结论（测试记录 T 系列）

- **T19 坐实立体声**：word0 填 0、word1 填 1kHz → 主机**出声** → 主机读的是 word1（右声道），不是 word0，也不是把两字拼成 32-bit mono。
- **排除 32-bit mono**（T5 反证）：若 word0/word1 拼成 32-bit 样本，负 sin 值会错拼成巨大正数，不可能听到"干净 1kHz"。
- **主机不能改 16k**：FS 12k / BCLK 384k 由主机固定，RSL10 只能跟随，不能自行改采样率。

### 关键限制

- 主机 SDO 引脚硬件封装点不到，**无法用逻辑分析仪抓数据线实测对齐格式**（FRAME_ALIGN / FRAME_WIDTH / 时钟极性），只能盲试，靠主机端能否正确解析来验证。

---

## 1. 第一步：PCM 接口配置初始化（已完成）

### 1.1 引脚（app.h）

| 信号 | DIO | 方向 | 说明 |
|------|-----|------|------|
| BCLK | DIO2 | 输入 | 外部主机 384 kHz |
| FS / LRCK | DIO3 | 输入 | 外部主机 12 kHz，同时作 ASCC 采样时钟源 |
| SERI | DIO4 | 输入 | 未用（从机不回传） |
| SERO | DIO14 | 输出 | 音频数据 |

### 1.2 `PCM_CFG_TX`（app.h）

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
|------|-----|------|
| BIT_ORDER | MSB_FIRST | 线上 MSB 先移出 |
| TX_ALIGN | LSB | 取 TX_DATA 低 16 位 |
| WORD_SIZE | **32** | 一个 FS 周期传一个 32-bit 字 |
| FRAME_ALIGN | FIRST | 数据对齐帧第一位 |
| FRAME_WIDTH | LONG | FS 高半帧 |
| MULTIWORD | 2 | 每帧 2 字 |
| SUBFRAME | ENABLE | 每 word 一个 FS（对应 LRCK 方波） |
| CONTROLLER | DMA | DMA 写 TX_DATA |
| SLAVE | SLAVE | 外部主机提供时钟 |

> **数据槽位**（WORD_SIZE_32 + MULTIWORD_2）：一个 FS 周期内把 `PCM->TX_DATA` 的 32 bit 完整移出，高 16 bit 与低 16 bit 对应主机的两个 16-bit 声道槽位（word0=左、word1=右，MSB-first 先出高 16）。
> 传单个 16-bit 样品 `s` 三种填法：
> - 高 16 = s、低 16 = 0（`s << 16`）→ 只有 word0（左）有值
> - 高 16 = 0、低 16 = s（`s`）→ 只有 word1（右）有值
> - 高 16 = s、低 16 = s（`(s<<16)|s`）→ 左右同声（单声道复制）

### 1.3 ASCC 时钟源（app.h）

```c
#elif (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
#define SAMPL_CLK                       PCM_FRAME_SYNC
#define SAMPLING_CLK_SRC                ((uint32_t)(SAMPL_CLK << \
                                                    DIO_AUDIOSINK_SRC_CLK_Pos))
```

ASCC 采样 **DIO3 的 12 kHz FS**（`PCM_FRAME_SYNC`），用于 ASRC 速率锁定。

### 1.4 输出 DMA 定义（app.h）

```c
#define PCM_DMA_NUM                     5
#define PCM_FRAME_WORDS                 (3 * FRAME_LENGTH / 4)   /* 120 */

#define RX_DMA_PCM_STEREO  (DMA_DEST_PCM | DMA_TRANSFER_M_TO_P | DMA_LITTLE_ENDIAN | \
                            DMA_COMPLETE_INT_ENABLE | DMA_COUNTER_INT_DISABLE | \
                            DMA_DEST_WORD_SIZE_32 | DMA_SRC_WORD_SIZE_32 | \
                            DMA_SRC_ADDR_INC | DMA_DEST_ADDR_STATIC | \
                            DMA_ADDR_LIN | DMA_DISABLE)
```

- ch5：`pcm_tx_buf → PCM->TX_DATA`，32-bit 写，LIN + 完成中断重武装。

### 1.5 初始化（app_init.c）

```c
void Initialize_Raw_PCM_Output_Type(void)
{
    Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP, PCM_CLK_DO,
                      PCM_FRAME_SYNC, PCM_SER_DI, PCM_SER_DO, DIO_MODE_INPUT);
    Sys_PCM_Config(PCM_CFG_TX);

    Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                          (uint32_t)&pcm_tx_buf[0], (uint32_t)&PCM->TX_DATA);
}
```

`Initialize_Receiver_Audio_Output` 的 PCM 分支：`AsrcOutDest = pcm_tx_buf` + 调用上述函数。

---

## 2. 第一步改动清单

### include/app.h

1. `#define PCM_TX_RAW_OUTPUT 5`，`OUTPUT_INTRF` 默认切 `PCM_TX_RAW_OUTPUT`
2. PCM DIO：`PCM_CLK_DO=2` / `PCM_FRAME_SYNC=3` / `PCM_SER_DI=4` / `PCM_SER_DO=14`
3. `PCM_CFG_TX`（WORD_SIZE_32）
4. `SAMPLING_CLK_SRC` PCM 分支（DIO3）
5. `PCM_DMA_NUM=5`、`PCM_FRAME_WORDS`、`RX_DMA_PCM_STEREO`

### code/app_init.c

1. `uint32_t pcm_tx_buf[PCM_FRAME_WORDS]`
2. `Initialize_Raw_PCM_Output_Type()`
3. `Initialize_Receiver_Audio_Output` 加 `#elif PCM` 分支

---

## 3. 数据流实现（已完成）

链路（从 RM 接收往下）：

> RM 接收(`App_Process_Incoming_Data`/`Renderer`) → G722 解码(`LPDSP32_Start_DEC`) → Buffer.output(16k) → DMA ch3 → ASRC(DEC_MODE2 16k→12k) → DMA ch4 → pcm_tx_buf → DMA ch5 → PCM 从机 SERO(DIO14)

关键改动：

- **`ASRC_Reconfig`**（app_func.c）：`DEC_MODE2` + 相位 `<<28`（16k→12k 唯一有效，0.75 ∈ 0.4~1.0）；`WIDE_BAND` 滤波。
- **`RX_DMA_ASRC_OUT`**（app.h）PCM 分支：`P_TO_M / SRC16 / DEST16 / LIN / 完成中断`。DEST16 写 uint32_t 数组右对齐低 16 位、**高 16 位天然补 0** → 每字 = `[word0=0, word1=样本]`（主机读 word1）。
- **`Initialize_ASRC`**（app_init.c）：`asrc_out_len = PCM_FRAME_WORDS(120)`，PCM 模式开机**不使能 ch4**（连接时武装）。
- **ch4/ch5 DMA IRQ**（app_func.c）：完成中断重武装。
- **`App_Process_Connected`**：使能 ch4 → ch5 + NVIC → 最后 `Sys_PCM_Enable()`。

## 4. 调试结论：金属杂音 / 蚊蚊

| 现象 | 来源 | 处理 |
|------|------|------|
| 静音「蚊蚊」 | ASRC 极限环（零输入下量化振荡） | **dither ±8 LSB 打破**（`ASRC_DITHER`） |
| 播放金属杂音 | ASRC 16k→12k（3:4 非整数）下采样固有伪影 | **未解决**（软件 FIR 重采样 / 接受 待定） |

排查记录（按时间）：

- 双缓冲（`PCM_DOUBLE_BUFFER`）：无变化 → 非 ch4/ch5 竞争
- `LOW_DELAY` 滤波：无变化 → 非滤波类型
- 固定相位：加重 → 主机 12k 有偏差，必须动态锁定
- 低通平滑相位：更差 → ASRC 需快速实时跟踪主机时钟
- **dither ±8 LSB：静音蚊蚊消失（保留）**

## 5. 宏开关（app.h）

| 宏 | 值 | 作用 |
|----|----|------|
| `PCM_DOUBLE_BUFFER` | 1 | 1=双缓冲，0=单缓冲 |
| `ASRC_DITHER` | 1 | 1=加 dither（±8 LSB）打破 ASRC 极限环 |

## 6. 待办（未决）

- [ ] **播放时金属杂音**：ASRC 16k→12k（3:4 非整数）下采样固有伪影（软件 FIR 重采样 / 接受 待定）

---

## 参考

- 验证基准工程：`remote_mic_rx_rawtest`
- 旧配置文档：`docs/pcm_config_final.md`（WORD_SIZE_16）、`docs/pcm_asrc_current_config.md`（WORD_SIZE_32 现状）、`docs/rm_rx_pcm_dataflow.md`（数据流）
