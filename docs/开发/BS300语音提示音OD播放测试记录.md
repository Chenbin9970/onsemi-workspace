# BS300 语音提示音 OD 播放 — 测试记录

> 2026-07-09 | RSL10 `peripheral_server_sleep` 项目

## 目标

通过 RSL10 OD (Output Driver) 播放语音提示音，经由 BS300 Telecoil 通道传入 DSP → 耳机输出。

## 架构

```
BLE cmd 0x02/0x03
  → vp_play() 从 Flash PCM 读数据
  → ke_timer 每 10ms 填 BufferOut[320]
  → OD DMA (循环, 32-bit 源) → AUDIO->OD_DATA
  → OD_P (DIO0) 单端 → DSP 线圈 → Telecoil 通道 → 耳机
```

## 测试过程与发现

### 1. 首次尝试：TTS 8kHz + 上采样 → "很糊很难听"

- PCM 采样率 8kHz，通过线性插值上采样到 16kHz
- **结论**：8kHz Nyquist 仅 4kHz，语音高频全丢，上采样无法恢复

### 2. 16kHz TTS → "杂音" + "蓝牙搜不到"

**PCM 大小**：9 条提示音约 124KB

**蓝牙消失原因**：
- 双缓冲 `vp_buf[640]` 占用 1280 字节 .bss
- 系统剩余 RAM 仅 ~2.9KB，BLE 协议栈需要 2-3KB 堆栈空间
- 内存溢出导致 BLE 初始化失败
- **解决**：删除 vp_buf，直接复用 `BufferOut[320]`

**杂音原因**：
- 误将 OD DMA 源宽度从 `DMA_SRC_WORD_SIZE_32` 改为 `DMA_SRC_WORD_SIZE_16`
- OD 外设要求 32-bit 源读取（每 32-bit 读 = 2×16-bit 写 = L+R 立体声对）
- 16-bit 源只提供一半数据，OD 通道错位→杂音
- **解决**：恢复原始 `RX_DMA_OD` 配置

### 3. OD_GAIN 削顶失真 → "很扁/杂音"

- 原始 `OD_GAIN = 0xfff`（最大增益）
- PCM 样本 ±16000，被过度放大导致削波
- **1kHz 纯音测试**确认：降低 OD_GAIN 后声音变干净
- **最佳值**：`OD_GAIN = 0x600~0x800`

### 4. 定时器 BufferOut 读写竞争 → 边界咔嗒声

**根因**：

| | RM 音频路径 | 语音提示音路径 |
|---|---|---|
| BufferOut 生产者 | ASRC_OUT DMA（硬件，AUDIOCLK 同步） | CPU via ke_timer（软件，10ms 精度） |
| 同步方式 | 硬件逐样本填充，与 OD DMA 同时钟源 | 一次性填满，10ms 刷新一次 |
| 边界行为 | 无碰撞 | ke_timer 抖动导致 DMA 读到半新半旧数据 |

**确认**：死循环测试（填一次 BufferOut，DMA 自循环）→ 声音干净。恢复定时器刷新 → 有差异。

**修复**：倒序填充 BufferOut（从 [319]→[0]），DMA 正向读取（[0]→[319]）。写入速度约 50µs，DMA 读取速度约 62.5µs/样本，倒序写保证 DMA 路过时已写完。

### 5. Windows SAPI TTS 音质限制

- 系统仅 `Microsoft Zira Desktop` (Win7 时代机械女声)
- 无法安装 edge-tts（需要 ffmpeg 解码 MP3，环境无 ffmpeg）
- Azure Neural TTS (edge-tts) 质量高一个量级，但需 ffmpeg 依赖

### 6. Telecoil 频率响应限制

- Telecoil 是电感耦合，天然带通特性 (300-3400Hz)
- RM 音频经 DSP 处理（WDRC/EQ）后输出，频谱已补偿
- 语音提示音走 Telecoil 旁路模式，未经 DSP → "扁"
- **软件补偿**：生成 PCM 时预加重低频 (+9.5dB @ 500Hz) + 衰减高频 (3kHz LPF)

## 最终可用配置

| 参数 | 值 | 说明 |
|------|-----|------|
| PCM 采样率 | 16kHz | 语音清晰度达标 |
| PCM 大小 | 65KB (6 条) | Flash 使用 230KB + 65KB = 295KB / 380KB |
| VP_CHUNK | 160 样本/10ms | 匹配 16kHz 采样率 |
| 上采样 | 无 | 16kHz 直接 stereo 复制 |
| OD DMA | RX_DMA_OD (32-bit 源) | 与 RM 音频一致 |
| OD_GAIN | 0x600 | 避免削波 |
| 填充策略 | 倒序 (319→0) | 避免 DMA 读/写碰撞 |
| 缓冲 | BufferOut[320] (复用) | 零额外 RAM |
| EQ | 低频 +9.5dB, HF 衰减 | 补偿 Telecoil |
| TTS 引擎 | Windows SAPI Zira | 可用但音质有限 |

## 未解决问题

1. **TTS 音质受限于 Zira 语音**：需安装 ffmpeg → 切换到 edge-tts (Azure Neural)
2. **Telecoil 带宽限制**：物理耦合特性无法根本改变，只能 EQ 补偿
3. **OD 差分→单端损耗**：OD 差分输出 (OD_P + OD_N)，当前仅用一个脚，丢失一半幅值
4. **ke_timer 10ms 抖动**：倒序填充缓解了大部分问题，但不能 100% 消除边界噪声

## 关键文件

| 文件 | 状态 | 说明 |
|------|------|------|
| `include/voice_prompt.h` | 新增 | API、VP_TIMER=0x11、6 个提示音枚举 |
| `code/voice_prompt.c` | 新增 | 播放引擎：OD 初始化、倒序填充、ke_timer 状态机 |
| `code/vp_pcm_data.c` | 生成 | 6 组 16kHz PCM (65KB Flash) |
| `scripts/gen_voice_prompts.py` | 工具 | TTS 生成 + Telecoil EQ 补偿 |
| `include/app.h` | 修改 | +voice_prompt.h, +VP_TIMER handler |
| `code/app_process.c` | 修改 | +VP_Timer() 消息处理器 |
| `app.c` | 修改 | +cmd=0x02 音量提示音 |
| `Debug/code/subdir.mk` | 修改 | +voice_prompt.c, +vp_pcm_data.c |
