# remote_mic_tx_coex_pcm 开发文档

> 状态：**边做边记，尚未收尾**。音频已能播，但「长时间播放会变差」的根因未定论。
> 本文档记录到 2026-11-03 为止的进展。所有改动**未提交**（基线见 §2）。
> 2026-10-29 新增第三台 PCM 主机（16 kHz / 64 BCLK），见 §14；
> 2026-11-03 补充该档的 ASRC 重采样（§14.6）与设备侧可行性判断（§14.7）。

## 1. 工程概述

`remote_mic_tx_coex_pcm` 基于 onsemi RSL10 `remote_mic_tx_coex` demo（远端麦克风发送机 / RM TX，
BLE + RM 共存），目标是把 **CM108B（USB 音频芯片）的 I2S 输出**作为音源，
经 RSL10 编码后用 RM 自定义协议广播，由 **1644** 设备接收播放。

PCM 输入是**编译期三选一**（`app.h:PCM_HOST_SEL`，2026-10-29 起）：CM108B（48 kHz）/
16 kHz 主机（32 BCLK）/ 16 kHz 主机（64 BCLK，当前生效），见 §3.0 与 §14。

完整通路：

```
PC/USB ──▶ CM108B ──I2S(48kHz/16bit/立体声)──▶ RSL10 PCM 从机
        ──3:1 抽取(48k→16k)──▶ LPDSP32 G.722 编码(48kbps)
        ──▶ tx_data_fifo ──▶ RM 广播 ──▶ 1644 解码播放
```

参考工程：`remote_mic_tx_coex`（DMIC 输入版，**端到端已验证能在 1644 上播放**），
本工程的后级通路照它的结构做，只有输入侧不同。

## 2. 来源与 git 基线

- Demo 基线已提交：commit `24b7199`（RSL10 原厂 `remote_mic_tx_coex` 示例，27 个文件，verbatim）。
- 之后所有改动**未提交**，涉及 8 个文件（+426 / −136 行）：
  `app.h`、`app_func.c`、`app_init.c`、`app_process.c`、`ble_std.c`、`queue.c`、`rm_app.c`、`.cproject`。
- 两个未跟踪的 PDF（规格书 / 原理图）放在工程目录下。

## 3. 引脚与接线

### 3.0 PCM 主机档位（编译期三选一）

| 档 | `PCM_HOST_SEL` | 时钟角色 | FS | BCLK/帧 | 时隙 | 抽取比 | 数据 / 时钟 / FS |
|---|---|---|---|---|---|---|---|
| CM108B | `PCM_HOST_CM108B` = 0 | RSL10 从机 | 48 kHz | 64 | 2 × 32-bit | 3 | DIO2 / DIO3 / **DIO9** |
| 16 kHz 主机（32 BCLK） | `PCM_HOST_16K_32BCLK` = 1 | RSL10 从机 | 16 kHz | 32 | 2 × 16-bit | 1 | DIO2 / DIO3 / **DIO10** |
| 16 kHz 主机（64 BCLK） | `PCM_HOST_16K_64BCLK` = **2（当前）** | **RSL10 主机** | **15.625 kHz（目标 16 kHz）** | 64 | 2 × 32-bit | 1 | DIO2 / DIO3 / **DIO9** |

三档互斥，同一时刻只编译一套，不是运行时可插拔。第三档设备是 I2S 从机，所以 RSL10
必须做时钟主机；受 16 MHz SYSCLK 整数分频限制，实际只能生成 15.625 kHz，见 §14.3。

**生效分支**：`PCM_RX_RAW_SOURCE = EZAIRO_7100(=0)`，即走 `app.h` 里那组 `#else` 定义。
它只表示「不走原厂 WM8731 codec shield 分支」，实际接的是外部 I2S 主机（上表三档）。

下表为 CM108B（档 0）的接线；第三档的完整参数见 §14。

| CM108B | 引脚 | 信号 | → | RSL10 DIO | 方向 |
|---|---|---|---|---|---|
| DALRCK | 46 | 帧同步 LRCK | → | **DIO9** | 输入 |
| SDOUT | 44 | 音频数据 | → | **DIO2** | 输入 |
| DASCLK | 47 | 位时钟 SCLK | → | **DIO3** | 输入 |
| DAMCLK | 45 | MCLK 12.288 MHz | | **不接** | — |
| GND | 14/24/33/36 | 地 | ↔ | GND | — |
| — | — | — | | DIO1 | 输出（高阻，空跑） |

### 3.1 ⚠️ DIO0 这颗 pad 是坏的

**本工程最大的硬件坑**：DALRCK 一接到 DIO0，CM108B 那一端的 48 kHz 就消失
（在 CM108B 引脚上量也消失）。

排查结论：**与固件无关**。在 `PCM_HW_OFF=1`（PCM 外设完全不配置、DIO0 只设成普通高阻输入）
的状态下依然如此。CM108B 的 I2S pad 驱动能力只有 **2 mA**（规格书 p.7），
任何低阻都会把它打死。同板上 DIO0 曾被用作 SPI 片选（输出）。

**处理**：只把帧同步挪到 DIO9，其余两根沿用原厂默认 DIO2/DIO3。
以后再接外部音频输入，**先绕开 DIO0**。

### 3.2 电平

CM108B 是 3.3 V 逻辑（数字 I/O 由 `DREG33` 供电，pad 为 5V 耐受的 3.3V pad），
RSL10 的 **VDDO 实测 3.3 V**，直接兼容，无需电平转换。

### 3.3 固件中其他被驱动的 pad

| pad | 用途 | 状态 |
|---|---|---|
| 8 | `DIO_SYNC_PULSE` | 输出低 |
| 11 | `DEBUG_DIO_SECOND` | **输出高**（RFX2401C 的 TXEN，Phase 8 结论：常开才成功） |
| 15 | `DEBUG_DIO_FIRST` | 输出低 |
| 13 | `RECOVERY_DIO` | 输入（拉低可暂停程序、方便重新烧写） |

DIO4/DIO5 定义了 UART 但代码里未配置（打印走 RTT），DIO6 是空出来的 LED 脚。

## 4. PCM 接收与数据流

### 4.1 CM108B 的 I2S 帧格式（实测得出）

用 5 MS/s 逻辑分析仪抓 CM108B 的 DA* 总线，**欠采样**，靠整段 0.2 s 的边沿总数解混叠：

| 通道 | 观测 | 真实频率 | 结论 |
|---|---|---|---|
| LRCK | 48.002 kHz | 48.002 kHz（低于 Nyquist，无混叠） | 帧同步，50% 方波 |
| SCLK | 1.927935 MHz | **3.072 MHz = 64 × fs** | 位时钟 |
| MCLK | 2.288258 MHz | **12.288 MHz = 256 × fs** | 不接 |
| DATA | 173 kHz、占空比 12.6% | — | 数据 |

→ **一个 LRCK 周期 = 64 SCLK = 2 个 32-bit slot**，每个 slot 里是 16-bit 数据。
按 slot 内边缘密度分析，数据段长 5.2 µs（位置 1.6~27.6）= 16 个 SCLK，
**MSB 延迟 1 个 SCLK** → 标准 I2S（Philips）格式。

