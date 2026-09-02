# RM 接收 → OD 输出：数据结构与传输

> 文档版本：2026-08-26，基于 `remote_mic_rx_rawtest` 工程（G722 + OD 输出）。
> 覆盖整条链路：2.4GHz 无线接收 → G722 解码 → ASRC 重采样 → BufferOut 打包 → OD Σ-Δ 输出 → 助听器。
> 姊妹篇：[rm_rx_pcm_dataflow.md](rm_rx_pcm_dataflow.md)（PCM 输出路径）。

## 1. 系统概览

```
┌─ TX 端（另一台 RSL10）─────────────────────────────┐
│ 麦克风 → G722 编码(48kbps) → 打包(60B/10ms) → 2.4GHz │
└───────────────────────┬──────────────────────────┘
                        │ RM 专有协议（2Mbps GFSK，跳频）
┌───────────────────────▼──────────────────────────┐
│ RM 协议引擎                                       │
│   ↓ RM_Callback_TRX → App_Process_Incoming_Data   │
└───────────────────────┬──────────────────────────┘
 ① 包分发/解密(关闭)     →  60B 编码帧
 ② G722 解码(LPDSP32)   →  160 样本 @16kHz（10ms 帧）
 ③ Buffer.output        →  共享 DRAM，16-bit 样本连续小端（字节流）
 ④ DMA ch3 (M_TO_P)     →  ASRC->IN（32bit 读→16bit 写，8 采样/子帧）
 ⑤ ASRC 重采样 16k↔16k  →  ASRC->OUT（DEC_MODE1，漂移补偿）
 ⑥ DMA ch4 (P_TO_M)     →  BufferOut（16bit 读→32bit 写，2:1 打包，环形）
 ⑦ DMA ch1 (M_TO_P)     →  AUDIO->OD_DATA（32bit 读→16bit 写，解包）
 ⑧ OD Σ-Δ               →  DIO0/DIO1 差分输出 16kHz
```

### 各级速率/格式汇总

| 阶段 | 采样率 | 宽度 | 数据量/10ms | 存储位置 |
|------|--------|------|------------|---------|
| 无线编码 | — | 字节流 | 60 B | 射频包 |
| G722 解码 | 16 kHz | 16-bit | 160 样本 / 320 B | Buffer.output |
| ASRC 输入 | 16 kHz | 16-bit | 160 样本（20×8 突发） | ASRC->IN |
| ASRC 输出 | ≈16 kHz | 16-bit | ≈160 样本 | ASRC->OUT |
| BufferOut | ≈16 kHz | 32-bit（含 2×16bit） | 160 字 / 640 B | BufferOut（环形） |
| OD 线上 | 16 kHz | 16-bit | 160 样本 | OD_DATA |

---

## 2. 无线传输（RM 链路）

