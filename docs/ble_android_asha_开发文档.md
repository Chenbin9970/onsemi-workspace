# ble_android_asha — 开发文档

> 工程：`ble_android_asha`（RSL10 ASHA 助听器示例工程，G722 解码 + ASRC + 输出）
> 状态：**2026-09-23** —— PCM 输出打通；音频出口已升级为 **24k**（`WORD_SIZE_16`）；音高/变调问题已定位并修好（§4.5①）。**遗留：音乐鞭炮声/持续哒哒只压掉约 3.5 倍、未根除**（§4.5②，暂停排查）
> 目标：把 ASHA 解码音频经 **PCM 从机接口** 输出给 7100 DSP（参照 **7160test 工程** 移植，**不是**原 ASHA 的 PCM_TX_RAW_OUTPUT）
> 工具链：GNU ARM Embedded 13.3，`EZAIRO_71XX_DIO_CFG=7100`，Debug 构建

## 1. 工程基线

- 原始 demo：`a74d806 feat: 新增 ble_android_asha ASHA 助听器示例工程`（SPI0 输出到 Ezairo）
- 2026-09-01 回退到纯净 demo 基线，commit `b56c70f`
- 音频流：BLE ASHA → G722 解码(16kHz, 160 采样/10ms 帧) → ASRC → 输出
- DSP 解码输出：**16 kHz**（`FRAME_LENGTH=160` @ 10ms）；喂入 80 字节编码帧/次

## 2. 当前 IO 分配（2026-09-01 最终）

| DIO | 用途 | 说明 |
|-----|------|------|
| 0 | **I2C SCL**（7100） | 硬件 I2C0，~410kHz，强上拉。原 SPI `CS_DO` 已禁用让出 |
| 1 | **I2C SDA**（7100） | 同上。原 SPI `SER_DO` |
| 2 | **PCM BCLK 输入** | 7100 提供 384kHz |
| 3 | **PCM FS 输入** | 7100 提供 12kHz（兼作 audiosink 采样钟 `SAMPL_CLK`） |
| 4 | **UART RX** | 调试串口（app_trace） |
| 5 | **UART TX** | 调试打印 115200，DMA ch7（后改 DIO12，见 §3.3） |
| 6 | ASCC 相位调试 | `ASCC_PHASE_ISR_DIO` |
| 7 | ASHA 事件 | `ASHA_EVENT_DIO` |
| 8 | 空闲 | 原 audiosink 采样钟位置（已改用 DIO3） |
| 9 | **PCM SERI 输入** | 未用（从机不回传），仅占脚 |
| 10 | 空闲 | |
| 11 | 空闲 | |
| 12 | 恢复脚 / **UART TX** | 上电恢复检查后 UART 占用（app_trace `UART_TX=12`） |
| 13 | 空闲 | |
| 14 | **PCM SERO 输出** | **RSL10 的 JTAG 数据脚，必须关 JTAG 释放（关键坑！）** |

**PCM 脚**：BCLK=2、FS=3、SERI=9、SERO=14。

## 3. 已实现功能

### 3.1 I2C 心跳（7100 通讯基础）

- 驱动：`source/i2c_7100_hal.c` + `include/i2c_7100_hal.h`（从 7160test 原样拷贝）
  - 硬件 I2C0，SCL=DIO0/SDA=DIO1，prescale=12 → ~410kHz，中断驱动
  - 从机地址 `I2C_7100_ADDR` = **0x02**（7-bit）；`Sys_I2C_StartWrite` 内部左移 1 位
  - ACK 极性：`I2C_HAS_ACK=0`，STATUS bit0 **=0 表示从机 ACK**
- 心跳：`APP_7100_HB_TIMER`（200ms ke_timer，**广播开始后**启动，自续），每 25 tick（5s）向 0x02 发 `{0x88, 0x01}`
  - **踩坑**：ke_timer 不能在 `main()` 里 BLE 栈初始化前 set（会被栈初始化清掉），要放到消息处理器里（服务注册完成后）
- SPI 初始化已 `#if 0`（DIO0/1 让给 I2C）

### 3.2 非破坏抓数基建（cap_in / cap_out）