⚠️ 实测时用的测试音是**单声道**，所以只有一个 slot 有数据。立体声源两个 slot 都会有。

### 4.2 配置

以下为 CM108B（`PCM_HOST_SEL=0`）配置。16 kHz / 32 BCLK 档（`=1`）用
`WORD_SIZE_16 + FRAME_ALIGN_LAST`；16 kHz / 64 BCLK 档（`=2`）用
`WORD_SIZE_32 + FRAME_ALIGN_LAST + SUBFRAME_DISABLE + PCM_SELECT_MASTER`，见 §14。

```c
#define PCM_CFG_RX  (PCM_SAMPLE_RISING_EDGE |   /* 标准 I2S：下降沿发、上升沿采 */
                     PCM_BIT_ORDER_MSB_FIRST |
                     PCM_TX_ALIGN_LSB |        /* 只用发送侧，保留原厂值 */
                     PCM_WORD_SIZE_32 |        /* 一个 word = 一个声道 slot */
                     PCM_FRAME_ALIGN_FIRST |
                     PCM_FRAME_WIDTH_LONG |    /* LRCK 50% 方波 */
                     PCM_MULTIWORD_2 |         /* 2 × 32 = 64 BCLK = 一个 LRCK 周期 */
                     PCM_SUBFRAME_ENABLE |     /* 每 32 BCLK 一个 FS 沿 */
                     PCM_CONTROLLER_DMA | PCM_DISABLE | PCM_SELECT_SLAVE)
```

选 `WORD_SIZE_32 + MULTIWORD_2` 的原因：几何上正好等于一个 LRCK 周期（64 BCLK），
且一个 DMA word 就是一个声道 slot，解包最干净。
（若用 `WORD_SIZE_16 + MULTIWORD_4`，MSB 会落在 word0/word1 交界处，还得拼位。）

### 4.3 数据流（8 步）

**① PCM 硬件接收** —— `PCM->RX_DATA` 每个 32-bit word = 一个声道 slot，L/R 交替。

**② RX DMA（环形 + 双缓冲）**
```c
Sys_DMA_ChannelConfig(RX_DMA_NUM, DMA_RX_CONFIG,
                      PCM_DMA_BLOCK_WORDS /*96*/, PCM_DMA_HALF_WORDS /*48*/,
                      &PCM->RX_DATA, pcm_buf);
```
`pcm_buf[96]` = 96 word = 48 个 LRCK 周期 = **1 ms**；
**counter 中断在 48 word**（前半块）、**complete 中断在 96**（后半块）→ 每 ms 两次中断。

**③ 中断服务** `Port_rx_raw_dma_isr`（向量 `DMA5_IRQHandler`）

| 中断 | 动作 |
|---|---|
| counter | `Pcm_decode_half(0, 0)` → 填 `left/right_data[0..7]` |
| complete | `Pcm_decode_half(48, 8)` → 填 `[8..15]`，**凑满一个 subframe 才入队** |

双缓冲的意义：任何时刻被读的那半块，DMA 正在写**另一半**。见 §5.3。

**④ 抽取 + 格式转换** `Pcm_decode_half(word_offset, out_offset)`
- `Pcm_unpack_sample(word) = (int16_t)(word >> 15)`
  —— 从 32-bit slot 里取出 16-bit 样本；`>>15` 就是补偿 I2S 那 1 个 SCLK 延迟
- 每个输出采样 = **3 个原始采样** int32 累加后 `/3`（统一增益）
- → **3:1 抽取，48 kHz → 16 kHz**；L/R 按 slot 交替顺序分开

**⑤ 入队 + 启动编码**（每 1 ms 一次）
```c
QueueInsert(&queue_tx[PKT_LEFT], &left_data[0]);   /* malloc 节点 + 拷 32 字节 */
Start_Enc_Lpdsp32_Channel(PKT_LEFT);
```
`Start_Enc_Lpdsp32()`：**memcpy 32 字节（16×int16）到 DSP 的 `Cm2DspBuff0enc`(DRAM5)，
再发 `DSS_CMD_0`**。右声道走 `Cm2DspBuff1enc` / `DSS_CMD_1`。

**⑥ DSP 编完** `DspEnc0_isr` → `StoreDspEncData()`
- `lpdsp_rdy = true`，然后 `Start_Enc_Lpdsp32_Channel(!side)` → **队列自我驱动**（左右交替）
- 从 DSP 输出缓冲拷 **6 字节** 进 `tx_data_fifo[side]`（16 采样 → 6 字节 = 48 kbps），写指针 +6

**⑦ RM 取走** `Read_buffer()`（每 10 ms/路，由 RM 库的定时器驱动）
返回 **60 字节**（= 10 subframe = 160 采样 = 10 ms @16 kHz），读指针 +60 → RM 填 payload 发射。

**⑧ ASRC 不参与** —— 启动链被 `#if (INPUT_INTRF != PCM_RX_RAW_INPUT)` 切断，
`Asrc_out_dma_isr` 从不运行。

### 4.4 速率汇总（CM108B 档；当前 16 kHz/64 BCLK 档见 §14）

| 位置 | 值 |
|---|---|
| PCM 输入 | 48 kHz / 16-bit / 双声道 |
| DMA | 96 word/ms，2 次中断/ms |
| 抽取后 | 16 kHz |
| 编码器输入 | 16 采样/ms/声道 |
| G722 输出 | 6 字节/ms/声道（48 kbps） |
| RM payload | 60 字节 / 10 ms / 路 |

### 4.5 关键尺寸（下表按 CM108B 档；16 kHz 两档见 §14）

| 宏 | 值 | 含义 |
|---|---|---|
| `SUBFRAME_LENGTH` | 16 | 一个编码 subframe 的采样数（= 1 ms @16k） |
| `PCM_DECIM_RATIO` | 3（CM108B）/ 1（两个 16 kHz 档） | 原始采样 → 编码率的抽取比 |
| `PCM_HALF_SAMPLES` | 8 | 半块的输出采样数（每声道） |
| `PCM_HALF_RAW` | 24（CM108B）/ 8（两个 16 kHz 档） | 半块的原始采样数（每声道） |
| `PCM_DMA_HALF_WORDS` / `PCM_DMA_BLOCK_WORDS` | 48 / 96（CM108B）；16 / 32（两个 16 kHz 档） | DMA 半块 / 整块 word 数 |
| `FRAME_LENGTH` | 160 | 一个 RM 帧的采样数（10 ms @16k，出厂值） |
| `ENCODED_SUBFRAME_LENGTH` / `ENCODED_FRAME_LENGTH` | 6 / 60 | G722 编码后字节数 |
| `TX_DATA_FIFO_LENGTH` | 120 | FIFO 深度（2 帧） |

## 5. RM 连接与参数

### 5.1 开机直启 RM（不走 BLE）

`RM_START_AT_BOOT=1`：`App_Initialize()` 里 `RF_SwitchToCPMode()` +
`NVIC_DisableIRQ(BLE_FINETGTIM_IRQn)`，并在**函数末尾**（TX 功率、flash overlay 都设好之后）
调 `RM_Enable(1000)`。

