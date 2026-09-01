# ble_android_asha — 开发文档

> 工程：`ble_android_asha`（RSL10 ASHA 助听器示例工程，G722 解码 + ASRC + 输出）
> 状态：**2026-09-01 调试中** —— DSP 解码已抓数验证正常，ASRC 缺 sink 时钟未转，PCM 输出待加
> 目标：把 ASHA 解码音频经 **PCM 从机接口** 输出给 7100 DSP（参照 **7160test 工程** 移植，**不是**原 ASHA 的 PCM_TX_RAW_OUTPUT）
> 工具链：GNU ARM Embedded 13.3，`EZAIRO_71XX_DIO_CFG=7100`，Debug 构建

## 1. 工程基线

- 原始 demo：`a74d806 feat: 新增 ble_android_asha ASHA 助听器示例工程`（SPI0 输出到 Ezairo）
- 2026-09-01 已回退到纯净 demo 基线（移除此前加的 PCM_TX_RAW_OUTPUT，恢复 SPI 输出），commit `b56c70f`
- 音频流：BLE ASHA → G722 解码(16kHz, 160 采样/10ms 帧) → ASRC → 输出
- DSP 解码输出：**16 kHz**（`FRAME_LENGTH=160` @ 10ms）；喂入 80 字节编码帧/次

## 2. 当前 IO 分配（2026-09-01）

| DIO | 用途 | 说明 |
|-----|------|------|
| 0 | **I2C SCL**（7100） | 硬件 I2C0，~410kHz，强上拉。原 SPI `CS_DO` 已随 SPI 禁用让出 |
| 1 | **I2C SDA**（7100） | 同上。原 SPI `SER_DO` |
| 2 | 空闲 | 原 SPI `SER_DI`，SPI 禁用后未配置 |
| 3 | 空闲 | 原 SPI `CLK_DO` |
| 4 | **UART RX** | 调试串口（app_trace，DIO4/DIO5） |
| 5 | **UART TX** | 调试打印 115200，DMA ch7 |
| 6 | ASCC 相位调试 | `ASCC_PHASE_ISR_DIO`（原 LED 位置，LED 已删） |
| 7 | ASHA 事件 | `ASHA_EVENT_DIO`（每收到音频包翻转） |
| 8 | **AUDIOSINK 采样钟输入** | `SAMPL_CLK`（原 DIO13 挪来，原"固定高电平"用途去掉） |
| 9 | 空闲 | |
| 10 | 空闲 | |
| 11 | 空闲 | |
| 12 | 恢复脚 | 上电按住暂停启动便于重刷 |
| 13 | 空闲 | 原 SAMPL_CLK |
| 14 | 空闲 | 原 Ezairo 复位 `RF_INT`（已删） |

**保留空闲脚：2、3、9、10、11、13、14** —— 给后续 PCM 接口预留。

## 3. 已实现功能

### 3.1 I2C 心跳（7100 通讯基础）

- 驱动：`source/i2c_7100_hal.c` + `include/i2c_7100_hal.h`（从 7160test 原样拷贝）
  - 硬件 I2C0，SCL=DIO0/SDA=DIO1，prescale=12 → ~410kHz，中断驱动
  - 从机地址 `I2C_7100_ADDR` = **0x02**（7-bit）
  - 关键：`Sys_I2C_StartWrite` 内部左移 1 位，不能传 0x04
  - ACK 极性：`I2C_HAS_ACK=0`，STATUS bit0 **=0 表示从机 ACK**
- 心跳：`APP_7100_HB_TIMER`（200ms ke_timer，**广播开始后**启动，自续），每 25 tick（5s）向 0x02 发 `{0x88, 0x01}`
  - 处理器在 `app_msg_handler.c`，注册在 `app.c`
  - **注意**：ke_timer 不能在 `main()` 里 BLE 栈初始化前 set（会被栈初始化清掉），要放到消息处理器里（服务注册完成后）——踩坑记录
- SPI 初始化已 `#if 0`（DIO0/1 让给 I2C）

### 3.2 非破坏抓数基建（cap_in / cap_out）

> 目的：J-Link 读 DSP 解码（16k）和 ASRC 输出（12k）实时数据，复刻 7160test 的抓数方案。

- 全局（`app_func_audio.c`）：`cap_in[512]`、`cap_out[512]`、`cap_in_idx`、`cap_out_idx`、`cap_done`（CAP_N=512，共 2KB，避开之前 4KB 挤堆的重启问题）
- `cap_in`：在 `DSP0_IRQHandler` 被动拷贝 `frame_dec`（160 采样/帧）——**不碰 DMA**
- `cap_out`：在 `DMA_IRQ_FUNC(ASRC_IN_IDX)` 轮询读 `ASRC->OUT` 寄存器——**不碰 ch4**（一次输入子帧采 1 个，~4k/s，欠采样但能看出有无信号）
- 两路都满 → `cap_done=1` 冻结；`APP_Audio_Start` 复位重抓
- 地址（每次构建变，从 `.map` grep）：`cap_in=0x20000224`、`cap_out=0x20000624`、`cap_in_idx=0x20000a24`、`cap_out_idx=0x20000a28`、`cap_done=0x20000a2c`