- 全局（`app_func_audio.c`）：`cap_in[512]`、`cap_out[512]`、`cap_in_idx`、`cap_out_idx`、`cap_done`（CAP_N=512，共 2KB，避开 4KB 挤堆重启问题）
- `cap_in`：在 `DSP0_IRQHandler` 被动拷贝 `frame_dec`（160 采样/帧）——不碰 DMA
- `cap_out`：在 **ch4 完成中断**（`Pcm_asrc_out_dma_isr`）从 `pcm_tx_buf` 读 **24k** 数据（完整流）
- 两路都满 → `cap_done=1` 冻结；`APP_Audio_Start` 复位重抓

### 3.3 打印口

- 默认 DIO5（app_trace.h `UART_TX`）；调试后期改 **DIO12**（上电兼恢复脚，`UART_TX=12`）

## 4. 调试结论（2026-09-01）

### 4.1 DSP 解码 ✅ 正常

- 手机放 1k 正弦 → cap_in 干净 1k 峰（1kHz @ +72.2dB），512 采样 = 32 个完整周期，逐点一致

### 4.2 ASRC 缺 sink 时钟（已解决）

- 初版 cap_out 恒空 → audiosink 无有效时钟（`SAMPL_CLK` 原在 DIO8 无信号）→ ASRC `inc=0` 卡死
- 解决：`SAMPL_CLK` 改跟 **PCM FS（DIO3，12k）**；且 `Sys_Audiosink_Config` 后必须显式启动 PHASE/PERIOD 计数器（`AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS/PERIOD_CNT_START_ALIAS`）——demo 原来只在初始化顶部设了 PERIOD 且被 `ResetCounters` 清掉

### 4.3 ASRC 输出 8k（已解决）

- 现象：cap_out 主峰 1500Hz（12k 假设下）= 实际 1kHz@8k；PHASE_INC=0x10000000
- 根因：demo 的 `asrc_reconfig` 范围检查**钳位 Ck**（`Ck=Ck_prev`），12k 的 Ck≈240 被拒（范围按 spp±20 假设 sink≈输入 16k）→ 钳到错误值 → 8k
- 解决：**去掉 Ck 钳位**（7160test 同款，只重置稳定计数）+ Ck≠0 防除零

### 4.4 **DIO14 卡高（PCM 不移位）——最终根因：JTAG 占用**（关键坑！）

- 现象：PCM 配置/数据全对（CTRL=0x0F8F、TX_DATA 有音频、BCLK/FS 正常），但 DIO14 持续高电平，PCM 不移位
- 排查：AUDIO 块配置、AUDIOCLK、时钟分频、DIO3 上拉全部试过无效；`0x5555` 直写也不移位
- 根因：**DIO14 是 RSL10 的 JTAG 数据脚**。7160test 显式关 JTAG 释放 DIO14，demo 没关 → JTAG 占用，`DIO->CFG[14]=PCM_SERO` 不生效
- 解决：`Initialize_Raw_PCM_Output_Type` 里关 JTAG：
  ```c
  DIO_JTAG_SW_PAD_CFG->CM3_JTAG_DATA_EN_ALIAS = CM3_JTAG_DATA_DISABLED_BITBAND;
  DIO_JTAG_SW_PAD_CFG->CM3_JTAG_TRST_EN_ALIAS = CM3_JTAG_TRST_DISABLED_BITBAND;
  ```

### 4.5 音乐鞭炮声 → 持续"哒哒"（① 已解决；② 部分改善、**未根除**）

现象：音乐播放伴随"鞭炮声"；1k 纯音听感上也有**持续、规律的"哒哒哒哒"**。

**根因是两个独立问题叠在一起**（都实测定位）：

**① 音高偏 0.9%（变调）——ASRC 用了"按包计数"的闭环跟踪**

- 把 1664 的 24k 配置搬过来时，ASRC 也照搬了 1664 的闭环 `inc=(Cr-2Ck)/2Ck`。该公式隐含假设
  **「每个重配置间隔送来的输入样本数 = Cr = samples_per_packet(=320=20ms)」**。
- ASHA 的包间隔实测 **≈19.7ms（0.63s ÷ 32 条日志）≠ 包时长 20ms** → 前提不成立 →
  闭环把「20ms 音频在 19.7ms 内播完」配成 **+1%~1.5%** 速率。
