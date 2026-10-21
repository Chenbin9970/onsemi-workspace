# remote_mic_rx_coex_1664_pcm_passthrough 开发文档

## 1. 工程概述

`remote_mic_rx_coex_1664_pcm_passthrough` 由 `remote_mic_rx_coex_1664`（提交 `1184f0f`）复制而来，
用于验证**同板有线 PCM 透传**：外部音源作为 PCM 从机，与 DSP7100 共用同一组 BCLK/FS，
数据从 RSL10 的 SERI（DIO12）进入，**原样**从 SERO（DIO14）转发给 7100，**取代 RM 无线音频**
作为主音频通路。原 1664 工程不做任何修改。

本工程只验证硬件通路，不涉及 7100 参数、Rempro 协议和 BLE 服务，这些逻辑与 1664 完全一致。

**状态**：2026-10-21 上板实测，音源经 DIO12 输入、RSL10 转发到 DIO14、7100 出声正常。

## 2. 硬件拓扑

```
外部 PCM 从机音源                    RSL10                        DSP7100
   BCLK  IN  ◄──────────────────── DIO2 ◄──────────────────────── BCLK OUT
   FS    IN  ◄──────────────────── DIO3 ◄──────────────────────── FS   OUT
   DATA OUT ──────────────────────► DIO12 (SERI)
                                    DIO14 (SERO) ────────────────► DATA IN
```

7100 是 PCM 主机，RSL10 与外部音源都是从机。RSL10 不产生任何 PCM 时钟。

### 2.1 引脚

| 信号 | 引脚 | 方向 | 说明 |
|------|------|------|------|
| BCLK | DIO2 | 输入 | 7100 提供，384 kHz |
| FS | DIO3 | 输入 | 7100 提供，12 kHz |
| SERI | DIO12 | 输入 | 外部音源数据；原 UART 打印口让出 |
| SERO | DIO14 | 输出 | 转发给 7100；需在初始化时关 JTAG 释放（沿用 1664） |
| 7100 I2C | DIO0/DIO1 | 双向 | 与 1664 相同，不受透传影响 |
| 7100 握手 | DIO13 / DIO11 | 输入 / 输出 | 与 1664 相同 |
| 7100 状态 | DIO9/DIO10 | 输入 | 与 1664 相同 |
| 调试打印 | — | — | 本工程走 **RTT**，不再占用 UART 引脚 |

PCM 四个 pad 统一配置为 **无上下拉**（`DIO_NO_PULL`）；DIO2/DIO3/DIO12 为输入，
DIO14 为 `DIO_MODE_PCM_SERO` 输出。

## 3. PCM 线上格式（音源必须匹配）

| 项目 | 值 |
|------|-----|
| 主机 | 7100 |
| BCLK | 384 kHz |
| FS | 12 kHz |
| 每帧 | 32 BCLK = **2 个 16-bit 字** |
| FS 波形 | 50% 方波（高 16 BCLK / 低 16 BCLK） |
| 有效字率 | **24,000 字/秒** |
| 位序 | MSB first |
| 数据格式 | 有符号 16-bit 补码 |
| 帧对齐 | frame-align-first；FS 高电平期间为第 1 个字，低电平为第 2 个字 |
| 采样沿 | RSL10 在 BCLK **下降沿**采样（极性位默认 0），音源应在**上升沿**更新数据 |
| 槽内容 | 两个字是**同一条单声道流的连续采样**，不是 L/R 声道 |

24k 的来源：`PCM_FRAME_WORDS = 3 × FRAME_LENGTH / 4 = 120` 字，注释与实现均按 24k
（`app_func.c` 的 ASRC 配置注释："输出速率 = 2×FS（每 FS 周期 2 采样）"）；tx_pcm 工程
上板实测同样得到 24 ksample/s、FS ≈ 12 kHz、32 BCLK/帧。

若音源原生只有 12 kHz 数据，应把同一采样在两个槽各填一次；不要把两个字按 L/R 分配。

## 4. 软件设计

### 4.1 开关与关键宏（include/app.h）

