# RSL10 Peripheral Server Sleep — 工作流程

## 系统启动

```
RESET
  │
  ▼
main()
  │
  ├─ [1] App_Initialize()          ← 硬件 + BLE + 音频初始化
  │     ├─ 关全局中断
  │     ├─ DIO12 拉低检测 → 恢复烧录模式
  │     ├─ 校准电压 (MANU_CALIB / USER_CALIB)
  │     ├─ 启动 48MHz 晶振 → SYSCLK 8MHz
  │     ├─ 初始化 BLE 基带 + 协议栈
  │     ├─ 设置 TX Power 0dBm
  │     ├─ 校准 RC 振荡器 → 配置 Sleep 参数
  │     ├─ [RM] LPDSP32 加载 G722 解码器
  │     ├─ [RM] ASRC DMA + OD (DIO8/DIO9) 初始化
  │     ├─ 配置 ADC (电池检测)
  │     ├─ App_Env_Initialize()
  │     │     ├─ 创建 TASK_APP 内核任务
  │     │     ├─ 启动 APP_Timer (200ms)
  │     │     ├─ 初始化 Custom Service
  │     │     └─ 初始化 Battery Service
  │     ├─ [RM] APP_RM_Init() → RF_SwitchToBLEMode()
  │     └─ 开全局中断
  │
  ├─ [2] 等待 3 秒 (重烧录窗口)
  ├─ [3] 点亮 LED (DIO6)
  │
  └─ [4] Main_Loop()              ← 主循环，永不返回
```

---

## 主循环 Main_Loop()

```
┌──────────────────────────────────────────────────────────┐
│  while (true)                                            │
│                                                          │
│  ① 喂看门狗                                               │
│                                                          │
│  ② 每 10 个 Sleep 周期触发一次 CS Notification            │
│                                                          │
│  ③ Measure_Battery_Level()                               │
│     └─ ADC 通道 0 读 VBAT/2 → 16 次滑动平均               │
│                                                          │
│  ┌─── 内层无限循环 ───────────────────────────────────┐   │
│  │                                                    │   │
│  │  ④ Kernel_Schedule()                               │   │
│  │     ├─ BLE 消息分发 (GAPM/GAPC/GATTC)              │   │
│  │     └─ 定时器回调 (APP_Timer 200ms)                │   │
│  │                                                    │   │
│  │  ⑤ RM_StatusHandler() [RM]                         │   │
│  │     └─ 远程麦协议事件处理 (收包/状态更新)           │   │
│  │                                                    │   │
│  │  ⑥ 若 APPM_CONNECTED:                              │   │
│  │     ├─ 电池变化 → Batt_LevelUpdateSend (Notify)    │   │
│  │     └─ TX Value 变化 → CustomService_SendNotif     │   │
│  │                                                    │   │
│  │  ⑦ 喂看门狗                                         │   │
│  │                                                    │   │
│  │  ⑧ 进入 Sleep [非音频流时]                          │   │
│  │     ├─ 保存 BLE/RF 寄存器 (DMA)                     │   │
│  │     └─ BLE_Power_Mode_Enter(SLEEP)                 │   │
│  │     └─ 唤醒后恢复寄存器                              │   │
│  │                                                    │   │
│  │  ⑨ WFI / WFE 等待中断                              │   │
│  └────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────┘
```

---

## BLE 状态机

```
APPM_INIT           ← BLE_Initialize() 后
  │
  └─→ APPM_CREATE_DB   ← 依次添加 GATT 服务
      │   ├─ Batt_ServiceAdd_Server()    → BASS
      │   └─ CustomService_ServiceAdd()  → Custom (含远程麦控制)
      │
      └─→ APPM_READY      ← 服务添加完毕
          │
          └─→ APPM_ADVERTISING ← Advertising_Start()
              │   广播信道: 37/38/39
              │   广播间隔: 10.24s
              │
              │   Central 发起连接
              ▼
          APPM_CONNECTED
              │   连接间隔: 500ms
              │   Latency: 19 (有效通信 ~10s)
              │   Superv.Timeout: 32s
              │
              │   断连
              ▼
          APPM_READY → 重新广播
```

---

## 远程麦工作流

### 启动（手机 App BLE 写入 ON_OFF=1 / 短按按键）

