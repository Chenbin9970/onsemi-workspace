# ASHA 移植到 Sleep 工程 — 开发记录

## 目标

将 `ble_android_asha/` 的 ASHA (Audio Streaming for Hearing Aids) 功能移植到 `peripheral_server_sleep/`，使助听器能在 RM（远程麦克风）和 ASHA（手机音频流）两种模式间切换。

## 架构决策

### DSP 固件

- Sleep 现有 RM 固件是 **G.722 CODEC_MODE=3 (48kbps, 8 samples → 3 bytes)**
- 手机 ASHA 发的是 **G.722 CODEC_MODE=1 (64kbps, 4 samples → 2 bytes, 带 PLC)**
- 两者 DSP 固件不同，采用 **运行时 loadDSPMemory() 热切换**

### 模式切换

通过 Custom Service RX 特征发 BLE 命令：`F0 01` 切 ASHA，`F0 00` 切回 RM。

| 命令 | 作用 |
|------|------|
| `0xF0 0x01` | ASHA_Initialize() → ASHA_App_Init() → Advertising_Start() |
| `0xF0 0x00` | ASHA_App_Deinit() → Advertising_Start() |

## 文件改动

### 新增文件 (30+)

#### Codec 框架 — 从 `remote_mic_rx_raw` 复制

| 文件 | 来源 | 说明 |
|------|------|------|
| `source/codecs/codec.c/h` | rx_raw | 多态编解码器抽象 |
| `source/codecs/codecInternal.h` | rx_raw | 内部结构 |
| `source/codecs/base/baseCodec.c/h` | rx_raw | 基类 |
| `source/codecs/baseDSP/baseDSPCodec.c/h/Internal.h` | rx_raw | DSP 编解码器基类 |
| `source/codecs/G722DSP/g722DSPCodec.c/h` | rx_raw | RM 用 G.722 (MODE=3) |
| `source/codecs/sharedBuffers.c/h` | rx_raw | ARM↔DSP 共享内存 |
| `source/codecs/logger.c/h` | rx_raw | 日志 |

#### DSP Loader — 从 `remote_mic_rx_raw` 复制

| `source/codecs/dsp/loader/loader.c/h` | rx_raw | loadDSPMemory() |
| `source/codecs/dsp/loader/flashCopier.c/h` | rx_raw | Flash→PRAM 拷贝 |

#### G.722 MODE=3 固件 — 从 `remote_mic_rx_raw` 复制

| `source/codecs/dsp/g722/g722_dsp.c/h` | rx_raw | 内存概述 |
| `source/codecs/dsp/g722/g722_dsp_PM.c/h` | rx_raw | 程序内存 |
| `source/codecs/dsp/g722/g722_dsp_DM_Hi.c/h` | rx_raw | 数据内存高 |
| `source/codecs/dsp/g722/g722_dsp_DM_Lo.c/h` | rx_raw | 数据内存低 |
| `source/codecs/dsp/g722/g722_dsp_symbols.h` | rx_raw | 符号地址 |

#### G.722 PLC MODE=1 固件 — 从 `ble_android_asha` 复制

| `source/codecs/dsp/g722_plc/g722_plc_dsp.c/h` | asha | 内存概述 |
| `source/codecs/dsp/g722_plc/g722_plc_dsp_PM.c/h` | asha | 程序内存 |
| `source/codecs/dsp/g722_plc/g722_plc_dsp_DM_Hi.c/h` | asha | 数据内存高 |
| `source/codecs/dsp/g722_plc/g722_plc_dsp_DM_Lo.c/h` | asha | 数据内存低 |
| `source/codecs/dsp/g722_plc/g722_plc_dsp_symbols.h` | asha | 符号地址 |

#### G.722 PLC Codec — 从 `ble_android_asha` 复制

| `source/codecs/G722PLCDSP/g722_PLC_DSPCodec.c/h` | asha | ASHA 用 G.722 PLC (MODE=1) |

#### ASHA 应用层

| `code/asha_app.c` | ASHA 音频管线 (OD 输出)：编解码器初始化、ISR、队列、音量 |
| `code/asha_app.h` | 应用接口 |
| `include/asha_audio.h` | ASHA 音频参数 (ASHA_ 前缀避免冲突) |
| `code/asha_queue.c` | 音频帧 FIFO 队列 (AshaQueue* 前缀避免冲突) |
| `include/asha_queue.h` | 队列接口 |

#### BLE 集成