- 实测（J-Link 抓 cap_out，24k，线性插值零穿越量频率，精度 ±0.05%）：
  | 信号 | 频率 |
  |---|---|
  | cap_in（16k 解码输出） | 1000.00 / 1000.03 Hz ✓ |
  | cap_out（24k 送 7100） | **1009.05 / 1009.02 Hz ✗（+0.9%）** |
- 修法：**改回名义固定比例** `inc=0xF5555556`（INT_MODE，16k→24k）+ `Sys_ASRC_Config(..., WIDE_BAND|ASRC_INT_MODE)`；
  修后实测 cap_out = **1000.03 / 1000.04 Hz** ✓。
- ⚠ **1664 能用闭环只因为 RM 包间隔精确 10ms（`FRAME_LENGTH=160`=10ms）**，前提成立；
  ASHA 不成立，**不要照搬闭环**（详见 `app_func_audio.c:asrc_reconfig` 的注释）。

**② PCM TX 欠载（"哒哒"本体）——2 块缓冲零余量，ch5 被抖动挤空**

- 原握手：ch4 填一块、ch5 立刻抢着流，**在途永远只有 1 块（5ms）→ 零余量**；只要瞬时抖动让
  ch5 比 ch4 早一步，代码就 `pcm_waiting=1` 让 PCM 处于**未武装**状态 → FIFO 见底 →
  移位器吐出坏采样 → 一个"哒"。
- 直接证据（J-Link 读 **PCM STATUS @0x40000A0C**，bit0=UNDERRUN、bit1=OVERRUN）：
  - 该状态位是**锁存**的，直接写寄存器清不掉，要用 **bit-band 别名**：
    `w4 0x42014180 1`（清 UNDERRUN）/ `w4 0x42014184 1`（清 OVERRUN）；
  - 清掉后每 20ms 读一次：`02 02 02 03 02 02 02 03 ...` → 20 次里 **7 次重新置位**
    = 欠载约 **十几 Hz** ✓ 与听感"哒哒哒哒"吻合；
  - 它在 `pcm_tx_buf` **内容里看不到**（坏采样发生在 FIFO/移位器那级）→ 所以 12 个抓数窗口全干净，
    用抓数找不到，必须查 STATUS 位。
- 为什么 1664 没有：**同样的 PCM/ASRC 代码 + 一致（对照实验确认）的 7100**，但 RM 路径的中断时序抖动小；
  ASHA 有 BLE 收包/队列（`QueueInsert` 里 `malloc` + `memcpy`，高优先级上下文）→ 抖动大。
- 修法：**PCM 输出改环形缓冲 `pcm_tx_buf[PCM_TX_BUFS][120]` + 首播预充 `PCM_PREFILL` 块**
  - ⚠ **关键在预充**：只加块数不预充，ch5 仍会立刻抢上、余量还是 0；
  - 余量 ≈ `(PCM_PREFILL-1)×5ms`，ch5 稳定落后 ch4 若干块，把抖动吸收掉；
  - 同一探针（清 UNDERRUN 后每 20ms 读一次 × 20）的迭代结果：
    | 配置 | 余量 | UNDERRUN 重新置位 |
    |---|---|---|
    | 2 块（原） | 0 | **7 / 20** |
    | 4 块 + 预充 2 | 15ms | 2 / 20 |
    | **8 块 + 预充 6（当前）** | 25ms | **1 / 20**（继续加深收益递减 → 不是缓冲被抽干，而是补数晚到） |
- 结果（2026-09-23 上板）：4 块版本已把"哒哒"从十几 Hz 降到偶发；8 块版进一步加大余量。
  若加大后 UNDERRUN 归零但仍有零星"鞭炮声"，则残余来自**上游 ASHA 流水**（手机侧交付速率
  与消费不匹配 → 帧被覆盖，对应 demo 自带的 `ASSERT(!env_audio.proc)//TODO` 告警），需在
  ASHA 的队列/渲染侧处理（如 `QueueInsert` 的 `malloc` 换静态池）。

**已排除（都有实测支撑，勿再重复怀疑）**