```
GATTC_WriteReqInd (CS_IDX_RM_ONOFF_VAL)
  │
  ├─ RM_Configure(&app_env.rm_param, callback)
  │     role = RM_SLAVE_ROLE
  │     interval_time = 10000 μs
  │     audio_rate = 48 (48kHz 采样率 → G722 编码)
  │     radio_rate = 2000 kbps
  │     hopList = {37, 9, 16, 20, 29, 32, 17}
  │
  ├─ RF_SwitchToCPMode()
  │     Radio 从 BLE 切换到自定义协议模式
  │     BBIF_COEX 控制共存
  │
  ├─ RM_Enable(1000)     ← 启动远程麦，1000ms 超时
  │
  └─ app_env.audio_streaming = 1
        └─ Main_Loop: 跳过 Sleep, 用 WFE 代替 WFI
```

**按键触发路径**（`DIO0_IRQHandler`, [audio_func.c:575](code/audio_func.c#L575)）：
```
DIO2 下降沿 → DIO0 中断
  ├─ app_env.RM_on_off = !app_env.RM_on_off
  └─ 若 ON: 同 BLE 启动路径（RM_Configure → RF_SwitchToCPMode → RM_Enable）
     若 OFF: 同 BLE 停止路径（RM_Disable → RF_SwitchToBLEMode）
```

**同步机制：** 按键和 BLE 共用 `app_env.RM_on_off` 状态变量，手机 App 读取 `ON_OFF` 特征值可看到按键切换后的正确状态。

### 音频接收数据通路

```
┌──────────────────────────────────────────────────────────────┐
│  空口                                                       │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 2.4GHz 自定义协议 RX                                 │    │
│  │   ├─ 跳频 (7 信道, FHSS)                             │    │
│  │   ├─ 数据率 2Mbps                                    │    │
│  │   ├─ Access Word 匹配 → 同步                          │    │
│  │   └─ CRC 校验 → 提取 Payload                          │    │
│  └──────────────┬──────────────────────────────────────┘    │
│                 ▼                                            │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ RM_Callback_TRX()                                    │    │
│  │   ├─ RM_RX_TRANSFER_GOODPKT → 提取编码帧             │    │
│  │   ├─ 声道分流 (Left/Right)                           │    │
│  │   ├─ 写入 frame_in[] 缓冲                            │    │
│  │   └─ 启动 Rendering_func()                           │    │
│  └──────────────┬──────────────────────────────────────┘    │
│                 ▼                                            │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Packet_regulator_timer_isr()  (TIMER2, ~200μs)       │    │
│  │   ├─ 每 3 字节 G722 编码子帧调用                      │    │
│  │   ├─ Start_Dec_Lpdsp32() → 写入 DSP 共享内存          │    │
│  │   └─ DSS_CMD_1 触发 LPDSP32 解码                      │    │
│  └──────────────┬──────────────────────────────────────┘    │
│                 ▼                                            │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ LPDSP32 (G722 解码器)                                │    │
│  │   ├─ G722 64kbps → 16-bit PCM (8 样本/子帧)         │    │
│  │   ├─ 结果写入 Dsp2CmBuff0dec (DSP_DRAM4)            │    │
│  │   └─ DSP1_IRQ 通知 CM3                              │    │
│  └──────────────┬──────────────────────────────────────┘    │
│                 ▼                                            │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ DspDec_isr()                                         │    │
│  │   ├─ DMA 启动 → 从 Dsp2CmBuff 送到 ASRC              │    │
│  │   └─ Asrc_in_dma_isr() → ASRC 重采样                 │    │
│  └──────────────┬──────────────────────────────────────┘    │
│                 ▼                                            │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ ASRC (异步采样率转换器)                               │    │
│  │   ├─ 输入: 解码 PCM (G722 16kHz → 内插至 ~31.25kHz)  │    │
│  │   ├─ 时钟参考: AUDIOSINK (DIO7 SAMPL_CLK)            │    │
│  │   ├─ Asrc_reconfig() 补偿时钟漂移                     │    │
│  │   └─ 输出: 对齐到音频主时钟的 PCM                     │    │
│  └──────────────┬──────────────────────────────────────┘    │
│                 ▼                                            │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ OD (Sigma-Delta 输出驱动器)                           │    │
│  │   ├─ AUDIO->OD_DATA ← 18-bit 有符号 PCM              │    │
│  │   ├─ Sigma-Delta 调制 @ 1MHz (AUDIOSLOWCLK)          │    │
│  │   ├─ DIO8 → OD_P (耳机+), DIO9 → OD_N (耳机-)       │    │
│  │   ├─ 外部 680μH 电感低通滤波                          │    │
│  │   └─ 等效采样率: 1MHz / 64 = ~15.6kHz                │    │
│  └─────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

### 停止（手机 App 写入 ON_OFF=0 / 短按按键）

```
GATTC_WriteReqInd (CS_IDX_RM_ONOFF_VAL)
  │
  ├─ BBIF_COEX_CTRL->RX = 0     ← 禁用共存
  ├─ BBIF_COEX_CTRL->TX = 0
  ├─ RM_Disable()               ← 停止远程麦协议
  ├─ RF_SwitchToBLEMode()       ← Radio 切回 BLE
  └─ app_env.audio_streaming = 0
        └─ Main_Loop: 恢复 Sleep + WFI
```

---

## BLE Custom Service 特征值

```
Custom Service (UUID: 0x24,0xdc...0x01)
  │
  ├─ TX_VALUE   (0x02)  RD/NTF   ← 周期性通知 (10 Sleep 周期)
  ├─ RX_VALUE   (0x03)  RD/WR    ← 手机写入
  ├─ ON_OFF     (0x04)  RD/WR    ← 远程麦开关 [RM]
  ├─ VOLUME     (0x05)  RD/WR    ← 音量 [RM]
  └─ CHANNEL_SIDE (0x06) RD/WR  ← 声道选择 [RM]
```

---

## 中断体系

| 中断 | IRQn | 优先级 | 来源 | 用途 |
|------|------|--------|------|------|
| AUDIOSINK_PERIOD | AUDIOSINK_PERIOD_IRQn | 4 | 音频时钟 | 音频时: ASRC 周期追踪; 空闲时: RC OSC 校准 |
| AUDIOSINK_PHASE | AUDIOSINK_PHASE_IRQn | 4 | 音频时钟 | ASRC 相位测量 |
| DSP1 | DSP1_IRQn | 4 | LPDSP32 | G722 子帧解码完成 |
| DMA3 | DMA3_IRQn | — | ASRC DMA | 解码数据送入 ASRC 完成 |
| TIMER2 | TIMER2_IRQn | 4 | 包调节器 | 每 3 字节触发一次 LPDSP32 解码 |
| DMIC_OUT_OD_IN | DMIC_OUT_OD_IN_IRQn | — | OD | Sigma-delta 输出数据请求 |
| APP_TEST_TIMER | — | — | Kernel | 200ms 周期: LED + 电池测量 |
| DIO0 | DIO0_IRQn | 0 | GPIO | 短按按键切换远程麦 ON/OFF |

---

## Sleep / Wakeup 周期

```
┌─────────┐    BB Timer 到期    ┌─────────┐
│  ACTIVE  │ ──────────────────→ │  SLEEP   │
│  (BLE)   │ ←────────────────── │ (32KB    │
│  ~3ms    │   Wakeup + 恢复     │ retention)│
└─────────┘                     └─────────┘
      ↑                              │
      │      BB Timer 到期           │
      └──────────────────────────────┘
              ~500ms (连接时)
              或 10.24s (广播时)
```

- 进入 Sleep 前: DMA 保存 BB/RF 寄存器 → 关 LED → 断电
- 唤醒后: DMA 恢复 BB/RF 寄存器 → 开 LED → 继续 BLE
- **音频流期间**: Sleep 禁用（`audio_streaming = 1`），保持 ACTIVE 以确保 RF 连续接收

---

## 功耗策略

| 场景 | 状态 | 功耗估算 |
|------|------|---------|
| 广播中 | 10.24s 间隔, TX 0dBm | ~μA 级别（大部分时间 Sleep） |
| 已连接 | 500ms 间隔 + Lat19 | ~μA 级别（每 10s 活动一次） |
| 远程麦 RX | Radio RX 持续 + LPDSP32 运行 + OD 输出 | ~mA 级别（全速运行） |
| 远程麦待机 | 同 "已连接" 状态 | ~μA 级别 |

---

## 关键配置速查

| 参数 | 值 | 文件:行 |
|------|-----|---------|
| 系统时钟 | 8 MHz | `app.h:84` |
| RTC 时钟源 | RC_OSC | `app.h:137` |
| 供电方式 | LDO | `app.h:69` |
| TX Power | 0 dBm | `calibration.h:47` |
| 校准策略 | MANU_CALIB | `calibration.h:76` |
| 广播间隔 | 10.24s | `ble_std.h:70` |
| 连接间隔 | 500ms | `ble_std.h:108` |
| 连接延迟 | 19 | `ble_std.h:110` |
| 监督超时 | 32s | `ble_std.h:111` |
| 拒绝参数更新 | cfm->accept = 0 | `ble_std.c:706` |
| APP_RM_ENABLE | 定义 | `app.h:29` |
| OD 输出引脚 | DIO8 (P) / DIO9 (N) | `app.h:126-127` |
| 音频时钟参考 | DIO7 | `app.h:123` |
| 按键引脚 | DIO2 | `app.h:130` |
| DIO0 中断源 | GPIO 2 | `RTE_Device.h:2341` |
| DIO0 触发方式 | 下降沿 | `RTE_Device.h:2349` |
| DIO0 去抖 | 硬件已启用 | `RTE_Device.h:2357` |
| DIO0 优先级 | preempt 0 | `RTE_Device.h:2363` |

---
## Debug UART / printf 状态

**当前状态：不可用。** 代码已写但被 `#if (DEBUG_UART_LOG)` 禁用。

| 问题 | 详情 |
|------|------|
| `DEBUG_UART_LOG` 未定义 | `.cproject` 和 `Makefile` 均无此宏 |
| UART 引脚宏缺失 | `UART_TX`, `UART_RX` 未定义 |
| UART 参数宏缺失 | `UART_BAUD_RATE`, `UART_TX_NUM`, `UART_TX_CFG`, `UART_DMA_MODE_ENABLE` 未定义 |
| 无 printf retarget | 无 `_write`/`fputc` 重定向 |

**启用需要：**
1. 在 `.cproject` C Compiler defined symbols 添加 `DEBUG_UART_LOG`
2. 添加 UART 配置宏：`UART_TX=5, UART_RX=4, UART_BAUD_RATE=115200, UART_TX_NUM=7, UART_TX_CFG=...`
3. 在 `App_Initialize()` 中调用 `UartLogInit()`
4. 添加 `_write()` retarget 或用 `sprintf + UartLogTx` 替代 printf

SDK 自带 printf 库位于 `%PACK_ROOT%/source/firmware/printf/printf.c`（UART_TX=5/DIO5, UART_RX=4/DIO4, BAUD=115200）。

---

## 文件地图

| 文件 | 职责 |
|------|------|
| `app.c` | 入口 + 主循环 |
| `code/app_init.c` | 全部硬件初始化 |
| `code/app_process.c` | 消息处理器 + Sleep 配置 + ISR |
| `code/ble_std.c` | BLE 事件处理 + 状态机 |
| `code/ble_custom.c` | 自定义服务读写处理 |
| `code/ble_bass.c` | 电池服务 |
| `code/calibration.c` | 电压校准 |
| `code/audio_func.c` | 音频 DSP 中断 + ASRC + OD |
| `code/rm_app.c` | 远程麦应用回调 |
| `code/rm_event.c` | 远程麦协议状态机 [RTE] |
| `code/rm_pkt_hdl.c` | 远程麦数据包处理 [RTE] |
| `code/config_data.c` | RF 寄存器 + 频率表 [RTE] |
| `code/dsp_pm_dm.c` | G722 LPDSP32 二进制 |
| `code/queue.c` | 环形缓冲 |
| `include/app.h` | 全局配置 + 宏 |
| `include/ble_std.h` | BLE 参数 |
| `include/ble_custom.h` | 自定义服务 UUID |
| `include/calibration.h` | 校准策略 |
| `RTE/Device/RSL10/sections.ld` | 链接器脚本 (含 .dsp) |
| `RTE/Device/RSL10/RTE_Device.h` | 驱动配置 |
| `syslib/rsl10_sys_*.c` | 系统库 [Makefile 专用] |