| `code/ble_asha_wrap.c` | Thin wrappers：MsgHandler_Add/Notify、GAPM_LepsmRegisterCmd、L2CC_*、GATTC_SendEvtCmd、GATTM_AddAttributeDB/GetHandle、ASHA GATT 读写分发 |
| `include/ble_asha_wrap.h` | Wrapper 接口 |

#### Stub

| `include/app_trace.h` | PRINTF stub → 转发到 printf |

#### RTE 组件

| `*.rteconfig` (ASHA Profile 组件) | SDK 提供 `ble_asha.c/h` |

### 修改文件 (8 个)

| 文件 | 改动 |
|------|------|
| `include/app.h` | +`APP_ASHA_ENABLE`、+ASHA 配置宏、+`SERVICE_ADD_FUNCTION_LIST` 添加 ASHA_ServiceAdd、+include |
| `app.c` | +`ASHA_App_Process()` 钩子、+`0xF0` 命令处理 (mode switch + ASHA_Initialize) |
| `code/app_process.c` | `Msg_Handler` 尾部调用 `MsgHandler_Notify()` |
| `code/ble_std.c` | +L2CAP CoC (`max_nb_lecb=1`)、+`GAPM_LepsmRegisterCmd`、+`MsgHandler_Notify` 在连接事件、+ASHA 广播数据 |
| `code/ble_custom.c` | +ASHA GATT 读写路由 (ReadReqInd/WriteReqInd)、+`GATTM_AddSvcRsp` 捕获 ASHA start_hdl |
| `code/app_func.c` | ISR 别名加 `#ifndef APP_ASHA_ENABLE` 保护 (DMA3/AUDIOSINK_PHASE/AUDIOSINK_PERIOD) |
| `RTE/Device/RSL10/sections.ld` | +`DRAM_DSP_CM3` (2K @ 0x2100B800)、+`.shared` section |
| `peripheral_server_sleep.rteconfig` | +ASHA Profile 组件、-BLE Abstraction (只用手动 thin wrappers) |

## 关键技术点

### ASHA vs RM 参数对比

| | RM (MODE=3) | ASHA (MODE=1) |
|---|---|---|
| 子帧 | 8 samples, 3 bytes | 4 samples, 2 bytes |
| 码率 | 48 kbps | 64 kbps |
| DSP 固件 | g722_dsp | g722_plc_dsp |
| 音频输出 | OD (DAC) | OD (DAC，统一) |
| DSP 中断 | DSP1_IRQ | DSP0_IRQ |
| Timer | TIMER_REGUL=2 | TIMER_RENDER=3 |
| 音频输入 | RM 射频 | L2CAP CoC (PSM 0xA8) |

### BLE 架构

- 不引入 SDK BLE Abstraction 组件（会带进 ble_bass.c 等冲突文件）
- Thin wrappers 在 `ble_asha_wrap.c` 中实现：MsgHandler_Add/Notify、GAPM_LepsmRegisterCmd、L2CC_*、GATTC_SendEvtCmd、GATTM_AddAttributeDB/GetHandle
- GATT 读写路由：`ble_custom.c` → `ASHA_GATTC_ReadReqHandler/WriteReqHandler`
- L2CAP CoC 事件：`Msg_Handler` → `MsgHandler_Notify` → `ASHA_MsgHandler`

### ISR 冲突处理

`app_func.c` 的 RM 模式 ISR 别名加 `#ifndef APP_ASHA_ENABLE`，asha_app.c 的 ASHA ISR 仅在 `APP_ASHA_ENABLE` 下编译。

### 枚举/宏冲突

- `LINK_DISCONNECTED` 等与 `rm_pkt.h` 冲突 → ASHA 版加 `ASHA_` 前缀
- `CODEC_MODE`/`SUBFRAME_LENGTH` 等 macro 冲突 → ASHA 版加 `ASHA_` 前缀
- `ble_gatt.h` 的 `GATTC_ReadReqInd` 与 `ble_custom.h` 冲突 → `ble_asha_wrap.h` 改用 `<rsl10_ble.h>`

## 测试状态

| 步骤 | 状态 |
|------|------|
| 编译通过 | 通过 |
| RM 模式回归 | 待测 |
| ASHA 模式切换 + 服务初始化 | 通过 (PRINTF 确认) |
| 手机识别 ASHA 广播 | 通过 (手机连接) |
| ASHA 服务发现 + 音频流 | **调试中** (3秒断连) |
| ASHA→RM 切回 | 待测 |

## 下一步

手机连接后 3 秒断开。已启用 ASHA PRINTF 输出，等待日志定位 GATT/L2CAP 哪一步失败。