与 PCM 路径共用同一 RM 协议，参数相同（详见 [rm_rx_pcm_dataflow.md#2](rm_rx_pcm_dataflow.md#2-%E6%97%A0%E7%BA%BF%E4%BC%A0%E8%BE%93rm-%E9%93%BE%E8%B7%AF)）：

| 参数 | 值 |
|------|----|
| 速率/调制 | 2 Mbps GFSK，7 通道跳频 |
| 帧间隔 | 10 ms，60 B/帧（G722 mode 3，48kbps，3 bit/样本） |
| 加密 | AES-128-ECB，当前关闭（`CRY_AES_128_ECB=0`） |

接收入口：[rm_app.c](remote_mic_rx_rawtest/code/rm_app.c) `RM_Callback_TRX` → `App_Process_Incoming_Data`。丢包（NOPKT/BADCRC）→ `length=0` → 0xaa 填充做丢包隐藏。

---

## 3. 共享内存与数据结构

### 3.1 共享内存 `Buffer`（[sharedBuffers.h](remote_mic_rx_rawtest/code/codecs/sharedBuffers.h)）

ARM 与 LPDSP32 通过共享 DRAM 交换数据：

| 字段 | 大小 | 内容 |
|------|------|------|
| configuration | 0x100 | codec 控制块 |
| scratch | 0x100 | 暂存 |
| input[2] | 0x200×2 | 编码输入双缓冲（每子帧 3B） |
| **output** | **0x200 (512B)** | **解码输出：16-bit 样本连续小端** |

> `Buffer.output` 声明为 `unsigned char[512]`，但内容是连续小端 int16 样本
> `[s0L,s0H, s1L,s1H, ...]`。按 32-bit 读即"每字含 2 个采样"。

### 3.2 OD 环形缓冲 `BufferOut`（[app_init.c:29](remote_mic_rx_rawtest/code/app_init.c#L29)）

```c
int16_t BufferOut[2 * FRAME_LENGTH];   /* = int16_t[320] = 640 字节 */
```

- 逻辑上是 int16 数组，物理上被 ch4（写）按 32-bit 打包字、ch1（读）按 32-bit 解包
- 双通道（ch4 生产者 / ch1 消费者）都配 `DMA_ADDR_CIRC`，环形复用
- 640 B = 320 采样 = 2 个 10ms 帧

### 3.3 测试正弦表（[app_init.c:276](remote_mic_rx_rawtest/code/app_init.c#L276)）

```c
int32_t pcm_asrc_sine_16k[FRAME_LENGTH];   /* = int32_t[160] */
static const int16_t sine_1k_16k[16] = {0,2296,4243,5544,6000,5544,4243,2296,
                                        0,-2296,-4243,-5544,-6000,-5544,-4243,-2296};
/* 1kHz @ 16k，周期 16 采样，幅度 ~6000 */
```

---

## 4. G722 解码与子帧节奏

### 4.1 编码格式（G722 mode 3, 48kbps）

| 项 | 值 |
|----|----|
| 采样率 | 16 kHz |
| 每帧 | 10ms = 160 样本 |
| 编码字节 | 60 B/帧（3 bit/样本） |
| 子帧 | 20 × **3 B** |
| 每子帧解码 | **8 样本** |

### 4.2 解码流程

- `Renderer`（[app_func.c:299](remote_mic_rx_rawtest/code/app_func.c#L299)）：`LPDSP32_Start_DEC(&frame_in[0])` 启动第一子帧
- `TIMER_REGUL` 节拍（[app_func.c:229](remote_mic_rx_rawtest/code/app_func.c#L229)）：每 tick 解一个 3B 子帧，`frame_idx += 3`，共 20 次/帧
  - 常规 200µs，`subframe_avoid`（左耳 8 / 右耳 10）处改 1000µs 避让射频
- `LPDSP32_Start_DEC`：拷贝子帧到 `Buffer.input`，写 `DSS_CMD` 触发 LPDSP32；DSP 解完 8 样本写 `Buffer.output`（`(short*)outBuffer` 连续 int16），拉 DSP0 中断
- `DSP0_IRQHandler`（[app_func.c:153](remote_mic_rx_rawtest/code/app_func.c#L153)）：解码完成 → 重武装 ch3 + `ASRC_ENABLE` → 设置下一子帧定时器

### 4.3 时序

```
每 10ms 帧：
  [子帧0: TIMER→解3B→DSP0 IRQ→喂8采样→ASRC停] → [子帧1: ...] → ... → [子帧19]
  = 20 × 8 = 160 采样 @16kHz
```

---

## 5. ASRC 重采样（16k ↔ 16k 漂移补偿）

### 5.1 配置（[app_func.c:272](remote_mic_rx_rawtest/code/app_func.c#L272)）

| 项 | 值 | 说明 |
|----|----|------|
| 模式 | `ASRC_DEC_MODE1` | ±25% 漂移容差档（sink≈source） |
| 滤波 | `WIDE_BAND` | 宽带，群延迟更大 |
| 相位增量 | `((Cr−Ck)<<29)/Ck` | `Cr=160<<20`, `Ck=audio_sink_cnt`（ASCC 实测） |
| 速率锁 | ASCC 测 sink 时钟 → `ASRC_Reconfig` | DSP0 IRQ 内 `flag_ascc_phase` 时更新 |

标称 16k↔16k → `phase_inc≈0`，几乎直通，只补偿 BLE 16k 与本地 OD 16k 两晶振的漂移。

### 5.2 ASRC 门控（突发模式）

```
DSP0 IRQ:  ASRC_ENABLE + 重武装 ch3
ch3 完成:  8 采样全部写入 ASRC->IN → ASRC_DISABLE
```

- 每子帧一段"使能→喂 8→禁用"，状态（`ASRC_STATE_MEM`/`ASRC_PHASE_CNT`）跨段保留，整体是连续流
- 真正复位只在 `App_Process_Connected` 的 `Sys_ASRC_Reset()`

---

## 6. OD 输出：打包 / 解包 DMA 链

### 6.1 三跳 DMA 与字长

| DMA | 通道 | 方向 | 源→目的 | 字长 | 传输长度 | 每传输 |
|-----|------|------|---------|------|---------|--------|
| ch3 ASRC_IN | 3 | M_TO_P | `Buffer.output → ASRC->IN` | 源32→目的16（解包） | 8（数目的16bit字） | 8 采样 = 4 个 32bit 字 |
| ch4 ASRC_OUT | 4 | P_TO_M | `ASRC->OUT → BufferOut` | 源16→目的32（打包） | 320（数源16bit字） | 320 采样 → 160 个 32bit 字 |
| ch1 OD | 1 | M_TO_P | `BufferOut → AUDIO->OD_DATA` | 源32→目的16（解包） | 320（数目的16bit字） | 320 采样 = 160 个 32bit 字 |

### 6.2 打包规则（本工程最易踩的坑）

DMA 字长不同时自动打包/解包（HW Ref §12.2.3.2）：

| 源字 → 目的字 | 行为 | 内存布局 |
|------|------|---------|
| 16 → 32 | **打包**：2 源字凑 1 目的字 | 低16=s[N]，高16=s[N+1] |
| 32 → 16 | **解包**：1 源字拆 2 目的字 | 同上反向 |
| 相等 | 直通 | — |

传输长度计数（§12.2.3.3）：**M_TO_P 数目的字数，P_TO_M 数源字数**。所以 ch3 的 8 是 8 个 16bit 样品，不是 8 个 32bit 字。

### 6.3 数据路径

```
Buffer.output（字节流: [s0L,s0H,s1L,s1H,...]）
   │ ch3 M_TO_P：读4×32bit字 → 解出8×16bit → ASRC->IN
   ▼
ASRC（DEC_MODE1，~1:1）
   ▼ ASRC->OUT
   │ ch4 P_TO_M（CIRC，ASRC请求驱动）：读16bit → 打包2:1 → BufferOut
   ▼
BufferOut[320]（环形，160 个 32bit 字）
   │ ch1 M_TO_P（CIRC，OD请求驱动）：读32bit → 解包 → OD_DATA
   ▼
AUDIO->OD_DATA（16bit）→ OD Σ-Δ → DIO0/DIO1
```

### 6.4 OD 采样率

- AUDIOCLK = `AUDIOCLK_PRESCALE_5` → 3.2 MHz
- `DECIMATE_BY_200` → **OD = 3.2MHz/200 = 16 kHz**
- ch1 由 OD 的 `OD_DMA_REQ_EN` 驱动，逐样本按 16kHz 拉取

---

## 7. 测试注入（1kHz 正弦）与已验证的坑

### 7.1 注入位置

DSP0 IRQ 内用正弦覆盖 `Buffer.output`（[app_func.c:209](remote_mic_rx_rawtest/code/app_func.c#L209)），保持官方 ch3 源地址不变：

```c
/* 修复后：每子帧打包 4 个字 = 8 采样，连续无零 */
int32_t *out = (int32_t *)Buffer.output;
for (uint8_t i = 0; i < (SUBFRAME_LENGTH / 2); i++)  /* i=0..3 */
{
    uint16_t a = (uint16_t)pcm_asrc_sine_16k[(pos + 2*i)     % FRAME_LENGTH];
    uint16_t b = (uint16_t)pcm_asrc_sine_16k[(pos + 2*i + 1) % FRAME_LENGTH];
    out[i] = (uint32_t)a | ((uint32_t)b << 16);
}
pos += SUBFRAME_LENGTH;
```

### 7.2 曾经的错误（务必避免）

原实现按"1 采样/32bit 槽"存，但 ch3 是 32→16 解包，导致：

```
内存槽:  [s0 | 0] [s1 | 0] [s2 | 0] [s3 | 0]
解包后:   s0, 0,   s1, 0,   s2, 0,   s3, 0     ← 隔一个塞 0
```

且写了 8 槽（32B）但 DMA 只读前 4 槽（16B），`s4..s7` 被丢弃、`pos+=8` 造成跳采样 → 非零包络周期变 8（等价 2kHz）。现象：峰间距只有 8。

**教训**：喂 32-bit 读/16-bit 写的 DMA，缓冲必须按"每 32bit 字 = 2 个 16bit 采样"打包。

---

## 8. 关键寄存器 / 中断

| 资源 | 编号 | 用途 |
|------|------|------|
| DMA ch1 | OD_DMA_NUM=1 | BufferOut → OD_DATA（M_TO_P，CIRC） |
| DMA ch3 | ASRC_IN_IDX=3 | Buffer.output → ASRC->IN（M_TO_P） |
| DMA ch4 | ASRC_OUT_IDX=4 | ASRC->OUT → BufferOut（P_TO_M，CIRC） |
| TIMER_REGUL | 2 | 解码子帧节拍 |
| DSP0 IRQ | — | 子帧解码完成 + 重配 ASRC + 喂 ASRC |
| ASCC PHASE/PERIOD | — | sink 时钟测量（速率锁） |

优先级：DSP0=2、TIMER=2、ASCC=4。调试 GPIO：DIO6=DSP0 频率，DIO7=ch3 ASRC-in 完成。

---

## 9. 与 PCM 路径的差异

| 项 | OD 路径（本工程） | PCM 路径（remote_mic_rx_raw） |
|----|------------------|------------------------------|
| ASRC 模式 | DEC_MODE1（16k↔16k 漂移补偿） | DEC_MODE2（16k→12k） |
| 输出速率 | 16 kHz | 12 kHz |
| ASRC 输出 DMA | ch4 → BufferOut，32-bit 打包，CIRC | ch4 → pcm_raw_buf，16-bit，双缓冲 |
| 次级打包 | 无（ch1 直接解包喂 OD） | CPU 打包 `(s<<16|s)` → ch5 → PCM |
| 消费端 | OD Σ-Δ（硬件拉动） | PCM 从机（外部 BCLK/FS） |

---

## 10. 当前状态

| 状态 | 说明 |
|------|------|
| 数据链路 | 已打通，能出声 |
| 正弦测试 | 修复注入后，输出为标准 1kHz 纯音（16 采样/周期），数据与声音均正确 |
| 待验证 | 实际 BLE 音频（G722 解码路径）经 ASRC→OD 的音质 |

---

## 附：与本文相关的关键代码位置

| 文件 | 内容 |
|------|------|
| [sharedBuffers.h](remote_mic_rx_rawtest/code/codecs/sharedBuffers.h) | 共享内存结构 |
| [app_init.c](remote_mic_rx_rawtest/code/app_init.c) | OD/ASRC/DMA 初始化、正弦表 |
| [app_func.c](remote_mic_rx_rawtest/code/app_func.c) | DSP0/TIMER/ASCC 中断、ASRC_Reconfig、正弦注入 |
| [app.h](remote_mic_rx_rawtest/include/app.h) | 常量、DMA 配置宏、OD 配置 |