BLE 仍然初始化但不参与：`Connection_SendStartCmd()` 顶部直接 `return`
（它是扫描/定向连接的唯一入口，4 处调用），否则 BLE 会自动发起连接和 RM 抢射频。

TX 是**单向广播**，不需要 RX 连接/应答；1644 自己进 RM 搜索。

### 5.2 RM 参数（必须与 1644 一致）

| 参数 | 值 | 说明 |
|---|---|---|
| `accessword` | `0x00cde629 \| (0xf2 << 24)` = **0xF2CDE629** | 1644 侧 = `(stream_addr << 8) \| 0x29`，默认 `stream_addr = 0xF2CDE6` |
| `RM_HOPLIST` | `{3, 9, 15, 21, 24, 33, 36}` | 与 `peripheral_server_sleep` 一致 |
| `numChnlInHopList` | 7 | |
| `role` | `RM_MASTER_ROLE` | |
| `audio_rate` / `radio_rate` | 48 / 2000 | |
| `OUTPUT_POWER_6DBM` | **0** | **本板硬约束**：6 dBm 会复位（§7.1）。宏在 `app.h`，`App_Initialize` 里早于 `RM_Enable` 生效 |
| `debug_dio_num[0..3]` | **全 `0xff`** | DIO11 是 FEM TXEN，不能被 RM 库当调试输出占用 |

## 6. 调试开关与探针

### 6.1 开关（`include/app.h`）

| 开关 | 当前 | 作用 |
|---|---|---|
| `PCM_HOST_SEL` | 2 | PCM 主机三选一：0 = CM108B 48k；1 = 16k/32BCLK；2 = 16k/64BCLK（见 §14） |
| `PCM_USE_ASRC` | 自动 | 由 `PCM_HOST_SEL` 派生，仅档 2 = 1：PCM 样本先经 ASRC 15.625k→16k 再进编码器（§14.6） |
| `RM_START_AT_BOOT` | 1 | 开机直启 RM；0 = 原厂 BLE 流程 |
| `RM_TEST_TONE` | 0 | 1 = 直接把 `coded_sample[]`（原厂预编码数据）灌进 RM 载荷，绕过输入和编码器 |
| `TX_TONE_TEST` | 0 | 1 = 把 1 kHz 表送进**真编码器**，绕过 PCM 输入 |
| `PCM_HW_OFF` | 0 | 1 = 完全不配置 PCM 外设、四个 pad 设为普通高阻输入（用于判定硬件问题） |
| `TX_DBG_PRINT` | 0 | 1 = 每 200 ms 打完整报告；**0 = 只在异常时打 ALARM**（见 §5.3） |

### 6.2 计数器（非阻塞，可用 J-Link 按符号名读）

| 符号 | 位置 | 正常值 |
|---|---|---|
| `dbg_cnt_pcm_isr` | PCM DMA ISR 顶部 | ~2000/s（2 次/ms） |
| `dbg_cnt_enc` | `DspEnc0_isr` | ~1000/s |
| `dbg_cnt_asrc_out` | `Asrc_out_dma_isr` | 档 2：**~32000/s**（两声道 16 kHz）；档 0/1：**0**（ASRC 不走） |
| `dbg_pcm_peak` | `Pcm_decode_half` | 播 1 kHz 时几百~几千 |
| `ptr_rst_cnt` | `Read_buffer` 重定位分支 | FIFO 太满/太空的次数，每次是一声不连续 |
| `dbg_q_alloc_fail` | `QueueInsert` 分配失败分支 | 堆不够、丢包次数 |
| `dbg_cnt_tx_req[2]` / `dbg_len_last` | `RM_Callback_TRX` | ~100/s 每路，`len`=60 |
| `dbg_cnt_status` / `dbg_last_status` | `RM_Callback_StatusUpdate` | 状态=0(DISCONNECTED)/1(FAIL)/2(ESTABLISHED) |

### 6.3 三层验证法（本次最有效的手法）

后级不明时，**先用已知数据把后级锁死，再攻输入**：

1. `RM_TEST_TONE=1` → 预编码数据直接进 RM 载荷 → 验证 **RM 发射 + 1644 播放**
2. `TX_TONE_TEST=1` → 1 kHz 表进真编码器 → 验证 **LPDSP32 编码 + FIFO + RM**
3. 都通了再开 PCM 输入 → 此时变量只剩 PCM 那一层

这次就是靠它发现「编码器以后全通」，从而把问题范围从六层缩到一层。

### 6.4 打印会破坏采样（观测者效应）

200 ms 一行 ~90 字符的串口打印约占 CPU **数 ms**，而 PCM 中断的余量只有 **0.5 ms**
→ DMA 转过一圈覆盖了没读到的半块 → 偶发噪声。

**纯音测试时看不出来**，因为那时 PCM 分支是空的，CPU 被占住不会坏任何采样。
所以音频路径跑起来后不要用阻塞打印，改用非阻塞计数器 + J-Link。

## 7. 排查历程（踩过的坑）

| # | 现象 | 根因 | 处理 |
|---|---|---|---|
| 1 | 完全没声（连预编码数据也没声） | `Asrc_out_dma_isr` 与 PCM 路**共用 `queue_tx`**，两个生产者互相踩 | PCM 路不走 ASRC，直接喂编码器队列（参考工程 DMIC 也没有 ASRC） |
| 2 | 偶发"滋滋" | 环形 DMA 用整块中断，ISR 读到 DMA 正在覆写的那半块 | 恢复原厂 counter 中断双缓冲（§4.3③） |
| 3 | 信号进不来（`pcm=0`） | **DIO0 硬件缺陷**（§3.1） | 换 DIO9 |
| 4 | 换成 PCM 输入就"滋滋" | 阻塞打印占 CPU → PCM 中断被推迟 → 采样被破坏（§6.4） | 打印默认关闭，只在异常时打 |
| 5 | 链接报 `audio_sink_phase_cnt` 重复定义 | 原厂遗留：`rm_app.c` 的 `/*for test */` 变量与 `app_func.c` 的 ASCC 变量同名；选 SPI_CODED 时 app_func.c 那段被 `#if` 排除所以不撞 | 删掉 `rm_app.c` 那份（它从未被引用） |
| 6 | 声音不对（用原厂 `coded_sample` 测） | 那张表的频率/编码模式未知 | 改用自造 1 kHz 表走真编码器（`TX_TONE_TEST`） |
| 7 | DIO1 可能成为"打架点" | `Sys_PCM_ConfigClk` 无条件把 sero 配成输出，而收音频用不到 | 之后立刻改回高阻输入 |
| 8 | 接上 7100 后开机 ~10 s 被复位、反复重来 | **6 dBm 的 TX 电流尖峰 + 7100 的负载叠加，把供电拉过阈值**（§7.1） | `OUTPUT_POWER_6DBM` 固定 0（0 dBm） |

### 7.1 复位专题：本板必须留 0 dBm（2026-10-15 定位）

**现象**：接上 7100 后，RSL10 在 RM 第一包发射的那一刻（`RM_Enable` 后约 1 ms）被复位，
banner 打到一半就断，反复重来。`[RST] st=` 每轮都是 `1` —— 只置 ACS 位，
**bit3 LOCKUP / bit2 WDT / bit1 SW 恒为 0** ⇒ 不是 CPU 跑飞、不是看门狗，是**供电域复位**。

