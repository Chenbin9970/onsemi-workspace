# RM 接收 → PCM 输出：数据流格式与传输

> 文档版本：2026-08-21，基于 `remote_mic_rx_raw` 工程当前代码（Path B 实现）。
> 覆盖整条链路：2.4GHz 无线接收 → G722 解码 → ASRC 重采样 → PCM 从机输出 → 外部设备。

## 1. 系统概览

```
┌─ TX 端（另一台 RSL10）─────────────────────────────┐
│ 麦克风 → G722 编码(48kbps) → 打包(60B/10ms) → 2.4GHz │
└───────────────────────┬──────────────────────────┘
                        │ RM 专有协议（2Mbps GFSK，跳频）
┌───────────────────────▼──────────────────────────┐
│ RM 协议引擎(rm_event/rm_pkt_hdl)                   │
│   ↓ RM_Callback_TRX → App_Process_Incoming_Data    │
└───────────────────────┬──────────────────────────┘
 ① 包分发/解密(关闭)     →  60B 编码帧
 ② G722 解码(LPDSP32)   →  160 样本 @16kHz（10ms 帧）
 ③ Buffer.output        →  共享 DRAM，16-bit 样本（32-bit 字存储）
 ④ DMA ch3              →  ASRC->IN（16kHz 输入）
 ⑤ ASRC 重采样 16k→12k  →  ASRC->OUT（12kHz mono）
 ⑥ DMA ch4 (P_TO_M)     →  pcm_raw_buf[2][120]（16-bit）
 ⑦ 打包 (s<<16|s)       →  pcm_tx_buf[2][120]（32-bit 帧）
 ⑧ DMA ch5 (M_TO_P)     →  PCM->TX_DATA（32-bit 写，一次一帧）
 ⑨ PCM 从机             →  SERO(DIO14) 按 BCLK 384k/FS 12k 移出
```

### 各级速率/格式汇总

| 阶段 | 采样率 | 样本宽度 | 数据量/10ms | 存储位置 |
|------|--------|---------|------------|---------|
| 无线编码 | — | 字节流 | 60 B | 射频包 |
| G722 解码 | 16 kHz | 16-bit（32-bit 字存） | 160 样本 / 320 B | Buffer.output |
| ASRC 输入 | 16 kHz | 16-bit | 160 样本 | ASRC->IN |
| ASRC 输出 | 12 kHz | 16-bit | 120 样本 / 240 B | pcm_raw_buf |
| PCM 打包 | 12 kHz | 32-bit（含 2×16bit） | 120 帧 / 480 B | pcm_tx_buf |
| PCM 线上 | 12 kHz | 16-bit × 2 字/帧 | 240 字 | SERO |

---

## 2. 无线传输（RM 链路）

### 2.1 物理层参数（[rm_app.c](remote_mic_rx_raw/code/rm_app.c)）

| 参数 | 值 | 说明 |
|------|----|------|
| 速率 | 2 Mbps（`radio_rate=2000`） | 自定义模式 RF 配置 |
| 调制 | GFSK | 与 BLE 共存，BBIF_COEX 硬件仲裁 |
| 跳频 | 7 通道 hoplist | `{3,9,15,21,24,33,36}`，`numChnlInHopList=7` |
| 频段 | 2.4GHz，BLE 通道附近 | `hopList` = (Nordic channel/2 - 1) |
| 帧间隔 | 10 ms（`interval_time=10000`） | 每 10ms 一个音频帧 |
| 重传周期 | 5 ms（`retrans_time=5000`） | TX 主发/重发交替 |
| 前导/同步 | `preamble=0x55`，access word | `accessword=(0x00cde629\|0xf2<<24)` |
| 加密 | AES-128-ECB（`CRY_AES_128_ECB=0`） | **当前关闭** |

### 2.2 包结构

```
包长 packet_length = (audio_rate × interval_time)/8000 = (48×10000)/8000 = 60 字节
空口时长 pktDuration = (packet_length+8)×8000/radio_rate = 68×4 = 272 µs
```

