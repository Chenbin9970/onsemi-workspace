# RM RX 音频接收 + OD 输出

## Context
当前项目已有 RM BLE 开关控制，但 RM_Callback_TRX 收到音频数据后只打日志。需参考 SDK `remote_mic_rx_raw` 添加完整的 OD (Output Driver) 音频输出链路：
RM Radio → G.722 DSP 解码 → ASRC → OD DMA → 耳机输出

## 音频链路架构
```
RM RX Packet → App_Process_Incoming_Data()
  → Renderer() → LPDSP32_Start_DEC() [G.722 decode, 20 subframes/frame]
    → DSP0_IRQHandler() [decode完成]
      → ASRC_Reconfig() [采样率同步]
      → DMA: DSP output → ASRC input
      → DMA: ASRC output → AUDIO->OD_DATA → OD → 耳机
```

## 新建文件 (从 remote_mic_rx_raw 完整复制)

### 1. `code/app_func.c` — 音频管线核心
所有音频 ISR 和处理函数：
- `AUDIOSINK_PHASE_IRQHandler` — 音频时钟相位测量
- `AUDIOSINK_PERIOD_IRQHandler` — 音频时钟周期测量  
- `DSP0_IRQHandler` — DSP 解码完成中断
- `TIMER_IRQ_FUNC(TIMER_REGUL)` — 子帧解码节流定时器
- `DMA_IRQ_FUNC(ASRC_IN_IDX)` — ASRC 输入 DMA 完成
- `DIO0_IRQHandler` — 按键左右耳切换
- `Renderer()` — 启动解码管线
- `LPDSP32_Start_DEC()` — 向 DSP 发送解码命令
- `ASRC_Reconfig()` — 同步 Cr/Ck 调整 ASRC
- `App_Process_Incoming_Data()` — RM 数据入口
- `App_Process_Connected()` — RM 连上：启用音频中断
- `App_Process_Link_Disconnected()` — RM 断开：禁用音频中断

### 2. `code/codecs/` 目录 (约 25 个文件)
完整 codec 框架 + G.722 DSP 固件：
- `sharedBuffers.h/c` — DSP/ARM 共享内存 (`.shared` section)
- `codec.h/c` + `codecInternal.h` — codec 抽象层
- `logger.h/c` — 日志
- `base/baseCodec.h/c` — base codec
- `baseDSP/baseDSPCodec.h/c` + `baseDSPCodecInternal.h` — DSP codec 基类
- `G722DSP/g722DSPCodec.h/c` — G.722 codec 适配
- `dsp/loader/loader.h/c` + `flashCopier.h/c` — DSP 固件加载
- `dsp/g722/*` (9 文件) — G.722 DSP 固件 PM/DM 映像

## 修改文件

### 3. `include/app.h`
新增 (全部在 `#ifdef APP_RM_ENABLE` 下):
- `CODEC_CONFIG_G722=0`, `CODEC_CONFIG=CODEC_CONFIG_G722`
- `OUTPUT_INTRF=OD_TX_RAW_OUTPUT` (内部 OD 驱动耳机)
- `OD_TX_RAW_OUTPUT=3`
- codec 参数: `CODEC_MODE=3`, `FRAME_LENGTH=160`, `SUBFRAME_LENGTH=8`, `CODEC_BLOCK_SIZE=4`, `ENCODED_FRAME_LENGTH=60`, `ENCODED_SUBFRAME_LENGTH=3`
- ASRC: `SHIFT_BIT=20`, `RX_DMA_ASRC_IN/OUT` DMA 宏
- DMA 通道号: `ASRC_IN_IDX=3`, `ASRC_OUT_IDX=4`, `OD_DMA_NUM=1`
- Timer: `TIMER_REGUL=2`
- OD 配置: `AUDIO_CONFIG`, `OD_P_DIO=0`, `OD_N_DIO=1`, `RX_DMA_OD`
- Audio Sink: `SAMPLING_CLK_SRC=AUDIOSINK_CLK_SRC_DMIC_OD`
- LPDSP32Context 类型、DSP_State 枚举
- extern 声明: `lpdsp32`, `BufferOut`, `audio_sink_cnt`, `audio_sink_period_cnt`, `flag_ascc_phase`
- 函数声明: `App_Process_Incoming_Data`, `App_Process_Connected`, `App_Process_Link_Disconnected`, `ClearBufferOut`, `Initialize_Receiver_Audio_Output`

### 4. `code/app_init.c`
新增函数 (在 `#ifdef APP_RM_ENABLE` 下):
- `App_CodecInitialize()` — 初始化 LPDSP32 codec 框架，加载 DSP 固件
- `Initialize_Raw_OD_Output_Type()` — 配置 OD DIO / AUDIOCLK / OD 外设 / DMA
- `Initialize_ASCC()` — 配置 Audio Sink Clock Counter
- `Initialize_ASRC()` — 配置 ASRC + DMA 通道
- `Initialize_Receiver_Audio_Output()` — 总入口：DSP IRQ → codec → OD → ASCC → ASRC
- `ClearBufferOut()` — 清零输出 buffer

在 `App_Initialize()` 的 `APP_RM_Init()` 之前调用 `Initialize_Receiver_Audio_Output()`

DSP 固件加载通过 `Sys_Flash_Copy()` 将 `.dsp` section 数据从 Flash 拷贝到 DSP DRAM

### 5. `code/rm_app.c`
RM_Callback_TRX 中 GOODPKT/BADCRCPKT/NOPKT 分支：
- 删除当前仅打日志的逻辑
- 改为调用 `App_Process_Incoming_Data(ptr, *length)`

RM_Callback_StatusUpdate 中：
- `LINK_DISCONNECTED`: 调用 `App_Process_Link_Disconnected()`
- `LINK_ESTABLISHED`: 调用 `App_Process_Connected()`

### 6. `code/app_process.c`
修改 `AUDIOSINK_PERIOD_IRQHandler`:
- 当 `audio_streaming==1`: 执行音频时钟周期测量 (同 raw demo)
- 当 `audio_streaming==0`: 保持现有 RC OSC 测量逻辑

### 7. `RTE/Device/RSL10/sections.ld`
- 新增 `DRAM_DSP_CM3` 内存区域 (2KB at 0x20011800)
- 缩减 `DRAM_DSP` 从 48K 到 46K
- 新增 `dspshared` output section 映射 `.shared` 到 `DRAM_DSP_CM3`

### 8. `peripheral_server_sleep.rteconfig`
- 添加所有新建源文件到 managed component 列表

## 不变的文件
- `app.c` — Main_Loop 已有 audio_streaming 判断 (WFE vs sleep)，无需改动
- `ble_custom.c/h` — RM ON/OFF BLE 控制完好

## 验证
1. 编译通过
2. 硬件测试：连接 RM 发射器，确认 BLE 发送 RM_ON 后耳机有音频输出
3. 确认 RM_OFF 后停止音频，恢复低功耗 sleep