**决定性对照**（三格把变量夹死）：

| 接 7100 | `OUTPUT_POWER_6DBM` | 结果 |
|---|---|---|
| 接 | 1（6 dBm） | **必复位** |
| 接 | **0（0 dBm）** | **正常** |
| 拔 | 1（6 dBm） | 不复位 |

⇒ 触发条件是「**6 dBm 的发射电流尖峰**」与「**7100 带来的负载/链路**」**同时**存在，缺一不可。

**机制（推断，未用仪器确认）**：本板供电余量不足 —— 原厂配置已把 `VDDPA` 关掉、
PA 改由 **VDDRF** 供（`App_Initialize` 里 `ACS_VDDPA_CTRL` 那两行），第一包 6 dBm 的
尖峰叠上 7100 的电流，把电源拉到阈值以下 → ACS 域复位。**现象是实测的，机制是推的**
（见下方"未做的确认"）。

**处理**：`OUTPUT_POWER_6DBM` 固定 **0**。宏定义处（`include/app.h`）与
`app_init.c` 末尾 `RM_Enable(1000)` 处都留了警告注释，别再改回 1。

**已排除的（省得下次重走）**：

- **不是 flash overlay** —— 那是"7100 I2C 读全 0"的根因，另一条线，已单独删掉。
- **不是"RM 第一包 TX 本身"** —— RM 发射全程中断驱动（`TIMER0`→`RM_TransmitPacket`、
  `TIMER1`→回调重填 payload、`RF_TX_IRQHandler`→拼 header + DMA 进 TX FIFO），
  `main()` 卡在 `app.c` 的 DIO13 握手死等时**照样发射**；"拔 7100"那次就是这么发出去的，
  没复位。所以不能只怪 RF。
- **不是 CPU 故障 / 看门狗** —— `st` 的 LOCKUP / WDT / SW 位恒 0。
- **不是 DMA 通道撞车** —— RM memcpy=ch0、ASRC 存取=ch2、ASRC in/out=ch3/4、
  PCM RX=ch5、TX=ch6（`include/app.h`），互不重叠。
- **不是 `Read_buffer()` 越界** —— FIFO 恒空时走"重定位读指针"分支，
  `data_rd_idx` 始终夹在 `[0, TX_DATA_FIFO_LENGTH)`，返回指针永远在 `tx_data_fifo` 内。

**⚠️ 对照表的读法注意**：「拔 7100」那一格同时少了两个变量 —— 7100 的**负载**，
以及**音频链路活动**（没有 PCM 时钟 ⇒ ASRC/AUDIOSINK/LPDSP32 都不跑），
而且那次 `main()` 还没进 RM 主循环。所以"6 dBm 是关键变量"这一条是硬的，
"省电/省元件就能救回 6 dBm"还不能这么下结论。

**未做的确认（若要彻底闭环）**：没有读 `ACS_RESET_STATUS`（@`0x40001354`，**整字读**；
pack 只给了位带视图）。它能把"供电跌落"和"外部有人拉复位脚"分开：
bit9 `PAD` = 有人在拉 RSL10 的 NRESET；bit8 `POR` / bit10 `VDDC` / bit11 `VDDM` /
bit12 `VDDA` = 供电域，其中 **bit12 直指模拟·RF 电源**（RM 一开就掉 ⇒ PA 电流）。
清位在 bit0..6（写 `0x7F` 全清），粘滞、上电不清。**若换板/换供电方案后想确认还有多少余量，
这是最便宜的仪器。**

**对产品的含义**：0 dBm 是固件侧的唯一选择。若整机要求更高发射功率，
只能从硬件侧找余量（去耦电容 / LDO 档位 / 供电走线 / 7100 单独供电），**固件无解**。

## 8. 已验证 / 未验证

### ✅ 已上板验证

- **RM 开机直启 + 链路**：1644 能收到
- **编码器 → RM → 1644 播放**：`TX_TONE_TEST=1` 时 1644 出**干净的 1 kHz 纯音**（端到端锁定）
- **PCM 输入能出声**：CM108B 播音频，1644 能听到
- CM108B 单独供电时 LRCK 正常（未被此前的电平对顶打坏）
- RSL10 VDDO = 3.3 V
- **供电/功率上限**：接 7100 时 6 dBm 必复位、0 dBm 正常；拔掉 7100 时 6 dBm 也不复位（§7.1）

### ❌ 未验证 / 未定论

- **第三台 PCM 主机（16 kHz / 64 BCLK，§14）**：代码已加（含 ASRC 15.625k→16k 重采样），
  未编译、未上板
- **ASRC 在 PCM 路径上的复用**（§14.6）：机制与 SPI 输入路径相同，但两路时钟比例、
  `PCM_USE_ASRC` 固定相位增量都未经过板上验证；`ptr_rst_cnt` 不增长才算通过
- **设备侧能否接受 15.625 kHz FS**（§14.7）：按 A 类（采样由 LRCK 决定）判定"目前可行"，
  但未拿到器件规格书、未上板；FS 自动检测与 FS 容差两项待确认
- **CM108B 档在 `PCM_HOST_SEL` 重构后未再验证**（`e14c652` 起）
- **复位的机制**（"6 dBm 的电流尖峰把供电拉过阈值"）是**推断**：未读 `ACS_RESET_STATUS`、
  未用示波器看电源轨。三格对照（6 dBm↔0 dBm、接↔拔）是实测的（§7.1）
- 6 dBm 究竟差多少供电余量未测 —— 不知道加多大去耦电容 / 换哪档 LDO 能救回来（§7.1）
- **长时间播放会变差，重启 RSL10 就好** —— 根因未定论，见 §9
- `>>15` 那一位（I2S 的 1 SCLK 延迟）是从数据段长度反推的，未直接观测
- 3:1 抽取的增益（选了统一增益 `/3`）未做电平标定
- 立体声源的表现（实测时只有单声道音源）

## 9. 已知问题：播放一段时间后音质变差

**现象**：播着播着声音逐渐变差，**重启 RSL10 即恢复** → 典型的**状态累加**。

两个候选：

### A. 时钟漂移（优先怀疑）

- **生产**由 CM108B 的晶振驱动（它出 LRCK/SCLK），**消费**由 RSL10 的射频时钟驱动
  （RM 每 10 ms 取 60 字节）——**两个时钟不同源**
- 失配 ~20 ppm → 0.32 采样/秒 → 0.12 字节/秒 → 漂 60 字节（一个包）约 **500 秒 ≈ 8 分钟**
- FIFO 初始水位 ≈ 60 字节（一个包延迟），触发重定位的门槛是 `< 6 字节`
  → 余量 ~54 字节 → 约 **7.5 分钟**后开始持续重定位 → 从"偶尔一声"变成**持续变差**
- **重启清空 FIFO、水位回中位 → 恢复** ✓ 完全吻合

**这正是"不走 ASRC"的代价**：ASRC 的本职之一就是跟踪这个漂移、平滑地补/丢采样。

### B. 堆碎片 / 耗尽

