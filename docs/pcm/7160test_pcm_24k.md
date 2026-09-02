# 7160test — PCM 24k 有效采样率发现

> 工程：`peripheral_server_sleep7160test`
> 日期：2026-09-01
> 结论：**PCM `WORD_SIZE_16 + MULTIWORD_2` 使每 FS 周期装 2×16-bit = 有效采样率 24k，7k/8k 纯音能干净播出。** 推翻了"12k 是带宽硬限制"的旧假设（见 `docs/pcm/7160test_音频杂音调试.md` 的带宽结论，部分前提已不成立）。

## 一、发现经过

### 1.1 背景：为什么之前 6/7/8k 播不出

原配置 `PCM_CFG_TX = WORD_SIZE_32 + MULTIWORD_2 + SUBFRAME_ENABLE`：

| 项 | 值 |
|----|-----|
| BCLK | 384 kHz（7100 提供） |
| FS | 12 kHz → **32 BCLK/帧** |
| WORD_SIZE_32 + MULTIWORD_2 | 每帧理论 2×32-bit = 64 bit，但 32 BCLK 只够 1 个 32-bit 字 |
| 有效采样率 | **12k**（每帧 1 采样） |
| 奈奎斯特 | 6 kHz → 6/7/8k 折回/听不到 |

### 1.2 触发发现

做 init 纯音注入测试（`PCM_TONE_TEST`）时，把 `WORD_SIZE_32` 改成 **`WORD_SIZE_16`**：

| 项 | 值 |
|----|-----|
| WORD_SIZE_16 + MULTIWORD_2 | 每帧 2×16-bit = 32 BCLK **正好** |
| 有效采样率 | **24k**（每帧 2 采样） |
| 奈奎斯特 | 12 kHz → 7k/8k 都能过 |

### 1.3 验证（2026-09-01）

- init 直接送 **24k 采样 8kHz ±6000 16-bit 正弦**（周期 3 采样，`{0, +5196, -5196}` 重复），7100 输出端测到**干净 8k 纯音**。
- 7k 同理干净播出（周期 24，`pcm_sine24k_7k`）。
- J-Link 读 `pcm_tx_buf`（0x200014c4）：每 32-bit 字低16 = 8k 采样、高16 = 0，数据正确。
- Scope 测 SERO：每个 FS 周期 = 32 BCLK = **2 段 16-bit，都具体值、连续样本**。

**关键机制**：PCM 硬件只移出 32-bit 字低 16 位（采样），不把高 16 的 0 移出去 → 线上是连续 16-bit 采样流，无插零 → 7100 按 24k 有效读 → 8k 干净。

## 二、测试基建（`PCM_TONE_TEST` 宏，已保留）

> 宏定义在 `include/app.h`，当前**关闭**（`//#define PCM_TONE_TEST`），真实路径已跑 16k→24k。取消注释回到纯音注入测试。

| 文件 | 改动 |
|------|------|
| `include/app.h` | `PCM_WORD_SIZE_32→16`；`PCM_TONE_TEST` 宏（8k 正弦说明） |
| `code/app_init.c` | `pcm_sine24k_8k[24]` 硬编码表 + `Pcm_Sine24k_Fill()`；`Audio_Init` 测试音分支（填两缓冲、绕过 ch4/ASRC、ch5 直接流） |
| `code/app_func.c` | `Pcm_tx_dma_isr` 测试音模式 ch5 自续双缓冲轮流 |

**填充方式**：每 32-bit 字低16（word1/右）= 一个采样、高16 = 0。120 字/缓冲 = 120 采样（24k 下 5ms/缓冲，双缓冲连续重武装）。

**其他频率表**（`pcm_sine24k_1k`/`pcm_sine24k_7k` 曾在调试中用过，当前文件是 8k 版）。

## 三、真实音频路径已实现 16k→24k（2026-09-01 验证）

```
RM → G722 解码(16k, 160/10ms) → ASRC(16k→24k, INT_MODE) → ch4 → pcm_tx_buf → ch5 → PCM(24k) → 7100
```

实现：
1. **ASRC 16k→24k**：`Asrc_reconfig` PCM 分支改用 **`ASRC_INT_MODE`（插值模式，f_sink>f_src）**，`inc = (Cr - 2Ck)<<29 / 2Ck`。
2. **必须闭环跟踪**：`2Ck = 2×audio_sink_cnt`（每 FS 周期 2 采样 = 24k 输出）。**硬编码名义 2:3（0xF5555556）会因 7100 时钟偏差导致"规律爆音"（周期欠载/溢出），闭环后解决。**
3. **`PCM_FRAME_WORDS` 保持 120**：24k 下 120 采样 = 5ms/缓冲，双缓冲连续重武装，不用改 240。
4. ch4/ch5 DMA 长度、`RX_DMA_ASRC_OUT` 打包方式均不变。

验证结果（2026-09-01）：
- **金属尾音消失**——16k→12k 非整数下采样伪影的根源被移除。
- **规律爆音 → 闭环跟踪解决**。
- 真实音频正常播放、高频保留（7k/8k 能出）。

## 四、参考

- PCM slave 输出基线：`docs/pcm/7160test_pcm_output.md`
- 音频杂音/带宽旧结论：`docs/pcm/7160test_音频杂音调试.md`
- ASRC 模式（SVD）：`ASRC_INT_MODE`（插值，f_sink>f_src）、`ASRC_DEC_MODE1/2/3`（下采样），比例范围见 `rsl10_sys_asrc.c` 的 `Sys_ASRC_ConfigRunTime`