| 怀疑项 | 排除依据 |
|---|---|
| PCM 数据本身（丢样/重采样/量化/AM） | 12 窗口（约 30s）残差 RMS≈70 LSB（−45dB，其中 ~39 LSB 来自 G.722 解码）；残差峰值 155~197 ≪ 丢一个采样应有的 ~6600；幅度波动仅 0.05%（无 AM） |
| 队列饥饿 → 空队列跑 PLC | 40 次采样 `audio_queue.front` 从未为 0，稳定 3~4 帧深 |
| 序号误判「丢包」→ 逐包 PLC | 11 秒日志中**没有**任何 `APP_Audio_Transfer: seq_num_prev=...` 打印 |
| 渲染节拍与帧时长不匹配 | `AUDIO_INTV_PERIOD=10000`（10ms @1MHz SLOWCLK/2）= 帧时长 10ms ✓ |
| 音量/AGC 变化（`Volume_Shift_Subframe`） | 跨 30s 各窗口幅度稳定 ±0.05%，无增益跳变 |
| 速率环 | 修①后 inc=0xF5555556（日志实测）、音高 1000.03Hz |

## 5. PCM 输出实现（参照 7160test/1664，**24k 版**）

- **引脚**：BCLK=DIO2、FS=DIO3、SERI=DIO9（未用）、SERO=DIO14
- **PCM 配置**：`PCM_CFG_TX` = MSB_FIRST | TX_ALIGN_LSB | **WORD_SIZE_16** | FRAME_ALIGN_FIRST | FRAME_WIDTH_LONG | MULTIWORD_2 | SUBFRAME_ENABLE | CONTROLLER_DMA | SLAVE
  → 每 FS 周期 2×16-bit = 32 BCLK，**有效采样率 24k**（7100 按 24k 读，采样在 32-bit 字**低 16 位**）
  （原 `WORD_SIZE_32` 每帧只装 1 个 32-bit 字 → 有效 12k）
- **数据流**：G722(16k) → ch3 → ASRC(**INT_MODE + 名义固定 inc=0xF5555556**，16k→24k) → ch4 →
  `pcm_tx_buf[8][120]`（**环形，8×5ms，首播预充 6 块 → 余量 25ms**） → ch5 → `PCM->TX_DATA` → SERO(DIO14)
- `SAMPL_CLK` = PCM_FS（DIO3），audiosink 同步
- **环形握手**（`Pcm_asrc_out_dma_isr` / `Pcm_tx_dma_isr`，DMA4/DMA5 别名）：
  `pcm_wr`(ch4 填) / `pcm_rd`(ch5 播) / `pcm_cnt`(待播块数) / `pcm_playing` / `pcm_started`；
  ch5 空闲时由 ch4 完成中断启动：**首播 `pcm_cnt >= PCM_PREFILL(6)`，续播 `pcm_cnt > 0`**（见 §4.5②）
- `APP_Audio_Start`：**每次开流重新 `Sys_PCM_Config(PCM_CFG_TX)` + 重配 ch4 + 清 NVIC pending**
  （同 1664 建链；只 `Sys_PCM_Enable` 会把上次会话残留的 PCM 状态/标志带过来）；`APP_Audio_Disconnect` 停止；
  `TIMER_FRAME_TX_END` 不再逐帧关 ASRC（PCM 保持连续）
- **与 1664 同步的**：`AUDIO->OD_CFG/SDM_CFG/OD_GAIN` 三条寄存器；ASRC 卡死看门狗（`ASRC->PHASE_CNT`
  连续 20 次不变 → `Sys_ASRC_Reset()` 自恢复）；开流重初始化 PCM 块
- **刻意不搬的**：1664 的 `asrc_stable`/`cntr_stability`（在 1664 里是死代码，只写不读）；
  Ck 闭环（§4.5① 已证 ASHA 上会变调）
- **改动文件**：`include/app_audio.h`、`source/app_init_audio.c`、`source/app_func_audio.c`

## 6. 已知问题 / 待办