`QueueInsert` 每 ms 每声道 malloc 一个节点（2000 次/秒），链接脚本无固定 heap 上限
→ 碎片化到某点开始失败 → `QueueInsert` **静默丢包** → 编码器断粮 → 变差；重启清堆 → 恢复。

### 判别器（已就位）

`TX_DBG_PRINT=0` 时只在异常时打：

```
[TX] ALARM rst=<n>(+<增量>) qfail=<n>(+<增量>) | pcm=.. pk=.. enc=..
```

- 布防期 5 s（吸收上电时 FIFO 初次填满的那次合法重定位），之后**健康就完全不打**
- **`rst` 增量涨得快** → A 漂移
- **`qfail` 增量在涨** → B 堆
- **都 0 但声音确实变差** → 另有原因（否证结果同样有用）

第一次 ALARM 出现的**时刻**也是判据：刚过 5 s 就出现 = 稳态不匹配；几分钟后 = 慢漂移。

⚠️ ALARM 一旦开始会每 200 ms 打一行，而这个打印本身又会引入杂音，
所以**第一行是有信息量的那一行**。

### 若确认是 A 的修法

把 **ASRC 接回来**。第一版就是那么做的（3:1 抽取 → 喂 ASRC →
`Cr = audio_sink_cnt / 3` → ASRC 输出进队列），当时"不工作"的三个原因现在都已找到并修掉：
DMA 缺双缓冲、DIO0 是坏的、阻塞打印毁采样。所以那一版**很可能本来就是对的**。

注意：ASRC 路径必须是**唯一**的生产者（要移除现在这条直接喂队列的路）。

## 10. 关键文件清单

| 文件 | 本次改动 |
|---|---|
| `include/app.h` | 输入接口切到 PCM、`PCM_CFG_RX`、尺寸宏、RM 参数、5 个调试开关、探针 extern |
| `code/app_init.c` | 早期 pad 高阻、PCM 初始化 + `PCM_HW_OFF` 分支、DMA 双缓冲配置、开机直启 RM、DIO11 常开、RM_Enable 移到末尾 |
| `code/app_func.c` | `Pcm_unpack_sample` / `Pcm_decode_half` / `Tone_feed_encoder`、ISR 半块双缓冲、ASRC 启动链切断、计数器 |
| `code/rm_app.c` | `RM_TEST_TONE` / `TX_TONE_TEST` 分支、`debug_dio_num` 全 0xff、删重复变量 |
| `code/app_process.c` | 200 ms 报告 / ALARM |
| `code/queue.c` | 分配失败计数 |
| `code/ble_std.c` | `RM_START_AT_BOOT` 时不发起 BLE 连接 |
| `code/dsp_pm_dm_enc.c` | LPDSP32 编码程序（原厂，未改） |

参考资料（工程目录下未跟踪）：
`C371347_音频接口芯片_CM108B_规格书_WJ83360.PDF`、`CM108B_SCHEMATIC_DEMOBOARD_V1.3.pdf`

相关文档：`docs/tx_coex/TX_COEX_DEV_LOG.md`（参考工程 TX 侧 12 个阶段的演进）、
`docs/pcm/pcm_config_final.md`（PCM 字段语义的实测结论，本文多处引用）

## 11. 待办

1. **上板验证第三档 PCM 主机 + ASRC（§14）**：先确认 `pcm_buf` / `dbg_pcm_peak` / DMA
   中断计数，再确认 `ASRC->PHASE_INC = 0xFF400000` 且 `ptr_rst_cnt` 不再增长（§14.5）
2. **判定"播一会儿变差"的根因**：烧当前固件，等 ALARM 第一行，看 `rst` / `qfail`
3. 若为漂移 → 把 ASRC 接回来（§9）
4. 清理临时脚手架：5 个调试开关、8 个计数器、打印、`Tone_feed_encoder`
5. `>>15` 位对齐做一次确认（换 `>>16` 对比听感）
6. 用立体声源和不同频率完整测一遍音质
7. **若整机要求 >0 dBm**：只能从硬件侧找供电余量（去耦电容 / LDO 档位 / 供电走线 /
   7100 单独供电）；固件侧已锁死 0 dBm（§7.1）
8. 提交

## 12. 构建注意

- 改动**未提交**，编译前确认烧的是当前工作区
- `.cproject` 的十几行改动是 Eclipse 自己写的（多一处 `storageModule`、
  几处 `useByScannerDiscovery`），**没有加任何 `-D` 或源文件**，不影响编译
- Windows 下源码是 CRLF，git 会按 LF 存储，提交时有换行警告属正常

## 13. 支线：改让 7100 当 PCM 主机（2026-10-16，结论：**放弃**）

### 13.1 动机

主线的音源是 CM108B（USB 音频芯片）。支线想换成**板上那颗 Ezairo 7100**：
让 7100 从它自己的 mic/ADC 取音，从它的 PCM 口发出来给 RSL10 收
（RSL10 仍是 PCM 从机，只是换了个主机）。

### 13.2 结论（一句话）

**RSL10 这一侧到此为止**。本次所有"找数据脚"的实验都建立在一套 **没在硬件上验证过的脚位映射**上
（见 §13.9），所以"三根脚都没音频"**不足以判定 7100 没在发**。靠 RSL10 侧能独立拿到的结论只有两条：
①有较大把握认为 7100 的 PCM **是开着的、且是 master**（RSL10 是从机 ⇒ 总线时钟只能来自对面）；
②按手册，"发不发、发什么"只能由 **7100 自己的 application** 决定（§13.4）。
⇒ 剩下的问题都指向 7100 侧；RSL10 侧再换脚、再扫脚不解决问题。

### 13.3 已做的实验与结果（全部上板实测）

| # | 实验 | 结果 | 判定 |
|---|------|------|------|
| 1 | 去掉 118 步 7100 引导 + 开机握手，只留 I2C 心跳 | PCM 时钟照旧在跑（DMA ISR ≈1487/s、编码器 ≈743/s ⇒ ≈24 ksample/s、FS≈12 kHz、32 BCLK/帧）；心跳 `{0x88,0x01}`→从机 0x02 照旧每 5 s 一次（`dbg_hb_cnt` 涨、`dbg_hb_fail`=0） | 引导/握手**不是**时钟和 I2C 的来源 |
| 2 | `Pcm_seri_apply()` 依次把 SERI 指到 DIO9/10/11，每根测 6 s（mic 一直放 1 kHz 纯音） | 9：样本**恒 0x0000**（1.5 s 一个 ≤40 的小毛刺）；10/11：样本**恒 0xFFFF**（`dbg_pcm_peak_max`=1） | 三根脚都**没有音频** |
| 3 | 上下拉驱动判别（`/d/tmp/pulldrv.jlink`）：DIO9 改**强上拉**、DIO10/11 改**弱下拉** | DIO9 顶着强上拉仍**恒低**（CFG[9]=0x33E 回读核对过）；DIO10/11 顶着弱下拉仍**恒高**（CFG=0x23E 回读核对过） | 两根都是被**主动驱动**，不是悬空 |
| 4 | 读 RSL10 自己的 PCM 配置 | `Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, …)`（`app_init.c:280`），且 SER_DO 被改回高阻（`app_init.c:294`）：**RSL10 什么都不驱动** | 总线上的 BCLK/FS **一定来自对面** |