## 4. 调试结论（2026-09-01，J-Link 抓数）

### 4.1 DSP 解码：✅ 正常

- 手机放 1k 正弦 → cap_in 抓到**干净 1k 峰**：1kHz @ +72.2dB，第二强峰低 ~59dB
- 512 采样 = 32 个完整 1k 周期（每周期 16 采样 @16k），峰 +8193 / 谷 -8168，峰峰 ~16360 LSB
- 各周期波形逐点一致 → G722 解码链路完全正常

### 4.2 ASRC：❌ 没转（缺 sink 时钟）

- `cap_out` 恒空（`cap_out_idx=0`）→ ASRC 输入 DMA（ch3）从没完成 → 中断从没触发
- 根因：**ASRC 需要 audiosink sink 时钟才能消费输入**。`SAMPL_CLK` 在 DIO8 但无时钟信号 → audiosink 不计数 → `Ck` 恒初始值 → ASRC `inc=0` → 卡死
- 解决方向：给 audiosink 真实时钟（PCM FS 脚），或内部时钟 `AUDIOSINK_CLK_SRC_DMIC_OD`（7160test OD 模式用过）

### 4.3 重启排查（已解决）

- 一次性 ch4 抓取（P_TO_M LIN 1024 后停排空）→ 移除后不重启
- 机制：ch4 停排空 → ASRC 输出堵 → 输入溢出 → `ASRC_ERROR_IRQHandler` 反复触发（本工程 `ASSERT` 是空宏 `#if 0`，不直接复位，是 IRQ 风暴饿死主循环 → 看门狗复位）
- 结论：抓数不能停掉 ch4 排空；改非破坏方案（§3.2）

## 5. J-Link 抓数流程

前置：手机连 ASHA 并持续播音频；`cap_done=1` 或 `cap_in_idx=0x200`（满）即抓到。

```
# JLink Commander（设备 RSL10，SWD）
w1  <cap_done_addr> 0      ; 复位抓数
w4  <cap_in_idx_addr> 0
w4  <cap_out_idx_addr> 0
sleep 1000                 ; 等 DSP 重新填满
mem8 <cap_done_addr> 1     ; 看抓满状态
savebin C:/tmp/cap_in.bin <cap_in_addr> 1024    ; 512×int16 = 1024B
savebin C:/tmp/cap_out.bin <cap_out_addr> 1024
```

分析：`python scripts/analyze_cap.py C:/tmp/cap_in.bin C:/tmp/cap_out.bin`
（cap_in 默认 16k、cap_out 默认 12k；脚本按文件大小取采样数，512 也支持）

## 6. 已知问题 / 待办

| 项 | 状态 | 说明 |
|----|------|------|
| DSP G722 解码 | ✅ 已验证 | 16k 输出干净 |
| ASRC 转换 | ❌ 未转 | 缺 sink 时钟 |
| PCM 输出 | ⏳ 待做 | **参照 7160test 工程移植**（不是原 ASHA PCM_TX_RAW_OUTPUT） |
| 7100 I2C 心跳 | ✅ 已加 | 只保留 5s 发送，完整协议未搬 |
| SPI 输出 | 已禁用 | DIO0/1 让给 I2C |
| 堆占用 | ⚠️ 注意 | cap 缓冲已从 4KB 降到 2KB，避免挤占堆 |

### PCM 移植要点（参照 7160test，见 `docs/7160test_pcm_output.md`）

- 7100 做时钟主机：BCLK=384kHz、FS=12kHz（DIO 待定，不能占用预留脚 2/3/9/10/11/13/14）
- ASRC `DEC_MODE2` + `<<28`（16k→12k），`SAMPL_CLK` 跟随 PCM FS
- ch4 ASRC→`pcm_tx_buf`（P_TO_M，120 采样/10ms 帧），ch5 `pcm_tx_buf`→`PCM->TX_DATA`（M_TO_P，32-bit [word0=s,word1=s]）
- 数据在 word1（右声道）

## 参考

- PCM 移植基准：`peripheral_server_sleep7160test`（`docs/7160test_pcm_output.md`、`docs/7160test_od_output.md`）
- IO / 调试总览：`docs/7160调试过程.md`
- 抓数分析：`scripts/analyze_cap.py`