| 项 | 状态 | 说明 |
|----|------|------|
| DSP G722 解码 | ✅ 正常 | 16k 干净（1k 验证） |
| ASRC 16k→24k | ✅ 正常 | **INT_MODE + 名义固定 `inc=0xF5555556`**（不要用 Ck 闭环，见 §4.5①） |
| PCM 输出 | ✅ 24k | `WORD_SIZE_16 + MULTIWORD_2`，DIO14 移位正常 |
| **音乐鞭炮声 / 持续哒哒** | ⚠ 部分改善 | PCM TX 欠载 7/20 → 1/20（8 块环形+预充）；**残留未根除**。已排除：ASRC 输入饿、BB 配置、ASCC/DIO3 顺序、DMA 优先级、队列饥饿、序号误判；见 §4.5 末尾 |
| **7160test 金属尾音** | ❌ 遗留 | 7100 侧问题（本工程同款），见 `docs/pcm/7160test_音频杂音调试.md` |
| 7100 I2C 心跳 | ✅ 已加 | 只保留 5s 发送，完整协议未搬 |
| SPI 输出 | 已禁用 | DIO0/1 让给 I2C |
| ASHA 侧队列开销 | ⚠ 待优化 | `QueueInsert` 里 `malloc` + 高优先级上下文 → 偶发 >10ms 延迟（欠载根因）；见 §4.5 末 |

## 7. J-Link 抓数/探测流程

### 7.1 音频数据抓数（cap_in / cap_out）

前置：手机连 ASHA 并持续播音频；`cap_done=1` 即抓到。地址每次构建从 `Debug/*.map` 取。

```
# JLink Commander（设备 RSL10，SWD）
w4  <cap_in_idx_addr> 0          # 只需复位两个索引即可重抓（cap_done 不参与采集判断）
w4  <cap_out_idx_addr> 0
sleep 400
savebin D:/tmp/cap_in.bin  <cap_in_addr>  0x400    # 512×int16
savebin D:/tmp/cap_out.bin <cap_out_addr> 0x400
```

> ⚠ **J-Link 字节写陷阱**：`w1 <addr> <v>` 在 RSL10 上会**连带改写 addr+1**。
> 本工程 `cap_done(0xa2c) / pcm_fill(0xa2d) / pcm_ready(0xa2e) / pcm_waiting(0xa2f)` 紧挨着——
> 早期用 `w1` 复位 cap_done 会顺手打坏三兄弟。**要么只用 `w4` 写 4 字节对齐的变量，
> 要么读整字→只改目标字节→写回**。另：`savebin` 的长度按**十六进制**解析，写 `0x400` 而不是 `1024`。

分析：`python scripts/analyze_cap.py D:/tmp/cap_in.bin D:/tmp/cap_out.bin --fs_out 24000`
（cap_in 16k、**cap_out 现为 24k**）

### 7.2 PCM TX 欠载探针（本次定位"哒哒"的关键工具）

`PCM STATUS` 在 **0x40000A0C**：bit0=UNDERRUN、bit1=OVERRUN，**锁存位**，直接写寄存器清不掉，
必须走 **bit-band 别名**：

```
w4 0x42014180 1      # 清 UNDERRUN（bit0 别名）
w4 0x42014184 1      # 清 OVERRUN （bit1 别名）
sleep 20
mem32 0x40000A0C 1   # 0x2=只溢出；0x3=这段里又发生了欠载
```

反复"清 → 等 → 读"，**重新置位的频率 = PCM 欠载频率**。这条在 `pcm_tx_buf` 内容里**看不出来**
（坏采样发生在 FIFO/移位器那级），是抓数查不到时的唯一直接手段。

### 7.3 量音高/速率（查变调、速率错）

用 J-Link 抓 cap_out 后，**别只看 FFT**（512 点分辨率 ≈47Hz，看不出 1% 误差），
用**线性插值零穿越**量频率：精度可达 ±0.05%，能分辨 1% 的速率差（本次就是靠它发现
1kHz 变成 1009Hz）。cap_in 与 cap_out 应给出**同一频率**。

## 参考

- PCM 移植基准：`peripheral_server_sleep7160test`（`docs/pcm/7160test_pcm_output.md`）
- 杂音调试：`docs/pcm/7160test_音频杂音调试.md`
- IO / 调试总览：`docs/7160调试过程.md`
- 抓数分析：`scripts/analyze_cap.py`