**⚠️ 反例 / 保留**：实验 3 没有"已知悬空脚"做阳性对照 —— CFG[13]/CFG[15] 是 0x313F
（disable + 强上拉）却读 0，说明这套读法本身有解释不清的地方。所以"被主动驱动"是**强证据但不是铁证**。

### 13.4 手册证据（Ezairo 7100 Firmware Reference Manual，M-20785-018）

手册 PDF 放在 `docs/firmware_reference_ezairo7100(1).pdf`。与本题相关的三条：

1. **PCM 只有一个使能位**（14.231 `Sys_PCM_Config`）：`PCM_CFG_ENABLE` 管整口
   （`Sys_PCM_Enable/Disable` 只是开关这一位），方向是 `PCM_[MASTER|SLAVE]`，
   发送/接收**只有中断使能位**（`PCM_RX_INT_*` / `PCM_TX_INT_*`），**没有单独的"只开 RX"开关**；
   数据来源由 `PCM_DATA_MGT_[IOC|CFX]` 决定。这一节的范例原文就是
   *"Configure the PCM interface as a slave receiver"*
   （`PCM_SLAVE | PCM_ENABLE | PCM_RX_INT_ENABLE | PCM_TX_INT_DISABLE | PCM_DATA_MGT_CFX …`）。
   ⇒ **"只配了输入"在 7100 侧的写法就是：PCM 开着、四根脚都分配好，但从来不往 TX 送数据** ——
   发送脚仍在被 PCM 驱动，输出恒定值（TX 寄存器为 0 就是恒低）。
2. **四根脚是一次分配的**（14.79 `Sys_DIO_Config_PCM(cfg, mode, clk, frame, seri, sero)`），
   分配 ≠ 有数据。发送数据要么由 CFX 程序写 TX 寄存器，要么由 **IOC** 路由进来
   （14.199 `Sys_IOC_OutputConfig(IOC_OUTPUT_CFG_PCM0_*)`、14.200 `Sys_IOC_PCMInputConfig`、
   14.5 `Sys_AudioMux_Config`）。⇒ **"mic/ADC → PCM 输出"这条路由要 7100 的程序自己配。**
3. **7100 的调试口就是一个 I2C 从机**（14.69–14.72）：7-bit 地址可编程（示例 0x50/0x55/0x5A）、
   `ACCESS_[RESTRICTED|UNRESTRICTED]`、`AUDIO_STREAM_[ENABLED|DISABLED]`，
   "外部系统可通过调试口切 application"。协议在另一本
   **《Communication Protocols Manual for Ezairo 7100》**里，**这本没有**。
   另有 CM3 侧的独立 PCM 块（`Sys_C_PCM_*`，14.17/14.18）。

### 13.5 推断（未上板证实，标注清楚）

- **时钟来自 7100**：依据 ①RSL10 是从机（§13.3 第 4 行）②时钟形态（32 BCLK/帧、每帧 2×16bit）
  与 7160test 家族里 7100 当 master 时的形态一致 ⇒ 7100 的 PCM **是开着的、且是 master**。
- **DIO9 恒低 = 7100 的 sero 被分配、但没人给它数据**（发送端输出数字 0）。
- DIO10/11 恒高 = 被 7100 的另一个数字功能停在高电平（另一个 PCM 口 / CM3 的 `C_PCM` / GPIO）。

⚠️ 上面后两条都建立在"本次实验那套脚位映射是对的"这个前提上；该前提本身没验证，见 §13.9。

### 13.6 若要重启这条线

按性价比排序：

1. **问写 7100 程序的人**三句话：PCM 是 master 还是 slave？`PCM_DATA_MGT` 选 IOC 还是 CFX？
   sero 分配在哪根脚？有没有把 mic/ADC 经 IOC 路由到 PCM0？—— **这一条能直接定生死**。
2. 要那本《Communication Protocols Manual for Ezairo 7100》，看能不能用现有 I2C
   （A7 协议，见 `docs/7100_A7协议.md`）**直接写 7100 的 PCM/IOC 寄存器**。
   顺带值得查：调试口的 `AUDIO_STREAM_ENABLED` 模式怎么开、数据从哪取 —— 能开的话
   也许不用 PCM，RSL10 直接从 I2C 收音频。
3. 扫脚的备选：本家族 PCM 数据脚是 **DIO4（入）/ DIO14（出）**（7160test、rx_raw、rx_rawtest、
   rx_coex、rx_coex_1664、asha 六个工程一致），本次只扫了 9/10/11，**DIO4 没试过**。

### 13.7 复现资料

- 手册文本：`/d/tmp/ez7100_fw.txt`（`pdftotext -layout` 抽的，可直接 grep）
- J-Link 脚本：`/d/tmp/sweep911.jlink`（扫 SERI 9/10/11）、`/d/tmp/pulldrv.jlink`（上下拉判别，
  日志 `/d/tmp/pulldrv.log`）
- 关键地址：`dbg_seri_pad`=0x20000018（改它切 SERI，白名单 {9,10,11} 在 `Pcm_seri_apply()`）；
  计数器 `dbg_cnt_pcm_isr`=0x2000064c、`dbg_nz_cnt`=0x20000650、`dbg_pcm_peak_max`=0x200006d8、
  `dbg_hb_cnt`=0x200008bc、`dbg_hb_fail`=0x200008c0
- J-Link 注意：数字按**十六进制**解析；写目标要 `h` → `w4` → `g`（`w4` 带校验会被固件自增撞掉）
- 本次实验的代码快照：`/d/tmp/tx_coex_pcm_7100_abandon_20261016.patch`（5 个改动文件）
  + `/d/tmp/tx_coex_pcm_7100_newfiles/`（新增的 3 组文件）
- 早先的 WIP：`git stash@{0}`（"tx_coex_pcm 7100 WIP (bisect 起点 e14c652) 2026-10-15"）
  + `/d/tmp/tx_coex_pcm_7100_wip_20261015.patch`

### 13.8 代码归宿

本节所述改动**全部回退到 `e14c652`**：`include/app.h`、`code/app_init.c`、`code/app_func.c`、
`code/app_process.c`、`app.c` 五处 `git checkout` 回退；新增的 `code/i2c_7100_hal.c`、
`code/dsp_7100_init.c`、`code/dsp_7100_init_tables.c`、`include/i2c_7100_hal.h`、
`include/dsp_7100_init.h` 移出工程（快照见 §13.7）。
→ 要捡起来：`git apply /d/tmp/tx_coex_pcm_7100_abandon_20261016.patch` 再把 §13.7 的新增文件拷回。

已核对：回退后 `git diff e14c652 -- remote_mic_tx_coex_pcm` 为空，工程里没有残留引用
（`dsp_7100_init` / `i2c_7100_hal` / `Pcm_seri_apply` / `dbg_seri_pad` 全部搜不到）。

### 13.9 ⚠️ 脚位映射本身没锁死（这条最该记住）

回退后的 HEAD（`e14c652`）里，生效的是**另一套**脚位（`app.h:395-404`）：