| 宏 | 值 | 作用 |
|----|----|------|
| `PCM_RELAY_INPUT` | 1 | 总开关；0 = 回到 1664 原无线路径（RM → 解码 → ASRC → PCM） |
| `PCM_SER_DI` | 12 | SERI 输入脚（原为 4） |
| `PCM_RELAY_DMA_NUM` | 1 | 透传采集 DMA 通道（DMA0 给 RM memcpy、DMA2 空闲，取 1） |
| `PCM_RELAY_RX_DMA` | — | `PCM->RX_DATA → pcm_tx_buf`，32-bit 字、LIN、完成中断 |
| `PCM_CLK_DO` / `PCM_FRAME_SYNC` / `PCM_SER_DO` | 2 / 3 / 14 | 沿用 1664 |
| `PCM_FRAME_WORDS` | 120 | 每缓冲 120 字 = 5 ms @24k |
| `PCM_CFG_TX` | — | 16-bit、MSB first、frame-align-first、long FS、2 words/frame、slave |

### 4.2 初始化（code/app_init.c）

`Initialize_Raw_PCM_Output_Type()`：

1. `Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_NO_PULL, 2, 3, 12, 14, DIO_MODE_INPUT)`
   —— 四根 PCM pad：输入无上下拉，SERO 由该函数最后配置为 PCM 输出。
2. `Sys_PCM_Config(PCM_CFG_TX)` —— **收和发共用一套 CTRL**，因此音源必须匹配 §3 的格式。
3. 配置 ch5（PCM TX DMA），初始化时**不使能**。
4. `PCM_RELAY_INPUT=1` 时配置并使能 ch1：`PCM->RX_DATA → pcm_tx_buf[0]`，
   完成中断换手；ch4（ASRC → 输出）沿用原代码但只在 `PCM_RELAY_INPUT=0` 时编译。
5. `Sys_PCM_Enable()` —— 必须等 LIN DMA 武装好后才使能。

`App_Initialize()` 中与 1664 的差异：

- 删除 UART 打印脚覆写（原先把打印改到 DIO12）；打印改由编译宏
  `OUTPUT_INTERFACE=OUTPUT_RTT` 走 SEGGER RTT。
- `PCM_RELAY_INPUT=1` 时**不调用** `APP_RM_Init()` / `RF_SwitchToCPMode()` / `RM_Enable()`。
- 7100 上电握手、106 步引导、I2C 读回、DIO11 引导后脉冲等逻辑保持不变。

### 4.3 中断与双缓冲（code/app_func.c）

`Pcm_relay_rx_dma_isr()`（`DMA1_IRQHandler` 别名）：

1. ch1 收满 `pcm_tx_buf[pcm_fill]`（120 字）后，把它标记为 `pcm_ready`，翻转 `pcm_fill`；
2. 重新武装 ch1 指向新的 fill 缓冲；
3. 若 ch5 空闲（`pcm_waiting=1`），用 `pcm_ready` 缓冲启动 ch5；否则 ch5 完成后自然续流。

ch5 的 `Pcm_tx_dma_isr()` 与 1664 相同：有 ready 缓冲就续流，没有就置 `pcm_waiting`。
两个中断同优先级、同帧边界完成，谁先到都能正确交接：

- ch1 先到：设置 `pcm_ready` → ch5 中断取走；
- ch5 先到：置 `pcm_waiting` → ch1 中断启动 ch5。

ch5 未在初始化时使能，避免首帧竞争；只有 ch1 完成第一次采集后才开始出数。

### 4.4 RM 完全停用

透传模式下 RM 无线通路**不存在**（不是"只让回调失效"）：

| 位置 | 改动 |
|------|------|
| `code/app_init.c` | 不 `APP_RM_Init`、不 `RF_SwitchToCPMode`、不 `RM_Enable`；RM 定时器和 RF 中断都不会启动 |
| `app.c` | 主循环不调用 `RM_StatusHandler()`；不打印 RM 流地址 |
| `code/ble_custom.c` | BLE 的 RM 开关（`CS_REMPRO_IDX_ONOFF_VALUE_VAL`）只存值，不 Configure/Enable |
| `code/rm_app.c` | `RM_Callback_TRX` / `RM_Callback_StatusUpdate` 保留早退，作为防御性代码（实际不会被调用） |

RM 库仍被链接，只为保证现有 BLE 服务/Rempro 代码可以编译；不产生任何射频行为。
副作用：`app_env.audio_streaming` 恒为 0，主循环不再以"RM 推流中"为由跳过 BLE 指令处理。

### 4.5 工程配置

- 复制后重命名：`.project` 名称、三个 `.cproject*` 内的 `rteConfig`、
  `remote_mic_rx_coex_1664_pcm_passthrough[_fota|_nofota].rteconfig`。
- `.cproject` / `.cproject_fota` / `.cproject_nofota` 的 C 编译宏中增加
  `OUTPUT_INTERFACE=OUTPUT_RTT`（每个文件的两个编译配置块各一处）。