包头（header byte）编码声道与序号（[rm_pkt.h](../..//C:/Users/ViewSSS/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.9.1182/source/firmware/remote_micLib/rm_pkt.h)）：

| 位域 | 含义 |
|------|------|
| `audio_ch` | 声道：LEFT=1 / RIGHT=2 |
| `trans_id` | 序号：N / N_1 / N_RETRY / N_1_RETRY |
| `codec` | 编码类型（G722/CELT） |

### 2.3 发送时序（每 10ms 周期）

TX 端每个周期交错发送左右声道（[rm_pkt_hdl.c RM_PrepareHeader](../..//C:/Users/ViewSSS/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.9.1182/source/firmware/remote_micLib/rm_pkt_hdl.c#L921)）：

```
主发(前 5ms):  N_LEFT → N_1_LEFT → N_RIGHT → N_1_RIGHT
重发(后 5ms):  N_LEFT_RETRY → N_1_LEFT_RETRY → N_RIGHT_RETRY → N_1_RIGHT_RETRY
```

每个包 272µs，重传周期 5000µs 切换主发/重发。

### 2.4 接收过滤（单声道）

- 无线链路上**左右声道都发**，接收端按 `audioChnl` 只接受自己一侧的包（[rm_pkt_hdl.c:67-68](../..//C:/Users/ViewSSS/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.9.1182/source/firmware/remote_micLib/rm_pkt_hdl.c#L67)）
- `ear_side`（RM_LEFT=0 / RM_RIGHT=1）→ `rm_param.audioChnl`，DIO0 按键切换
- 应用层 `App_Process_Incoming_Data` 收到的始终是**单声道** 60B/帧
- 丢包（NOPKT/BADCRC）→ `length=0` → 0xaa 填充做丢包隐藏

### 2.5 渲染延迟（左右耳不同）

`RM_Configure` 按声道算 renderDelay（收包→交解码器的延迟）：

| 耳 | 声道 | renderDelay | 计算 |
|----|------|------------|------|
| 左 | FIRST(=RM_LEFT) | **1441 µs** | 200 + 4×272 + 3×45 + 18 |
| 右 | SECOND(=RM_RIGHT) | **789 µs** | 200 + 2×272 + 45 |

这导致解码子帧流相对无线窗口的相位不同 → `subframe_avoid` 避让位不同（左=8，右=10）。

---

## 3. G722 解码

### 3.1 编码格式（G722 mode 3, 48kbps）

| 项 | 值 |
|----|----|
| 采样率 | 16 kHz |
| 每帧 | 10ms = 160 样本 |
| 编码字节 | 60 B/帧（3 bit/样本） |
| 子帧 | 20 × **3 B** |
| 每子帧解码 | 8 样本 |

### 3.2 解码引擎（LPDSP32 协处理器）

- `App_CodecInitialize` 加载 G722 DSP 程序，绑定共享内存
- `LPDSP32_Start_DEC`：编码子帧拷入 `Buffer.input[0]`，写 `DSS_CMD` 触发 DSP
- DSP 解完写回 `Buffer.output`，拉 DSP0 中断
- 子帧节奏：TIMER_REGUL 每 200µs 解一个子帧（特定位置 1ms 避让射频），DSP0 IRQ 每子帧完成后推给 ASRC

### 3.3 共享内存布局（[sharedBuffers.h](remote_mic_rx_raw/code/codecs/sharedBuffers.h)）

`Buffer` 位于 `.shared` 段 = `DRAM_DSP_CM3`（0x2100B800，2KB，ARM/DSP 共享）：

| 字段 | 偏移 | 地址 | 内容 |
|------|------|------|------|
| configuration | +0x000 | 0x2100B800 | codec 控制块 |
| scratch | +0x100 | 0x2100B900 | 暂存 |
| input[0] | +0x200 | 0x2100BA00 | **编码输入** |
| input[1] | +0x400 | 0x2100BC00 | 双缓冲 |
| output | +0x600 | 0x2100BE00 | **解码输出**（16-bit 样本，32-bit 字对齐） |

---

## 4. ASRC 重采样（16k → 12k）

### 4.1 配置

| 项 | 值 | 说明 |
|----|----|------|
| 模式 | `ASRC_DEC_MODE2` | 12/16=0.75，DEC_MODE2 唯一有效 |
| 滤波 | `WIDE_BAND` | 宽带响应滤波器 |
| 相位增量 | `((Cr−Ck)<<28)/Ck` | `Cr=160<<20`, `Ck=audio_sink_cnt`（ASCC 实测） |
| 速率锁 | ASCC 测 `PCM_FRAME_SYNC`(12k) → `ASRC_Reconfig` | DSP0 IRQ 内定期更新 |

### 4.2 数据路径 / DMA

| DMA | 通道 | 配置 | 方向 |
|-----|------|------|------|
| ch3 ASRC_IN | 3 | `Buffer.output → ASRC->IN`，16-bit，8 拍/子帧 | M→P |
| ch4 ASRC_OUT | 4 | `ASRC->OUT → pcm_raw_buf`，16-bit，120 拍/帧 | **P→M** |

- ASRC **开关门**：DSP0 IRQ（子帧解完）→ `ASRC_ENABLE` + 重武装 ch3；ch3 完成中断 → `ASRC_DISABLE`（防止空跑）
- 8 个 16kHz 输入 → ASRC 产 ~6 个 12kHz 输出，每帧 20×6=120

---

## 5. PCM 输出（Path B 当前实现）

### 5.1 PCM 硬件配置（[app.h](remote_mic_rx_raw/include/app.h)）

```
PCM_BIT_ORDER_MSB_FIRST | PCM_TX_ALIGN_LSB | PCM_WORD_SIZE_16 |
PCM_FRAME_ALIGN_LAST   | PCM_FRAME_WIDTH_LONG | PCM_MULTIWORD_2 |
PCM_SUBFRAME_DISABLE   | PCM_CONTROLLER_DMA | PCM_SELECT_SLAVE
```

- **从机**：BCLK/FS 由外部主机提供，RSL10 不产时钟
- 帧 = MULTIWORD_2 × WORD_SIZE_16 = **32 bit/帧**
- BCLK 384kHz ÷ 32 = **FS 12kHz** ✓

### 5.2 引脚（[app.h:225-231](remote_mic_rx_raw/include/app.h#L225)）

| 信号 | DIO | 方向 | 说明 |
|------|-----|------|------|
| CLK (BCLK) | DIO2 | 输入 | 外部 384kHz |
| FRAME (FS) | DIO3 | 输入 | 外部 12kHz |
| SERI | DIO4 | 输入 | 未用 |
| **SERO** | **DIO14** | 输出 | 音频数据 |

### 5.3 打包与 DMA（Path B）

```
pcm_raw_buf[2][120]  (16-bit mono, 双缓冲)
    ↓ DMA4(ch4, P_TO_M, 120拍/10ms, 完成中断)
DMA4 中断: ① 重武装 ch4→另一个 raw 缓冲  ② 打包 120 样本 → pcm_tx_buf[fill]
    ↓ 每样本: (s<<16)|s  →  uint32[120]
pcm_tx_buf[2][120]   (32-bit 帧, 双缓冲)
    ↓ DMA5(ch5, M_TO_P, 32-bit, 120拍/10ms, 完成中断)
DMA5 中断: pcm_fill_pos≥120 则换缓冲 → 重武装
    ↓ 每拍写一个 32-bit = 一帧 [word0=s, word1=s]
PCM->TX_DATA
```

- **ch4**：`RX_DMA_ASRC_OUT` = P_TO_M、16-bit、线性、完成中断
- **ch5**：`RX_DMA_PCM_STEREO` = M_TO_P、**32-bit**、线性、完成中断，120 拍/10ms
- 双缓冲 raw + 双缓冲 PCM，消除 ch4 丢样本窗口

### 5.4 时序账

- 12k mono 进来 → 10ms 攒 120 样本 → DMA4 完成 → 打包 480B → ch5 每 10ms 换缓冲播 120 帧
- 一帧（10ms）时间线：

```
包到 → 解码20子帧(~4ms) → ASRC 120样本 → ch4完成 → 打包 → ch5播放(10ms)
```

---

## 6. 关键寄存器 / 中断

| 资源 | 编号 | 用途 |
|------|------|------|
| DMA ch3 | ASRC_IN_IDX=3 | Buffer.output → ASRC->IN |
| DMA ch4 | ASRC_OUT_IDX=4 | ASRC->OUT → pcm_raw_buf（P_TO_M） |
| DMA ch5 | PCM_DMA_NUM=5 | pcm_tx_buf → PCM->TX_DATA（M_TO_P） |
| TIMER_REGUL | 2 | 解码子帧节拍 |
| DSP0 IRQ | — | 子帧解码完成 + ASRC 重配 |
| ASCC PHASE/PERIOD | — | 12k FS 测量（速率锁） |

优先级：DSP0=2、TIMER=2、DMA4=3、DMA5=3、ASCC=4。

---

## 7. 当前状态与已知问题

| 状态 | 说明 |
|------|------|
| 数据链路 | 已打通，能出声 |
| 帧格式 | 32-bit 写 TX_DATA 版本"更接近正常"（host 读取方向待确认） |
| 待解决 | **破音 + 尾音带金属音** —— 疑似 G722 48kbps 编码伪影（zipper noise / slope overload），待测试音隔离确认 |
| 已回退 | 16-bit 展开版本杂音更大；pack-in-DMA5 版本不稳定，均回退 |

### 调试开关（[app.h:248-264](remote_mic_rx_raw/include/app.h#L248)）

| 宏 | 作用 |
|----|------|
| `PCM_TEST_TONE` | 1 = 输出路径播放 1kHz 测试音（绕过解码+ASRC） |
| `PCM_TEST_SWEEP` | 1 = 1k→10k 扫频（需 TEST_TONE=1） |

用测试音隔离：干净 → 输出路径 OK，金属音来自 G722 编码端（需 TX 提码率或换 CELT）；不干净 → 输出路径继续查。

---

## 附：待确认项

- PCM 主机是按 16-bit × 2 字读，还是按 32-bit 字读（决定 `(s<<16|s)` 打包 vs 其他对齐）
- ASRC 输出速率是否精确锁定 12k（影响帧边界是否周期性丢/重样本 → 金属音）
- 若为 G722 码率问题，TX 端 `audio_rate` 48→56/64kbps 或切 CELT 是唯一出路