```c
#define PCM_SER_DI     2      /* 数据：来自主机 */
#define PCM_CLK_DO     3      /* 位时钟 */
#define PCM_FRAME_SYNC 10 / 9 /* 帧同步：16k 主机=DIO10 / CM108B=DIO9 */
// 注释原文："The data and clock stay on DIO2/DIO3 on both hosts;
//            only the frame sync moves"
```

而本次 7100 分支把它改成了 `PCM_CLK_DO=2 / PCM_FRAME_SYNC=3 / PCM_SER_DI=9`
（来自家族约定 BCLK=DIO2/FS=DIO3 + 自己猜的数据脚 9）。三套说法互相冲突：

| 数据脚 | 出处 | 依据 |
|--------|------|------|
| DIO2 | HEAD `app.h` 的注释（本板） | "两主机数据/时钟都在 DIO2/DIO3" |
| DIO9 | 本次 7100 分支 | 我们自己引入的假设，**无实测** |
| DIO4 | 1664 家族（`PCM_SER_DI`=4） | 六个工程一致，但那是**另一块板** |

两条后果：

1. **本次"扫 DIO9/10/11 找数据"很可能扫的是帧同步线** —— 按 HEAD 语义，DIO9/DIO10 正是两台主机
   各自的 FS；实验 3 量到的"被主动驱动"（9 恒低、10 恒高）也正好符合"FS 停在固定电平（主机没在跑）"。
2. 但反过来说，本次量到的速率是**自洽**的（12 kHz FS × 每帧 2 word = 24 ksample/s），
   这又偏向"时钟/帧同步确实在 DIO2/DIO3"（否则 RSL10 的时钟脚接在数据线上，根本不该有时钟）。
   ⇒ **不量通断就定不了论。**

**下次开工第一件事：万用表量通断，把数据脚钉死，再谈其它**（`DIO0` 是本板唯一实测确认坏的 pad）。

## 14. 第三台 PCM 主机：16 kHz / 64 BCLK（2026-10-29）

原先 `app.h` 只有二值开关 `PCM_HOST_16K`（1 = 16 kHz/32 BCLK，0 = CM108B）。本次改成
三值 `PCM_HOST_SEL` 并新增第三档，当前生效 = **2**。

### 14.1 三档对照

| 档 | 宏 | 值 | 时钟角色 | FS（LRCK） | BCLK/帧 | 时隙 | 抽取比 | 解包移位 | FS 脚 |
|---|---|---|---|---|---|---|---|---|---|
| CM108B | `PCM_HOST_CM108B` | 0 | RSL10 从机 | 48 kHz | 64 | 2 × 32-bit | 3 | `>>15` | DIO9 |
| 16 kHz / 32 BCLK | `PCM_HOST_16K_32BCLK` | 1 | RSL10 从机 | 16 kHz | 32 | 2 × 16-bit | 1 | 0 | DIO10 |
| **16 kHz / 64 BCLK** | `PCM_HOST_16K_64BCLK` | **2（当前）** | **RSL10 主机** | **15.625 kHz（目标 16 kHz）** | 64 | 2 × 32-bit | 1 + **ASRC 1.024×** | `>>15` | **DIO9** |

数据 / 时钟三档都是 DIO2 / DIO3；DIO9 由档 0 与档 2 共用（编译期互斥，不能同时接两台）。

### 14.2 新主机参数

| 项 | 值 |
|---|---|
| 设备角色 | **SLAVE**，SCLK / LRCLK 均由外部 MASTER（= RSL10）产生 |
| 目标 FS（LRCK） | 16 kHz，占空比 50% |
| 目标 BCLK | 64 × FS = 1.024 MHz |
| 帧格式 | 每帧 64 BCLK = 2 声道 × 32 位时隙 |
| 数据格式 | Philips 标准 I2S，LRCK 跳变后 1 bit 延迟 |
| 数据位宽 | 16 bit（高 16 位有效，低 16 位时隙可忽略） |
| 位序 | MSB first |
| 声道映射 | LRCK 低 = 左，LRCK 高 = 右 |

**设备是从机，所以 RSL10 必须做 PCM 主机**（`PCM_SELECT_MASTER`），时钟不是从外部收的。

### 14.3 ⚠️ RSL10 做主机的时钟限制：实际 FS = 15.625 kHz

RSL10 的 PCM_CLK 对 PCM 模块**永远是输入**，主机模式只能把内部时钟从同一个 DIO 回灌
（硬件手册 §11.5）。可用的内部时钟是 USRCLK = SYSCLK / N，N 为 **1~4096 的整数**
（§6.3.6；SAI 驱动的主机模式也是这么做的：pad 设 `USRCLK_OUT` + 整数预分频）。

本工程 SYSCLK = 48 MHz / 3 = **16 MHz**（`app_init.c` 的 `CK_DIV_1_6_PRESCALE_3`）：

| 目标 | 需要的分频 | 结果 |
|---|---|---|
| 1.024 MHz（精确 16 kHz） | 16 / 1.024 = **15.625** | ❌ 非整数，做不到 |
| **N = 16（本次采用）** | 16 MHz / 16 = **1.000 MHz** | ✅ FS = **15.625 kHz**（−2.34%） |
| N = 15 | 15 → 1.0667 MHz | ✅ FS = 16.667 kHz（+4.2%），更差，未采用 |

EXTCLK（如 12.288 MHz）也不能用：手册明确 **EXTCLK 不能在有 RF 活动时做 SYSCLK**，
本工程跑 BLE + RM。⇒ 要精确 16 kHz，只能让设备侧当 master，或板上另加时钟源；
当前按方案 C 接受 15.625 kHz，并用 ASRC 把它重采样到 16 kHz（§14.6）。不做重采样
会怎样：编码器产出 5.859 B/ms，而 RM 每 10 ms 固定取 60 B（6 B/ms），FIFO 约 0.4 s
掏空一次，`Read_buffer()` 每次重定位丢掉约 9 ms 音频 —— 听到的是周期断音，不只是变调。

### 14.4 实现

- `PCM_ROLE`：档 2 = `PCM_SELECT_MASTER`；档 0/1 = `PCM_SELECT_SLAVE`。
- `PCM_CFG_RX`：`WORD_SIZE_32 + MULTIWORD_2`（2 × 32 = 64 BCLK）、
  `FRAME_ALIGN_LAST + SUBFRAME_DISABLE`（手册 I2S 行的写法；主机模式必须用它才能生成
  标准 I2S 的 LRCK）、`PCM_SAMPLE_RISING_EDGE`。
- `PCM_UNPACK_SHIFT = 15`：1 bit 延迟让 16-bit 样本落在 32-bit 时隙的 bit30..15。
- `PCM_DECIM_RATIO = 1`。派生出 `PCM_HALF_RAW=8`、`PCM_DMA_HALF_WORDS=16`、
  `PCM_DMA_BLOCK_WORDS=32` → 一块 = 16 个 LRCK ≈ 1.02 ms，DMA 中断仍 2 次/ms。
