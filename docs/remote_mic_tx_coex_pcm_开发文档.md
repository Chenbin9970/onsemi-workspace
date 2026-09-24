# remote_mic_tx_coex_pcm 开发文档

> 状态：**边做边记，尚未收尾**。音频已能播，但「长时间播放会变差」的根因未定论。
> 本文档记录到 2026-09-24 为止的进展。所有改动**未提交**（基线见 §2）。

## 1. 工程概述

`remote_mic_tx_coex_pcm` 基于 onsemi RSL10 `remote_mic_tx_coex` demo（远端麦克风发送机 / RM TX，
BLE + RM 共存），目标是把 **CM108B（USB 音频芯片）的 I2S 输出**作为音源，
经 RSL10 编码后用 RM 自定义协议广播，由 **1654** 设备接收播放。

完整通路：

```
PC/USB ──▶ CM108B ──I2S(48kHz/16bit/立体声)──▶ RSL10 PCM 从机
        ──3:1 抽取(48k→16k)──▶ LPDSP32 G.722 编码(48kbps)
        ──▶ tx_data_fifo ──▶ RM 广播 ──▶ 1654 解码播放
```

参考工程：`remote_mic_tx_coex`（DMIC 输入版，**端到端已验证能在 1654 上播放**），
本工程的后级通路照它的结构做，只有输入侧不同。

## 2. 来源与 git 基线

- Demo 基线已提交：commit `24b7199`（RSL10 原厂 `remote_mic_tx_coex` 示例，27 个文件，verbatim）。
- 之后所有改动**未提交**，涉及 8 个文件（+426 / −136 行）：
  `app.h`、`app_func.c`、`app_init.c`、`app_process.c`、`ble_std.c`、`queue.c`、`rm_app.c`、`.cproject`。
- 两个未跟踪的 PDF（规格书 / 原理图）放在工程目录下。

## 3. 引脚与接线

**生效分支**：`PCM_RX_RAW_SOURCE = EZAIRO_7100(=0)`，即走 `app.h` 里那组 `#else` 定义。

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

### 4.4 速率汇总

| 位置 | 值 |
|---|---|
| PCM 输入 | 48 kHz / 16-bit / 双声道 |
| DMA | 96 word/ms，2 次中断/ms |
| 抽取后 | 16 kHz |
| 编码器输入 | 16 采样/ms/声道 |
| G722 输出 | 6 字节/ms/声道（48 kbps） |
| RM payload | 60 字节 / 10 ms / 路 |

### 4.5 关键尺寸

| 宏 | 值 | 含义 |
|---|---|---|
| `SUBFRAME_LENGTH` | 16 | 一个编码 subframe 的采样数（= 1 ms @16k） |
| `PCM_DECIM_RATIO` | 3 | 48k → 16k 的抽取比 |
| `PCM_HALF_SAMPLES` / `PCM_HALF_RAW` | 8 / 24 | 半块的输出 / 原始采样数（每声道） |
| `PCM_DMA_HALF_WORDS` / `PCM_DMA_BLOCK_WORDS` | 48 / 96 | DMA 半块 / 整块 word 数 |
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

TX 是**单向广播**，不需要 RX 连接/应答；1654 自己进 RM 搜索。

### 5.2 RM 参数（必须与 1654 一致）

| 参数 | 值 | 说明 |
|---|---|---|
| `accessword` | `0x00cde629 \| (0xf2 << 24)` = **0xF2CDE629** | 1654 侧 = `(stream_addr << 8) \| 0x29`，默认 `stream_addr = 0xF2CDE6` |
| `RM_HOPLIST` | `{3, 9, 15, 21, 24, 33, 36}` | 与 `peripheral_server_sleep` 一致 |
| `numChnlInHopList` | 7 | |
| `role` | `RM_MASTER_ROLE` | |
| `audio_rate` / `radio_rate` | 48 / 2000 | |
| `OUTPUT_POWER_6DBM` | **1** | 参考工程也是 1，且在 `RM_Enable` 前 `Sys_RFFE_SetTXPower(6)` |
| `debug_dio_num[0..3]` | **全 `0xff`** | DIO11 是 FEM TXEN，不能被 RM 库当调试输出占用 |

## 6. 调试开关与探针

### 6.1 开关（`include/app.h`）

| 开关 | 当前 | 作用 |
|---|---|---|
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
| `dbg_cnt_asrc_out` | `Asrc_out_dma_isr` | **0**（ASRC 不走） |
| `dbg_pcm_peak` | `Pcm_decode_half` | 播 1 kHz 时几百~几千 |
| `ptr_rst_cnt` | `Read_buffer` 重定位分支 | FIFO 太满/太空的次数，每次是一声不连续 |
| `dbg_q_alloc_fail` | `QueueInsert` 分配失败分支 | 堆不够、丢包次数 |
| `dbg_cnt_tx_req[2]` / `dbg_len_last` | `RM_Callback_TRX` | ~100/s 每路，`len`=60 |
| `dbg_cnt_status` / `dbg_last_status` | `RM_Callback_StatusUpdate` | 状态=0(DISCONNECTED)/1(FAIL)/2(ESTABLISHED) |

### 6.3 三层验证法（本次最有效的手法）

后级不明时，**先用已知数据把后级锁死，再攻输入**：

1. `RM_TEST_TONE=1` → 预编码数据直接进 RM 载荷 → 验证 **RM 发射 + 1654 播放**
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

## 8. 已验证 / 未验证

### ✅ 已上板验证

- **RM 开机直启 + 链路**：1654 能收到
- **编码器 → RM → 1654 播放**：`TX_TONE_TEST=1` 时 1654 出**干净的 1 kHz 纯音**（端到端锁定）
- **PCM 输入能出声**：CM108B 播音频，1654 能听到
- CM108B 单独供电时 LRCK 正常（未被此前的电平对顶打坏）
- RSL10 VDDO = 3.3 V

### ❌ 未验证 / 未定论

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

1. **判定"播一会儿变差"的根因**：烧当前固件，等 ALARM 第一行，看 `rst` / `qfail`
2. 若为漂移 → 把 ASRC 接回来（§9）
3. 清理临时脚手架：5 个调试开关、8 个计数器、打印、`Tone_feed_encoder`
4. `>>15` 位对齐做一次确认（换 `>>16` 对比听感）
5. 用立体声源和不同频率完整测一遍音质
6. 提交

## 12. 构建注意

- 改动**未提交**，编译前确认烧的是当前工作区
- `.cproject` 的十几行改动是 Eclipse 自己写的（多一处 `storageModule`、
  几处 `useByScannerDiscovery`），**没有加任何 `-D` 或源文件**，不影响编译
- Windows 下源码是 CRLF，git 会按 LF 存储，提交时有换行警告属正常