- `Debug/` 为 Eclipse 构建产物，已由 `.gitignore` 排除。

## 5. 运行时数据流

```
音源 SERI ──► PCM->RX_DATA ──ch1 DMA──► pcm_tx_buf[pcm_fill] (120 字 = 5 ms)
                                             │ 完成中断换手
                                             ▼
                             pcm_tx_buf[pcm_ready] ──ch5 DMA──► PCM->TX_DATA ──► SERO ──► 7100
```

- 每 5 ms 一次 ch1 完成中断、一次 ch5 完成中断；稳态下两个缓冲交替。
- 总延迟约 **5–10 ms**（一到一个半缓冲）。
- 收发使用同一组 7100 时钟，**不存在漂移**，不需要 ASRC。
- 7100 不上电/不供时钟时，ch1 不会完成，ch5 保持空闲，不会误发数据。

## 6. 与 1664 的差异汇总

| 项目 | 1664 | 本工程 |
|------|------|--------|
| 音频源 | RM 无线 → 解码 → ASRC 16k→24k | 外部 PCM 从机 → DIO12 |
| PCM SERI | 未用（DIO4） | DIO12 |
| PCM 输出 | DIO14 | DIO14（不变） |
| 采样源 DMA | ch4：`ASRC->OUT` | ch1：`PCM->RX_DATA` |
| RM | 初始化并 `RM_Enable` | 完全不启动 |
| 打印 | UART DIO12 | RTT |
| PCM pad | 弱上拉 | 无上下拉 |
| 双缓冲状态机 | 相同 | 相同 |

## 7. 编译与调试

### 7.1 编译

Eclipse 导入 `remote_mic_rx_coex_1664_pcm_passthrough`，使用默认的
`remote_mic_rx_coex_1664_pcm_passthrough.rteconfig` 编译。打印已切换到 RTT，无需接 UART。

### 7.2 调试手段

| 方法 | 内容 |
|------|------|
| RTT Viewer | 启动日志、7100 读回日志、Rempro 命令日志 |
| J-Link 变量 | `pcm_fill`、`pcm_ready`、`pcm_waiting` 应持续翻转；`pcm_tx_buf` 内容应跟随音源 |
| `PCM->STATUS` | 检查 overrun / underrun |
| 逻辑分析仪 | DIO2=384 kHz、DIO3=12 kHz（32 BCLK/帧）、DIO12 与 DIO14 波形一致、延迟约 5–10 ms |

### 7.3 常见问题定位

| 现象 | 优先检查 |
|------|----------|
| 完全没声 | DIO3 是否有 12 kHz FS；DIO2 是否有 384 kHz BCLK；`pcm_waiting` 是否一直为 1 |
| 噪声/变调 | 音源格式是否匹配 §3（边沿、MSB 延迟、帧同步极性、24k/12k） |
| 声音正常但偶发爆音 | `PCM->STATUS` 是否 overrun；音源是否在帧中间停钟/高阻 |
| DIO14 无输出 | 是否关掉了 JTAG（DIO14 默认是 JTAG 脚）；ch5 是否被首次 ch1 中断启动 |

## 8. 已知限制 / 待确认

1. **编译期选路**：`PCM_RELAY_INPUT` 为编译宏，没有运行时"有线优先、无线回退"。
2. **无音源时**：DIO12 无上下拉，悬空时会采到随机数据并原样转发，可能出现噪声；
   未做静音检测。
3. **收发共用一套 PCM CTRL**：接收采样沿（下降沿）与已验证的发送格式绑定，
   不能单独修改；音源必须适配，而不是改 RSL10 的采样沿。
4. **RM 库仍链接**：Flash/RAM 占用未优化；BLE 的 RM 参数接口仍存在，但只操作内存变量。
5. **解码/ASRC/DSP 路径仍编译并初始化**（`OUTPUT_DECODE_PATH` 包含 `PCM_SLAVE_OUTPUT`），
   透传模式下 ch4 不使能、不参与数据流；后续若做功耗优化可考虑裁剪。
6. 本工程只验证了 24k 单声道流；多声道、其他采样率不在当前范围。

## 9. 验证记录

| 日期 | 内容 | 结果 |
|------|------|------|
| 2026-10-19 | 由 1664（`1184f0f`）复制并完成透传改造 | 未编译 |
| 2026-10-19 | RM 改为完全不启动；PCM 输入脚改无上下拉 | 未编译 |
| 2026-10-21 | 外部 PCM 从机音源接入 DIO12，上板实测 | **功能正常**：7100 正常出声，BLE/7100 控制不受影响 |