- `PCM_FRAME_SYNC = 9`：主机模式下该脚由 RSL10 输出 LRCK（`DIO_MODE_PCM_FRAME`）。
- `PCM_CLK_DO = 3`：主机模式下由 `DIO_MODE_USRCLK` 输出 1.000 MHz，同时作为 PCM 时钟输入。
- USRCLK 预分频在 `App_Initialize()` 的 `CLK->DIV_CFG0` 里按档切换（字段值 `0xF` = /16）。
- 声道映射由 `Pcm_decode_half()` 的取词顺序直接满足：偶数 word → `left_data`（LRCK 低），
  奇数 word → `right_data`（LRCK 高）。
- **ASRC（仅档 2）**：`PCM_USE_ASRC = 1`。PCM DMA 中断解出的 subframe 不再直接进
  `queue_tx`，而是写入 `asrc_in_buf` 左右两半后启动 ASRC；`Asrc_out_dma_isr` 每凑满
  16 个输出样本喂一次编码器（与 SPI 输入路径同一套机制）。`MEMCPY_SAVE_STATE_MEM`
  状态保存/恢复链在这一档打开（原来只给 SPI 输入开）。
- ASRC 相位增量是**固定值** `PCM_ASRC_PHASE_INC` = `-0x00C00000`（`0xFF400000`），
  即 15.625 kHz → 16 kHz 的 1.024：PCM 主机时钟和 RM 帧都来自同一个 48 MHz 晶振，
  比例是精确已知的，不需要 AUDIOSINK 测量（14.6）。

### 14.5 验证状态

**代码已改，未编译、未上板。** 上板检查项：

1. 逻辑分析仪：DIO3 出 **1.000 MHz** BCLK，DIO9 出 **15.625 kHz**、50% 方波 LRCK；
2. `pcm_buf` 偶数索引字应为光滑正弦（1 kHz 输入 ⇒ 15~16 采样一周期），确认 `>>15` 取对半区；
3. `dbg_pcm_peak` 不恒为 1（恒 1 ⇒ 取错半区，同 `WORD_SIZE_16` 那次的教训）；
4. `dbg_cnt_pcm_isr` ≈ **2000/s**（每 subframe 2 次）；
5. **ASRC 是否真的在跑**：`ASRC->PHASE_INC` 应为 **`0xFF400000`**（−0x00C00000）；
   `dbg_cnt_asrc_out` ≈ **32000/s**（每输出样本一次，两声道共 32 kHz）；
   `dbg_cnt_enc` 两路各 ≈ 1000/s；
6. `Read_buffer()` 的 `ptr_rst_cnt` 应**停止增长**，`w2r` 稳定在 60 附近而不是缓慢下降 ——
   这两个指标是"速率匹配"的直接证据；若 `ptr_rst_cnt` 仍每 ~0.4 s 加一，说明 ASRC 没有
   把输出拉到 16 kHz；
7. 音调：ASRC 生效后，设备按 15.625 kHz 采到的 1 kHz 输入在 1644 上应仍是 **1 kHz**
   （重采样恢复 16 kHz 时间基）。若听到约 976 Hz，说明 ASRC 输出仍是 1:1。
8. 原两档本次只改宏名/配置宏，逻辑未动：档 1 曾上板验证；档 0 自 `e14c652` 起仍未验证。

### 14.6 为什么必须重采样（速率链条）

| 环节 | 16 kHz 正常档 | 15.625 kHz 档（无 ASRC） |
|---|---|---|
| PCM 输入 | 16 000 样本/s/声道 | 15 625 样本/s/声道 |
| 编码器产出 | 6 B/ms | **5.859 B/ms** |
| RM 取数（`Read_buffer`） | 6 B/ms（60 B / 10 ms） | 6 B/ms（不变） |
| TX FIFO（120 B，初始水位 60） | 稳定 | 水位每 10 ms 降 1.41 B，约 **0.38 s** 到阈值 6 B |
| 阈值触发后 | — | 读指针重置到 `wr-60`，**丢掉约 9 ms 音频** → 每 0.38 s 一次断音 |

ASRC 把 15.625 kHz 插值到 16 kHz（比例 1.024），编码器重新按 6 B/ms 产出，FIFO 稳定，
同时 1 kHz 内容的音调保持正确（重采样恢复了 16 kHz 时间基）。

**实现要点**：复用 SPI 输入已验证的 ASRC 链路（单 ASRC 分时处理左右声道 + 状态
保存/恢复 DMA），PCM 侧只负责把每个 subframe 的 16+16 样本写进 `asrc_in_buf` 两半。
ASRC 模式仍用 `LOW_DELAY | ASRC_DEC_MODE1`：该模式支持 Fsink 在 0.875~1.125×Fsrc
之间，1.024 在范围内（手册 Table 34 / `rsl10_sys_asrc.h` 的范围检查宏）。

**风险点（未上板证实）**：
- 主机模式下 DIO9 是输出脚，`DIO_AUDIOSINK_SRC` 是否仍能数到它**不影响本档** ——
  相位增量用的是固定值，不用 AUDIOSINK 计数；`Ascc_phase_isr` 即使不触发也不会让
  比例失效。
- ASRC 的左右声道分时处理与 PCM subframe（1.02 ms）的时序余量，与 SPI 输入路径相同，
  按同一套队列/状态机工作。

### 14.7 设备侧（16 kHz 从机）能否接受 15.625 kHz —— 结论：目前可行

> 2026-11-03 补充。**这是分析结论，不是上板实测**；判据见文末。

**协议层：可以。** I2S 从机没有自己的采样时钟，LRCK 就是它的采样率。1.000 MHz BCLK ÷ 64
= 15.625 kHz，帧长、位宽、1 bit 延迟、占空比全部保持合法 I2S 时序，器件不会因为"不是
16 kHz"而拒收；BCLK 只比标称（1.024 MHz）低 2.34%，属于常规 I2S 器件的容差范围。

**后果取决于器件的数据是怎么产生的：**

| 器件类型 | 行为 | 结果 |
|---|---|---|
| **A 类**：ADC / 麦克风 / DSP 前端，采样时刻由 LRCK 决定 | 实际按 15.625 kHz 采样，数字流代表真实时间 | ASRC 到 16 kHz 后音调、时长都正确 → **可用（当前按这一类判定）** |
| **B 类**：内部固定 16 kHz 数据源，只按 LRCK 往外吐样本 | 16k 样本按 15.625k 节奏输出，整体慢 2.34% | ASRC 只修速率、修不了音调 → 必须设备当 master，或板上加 1.024 MHz 时钟 |

**拿到器件规格书前，以下两项仍是"待确认"：**

1. **有没有 FS 自动检测**。部分 codec 只认 8/16/32/44.1/48 kHz，靠 LRCK 判别采样率；
   15.625 kHz 不在表里时可能拒绝输出或把内部滤波器配错。
2. **FS 容差**。若规格书写的是 16 kHz ±1%，−2.34% 已超规格；写 ±5% 或"FS 由外部
   时钟任意给定"则没问题。

MCLK 类器件若用 256 × FS，会得到 15.625k × 256 = **4.000 MHz**，比例仍然自洽。

**上板判据**：DIO3 = 1.000 MHz、DIO9 = 15.625 kHz 方波；设备侧如有输出（`dbg_pcm_peak`
非零、波形正常）即按 A 类成立。若设备静音/无数据，先按上面两项查 datasheet，再考虑
回到"设备当 master"或"板上另加时钟源"。
