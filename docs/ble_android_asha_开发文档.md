# ble_android_asha — 开发文档

> 工程：`ble_android_asha`（RSL10 ASHA 助听器示例工程，G722 解码 + ASRC + 输出）
> 状态：**2026-09-01 阶段性收尾** —— PCM 输出打通，音乐正常播放，**遗留：音乐鞭炮声（爆音）+ 7160test 金属尾音**
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
- `cap_out`：在 **ch4 完成中断**（`Pcm_asrc_out_dma_isr`）从 `pcm_tx_buf` 读 12k 数据（完整流）
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

### 4.5 音乐鞭炮声（遗留未解决）

- 现象：音乐正常播放但伴随"鞭炮声"（爆音）；**1k 正弦干净无爆音** → 内容相关
- 已确认：pcm_tx_buf 数据连续（缓冲边界跳变正常）、ASRC 速率已修对（固定 3:4，PHASE_INC=0x05555555）、1k 链路干净
- PCM STATUS 清后复置 **OVERRUN**（TX_DATA 被覆盖丢采样）
- 可能方向（未深入）：
  - G722（mode 1，64kbps）对复杂音乐的解码伪影（纯音干净、音乐爆）
  - 7100 侧处理音乐失真（7160test 的金属尾音也在此侧）
  - BLE 包间隔（~19.83ms）与 20ms 音频包时长不匹配的队列漂移

## 5. PCM 输出实现（参照 7160test，非原 ASHA 方案）

- **引脚**：BCLK=DIO2、FS=DIO3、SERI=DIO9（未用）、SERO=DIO14
- **PCM 配置**：`PCM_CFG_TX` = MSB_FIRST | TX_ALIGN_LSB | WORD_SIZE_32 | FRAME_ALIGN_FIRST | FRAME_WIDTH_LONG | MULTIWORD_2 | **SUBFRAME_ENABLE** | CONTROLLER_DMA | SLAVE；数据放 32-bit 字低 16 位（word1 右声道）
- **数据流**：G722(16k) → ch3 → ASRC(**DEC_MODE2 + 固定 inc=0x05555555**, 16k→12k) → ch4 → `pcm_tx_buf[2][120]`（双缓冲） → ch5 → `PCM->TX_DATA` → SERO(DIO14)
- `SAMPL_CLK` = PCM_FS（DIO3），audiosink 同步
- ch4/ch5 完成中断换手（`Pcm_asrc_out_dma_isr`/`Pcm_tx_dma_isr`，DMA4/DMA5 别名）
- `APP_Audio_Start` 武装 ch4/ch5 + `Sys_PCM_Enable`；`APP_Audio_Disconnect` 停止；`TIMER_FRAME_TX_END` 不再逐帧关 ASRC（PCM 保持连续）
- **改动文件**：`app_audio.h`（PCM 定义）、`app_init_audio.c`（JTAG 释放 + AUDIOCLK + PCM 初始化 + ch4/ch5 配置 + audiosink 计数器启动）、`app_func_audio.c`（ASRC 固定比例 + ch4/ch5 中断 + 武装/停止）

## 6. 已知问题 / 待办

| 项 | 状态 | 说明 |
|----|------|------|
| DSP G722 解码 | ✅ 正常 | 16k 干净（1k 验证） |
| ASRC 16k→12k | ✅ 正常 | 固定 3:4 比例 |
| PCM 输出 | ✅ 打通 | 音乐正常播放，DIO14 移位正常 |
| **音乐鞭炮声** | ❌ 遗留 | 1k 干净、音乐爆；OVERRUN 丢采样；见 §4.5 |
| **7160test 金属尾音** | ❌ 遗留 | 7100 侧问题（本工程同款），见 `docs/7160test_音频杂音调试.md` |
| 7100 I2C 心跳 | ✅ 已加 | 只保留 5s 发送，完整协议未搬 |
| SPI 输出 | 已禁用 | DIO0/1 让给 I2C |

## 7. J-Link 抓数流程

前置：手机连 ASHA 并持续播音频；`cap_done=1` 即抓到。

```
# JLink Commander（设备 RSL10，SWD）；地址每次构建从 .map grep
w1  <cap_done_addr> 0
w4  <cap_in_idx_addr> 0
w4  <cap_out_idx_addr> 0
sleep 1000
mem8 <cap_done_addr> 1
savebin C:/tmp/cap_in.bin <cap_in_addr> 1024
savebin C:/tmp/cap_out.bin <cap_out_addr> 1024
```

分析：`python scripts/analyze_cap.py C:/tmp/cap_in.bin C:/tmp/cap_out.bin`
（cap_in 默认 16k、cap_out 默认 12k）

## 参考

- PCM 移植基准：`peripheral_server_sleep7160test`（`docs/7160test_pcm_output.md`）
- 杂音调试：`docs/7160test_音频杂音调试.md`
- IO / 调试总览：`docs/7160调试过程.md`
- 抓数分析：`scripts/analyze_cap.py`
