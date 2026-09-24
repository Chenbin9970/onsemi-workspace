# remote_mic_rx_coex_1654 开发文档

## 1. 工程概述

`remote_mic_rx_coex_1654` 基于 onsemi RSL10 `remote_mic_rx_coex` demo（远端麦克风接收机 / RM receiver，BLE+RM 共存）。
在 demo 基础上做了两件核心事情：

1. **音频出口改为 OD 直驱**：不再把音频经 SPI0 泵给外部 Ezairo 7100 板，而是 RSL10 片上
   LPDSP32 解码 + ASRC 重采样，最终由内置 Output Driver（OD，DIO0=OD_P / DIO1=OD_N 差分）
   直接驱动受话器。
2. **引入 BS300 DSP I2C 通讯子系统**：RSL10 通过 bit-bang I2C（DIO8=SCL / DIO7=SDA，
   从机地址 0x01）控制 BS300 DSP，移植自 `peripheral_server_sleep`（到 driver 编排，接近全量）。

参考工程：`peripheral_server_sleep`（OD 输出路径 + BS300 通讯来源）、`peripheral_server_sleep7160test`（历史调试参考）。
与 `remote_mic_rx_coex`(master) 的 7100 A7 读回/心跳 **无关**，不移植那套。

## 2. 来源与 git 基线

- Demo 基线已提交：commit `14811e3`（27 个文件，onsemi 原始未改动的 CRLF 副本）。
- 之后所有移植/修改均**未提交**，分散在工作区（新增 bs300_* 文件 + 修改 6 个工程文件）。

## 3. 相对 demo 的改动总览

| 改动 | 涉及文件 | 说明 |
|------|----------|------|
| 新增 OD 输出模式 `OD_OUTPUT(5)`，设为默认 `OUTPUT_INTRF` | include/app.h, code/app_init.c | 编译解码+ASRC 全链路（见 §6） |
| `OUTPUT_DECODE_PATH` 宏放宽解码门控（RAW 与 OD 共用） | include/app.h + 各 .c | 替换原 `OUTPUT_INTRF == SPI_TX_RAW_OUTPUT` |
| BS300 子系统文件（9 .c + 10 .h，verbatim 自 sleep） | code/bs300_*, include/bs300_* | 不含 bs300_test |
| BS300 应用胶水：boot init、主循环 deferred、sync timer 消息 | app.c, app_process.c, app.h | 见 §7 |
| RM 流窗口联动 BS300（切 prog3/active/mute） | code/rm_app.c | 见 §7 |
| 移除原按键（DIO5/DIO0 中断、ear_side 切换） | app_init.c, app_func.c, app.h | 去掉 DIO0_IRQHandler |
| 新增按键（DIO12，参考 sleep：短按音量+1 / 长按切程序） | app.c, app_init.c, app.h | `Button_Process()`，见 §8 |
| 打印 IO → DIO5（pack printf.c，机器级共享） | 外部 pack printf.c | TX=DIO12→DIO5，RX=DIO6；DIO12 让给按键 |
| 删除 LED 功能（DIO6） | app_init.c, app_process.c, app.h | LED_DIO_NUM 删除 |
| RM 无线电参数对齐 sleep | include/app.h, code/rm_app.c | hoplist、accessword（见 §9） |
| 链接修复 | code/rm_app.c | 注释与 app_func 重复的 `audio_sink_phase_cnt` 定义 |
| bs300 内部日志放开 | code/bs300_*.c（6 个） | guard 前加 `<printf.h>`（见 §10） |
| BLE 移植（单设备连接，对齐 sleep） | include/ble_std.h, code/ble_std.c | 设备名 Smart1654、广播/地址配置照 sleep（见 §14） |
| 修 bdaddr PUBLIC 分支 | code/ble_std.c | 读到公共地址不再被 `default_addr` 覆盖（见 §14） |
| 新增 Rempro Service（全功能，UUID 自定 F36F…） | ble_rempro.* (新增) | HDLC 验配协议，见 §15 |
| BLE 精简为只保留 Rempro | app.h 服务表等 | 去掉 Battery/Custom 注册与运行时电池（见 §15） |
| RM 断链程序恢复 | code/rm_app.c | `saved_prog_before_rm`，断开切回原程序并 active（见 §15） |
| 按键推送手机 | app.c | 长按/短按触发 `rempro_push_*`（见 §15） |
| **设置掉电保存补断链落盘** | code/ble_std.c | `GAPC_DisconnectInd` 调 `bs300_settings_persist()`（见 §15.1） |
| **采样钟 DIO7 → DIO10**（修 RM 重连失真） | include/app.h | 原与 BS300 I2C SDA 共用 DIO7，详见 §12.6 |
| 关闭 RM 调试 IO + 清死宏 | app_init.c, rm_app.c, app.h | `debug_dio_num=0xff`，删 `DEBUG_DIO_*` / `DIO_SYNC_PULSE`（见 §12.6） |
| **RM 期间 BLE 指令白名单** | app.c, code/ble_rempro_cmd.c | 只放行 26/4/15 查询类，其余静默丢弃（见 §18） |
| **RM ↔ BS300 切换改异步 + 抢断续传** | code/rm_app.c, code/bs300_ram_sync.c, include/bs300_ram_sync.h | 新增 `bs300_switch_pending()`；修 BS300 卡死（见 §12.7、§19） |
| 新增 Rempro SetMuteData(21) | include/ble_rempro_cmd.h, code/ble_rempro_cmd.c | 功能同 3 号 SetDeviceOnOff（见 §18） |
| **RM 声道选择** | include/app.h | `APP_RM_AUDIO_CHANNEL` = `RM_LEFT`(左) / `RM_RIGHT`(右)，出固件时切（**当前：`RM_LEFT` 左**，2026-09-24 从左/右对调） |
| **RM 断开过渡音量** | code/rm_app.c, code/app_process.c, include/app.h | 切回助听模式前先压到档位 5，2s 后回原设定值（见 §19.5） |
| **RM 流中断静音（坏包 PLC + 断开静音）** | code/rm_app.c, code/app_func.c, include/app.h | 修 TX 硬断电 1~2 秒「滋」声；新增 `Od_Stream_Break/Resume`（见 §6.1） |
| **电池量程重标定 + 采样改 60s** | include/app.h, code/app_process.c | 锚点 6950/9374 → 7273/9050（100% 端两次下调 9374→9174→9050）；200ms×16 平均 → 每 60s 直出，`__BATT` 带 raw（见 §17、§17.2） |
| **低电量告警音** | code/app_process.c, code/bs300_ram_sync.c, include/app.h | 跌破 20% 立即播、之后每 4min 重播；提示音 `0xFC12F2`（见 §17.1） |
| **新增 Rempro 37 SetFittingStatus** | include/ble_rempro_cmd.h, code/ble_rempro_cmd.c | 只回 ACK，按 `Fitting_Status` 挂起/恢复 RM（见 §18） |
| **验配/测听期间挂起 RM** | code/ble_rempro_cmd.c | 37/40 进出时关/开 RM，防 RM 建链切程序 3 搅乱 DSP（见 §18.1） |

## 4. 构建与总开关

- IDE：ON Semi Eclipse（GNU ARM，arm-none-eabi-gcc，`-mcpu=cortex-m3`）。
- `.cproject` sourceEntries 按目录整收：**新增到 `code/` 的 .c、`include/` 的 .h 自动参与编译**，无需改工程文件。
- 总开关（include/app.h）：
  - `OUTPUT_INTRF = OD_OUTPUT`（解码直出 OD）；可选 `SPI_TX_CODED_OUTPUT` / `SPI_TX_RAW_OUTPUT`。
  - `BS300_ENABLE`：BS300 子系统总开关（定义即启用）。
  - `CFG_FOTA`：FOTA 空中升级开关（**当前：ON**，2026-09-24 重新开启，见 §16）。
  - `OUTPUT_INTERFACE`（在 pack 的 printf.h，未在本工程覆盖 → 默认 UART）：打印出口选择。

## 5. 引脚分配

| 功能 | 引脚 | 说明 |
|------|------|------|
| OD_P / OD_N（受话器，差分） | DIO0 / DIO1 | `DIO_MODE_OD_P`，照 peripheral_server_sleep |
| BS300 I2C SCL / SDA（bit-bang） | DIO8 / DIO7，addr 0x01 | 与 sleep 一致 |
| **采样/audiosink 时钟输入** | **DIO10** | `Sys_Audiosink_InputClock(SAMPL_CLK…)`，无条件配置。**原为 DIO7，见 §12** |
| 调试 UART TX / RX | DIO5 / DIO6（**pack printf.c 内硬编码**） | 115200；当前调试口在 DIO5，改引脚需改 pack 的 printf.c（影响所有工程） |
| 按键（active low，上拉） | DIO12 | 短按音量+1 / 长按切程序；参考 sleep；由原打印脚让出 |
| 上电暂停/恢复(recovery) | DIO13 | 接地暂停便于重刷，勿占用 |
| 空闲/预留 | DIO2/3/4/9/14 | DIO15/11 原为 RM 调试 GPIO 输出，已关闭（见 §12） |

> ⚠ **采样钟绝不能放回 DIO7**：DIO7 同时是 BS300 I2C 的 SDA，而 bit-bang 收发会把该脚
> 重配成 GPIO 并来回翻转（`bs300_hal.c` 的 `sda_out/sda_h/sda_l`），采样钟就此丢失/被注入
> 毛刺 → ASRC 失去锁相参考 → 音频失真。详见 §12。

## 6. 音频通路（OD 直驱）

接收链路（RX）：
```
RM 射频包 → RM_Callback_TRX(RM_RX_TRANSFER_GOODPKT)
         → Rendering_func(outTempBuff)      [app_func.c]
         → Start_Dec_Lpdsp32 → LPDSP32 解码 (DspDec_isr)
         → ch3 DMA: Dsp2CmBuff0dec → ASRC->IN
         → ASRC 重采样（锁定 DIO7 采样钟，Ascc_phase/period_isr）
         → ch4 DMA: ASRC->OUT → BufferOut  (OD_RX_DMA_ASRC_OUT, circ)
         → ch5 DMA: BufferOut → AUDIO->OD_DATA (RX_DMA_OD, OD_DMA_NUM=5)
         → OD 输出 DIO0/DIO1
```
关键配置（照 peripheral_server_sleep `Audio_Init` 末尾）：
- `Sys_Clocks_SystemClkPrescale1(AUDIOCLK_PRESCALE_5)`
- `Sys_Audio_Set_Config(AUDIO_CONFIG)`；`AUDIO->OD_CFG/SDM_CFG/OD_GAIN`
- `Sys_DIO_Config(OD_P_DIO(=0), …DIO_MODE_OD_P)`
- `BufferOut[2*FRAME_LENGTH]`（code/app_init.c 全局）
- OD DMA 在 RM 建链/断链时由 `rm_app.c` 的 `RM_Callback_StatusUpdate` 启停（断链下溢保护静音）

门控宏：`OUTPUT_DECODE_PATH = (OUTPUT_INTRF==SPI_TX_RAW_OUTPUT || ==OD_OUTPUT)`，
用于 app.h/app_init.c/app_func.c/rm_app.c 中所有“解码+ASRC 初始化”的 `#if`。

### 6.1 流中断处理：坏包 PLC + 断开静音（远端 TX 硬断电杂音修复，2026-09 已上板）

**现象**：远端麦克风（TX）**持续推流中被硬断电**（掉电/出范围），RX 侧出现 1~2 秒的
「滋」声，之后才安静。TX 只是把音量调小（流没断）时不出现。

对齐 `remote_mic_rx_coex_1664` 的同名修复（其 §6.7，已上板），分**两条独立通路**处理 ——
只做其中一条解决不了问题，这是本工程第一次修复失败的教训。

#### (一) 坏包 / 丢包处理 —— PLC（`code/rm_app.c` 的 `RM_Callback_TRX`）

**根因**：RM 库对 `RM_RX_TRANSFER_BADCRCPKT` / `NOPKT` 也传**非 0** 的 `packet_length`：
`rm_pkt_hdl.c:812-826` 三种类型都把 `&rm_env.packet_length` 交给回调，而该字段只在
`rm_event.c:75` 按音频配置算一次、**从不归零**。所以回调里 `if ((*length) == 0)` 这条
「按无包处理」的分支**永远走不到**，损坏 payload 会一路喂进 `Rendering_func()` 被解成
满量级爆音。

**做法**：好帧存进 `rm_last_good[]`；坏包/丢包时**重复 `rm_last_good`** 喂解码器（即库
注释 `repeat previous packet` 的本意），损坏数据**一字节都不进解码器**。

#### (二) 断开 / 流中断处理 —— 立刻静音（`code/app_func.c` 的 `Od_Stream_Break/Resume`）

**根因（这才是「滋」声的来源）**：TX 消失后不再有新解码数据，但整条流水照跑 ——
**ASRC 输入枯竭后并不输出 0，而是输出极限环/残留**，经 ch4 → `BufferOut` → ch5 一路送到
OD。而停机挂在 `LINK_DISCONNECTED` 上，RM 库要**丢满 `pktLostHighThrshld = 200` 包（≈2s）**
才判掉线，这 2 秒没人管。

> ⚠ **「把 0 喂进解码器」解决不了这个问题**（本工程第一次修复就是这么失败的）：0 进了
> 解码器，下游 ASRC 该出极限环还是出。必须**停采 ASRC** 才能切断噪声源。

**做法**：

| 触发 | 动作 |
|------|------|
| GOODPKT | 存 `rm_last_good` → 喂解码器 → `rm_stream_good()`：计数归零 + `Od_Stream_Resume()` |
| 坏包 / 丢包 | PLC 重复 `rm_last_good` → 喂解码器（**已静音时不喂**）；`rm_stream_loss()` 计数 |
| 连续 `RM_STREAM_BREAK_LOSS_N`(=2) 个非好包 | `Od_Stream_Break()` |
| `LINK_DISCONNECTED` | 兜底再 `Od_Stream_Break()`，然后停 ch5 |
| `LINK_ESTABLISHED` | 计数归零 + `Od_Stream_Resume()`，再重配 ch5 |

`Od_Stream_Break()`：`Sys_DMA_ChannelDisable(ASRC_OUT_IDX)` **停采 ASRC** → 清零
`BufferOut`。**ch5 不动**（继续循环全 0），所以没有 OD 下溢状态切换；「OD 输入恒 0」正是
`LINK_DISCONNECTED` 之后已验证干净的那个状态。`Od_Stream_Resume()` 反向：重配 ch4 回
`ASRC->OUT → BufferOut`。未静音时 `Resume` 自判为空操作，所以 GOODPKT 可以无条件调它。

> ⚠ **`BufferOut` 必须清零，不能只淡出**：ch5 是循环 DMA（`DMA_ADDR_CIRC`），会反复重播
> **整块** `BufferOut`。只做淡出的话，被循环的仍是一段有内容的波形，依然是嗡声。

**上板结论**（2026-09）：TX 硬断电后不再有 1~2 秒「滋」声。踩坑记录见下 ——
**第一次修复只做了 (一)**（给解码器喂全 0 帧），实测「滋」声依旧；补上 (二) 才解决。
所以这两条通路是**并列必需**的，改这一块时别只改一条。

**已知取舍**（可接受，出问题从这里查）：
- **无淡出/淡入**：1664 的淡出挂在 ch5「由 7100 BCLK/FS 外部驱动、必然完成」的中断上；
  本工程 OD 通路的 ch4(`ASRC_OUT_IDX`) / ch5(`OD_DMA_NUM`) **都不带完成中断**
  （`RX_DMA_OD` / `OD_RX_DMA_ASRC_OUT` 均 `DMA_COMPLETE_INT_DISABLE`，别名表
  `app_func.c:229-244` 里也没有 DMA4/DMA5 handler），没有可挂的中断，所以断开/恢复
  瞬间可能有极短促的「咔」；当前上板听感可接受，未做淡入淡出。若以后要优化，可用
  `Ascc_phase_isr`（音频相位中断，独立于 RM）做分块淡入淡出。
- **连续丢 2 包即静音**：弱信号下偶发连丢会带来一次「静音 → 恢复」的短暂下沉。
  **回归重点** —— 必须有 GOODPKT 把它拉回来，否则会永久静音。
- **为什么不调 `bs300_mute()`**：那是阻塞 I2C 命令（`bs300_ram_sync.c:1487`），在收包回调
  里执行会饿死 RM 音频包投递（见 §12.6 的 DIO7 教训）。所以走纯软件路径，不碰 I2C。

## 7. BS300 子系统

文件（copied verbatim from `peripheral_server_sleep`，含头文件；未拷 bs300_test）：

| 层 | 文件 | 作用 |
|----|------|------|
| 传输 | bs300_hal.c | bit-bang I2C 原语（DIO8/7，addr 0x01），喂狗、NAK 快速失败 |
| 协议帧 | bs300_startup.c | 帧封装/校验/状态轮询；MUTE→KEY_LOCK→VERIFY_COMM 启动序列；read_calibration/profile |
| 程序读 | bs300_program_read.c | 读 480B/程序（4 个程序） |
| 存储 | bs300_storage.c | 主 Flash 高位区 `0x0015C800+` 缓存（settings + Prog0-3，各 2KB） |
| 参数编码 | bs300_param_encode.c / param_tables.c / calib.c | flash↔struct 编解码/查表/校准解析（纯逻辑） |
| 同步引擎 | bs300_ram_sync.c | DSP 状态机、sync/mute/active/switch_program、volume、deferred、内核 sync timer |
| 编排 | bs300_driver.c | `bs300_driver_init()`：hal→startup→(读/缓存 4 程序)→恢复设置→sync 激活 |

应用侧胶水（本工程新增）：
- `app.c main`：`App_Initialize()` 后 `bs300_driver_init()`（首启约 2–3 s 阻塞，已喂狗；无芯片快速失败）；主循环每轮 `bs300_process_deferred()`。
- `app_process.c`：`BS300_SyncTimer` 消息处理器 → `bs300_sync_timer_handler()`；消息 id `BS300_SYNC_TIMER(0x10)` 登记在 app.h 的 `APP_MESSAGE_HANDLER_LIST`。
- `rm_app.c` RM 流窗口（镜像 sleep 最简版，`#ifdef BS300_ENABLE`）：
  - `LINK_ESTABLISHED`：`bs300_set_prog_volume(3,9); bs300_mute(); bs300_switch_program(3); bs300_active();`，置 `app_env.audio_streaming=1`
  - `LINK_DISCONNECTED`（流中）：`bs300_mute()`，清 `audio_streaming`

## 8. 启动流程

`App_Initialize()`（code/app_init.c）：
1. 关中断、禁 JTAG DATA/TRST（释放 DIO）、等待 DIO13 释放
2. 48MHz 时钟/RF/ADC(VBAT) 配置
3. audiosink 计数+采样钟输入(DIO7)（无条件）
4. `#if OUTPUT_DECODE_PATH`：DSP 固件 Flash_Copy、DSS reset、设 codec message；`#if OD`：ASRC/OD/DMA 初始化（DIO0 OD_P、ch3/4/5）……
5. 10k 喂狗延时 → `BLE_Initialize()` → `App_Env_Initialize()` → `printf_init()` → `APP_RM_Init(ear_side)`
6. `RF_SwitchToCPMode(); RM_Enable(1000);`（对齐 sleep：开机即进 RM）
7. Flash overlay + loop cache（恢复保留，与 rx_coex 处理的取舍见 §12）
8. DEBUG DIO/RF TX power 等

`main()`（app.c）：`App_Initialize()` → 打印 started → `bs300_driver_init()` → while(1){ Kernel_Schedule(); 电池通知;
`RM_StatusHandler()`; `Button_Process()`; `bs300_process_deferred()`; 喂狗 }。

- **按键 `Button_Process()`（app.c，DIO12，参考 sleep）**：5 次采样去抖；短按 = 当前程序音量 +1（0..9 循环），
  长按（≥`BTN_LONG_MS`=500ms，按按住时间累加） = 切程序 0→1→2→0（跳过程序 3）；动作走
  `bs300_switch_program_async` / `bs300_set_volume_async` + `bs300_settings_persist()`；
  RM 音频中（程序 3）、BS300 忙或未初始化时屏蔽。
- 长按计时依赖主循环迭代频率：**按键按住期间主循环跳过 `SYS_WAIT_FOR_EVENT`**（否则 ~200ms 才醒一次，
  计时被稀释导致长按永远判不成，会误判成短按）。DIO12 在 app_init 配为上拉输入。

> **已验证**：以上启动顺序——`printf_init()` 放在 `App_Initialize()` 末尾（BLE/Env 初始化之后、
> `APP_RM_Init` 之前），以及开机即 `RF_SwitchToCPMode(); RM_Enable(1000);` 切 RM——均已在板上验证正常。
> 说明：`ble_custom.c` 里另有一组 BLE 写命令控制的 RM 启停路径，用的是同样参数
> （start：`RF_SwitchToCPMode(); RM_Enable(1000);`；stop：`RM_Disable(); RF_SwitchToBLEMode();`）。

## 9. RM 无线电配置（对齐 sleep）

- `RM_HOPLIST = { 3, 9, 15, 21, 24, 33, 36 }`（include/app.h，已从 demo 的 `{37,…}` 改掉）
- `accessword = 0x00cde629 | (0xf2<<24)` = `0xf2cde629`（rm_app.c，已从 `0x0d` 改掉）
- 其余 rm_param（interval 10000 / retrans 5000 / audio_rate 48 / radio_rate 2000 / scan 6500 /
  preamble 0x55 / renderDelay 200 / preFetch 1300(RM_APP_REQUEST) / pkt/搜索阈值）与 sleep 一致。
- ⚠ 这些是收发对端配对参数：发射机与 1654 接收机必须一致才能建链。

## 10. 打印 / 调试

- 出口：pack `printf.h` 默认 `OUTPUT_UART` → pack `printf.c`（本机已改成 **UART TX=DIO5 / RX=DIO6 @115200**）。
  `printf_init()` 在 `App_Initialize()` 末尾调用（app_init.c），app.c/rm_app.c 的 `PRINTF` 与其一致。
- bs300 内部日志：bs300_*.c 已在各自 `#ifndef PRINTF` 前 `#include <printf.h>`，`[BS300] …` 会输出；
  若想静音删除这几行 include。
- 想看 RTT：工程已链 SEGGER_RTT，把 `OUTPUT_INTERFACE` 定义为 `1`（RTT）即可（需在每处 include printf.h 之前生效，一般放 app.h 顶部用数值 `1`）。
- 说明：pack printf.c/h 是共享文件，改引脚/出口会影响所有 RSL10 工程。

> ⛔ **禁止在 `app_init.c:341`（`__set_PRIMASK(PRIMASK_ENABLE_INTERRUPTS)`）之前调用 `PRINTF`** ——
> 会**死锁**，不是丢日志。
>
> **原因**：UART 出口走 **DMA + 完成中断**。`UART_printf()` 开头是
> `while (tx_busy == 1);`，而 `tx_busy` **只由 `DMA_UART_TX` 中断服务程序清零**
> （pack `printf.c`）。`app_init.c:47` 起 `PRIMASK` 屏蔽中断，到 341 行才开。
> 于是中断前第一次打印：DMA 启动、**字节真的发出去了（日志能看见！）**，但 `tx_busy`
> 留在 1；**下一次** `PRINTF` 就永久卡在 `while (tx_busy == 1)`。
>
> **症状极具误导性**：最后一行日志正常打出，然后整机「卡死」，看起来像那行之后的代码有问题。
> 2026-09-23 在 `APP_RM_Init()`（app_init.c:303 调用）加了一句日志就踩了 —— 它成了启动
> 流程的**第一句**打印。
>
> **做法**：需要早期值就用变量带出来，等到 `App_Initialize()` 返回后（`app.c` 主函数里）再打。
> `app_init.c` 全文 0 个 `PRINTF` 就是这个原因。同理：`bs300_*` 的日志都只在
> `bs300_driver_init()`（app.c 里调用）之后才安全。

## 11. 关键文件清单

修改（相对 commit `14811e3`）：
- include/app.h、code/app_init.c、code/app_func.c、code/app_process.c、code/rm_app.c、app.c、
  code/ble_std.c（设置掉电保存断链落盘，见 §15.1）、include/ble_std.h
新增（copied from peripheral_server_sleep）：
- code/bs300_hal/startup/program_read/storage/ram_sync/param_encode/param_tables/calib/driver.c
- include/bs300_*.h（9 个）+ include/bs300_encode_tables.h

## 12. 已知问题 / 待办

1. **UART 乱码排查（未定论）**：曾对比 rx_coex(master)（打印正常）。期间尝试过删 flash overlay+loop cache、
   DIV_CFG1 读改写、关 OD 硬件——**均已回退**（现回到“打印没有开吗”之前的基线：overlay 保留、
   `Sys_Clocks_SystemClkPrescale1` 原样、OD 正常）。当时发现烧错了固件，先重刷本工程新固件验证；
   若仍乱码再继续（优先查波特率/UART 时钟，其次 DIO12 冲突）。
2. RM↔BS300 流窗口目前是最简版（无 sleep 的 `saved_prog_before_rm`/debounce 状态机恢复逻辑）；
   若产品需要“RM 结束后切回原听音程序”，再补。
3. BS300 首启读 4 程序会写主 Flash 高位 `0x0015C800+`，需确认该区域未被 1654 镜像占用（sleep 同址验证过）。
4. 板上是否确有 BS300：无芯片时 `bs300_driver_init()` 应约 2 s 返回 false（不挂死），需实测确认。
5. OD 引脚目前为 DIO0/1 差分（照 base sleep）；如换 7160test 的 DIO12 单端+内部钟方案，需另改 OD_P_DIO 与采样钟源。
6. **【已修复】RM 重连后音频失真/断续 —— 采样钟与 BS300 I2C 共用 DIO7**

   **现象**：开机首次 RM 连接音频正常；断开后重连，音频在播但严重失真/断续（100% 复现）。

   **根因**：`SAMPL_CLK`（audiosink 采样钟输入）与 BS300 I2C 的 SDA **是同一个脚 DIO7**：

   | 用途 | 位置 |
   |---|---|
   | BS300 I2C SDA = DIO7 | `include/bs300_hal.h` 的 `BS300_I2C_SDA_DIO` |
   | 采样钟 = DIO7 | `include/app.h` 的 `SAMPL_CLK` → `app_init.c` 的 `Sys_Audiosink_InputClock()` |

   `bs300_hal.c` 的 bit-bang 每次收发都把 DIO7 重配成 GPIO 并翻转（`sda_out()` =
   `DIO_MODE_GPIO_OUT_0`，`sda_h/sda_l()` 直接置位，`i2c_stop()` 结束时还停在「输出高」），
   采样钟就此丢失/被注入毛刺 → `audio_sink_cnt`（Ck）测错 →
   `asrc_inc_carrier = ((Cr - Ck) << 29) / Ck` 算错 → **ASRC 重采样失锁 → 失真**。

   首次连接之所以正常：`LINK_ESTABLISHED` 里 `bs300_switch_program(3)` 的 I2C 恰好发生在音频
   刚起跑的位置，被启动过程掩盖；重连时同样的 I2C 落在播放中间，就听得出失真。

   **排查弯路（勿重复）**：先后怀疑并试过 ASRC 未重新使能、三通道 DMA 重配、`frame_decoded`
   守卫、CRC 坏包拆分、ch4/ch5 环形错位、把阻塞 I2C 改异步 —— **全部无效**。其中「改异步」
   反而更差（摊开 = DIO7 被反复抢占、干扰窗口更长），这恰好是引脚冲突的有力旁证。

   **修复**：`SAMPL_CLK` 由 **DIO7 改为 DIO10**（`include/app.h`），采样钟与 BS300 I2C 物理解耦。
   DIO10 在 1654 本为空闲脚。

   **试过但不可行的替代方案**：改用片上内部源 `AUDIOSINK_CLK_SRC_DMIC_OD` —— 能解决重连失真，
   但该源不是音频速率的正确基准，ASRC 会周期性重锁，引入**间歇性噗噗声**。

   **连带清理**：RM 调试 IO 一并关闭（`rm_app.c` 的 `debug_dio_num[0..1] = 0xff`，删掉
   DIO15/DIO11/DIO8 的 `Sys_DIO_Config` 与 `Sys_GPIO_Set_High(DIO_SYNC_PULSE)`）；
   死宏 `DEBUG_DIO_FIRST` / `DEBUG_DIO_SECOND` / `DIO_SYNC_PULSE` 已删除。

   > ⚠ **采样钟绝不能再放回 DIO7。**

7. **【已修复】RM 快速断/连导致 BS300 卡死在程序3（静音）**

   **现象**：RM 快速反复断/连后，BS300 停在程序 3 再也不会切回，听不到听音程序的声音。

   **根因（`s_saved_prog_before_rm` 被污染）**：RM 库的 `rm_env` **只有一个 `statusChange` 槽**，
   且置位前会比较 `oldLinkStatus`（`rm_event.c`）：

   ```c
   if(rm_env.statusChange) { rm_env.statusChange=0; status_update(rm_env.linkStatus); ... }
   ```

   所以 `ESTABLISHED→DISCONNECTED→ESTABLISHED` 这种闪断会被**整个吞掉**，主循环只看到最后的建立。

   而原实现把「记录原程序」放在 `LINK_ESTABLISHED` 里：

   ```c
   s_saved_prog_before_rm = bs300_get_active_prog();   // ← 此刻 s_cur_prog 可能已是 3
   bs300_switch_program(3);
   ```

   `bs300_switch_program()` 在**发 I2C 之前**就改掉 `s_cur_prog`（`bs300_ram_sync.c`），
   于是第二次建链时把「原程序」记成了 **3**。之后断链：

   ```c
   if (s_saved_prog_before_rm != 3)      // ← 3，恒为假
       bs300_switch_program(s_saved_prog_before_rm);
   ```

   **切不回去 → 永久停在程序 3 静音。**

   **修复**：加「只在本次会话首次建链时记录」的守卫（断链时本就清成 `0xFF`，语义自洽）：

   ```c
   if (s_saved_prog_before_rm == 0xFF)
       s_saved_prog_before_rm = bs300_get_active_prog();
   ```

   **附带一并修的两处**（见 §19）：

   - 进程序3 / 退回原程序都改**异步**（`bs300_switch_program_async()`），不再阻塞主循环
   - 与按键 / Rempro 的异步会话冲突，交给 `bs300_switch_program_async()` 自身的**抢断**机制处理

   > ⚠ 断链**必须**只进防抖 / 只 mute，**不要立刻切回**；否则每次闪断都要多一趟切换。
   > （1654 未移植 sleep 的 `rm_disc_state` 防抖状态机，当前靠上面的守卫 + 抢断续传兜住。）

8. **【已修复】延时推送（`bs300_schedule_delayed_push`）永不触发 —— 条件写成了 `state == IDLE`**

   **现象**：发 40 号 SetAudiometryStatus 进入测听，收不到本该 2s 后主动推送的 `CMD=6`
   （`CMD_PUSH_INITIAL_STATUS`）；串口也没有 `[REMPRO] push initial status done`。

   **根因**：`bs300_sync_timer_handler()` 里延时回调的触发条件与忙闲定义不一致：

   | 位置 | 判断 |
   |---|---|
   | `bs300_sync_is_busy()` | `IDLE` / `DONE` / `ERROR` **都算不忙** |
   | 延时回调分支 | 只认 `state == BS300_SYNC_IDLE` ✗ |

   而**会话跑完 state 停在 `DONE`（或 ERROR），不会回到 `IDLE`**（枚举 `IDLE=0`，
   结束置 `DONE`）。于是只要之前跑过任何一次异步 BS300 会话，回调就永远不触发 ——
   `s_delayed_push_cb` 被静默丢弃（`bs300_sync_tick()` 对 `DONE` 直接返回 0，
   走到 `session_ended` 分支时 `g_bs300_sync_on_done` 又是 NULL）。
   而 `bs300_audiometry_enter()` 内部全用阻塞 `bs300_advanced_write()`，不重置 state，
   所以它救不了自己。

   **修复**：触发条件改用现成的忙闲判断，避免两处定义漂移：

   ```c
   if (!bs300_sync_is_busy() && s_delayed_push_cb) { ... cb(); return; }
   ```

   **影响面**：所有走 `bs300_schedule_delayed_push()` 的功能 —— 测听进入的 06 推送
   （`rempro_push_initial_status_done`）、测听退出的推送（`rempro_push_audiometry_exit`）。

   > ⚠ 这是 BS300 移植时带来的**既有** bug；但 §19 把 RM↔BS300 切换改成异步后，
   > 会话跑得频繁、state 停在 `DONE` 的机会大增，该问题从「偶发」变成「几乎必现」。

9. **【已修复】远端 TX 硬断电时的「滋」声（2026-09 已上板）**

   **现象**：远端麦克风（TX）**持续推流中被硬断电**（掉电/出范围），RX 侧出现 1~2 秒
   「滋」声才安静。TX 只是把音量调小（流没断）时不出现。

   **根因（两条独立通路，缺一不可）**：
   1. 坏包被当有效数据解码 —— 库对 `BADCRCPKT`/`NOPKT` 也传非 0 `packet_length`，
      损坏 payload 被解成满量级爆音；
   2. **噪声源在下游** —— ASRC 输入枯竭后不输出 0 而是输出极限环/残留，经
      ch4 → `BufferOut` → ch5 送到 OD，而停机挂在 `LINK_DISCONNECTED`（要丢满 200 包
      ≈2s）上，这 2 秒没人管。

   **修复**：见 §6.1。(一) 坏包 PLC（重复 `rm_last_good`，损坏数据不进解码器）；
   (二) 连丢 2 包即 `Od_Stream_Break()`（停采 ASRC + 清零 `BufferOut`），
   `LINK_DISCONNECTED` 兜底，`LINK_ESTABLISHED`/GOODPKT 恢复。

   **弯路（勿重复）**：第一次只做了 (一)、把全 0 帧喂进**解码器**，实测「滋」声依旧 ——
   0 进了解码器，下游 ASRC 该出极限环还是出，必须**停采 ASRC** 才能切断噪声源。

   **回归重点**：必须有 GOODPKT 把静音拉回来，否则会**永久静音**（弱信号偶发连丢时
   会出现一次「静音 → 恢复」的短暂下沉，属预期）。

10. **AGCO 的 `AGCO_Enable=0` 无处可存（待确认）**：Flash 无 enable 位，`decode_agco_flash`
    在模块存在时硬编码 `enable=1`；当前传 0 回失败 ACK。需手册/芯片侧确认后再补。见 §20.5。
11. **60/61/88/89 号指令未上板实测**：尤其「AGCO 断电重启后是否保持」（唯一能证明反向编码正确的
    测试）与「流地址设完后 RM 重连能否与 TX 对上」。验证清单见 §20.5 / §20.6。
12. **Settings 槽格式变更的后果（一次性）**：89 号加字段把槽格式推到 `SETTINGS_VER 5`，
    **v4 旧槽会被判无效** → 首次烧录本版固件开机时，音量/EQ/降噪/DFBC 回到默认一次。
    详见 §15.1 的版本变更提示。

## 13. 验证步骤

1. Eclipse 导入/编译 `remote_mic_rx_coex_1654` Debug，确认链接通过（bs300 新增文件自动入编）。
2. 烧录后用 UART DIO5(115200) 观察：`started` → `[BS300] …` → `BS300_INIT_OK/FAIL`。
3. 与已配对发射机建链：应听到 OD 输出音频；断链应静音；串口出现 `RM_LINK_ESTABLISHED/DISCONNECTED`。
4. 手机/工具扫描应看到名为 **Smart1654** 的可连接广播（FOTA ON 时为 `Smart1654FOTA`，见 §16）；
   用主动扫描可读到 scan response 里的厂商段（含 MAC/耳侧，company data[10] = `0x01` 左 / `0x02` 右）。
5. 示波器查 DIO0/DIO1（OD 差分）与 DIO8/DIO7（BS300 I2C）波形。
   **DIO7 上不应再出现采样钟**（已改到 DIO10）。
6. **RM 重连回归测试（必测）**：建链 → 断开 → **反复重连 5~10 次**，每次音频都应正常，
   不应出现失真/断续。这是 §12.6 那个 bug 的验证入口 —— 该 bug 曾是 100% 复现。
   同时确认播放过程中**无间歇性噗噗声**（那是误用内部时钟源的副作用）。
6. **设置掉电保存（见 §15.1）**：手机连上后改音量 / 切模式 / 拉 EQ / 改降噪 / 开关 DFBC →
   串口应在**断链那一刻**出现 `[BS300] settings saved prog=N slot=M vol=[...]` →
   断电重启应出现 `[BS300] settings loaded from flash` / `settings restored prog=N` /
   `boot cache: prog=... vol=... denoise=...`，且听感与断电前一致。
7. **RM 断开过渡音量（见 §19.5）**：建链播放 → 断开 → 切回助听模式应**先以档位 5 出声**，
   约 2s 后自动回到该程序的原设定值，串口出现 `[RM] trans volume restore: prog=N vol=M`。
   反例回归：切回后 2s 内 RM 重连、或短按按键改了音量，都**不应**回写旧值。
8. **RM 流中断静音（见 §6.1，必测）**：
   - **TX 硬断电**（拔电/关机）→ RX 侧**不应**有 1~2 秒「滋」声，应立即安静；
   - **TX 只是调小音量**（流没断）→ 不应触发静音；
   - **弱信号反复丢包** → 听感应平滑（单包丢失被 PLC 接上，不出现「咔」）；连丢触发
     静音后**必须能被 GOODPKT 拉回来**，不能永久静音（这是本改动最需要盯的回归点）；
   - 靠近 / 重新上电恢复时不应有爆音。

## 14. BLE 配置（参考 sleep：单设备连接）

- **单设备连接**：沿用原有 peripheral 单连接（`APP_IDX_MAX = 1`）；**未移植** sleep 的左右耳 peer、BLE Central、双耳 GATT 同步等双连逻辑；**Rempro 已单独移植**（见 §15）。
- **设备名**：`APP_DFLT_DEVICE_NAME = "Smart1654"`（include/ble_std.h）。
- **地址配置**（同 sleep）：`BD_ADDRESS_TYPE = BD_TYPE_PUBLIC`；`PRIVATE_BDADDR`、`APP_PUBLIC_BDADDR`、`RADIO_CLOCK_ACCURACY(500)` 均照 sleep。
- **广播**：ADV 放设备名，可发现模式 `GAP_GEN_DISCOVERABLE`，广播间隔 160×0.625ms≈100ms（与 sleep 一致）；
  公司厂商段 18B 用 sleep 的 `APP_COMPANY_ID_DATA`，并把“耳侧 + 设备 MAC”编入 company data（同 sleep 的 `Advertising_Start`）。
  因名字 Smart1654 为 9 字符、ADV 放不下 18B 厂商段，MAC/耳侧数据改放 **scan response**（主动扫描可读）。
- **bdaddr 修正**（code/ble_std.c）：原 1654 在 PUBLIC 分支里 `Device_Param_Read(PARAM_ID_PUBLIC_BLE_ADDRESS)` **成功后又用 `PRIVATE_BDADDR` 覆盖 bdaddr**
  （原 demo 只走 PRIVATE 分支，坏逻辑未暴露）。已照 sleep 改为：**读到即保留**，读不到回退 `co_default_bdaddr`。
- ⚠ 注意：PUBLIC 实际 MAC 来自 NVR3 / APP 参数里已存的公共地址（sleep 为 `33:44:44:22:22:11`）；
  1654 板若要与 sleep 同 MAC，需预烧 NVR（或后续加“为空则写入 `APP_PUBLIC_BDADDR`”的一次性逻辑，暂未加）。

## 15. Rempro 服务移植 & 服务精简（近期）

**GATT 服务：只保留 Rempro**（Battery/Custom 已移除）
- `SERVICE_ADD_FUNCTION_LIST` 只剩 `RemproService_ServiceAdd`；`SERVICE_ENABLE_FUNCTION_LIST` 空（NULL）。
- Rempro 的 ATT 读写路由仍复用 `ble_custom.c` 的 GATTC 处理器（按 `rempro_env.start_hdl` 区间分流）；`GATTM_AddSvcRsp` 直接记 `rempro_env.start_hdl`。
- app.c/app_process.c 里的电池采样与通知已移除（Battery 不再注册）。

**Rempro Service（新 UUID，自定）**
| 项 | UUID |
|---|---|
| 服务 | `F36F8680-ABEC-11F1-8F9E-7265746F6E65` |
| 特征 ROLE（手机→设备 命令，Read/Write） | `F36F8681-ABEC-11F1-8F9E-7265746F6E65` |
| 特征 ONOFF（设备→手机 Notify） | `F36F8683-ABEC-11F1-8F9E-7265746F6E65` |

- 文件：`code/ble_rempro.c`（服务 DB）、`code/ble_rempro_cmd.c`（HDLC 验配协议：SetVolume/Scene/Gain/MPO/EQ/Denoise/Feedback/Audiometry/GetFitting/电池等，分块 Notify 发送），verbatim 自 sleep。**全部指令的索引表见 §20**。
- 应用胶水：boot `RemproService_Env_Initialize`；主循环 `rempro_tx_poll()`（Kernel_Schedule 后）与连接态 `rempro_cmd_process()`；断链 `rempro_reasm_reset()`；电池量程按 1654 VBAT 采样（BAT_ADC_* 宏）。
- **收发日志已开**：`ble_rempro*.c` 含 `<printf.h>`，UART DIO5 可见 `[REMPRO RX/TX frame/chunk/push]` 等。
- 按键联动：短按音量+ / 长按切程序后推送 `rempro_push_volume_change / rempro_push_scene_change` 给手机（参考 sleep）。

**RM ↔ BLE/调机兼容性（审计后已落地一项）**
- 已加 `saved_prog_before_rm`（rm_app.c）：LINK_ESTABLISHED 记录 RM 前程序，LINK_DISCONNECTED 切回原程序并 `bs300_active()`，避免 RM 掉线后停在程序3 静音。
- 待办（未做）：Rempro 拟合指令的 `audio_streaming/prog3` 护栏；手机侧 RM 启停通道 + RF 回 BLE（当前 RM 开机常开）。

### 15.1 设置掉电保存（程序 / 音量 / EQ / 降噪 / DFBC）

五项用户设置在断电后恢复，存储层由 sleep 原样移植，**本次只补了 Rempro 路径的落盘触发点**。

**保存内容**（均按程序 0-3 各一份）：

| 项 | 字段 | 来源 |
|---|---|---|
| 当前程序 | `active_prog` | 按键长按切程序 / Rempro SetCurrentScene |
| 音量 | `volume[4]` | 按键短按 / Rempro SetVolume |
| 均衡器 | `eq_low[4]` / `eq_mid[4]` / `eq_high[4]` | Rempro SetEqualizer |
| 降噪档位 | `denoise[4]` | Rempro SetDenoise |
| DFBC 开关 | `feedback_onoff[4]` | Rempro SetFeedbackOnOff |
| RM 音频流地址 | `rm_stream_addr`（3B，24 位） | Rempro SetStreamAddress(89) —— **写完立即落盘并复位**，见 §20.6 |

**存储**（[bs300_storage.c](remote_mic_rx_coex_1654/code/bs300_storage.c)）：
Main Flash **Settings sector `0x0015C800`**（2KB），**64B append-only 槽 ×32**，写满才擦一次扇区。
槽内 = `active_prog(1) + volume(4) + eq_low/mid/high(各4) + denoise(4) + feedback_onoff(4)
+ rm_stream_addr(3)` + magic `"BSST"`(4) + CRC16-XMODEM(2) + version(1)。
**读取时从后往前扫，取最新有效槽**（避免擦写抖动）。

> ⚠ **槽格式版本：`SETTINGS_VER` 4 → 5**（2026-09-23 加 `rm_stream_addr`，magic/CRC/ver 整体后移）。
> **v4 的旧槽在新固件下会被判为无效**（magic 位置变了）→ 升级后**首次开机**音量/EQ/降噪/DFBC
> **一次性回到默认**，之后的新槽正常。这是格式变更的既定代价（与历史上加 `feedback_onoff` 时相同）。

**恢复**（[bs300_driver.c:92-118](remote_mic_rx_coex_1654/code/bs300_driver.c#L92-L118)，`bs300_driver_init()` Step 4）：
`bs300_settings_load()` → `bs300_restore_settings()` 灌入 RAM 影子状态 →
`bs300_cache_boot_state()` 把值应用到 DSP 状态（音量 / EQ / 降噪 max_att 偏移 / DFBC 覆盖位）。

**落盘时机 —— 两条路**（与 sleep 行为一致）：

| 来源 | 时机 | 位置 |
|---|---|---|
| **按键**（短按音量+1 / 长按切程序） | 动作发起后**立即**同步落盘 | [app.c:96](remote_mic_rx_coex_1654/app.c#L96) |
| **Rempro 手机命令** | 命令 handler 只改 RAM（注释 *"Flash persist deferred to BLE disconnect"*），**延后到 BLE 断链**时统一落盘 | [ble_std.c](remote_mic_rx_coex_1654/code/ble_std.c) `GAPC_DisconnectInd` |
| **Rempro SetStreamAddress(89)** | **立即**落盘，随后复位（该值只能靠重启生效，见 §20.6）—— 是唯一一条不走「延后到断链」的 Rempro 写指令 | `ble_rempro_cmd.c` `cmd_setstreamaddress()` |

> Rempro 路径延后的理由：`Flash_EraseSector` 在 BLE 连接态不安全（sleep 同注释）。
> 本次补的即是该触发点 —— 此前 handler 里注释声明了"延后到断链"，但 `GAPC_DisconnectInd`
> 里并没有对应的 `bs300_settings_persist()`，导致手机改的设置只活在 RAM，**断电即丢**。

**验证**：手机改五项 → 串口在**断链那一刻**出现 `[BS300] settings saved prog=N slot=M vol=[...]`；
断电重启出现 `[BS300] settings loaded from flash` → `settings restored prog=N` → `boot cache: ...`。
反复改参数不重启则 slot 递增（0→1→2…），满 32 次打 `settings sector erased (32 slots full)`。

> ⚠ **两处已知取舍**（与 sleep 相同，本次未改）：
> 1. **连接期间直接断电会丢** —— 手机没断链就拔电，这期间改的值只在 RAM 未落盘。
> 2. **RM 推流中手机断链** —— 此时 `s_cur_prog == 3`，`bs300_settings_persist()` 会把程序号存成
>    **0**（[bs300_ram_sync.c:426](remote_mic_rx_coex_1654/code/bs300_ram_sync.c#L426) 的 `(s_cur_prog==3)?0:` 逻辑），
>    而非用户原本的听音程序（程序 3 是 RM 音频模式，本就不跨掉电保存）。

## 16. FOTA 空中升级（照 sleep，CFG_FOTA 开关 / fotaskill）

**机制**（同 peripheral_server_sleep）：FOTA ON 时 app 重定位到 `0x00130800`（前部预留给 boot + `fota.bin` 子镜像），
启动向量 7/8 = `Sys_Boot_app_version` / `image_descriptor`，Reset_Handler 在 SystemInit 后调用 `SystemFotaInit()`；
BLE 侧在 Rempro ROLE 写入收到首字节 `0xFD` 时 `Sys_Fota_StartDfu(1)` 进入升级（0xFD 非 HDLC 帧头 0x7E，安全保留）。

**开关**：`include/app.h` 顶部 `//#define CFG_FOTA`（注释=关/开）；开启同时需替换 RTE 变体。
FOTA 开启时 BLE 广播名自动带标识 `Smart1654FOTA`（`ble_std.h` 按 `CFG_FOTA` 分支），方便扫描区分。

> **当前状态：ON**（2026-09-21 切回；2026-09-20 当天曾切 OFF 做普通固件验证，同日又切 ON 过一次）。
> `app.h` 的 `CFG_FOTA` 已放开，`startup_rsl10.S` / `sections.ld` / `.rteconfig` 三个变体文件
> 已换成 `_fota`，`.cproject` 为 FOTA 配置（`CFG_FOTA=1` + 链接 `libfota.a` + post-build 出 `.fota`）。
>
> ⚠ **2026-09-20 补充：`.cproject_fota` 还漏了一处 exclude —— `rsl10_protocol.c`**（见 §16.1 第 7 项）。
> 已修好备份；现在 `cp .cproject_fota .cproject` 才是完整可用的 FOTA 配置。
>
> ⚠ **`.cproject` 会被 Eclipse 覆盖回去**：2026-09-21 发现，用 `cp` 从 IDE 外部改过 `.cproject` 后，
> Eclipse 若开着该工程，之后再保存/刷新会**按它内存里的旧模型重写**，把外部改动抹掉（实测退回成
> 缺 `CFG_FOTA=1` + 缺 `mkfotaimg.py|fota.bin` 排除的旧版，而 rteconfig/startup/sections 没被动）。
> 所以：切换后**先关 Eclipse 工程或切换完再开**；编译后按 §16.1 的自检命令复查一遍 `.cproject`。
>
> **2026-09-24 再次复现**：`cp .cproject_fota .cproject` 后该文件与备份不一致了，一查又退回成
> 上面那个「旧版」—— 4 个变体文件与 `rsl10_protocol.c` 的排除都在、库也只有 `libbass+libfota`，
> 但 **`mkfotaimg.py` / `fota.bin` 只在 Release 配置排除了（Debug 没有）**、`CFG_FOTA=1` 也没了。
> 排查命令（期望各为 2）：
> ```
> for k in rsl10_protocol.c mkfotaimg.py fota.bin; do
>   printf "%-18s %s\n" $k "$(grep -o 'excluding="[^"]*' .cproject | grep -c $k)"
> done
> ```
> 实际影响很小（`.py`/`.bin` 本来就不会被 CDT 编译），但**要出 FOTA 镜像前按 §16.1 重新 `cp` 一次**。

**文件**（remote_mic_rx_coex_1654/ 下）：
- 代码：`code/fota_system.c`（SystemFotaInit→fota_init + weak Device_Param_Prepare）、
  `include/fota_system.h`；`ble_std.c` 里 `SYS_FOTA_VERSION(VER_ID,…)`（CFG_FOTA 时）生成版本符号；
  `ble_custom.c` Rempro ROLE 写 0xFD 触发。
- RTE/Device/RSL10：`startup_rsl10_fota.S` / `_nofota.S`、`sections_fota.ld` / `_nofota.ld`
  （sections_fota 加 `__rom_start=0x00130800`、`__image_size`、FOTA rodata，不含 sleep 的 DRAM_DSP_CM3/.shared）。
- 工程变体：`remote_mic_rx_coex_1654_fota.rteconfig` / `_nofota.rteconfig`（fota 把 Bluetooth Core 的
  BLE Stack+Kernel 换成 Fota，提供 libfota.a/fota.bin/mkfotaimg.py）；`.cproject_fota`（CFG_FOTA=1 定义、
  链接 libfota.a、post-build 用 mkfotaimg.py 生成 `.fota`）与 `.cproject_nofota`。

**切换方法**
- FOTA ON：
  ```
  cp RTE/Device/RSL10/startup_rsl10_fota.S  RTE/Device/RSL10/startup_rsl10.S
  cp RTE/Device/RSL10/sections_fota.ld      RTE/Device/RSL10/sections.ld
  cp remote_mic_rx_coex_1654_fota.rteconfig remote_mic_rx_coex_1654.rteconfig
  cp .cproject_fota .cproject
  # app.h 取消注释 #define CFG_FOTA
  ```
- FOTA OFF：把上面 4 个文件换回 `_nofota`（或 git 还原），并注释 `CFG_FOTA`。
- OFF 构建不依赖 FOTA 库/头，行为与普通固件一致（fota_system.c 由 `--gc-sections` 剥掉）。

**已验证**（FOTA ON 在 IDE 编译通过，0 错误）：链接 `libfota.a`（无 libblelib/libkelib）、post-build 产出
`remote_mic_rx_coex_1654.fota`、`text≈115KB` 自 `0x130800` 起结束低于 `0x0015C800`（bs300 高位区）。
**最近一次 ON 构建：2026-09-17 14:04**（`elf/fota/hex/map` 齐全，map 中 `SystemFotaInit` 落在
`0x00136754` → 高于 `0x00130800`，重定位生效；链接库只有 `libbass.a` + `libfota.a`）。

**2026-09-20 重开 FOTA**：按上面步骤切回 ON，静态自检全部通过（`SystemFotaInit`=1、`__rom_start` 3 处、
`Csub="Fota"`=1、库只有 `libbass.a`+`libfota.a`、`rsl10_protocol.c` 两个配置都已排除）。
**待 IDE 重新编译确认**（未代编）。

> ⚠ **FOTA ON 下的已知告警（既有，非本次引入）**：`ble_std.c` 的 `SYS_FOTA_VERSION(VER_ID,…)` 会报
> `initializer-string for array of 'char' is too long` —— `Sys_Boot_app_id_t` 是 **`char[6]`**，而
> `VER_ID = "Smart1654"` 有 10 字节，被截断（无 NUL）。参考工程 sleep 用的是 `"BS300"`（正好 6 字节）。
> 1664 同样如此（`"Smart1664"`）。两个工程都只在 FOTA 模式下暴露。是否影响 bootloader 的版本校验
> **未定**（镜像与比较用同一串被截断的常量时通常自洽）—— 若 FOTA 升级出问题，从这里先查。

### 16.1 两个 `.cproject` 备份的坑（2026-09-17 修好，2026-09-20 补第 7 项）

> **现状：两个备份已刷新为可用版本**，`cp .cproject_fota .cproject` / `cp .cproject_nofota .cproject`
> 现在可以直接用（2026-09-20 补上了第 7 项，见下）。下面保留问题描述，用于识别**其它工程**
> （sleep / 1664 / 7160test 的备份同样可能是坏的：sleep 与 1664 的 `.cproject_fota` 都含
> `libfota`+`libblelib`+`libkelib` 三个库；它们的备份也**没有**第 7 项）和判断备份是否可信。

`cp .cproject_fota .cproject`（或 `_nofota`）**曾经不能直接用** —— 备份落后于活跃 `.cproject`，
有**两个**问题（2026-09-14 实测）：

| 备份 | 问题 | 后果 |
|---|---|---|
| `.cproject_fota` | ① 缺 6 项 `exclude`；② 库列表含 `libblelib`+`libkelib`**加**`libfota` | ① 启动/链接变体同时入编 → 重复 `Reset_Handler`；② 与 libfota **大量符号重复定义**（`rwble_isr`/`l2cc_send_error_evt`/`gattc_send_error_evt`…）→ 链接失败 |
| `.cproject_nofota` | 缺 6 项 `exclude` | 同上① |

**缺的 6 项 exclude**（Debug + Release 两个配置都要加）：

```
RTE/Device/RSL10/startup_rsl10_nofota.S | RTE/Device/RSL10/startup_rsl10_fota.S |
RTE/Device/RSL10/sections_nofota.ld | RTE/Device/RSL10/sections_fota.ld |
RTE/Device/RSL10/mkfotaimg.py | RTE/Device/RSL10/fota.bin
```

（正确状态是：`startup_rsl10.S` / `sections.ld` 入编，四个变体文件全部排除。）

**第 7 项：`rsl10_protocol.c`（2026-09-20 新增，上面那份清单漏了）**

FOTA 版**还要排除** `RTE/Device/RSL10/rsl10_protocol.c`，nofota 版**必须保留**（即 `_fota` / `_nofota`
在这一项上不同）。Debug + Release 两个配置都要加。它得加在 `excluding="…"` 串的**最前面**。

**为什么**（不是「多排一个更安全」，是语义必需）：`libfota.a` 里有个 `fota_sym.o`，是**符号转发对象** ——
它把整个 BLE 栈以 **absolute 地址**代理到 bootloader 区，实测包含

```
001023fd A BLE_DeviceParam_Set_ADV_IFS     00102265 A Device_Param_Read
00102409 A BLE_DeviceParam_Set_ClockAccuracy  001058e5 A BLE_EVENT_IRQHandler …
```

而 `rsl10_protocol.c` 定义的正是 `Device_Param_Read` / `BLE_DeviceParam_Set_*` 这几个**真实函数**
（源文件在 `pack/source/firmware/syslib/code/`）。FOTA 模式下它们必须走 bootloader 的副本 →
不排除就是**重复定义**。反过来 nofota 版链接的 `libblelib.a` / `libkelib.a` **都不提供**这些符号
（`arm-none-eabi-nm` 实测为空），所以才必须编 `rsl10_protocol.c`。

> 判定方法：`arm-none-eabi-nm --defined-only …/lib/Release/libfota.a | grep Device_Param_Read`
> → 命中 `fota_sym.o`，即 FOTA 版要排除；对 `libblelib.a`/`libkelib.a` 做同样查询为空，即 nofota 版要保留。

自检（切换后应各为 2，Debug/Release 各一处）：
```
grep -o 'excluding="[^"]*' .cproject | grep -c rsl10_protocol.c
```

**正确的库组合**（`libfota` 与 `libblelib`/`libkelib` **互斥**，FOTA 是替换整个 BLE 栈而非叠加）：

| 配置 | 库 |
|---|---|
| FOTA ON | `libfota.a` + `libbass.a` |
| FOTA OFF | `libblelib.a` + `libkelib.a` + `libbass.a` |

切换后自检：`grep -o "lib[a-z]*\.a" .cproject | sort -u`。

**2026-09-17 的修法**（也适用于修其它工程）：**不要** `cp .cproject_fota`（它是坏的），
而是以**当前活跃、且 exclude 完整的 `.cproject`** 为底，只打 FOTA 的四处差异：

1. 两个配置（Debug / Release）的 `assembler.defs` / `c.compiler.defs` / `cpp.compiler.defs`
   各加一条 `CFG_FOTA=1`（共 6 条）；
2. `c.linker.otherobjs` / `cpp.linker.otherobjs` 里把 `libblelib.a` + `libkelib.a` 两行换成 `libfota.a`
   （**换成，不是追加**；`libbass.a` 保留，共 4 处）；
3. `<builder … postbuildStep="">` 填上 `objcopy -O binary … && mkfotaimg.py -o … .fota …`
   （FOTA 版的 post-build；nofota 版为空串）；
4. **两个配置的 `sourceEntries` 各把 `RTE/Device/RSL10/rsl10_protocol.c|` 加到 `excluding` 串最前面**
   （见上面「第 7 项」；2026-09-17 那次漏了这条 —— 那一版活跃 `.cproject` 其实只在 Debug 配了、
   Release 没配，属漏配）。

**判定备份可信的旁证**（比 diff 备份本身可靠）：
- `Debug/objects.mk` 的 `USER_OBJS` —— 上次成功构建实际链的库；
- 同族工程的活跃 `.cproject`（1664 的活跃配置就是干净的 FOTA 版：只有 `libbass.a` + `libfota.a`）。

改完后把活跃 `.cproject` 回写到对应备份（本次已回写：`.cproject_fota` = 干净 FOTA、`.cproject_nofota` =
切换前的 nofota 状态），免得下次切换再踩。

### 16.2 构建注意

- **`.bin` / `.fota` 不是 `make main-build` 产出的**：post-build（`objcopy` → `mkfotaimg.py`）
  挂在 Eclipse 的 `all` 目标上，而 CLI 的 `make all` 因 `$(MAKE)` 展开成含空格的绝对路径而跑不起来。
  → 只跑 CLI 的话要手工补：
  ```
  objcopy -O binary <elf> <bin>
  python RTE/Device/RSL10/mkfotaimg.py -o <fota> RTE/Device/RSL10/fota.bin <bin>
  ```
  Eclipse 里正常 Build 会自动产出。
- **改过 `.cproject` 后要 `Project → Clean` 再 Build**：`Debug/` 下 `makefile`/`subdir.mk`/`objects.mk`
  是 Eclipse 生成的，`.cproject` 变了它们不会自动跟上（实测出现「OBJS 列了对象但没有构建规则」
  → 链接报 `cannot find xxx.o`；以及编译宏缺 `-DCFG_FOTA`）。Clean 会重新生成。

## 17. 电池 DIO3(IO) 采样 & GetBatteryInfo 保护

- **采样方式**（参考 peripheral_server_sleep）：电池经 **DIO3** 进 ADC，每读前重配
  `ADC_NORMAL | ADC_PRESCALE_1280H`、输入 `ADC_POS_INPUT_DIO3`（channel 0）。
- **量程**（app.h）：`BAT_ADC_DIO=3`、`BAT_ADC_MIN=7273`(≈3.19V)、`BAT_ADC_MAX=9050`(≈4.21V)、`BAT_LVL_MAX=100`。
  0% 锚点是 2026-09-20 实测重标定的结果（不是原来那对 6950/9374）；100% 锚点先后两次下调
  （9374 → 9174 → 9050），见 §17.2。
- **周期采样**：`battery_sample_tick()`（app_process.c）挂在 200ms 的 `APP_Timer` 上，按
  `BAT_SAMPLE_TICKS=300` 分频 → **每 60s 读一次 ADC 直接出值**（不做多次平均，原 200ms×16 次
  平均已取消）→ 更新 `app_env.batt_lvl`，每分钟打一行 `__BATT n% raw=<raw>`
  （raw = 该次采样的 ADC 原值，供放电曲线标定用；暂无 BLE 上报）。
- **按需读取**：Rempro `GetBatteryInfo` 命令走同一 `read_battery_raw()`。
- **保护**：`cmd_getbatteryinfo` 算完百分比后 `if (pct==0) pct=1;` —— 低于阈值/取整到 0 时**最低报 1%**，不回 0。
- 注：假定板子电池分压接 DIO3（同 sleep）；脚位/分压不同则改 `BAT_ADC_DIO` 与量程。

### 17.1 低电量告警音

- **触发条件**：`app_env.batt_lvl < LOW_BATT_PCT`（app.h，=20%）。
- **时机**：每次电池采样后调 `low_batt_check()`（app_process.c），即**每 60s 判定一次**——
  - 首次跌破 20% → **立即**播一次；
  - 之后仍低于 20% → 每 `LOW_BATT_CHECK_MS`（app.h，=240000ms=4min）重复一次
    （累加步长 = `BAT_SAMPLE_TICKS × 200` = 60s，即每第 4 次判定播一次）；
  - 回到 ≥20% → `seen`/`elapsed_ms` 复位，下次跌破重新立即播。
  - 跟随电池采样（而不是独立挂在 `Main_Loop`）的两个原因：① 1654 的 `APP_Timer` 无条件每
    200ms re-arm，不像 sleep 要按 RM/BLE 状态分别累加时间；② `app_env.batt_lvl` 开机是 0
    （`App_Initialize` memset），挂在「首次采样之后」天然避开开机误报。
- **提示音命令**：`bs300_play_low_batt_tone()`（bs300_ram_sync.c）→ 直接 I2C 写 **`0xFC12F2`**，
  `bs300_sync_is_busy()` 为真（BS300 session 进行中）时跳过。调用点包在 `#ifdef BS300_ENABLE`。
  ✅ **2026-09-20 上板实测：能正常播报**。
- 注：**`0xFC12F2` 在协议手册里查不到出处**——手册 §2.10 Tune Alerts 只列了 Battery low warning
  的**配置**命令 `0x8012F2`（读 `0x8002F2`），没有这条播放命令的推导规则。代码里另两个播报命令
  是 `0xFD12F2`（音量=0）和 `0xFCD2F2`（音量≠0），sleep 的低电提示音用的是 `0xFD12F2`。
  本工程的 `0xFC12F2` 属**实测获得**，已回填 `docs/bs300/BS300_RSL10_IMPL.md` §6 提示音表。
- **粒度**：阈值用的是单次采样（无平均），所以「实际跌破」到「响第一声」**最多差 60s**；
  代价是阈值附近单次采样噪声可能让 `batt_lvl` 在 19↔20 之间抖动，从而反复触发「首次跌破立即播」。
  若实测发现告警抖动，再给 `LOW_BATT_PCT` 加迟滞（如跌破 19 播、回到 21 才复位）。

### 17.2 百分比曲线重标定（0% 锚点 6950 → 7273，100% 锚点 9374 → 9174）

**起因**：实测发现旧百分比偏乐观——**报 40% 时只剩 1 小时，报 20% 时只剩 15min**。

**0% 锚点（7273）**：设旧显示 `p_old`、新显示 `p_new = a·p_old + b`。剩余时间比
`t(40)/t(20) = 60/15 = 4` 且 `t(p) ∝ p_new` → `(40a+b)/(20a+b) = 4`，解得
`a=1.15385`、`b=−15.3846`，反解 `p_new = 0` 落在 `p_old = 13.33%` —— 即旧的 0% 锚点
（raw 6950 ≈3.0V）**不是真正的关机点**，真正的关机点在 raw 7273。

> 注：`t(40)/t(20)=4` 这个比值约束单独就定出了 `p_new = 0` 的位置（raw 7273），与 100% 锚点取值无关。

**100% 锚点（9174）**：取**满电实测 raw**。原 9374 是满电之上的外推值，实测满电 raw 只有 9174
（先后下调共 200）。原锚点下满电只能显示 91%，设备永远够不到 100%。

```
pct = (raw - 7273) * 100 / (9174 - 7273)      /* 跨度 1901（旧 6950~9374 跨度 2424） */
```

| raw | 电压≈ | 旧 % | 新 % |
|---|---|---|---|
| 9374 | 4.40V | 100 | 100（夹断，≥9174 一律 100） |
| 9174 | 4.29V | 91 | **100** ← 满电实测 raw ✅ 2026-09-20 上板确认满电报 100% |
| 8500 | 3.90V | 63 | 64 |
| 8000 | 3.61V | 43 | 38 |
| **7653** | 3.41V | 29 | **19** ← 低电告警新触发点（`pct < 20`） |
| 7435 | 3.28V | 20 | 8 |
| 7273 | 3.19V | 13 | 0（`cmd_getbatteryinfo` 夹到 1%） |

电压列按 `6950↔3.0V`、`9374↔4.4V` 线性推算，仅供直觉，非实测。

**副作用（预期内）**：

- 低电告警提前：阈值仍是 20%，但 20%（截断后为 19）现在对应 raw ≤7653（≈3.41V），
  按模型剩约 **35min**（原 15min）。
- `GetBatteryInfo`（App 看到的电量）与周期 `__BATT` 打印都走同一组宏，自动同步。
- 越接近空档越保守：raw 落在 6950~7273（≈3.0~3.19V）时一律报 1%。

**可证伪的预测**：按两点线性外推，**满电到关机 ≈176min ≈2.9 小时**。若实测满电续航与之
相差很大（说明真实放电曲线是弯的、不是直线），则 2 个点不足以定曲线，必须补中间采样点
（`(raw, 已用时间)` 若干组）再重标定。

> 已验证部分：2026-09-20 上板确认**满电时 `__BATT` 报 100%**（即 9174 锚点正确）。
> 上面那条总续航预测**尚未验证**——跑一次完整放电、把 `__BATT ... raw=` 记下来即可判定。

#### 17.2.1 100% 锚点再次下调（2026-09-23）

**改动**：`BAT_ADC_MAX` **9174 → 9050**（≈4.29V → ≈4.21V）。只动上限，0% 锚点 7273 不变。

**换算依据（实测标定，不是估算）**：`docs/开发/ADC电量检测开发记录.md` 记的是硬件实测
—— **raw 9374 = 4.4V、raw 6950 = 3.0V**，即 `2424 count / 1400 mV = 1.7314 count/mV`。
本次下调 **124 count ≈ 72mV**（按此斜率；若要刚好 100mV 应为 `9174 − 173 = 9001`）。
换算只用斜率，不受 ADC 零点偏移影响。

**影响**：

| 项 | 旧 | 新 |
|---|---|---|
| 跨度 `MAX − MIN` | 1901 | **1777** |
| 100% 对应电压 | ≈4.29V | ≈4.21V |
| 低电告警（`pct < 20%`）触发 raw | ≤7653 | **≤7628**（≈3.41V） |

跨度变短 → 同样 raw 掉幅对应更大百分比降幅 → **曲线更保守、电量掉得更快**（与 §17.2 修
「偏乐观」的初衷一致）。低电告警触发点随之外移约 14mV，可忽略。

**⚠ 未上板验证**：只改了宏（`app_process.c` 与 `cmd_getbatteryinfo` 共用，自动同步）。
实测时看 `__BATT n% raw=<raw>`：满电 raw 若仍 ≈9174，则显示 100%（夹断）；观察
**掉到 20% 时的 raw 是否落在 7628 附近**即可确认新曲线生效。

### 17.3 读完 disable ADC —— 已试并回退（2026-09-20）

**试过**：`read_battery_raw()` 读完加一行 `Sys_ADC_Set_Config(ADC_DISABLE)`
（`ADC_DISABLE` = `ADC_CFG.FREQ` 字段写 0），想省掉 ADC 本体和内部 VBAT/2 分压的常开电流。

**结果**：功耗**确有下降**，但**采样值全错——一直报 100%**，已回退。**不要再加。**

**原因**：`ADC_NORMAL` 模式下 8 个通道靠序列轮询刷新 `DATA_TRIM_CH[]`，配置完**立刻**读时
序列还没扫到 ch0。以前不暴露，是因为 ADC 一直开着、序列一直在刷，读到的总是上一轮的合法值；
加了 disable 之后 ADC 真的处在「刚使能、尚未完成转换」的窗口，`DATA_TRIM_CH[0]` 是非法值
（读回接近满量程 `0x3FFF`=16383）→ `raw ≥ BAT_ADC_MAX` → 夹到 100%。
**所以真正的病根是「配置后立刻读」，disable 只是让它显形。**

**要真想省这点电**，必须改成**两阶段采样**：本次 tick 只配置+使能，隔一个 200ms tick 再读再关。
`SLOWCLK = 2 MHz`（`app_init.c` `SLOWCLK_PRESCALE_8`）、`PRESCALE_1280H` 下扫完 8 通道约
**5.1ms**，200ms 有约 39 倍余量，不需要猜任何延时常数。代价：`GetBatteryInfo` 不能再同步读，
得改用周期缓存值（`app_env.batt_lvl`）。

**为什么 SDK 能这么干**：`Sys_RFFE_SetTXPower()`（`rsl10_sys_rffe.c`）也是量完就 disable，
但它在配置和读之间插了 `Sys_Delay_ProgramROM(ADC_MEASUREMENT_DELAY)`（10000 个**系统时钟周期**，
`Sys_Delay_ProgramROM` 的参数单位是 cycles），且用的是 `ADC_PRESCALE_200`（比 1280H 快 6.4 倍）。
照抄到 1280H 至少要 25 万周期 ≈ 6ms 阻塞，RM 流期间有拖死音频的风险，不可取。

> 附带收获：**§2.1「每次读前必须重配 ADC」的真正原因是 RF 驱动**——`Sys_RFFE_SetTXPower()`
> 会把 ch0 输入从 DIO3 改成 `ADC_POS_INPUT_AOUT`（量 VDDRF），再 `Sys_ADC_Set_Config(... |
> ADC_DISABLE)` 收尾。我们的重配不是防御性编程，是必需的。
> 省不掉的固定开销：板上 `1MΩ+360kΩ` 外部分压直接跨电池，`4.4V/1.36MΩ ≈ 3.2µA` 恒定流着。

## 18. RM 与 BLE/按键互斥 + 特殊 Rempro 命令

**RM 连接(流)期间：指令白名单**（app.c + ble_rempro_cmd.c）
- `app_env.audio_streaming`（RM LINK_ESTABLISHED=1 / DISCONNECTED=0）期间：
  - **BLE 指令按白名单过滤**：主循环**照常**调 `rempro_cmd_process()` 收帧，白名单在分发侧把关
    （`ble_rempro_cmd.c` 里 switch 之前）：
    - **放行**：`GetDeviceConfig(26)` / `GetBatteryInfo(4)` / `GetCurrentScene(15)` —— 三者都是
      只读查询，**不做 I2C、不改 DSP 状态**，不会干扰 RM 音频
    - **其余一律静默丢弃，不回任何响应**；帧仍要从重装缓冲移除（否则会卡住后续帧解析），
      日志 `[REMPRO] RM streaming, CMD=%u dropped`
  - **按键无效**：`Button_Process()` 动作条件加 `&& !app_env.audio_streaming`（音量/切程序不响应）。
- 放行的指令经 `hdlc_response` + `rempro_tx_poll()` 分块 Notify 发出 —— RM 期间照常能发
  （GATTC 完成事件驱动，不依赖 ke_timer）。
- 缓冲移除抽成了 `reasm_drop_consumed()`（分发路径现在有两个出口：正常处理 / RM 拒绝）。

> ⚠ 白名单的判据是「**只读查询**」。任何会写 BS300 的指令（音量/程序/降噪/EQ/DFBC/验配…）都**不要**
> 加进白名单 —— BS300 的 bit-bang I2C 与 RM 音频通路历史上出过耦合问题（见 §12.6）。

**RM 程序号主动上报**（rm_app.c）
- RM 建立并切到**程序3**播放时，若 `ble_env.state==APPM_CONNECTED` → `rempro_push_scene_change(3)`；
- RM 断开并恢复 `saved_prog_before_rm` 后，若 BLE 连接 → `rempro_push_scene_change(saved_prog)`。

**0xFE：重启并重读 BS300**（ble_custom.c，参照 sleep CS 0xFE）
- Rempro ROLE 写入首字节 `0xFE` → 清 4 程序缓存(`bs300_storage_invalidate`) + `bs300_settings_invalidate()`
  + `bs300_reset_to_defaults()`，然后 `NVIC_SystemReset()`；重启后 `bs300_driver_init()` 从芯片重读参数。

**SetFOTAStatus(ID:87)**（ble_rempro_cmd.h/c）
- 请求：`SYS=0 / CMD=87 / Device_Type(0左右/1左/2右)`；响应：`Flag`(0成功/非0失败)+`status`(0不支持/非0成功)。
- `CFG_FOTA` 开：回 `Flag=0/status=1` 后 `Sys_Fota_StartDfu(1)` 进入 FOTA；
  `CFG_FOTA` 关（普通固件）：回 `Flag=1/status=0`（不支持）。
- 旧入口：Rempro ROLE 写首字节 `0xFD`（CFG_FOTA 下）也能直接触发 FOTA。

**SetMuteData(ID:21)**（ble_rempro_cmd.h/c）
- **按接口文档的 `Mute` 语义**：`data[1]` 非 0 → `bs300_mute()`（静音开），
  0 → `bs300_active()`（取消静音），并同步 `s_device_on`。同样有 `len<2` /
  `bs300_sync_is_busy()` 保护，同样回 `Flag=0, status=1`；`data[0]`（Device_Type）读入但不使用。
- ⚠ **方向与 3 号 `SetDeviceOnOff` 相反**：3 号的 `data[1]` 非 0 是「开」，这里非 0 是「静音」。
  两者不要混用（函数头有注释）。
- ⚠ **不在** RM 白名单里 → RM 推流期间发 21 会被静默丢弃（它会写 BS300，属写指令）。

**SetAudiometryStatus(ID:40)**（ble_rempro_cmd.h/c）
- 请求 `data[1]`：`0`=进入测听，`1`=退出测听。先**立即回** `Flag=0, status=1`，
  再做实际工作（避免上位机等待）。
- **进入（`status=0`）**：`bs300_audiometry_enter()` 写一整套测听参数，然后
  **保持静音、不发 `ACTIVE`（`0x800010`）** —— 测听期间由上位机控制发声，听音程序不应出声。
  完成后重新 arm 一个 2s 的延时推送（`rempro_push_initial_status_done`）。
- **退出（`status=1`）**：`bs300_audiometry_exit()` 恢复（结尾发 `bs300_active()`），
  并 arm 延时推送 `rempro_push_audiometry_exit`。
- 两个推送都发 `CMD_PUSH_INITIAL_STATUS(6)`，靠 `data[1]` 区分：
  `2`=初始化完成（进入测听），`1`=未初始化（退出测听）。
- ⚠ 这两个推送依赖 `bs300_schedule_delayed_push()`，曾因触发条件 bug 完全不工作，见 §12.8。
- `len<2` / `bs300_sync_is_busy()` 时回 `Flag=1`，不做事。
- **进入时挂起 RM、退出时恢复**（见 §18.1）。

**SetFittingStatus(ID:37)**（ble_rempro_cmd.h/c）
- 请求 `data[0]`=Device_Type、`data[1]`=Fitting_Status；响应 `Flag=0` + `status=1`。
- **只回 ACK，不做其他验配动作**，但按 `Fitting_Status` 挂起/恢复 RM（见 §18.1）：
  `0` 开始验配 / `2` 开始OTA升级 / `3` 门店端开始验配 → 关 RM；
  `1` 验配完成 / `4` 门店端结束验配 → 开 RM。
- `len<2` 时回 `Flag=1`。

### 18.1 验配 / 测听期间挂起 RM

**为什么**：RM 处于「已使能、正在搜索」时若在验配过程中突然建链，`LINK_ESTABLISHED` 会
`bs300_mute()` + 切到程序 3，把测听/验配刚写进 DSP 的配置搅乱。所以进这两个流程时先把 RM 关掉。

**不需要做断开收尾**：RM 流期间 `app_env.audio_streaming=1`，白名单只放行 26/4/15，
**37/40 会被静默丢弃** —— 也就是说收到 37/40 时 `audio_streaming` 必为 0。这也是
「RM 流期间 App 无法进入测听」的原因（同样也无法在流期间退出测听）。

**实现**：`ble_rempro_cmd.c` 的 `fitting_rm_disable()` / `fitting_rm_enable()`，
开关序列照抄 `ble_custom.c` Rempro ONOFF 的现成写法：

```c
/* 关 */ BBIF_COEX_CTRL->RX_ALIAS = 0; BBIF_COEX_CTRL->TX_ALIAS = 0;
        RM_Disable(); RF_SwitchToBLEMode();
/* 开 */ RM_Configure(&app_env.rm_param, callback);   /* callback = RM_Callback_TRX/StatusUpdate */
        RF_SwitchToCPMode(); RM_Enable(1000);
```

用静态 `s_rm_held_off` 保证**幂等**：37(0) 后又来 40(0) 只关一次；退出时只把**我们关掉的**
那次开回来（否则重复 `RM_Enable` 会重置 RM 搜索状态）。

`app_env.RM_on_off`（App 侧 ONOFF 偏好位）**不动** —— 即验配期间 App 若去读 ONOFF，
读到的仍是它自己上次写的值，与「RM 实际被挂起」不符。这是**已知的小失真**，因为该标志
只用于「App 写 ONOFF 时决定开/关」，恒读取不参与逻辑；若要让 App 也看到真实状态，需
在 `fitting_rm_disable/enable()` 里同步改它（当前未做）。

- 测听：`40/status=0` **在 `bs300_audiometry_enter()` 之前**关（enter 是一长串阻塞 I2C，
  窗口很长）；enter 失败则立刻恢复 RM，不把设备撂在「没有 RM」的状态。`40/status=1`
  在 `bs300_audiometry_exit()` 之后开（DSP 恢复完再放 RM 回来）。
- `RM_Disable()` **不会回调 `status_update`**（RM 库只在其事件处理里回调），所以不能靠它
  触发断开收尾 —— 好在上面已论证该场景不存在。

## 19. RM ↔ BS300 程序切换（异步 + 抢断续传）

RM 建链要切到程序 3、断链要切回原程序。**两个方向都必须走异步**
（`bs300_switch_program_async()`），否则同步阻塞会饿死 `RM_StatusHandler()` 的音频包投递
（历史教训见 §12.6 的排查弯路）。

### 19.1 「被抢断从当前结束包续传」是现成的，不用新写

`bs300_switch_program_async()` 把切换拆成「每 tick 一条 I2C 命令」，由 `BS300_SYNC_TIMER` 推进。
被抢断时**不需要续传逻辑** —— 它是**状态收敛**而非命令重放：

| 环节 | 位置 | 作用 |
|---|---|---|
| diff 基准 | `switch_diff_*()` 比对的是 `s_dsp_state` | 与「当前已生效状态」比，不回放命令序列 |
| 影子更新 | `bs300_sync_tick()` 里的 `dsp_state_apply()` | **成功门控**（`poll_furproc()` 返回 0 才更新）|

所以影子状态精确等于芯片已生效的参数；任何时刻被抢断，新会话的 diff 只发差额 →
**自动从当前进度续传**。若某条命令重试 30 次后放弃（`state=ERROR`），影子不会更新，
下次 diff 会把它再发一遍 —— **自愈**。

### 19.2 抢断机制

`bs300_switch_program_async()` 忙时（包括按键 / Rempro 正在跑的会话）：

```c
if (bs300_sync_is_busy()) {
    g_bs300_sync.abort_requested = true;      /* 中止当前会话 */
    s_pending_switch = new_prog_idx;          /* 单槽排队 —— 连抢两次保留最后一次（最后意图赢）*/
    s_pending_switch_on_done = on_done;
    return 0;
}
```

被中止的会话 `state → IDLE`；`bs300_sync_timer_handler()` 仍会调用它的 `on_done`
（DONE / ERROR / IDLE 三态都会调），之后主循环的 `bs300_process_deferred()` 再启动排队的那个。
优先级上 RM 高于按键/验配（RM 请求会中止它们）。

### 19.3 完成回调不能盲调 `bs300_active()`

```c
static void rm_bs300_switch_done(void)
{
    if (bs300_switch_pending()) return;   /* 已被抢断，让最后那次收尾 */
    bs300_active();
}
```

被抢断的旧会话其 `on_done` **也会**被调用；若它直接 `active()`，会在过渡中途解除静音，
紧接着撞上下一次切换的参数写入（那次没有 mute 兜底）。
为此在 `bs300_ram_sync.c` 新增查询接口 `bs300_switch_pending()`（返回 `s_pending_switch >= 0`）。

### 19.4 现状与未做项

- 进 / 退两个方向都已异步，`rm_app.c` 内已无同步切换调用
- 建链到真正出声会有几百毫秒延迟（异步会话期间 BS300 保持 mute，直到回调 `active()`）——
  这是异步切换的固有代价
- **未移植 sleep 的 `rm_disc_state` 防抖状态机**：闪断会变成「排队切3 → abort → 排队切回 →
  abort → 排队切3」，**能收敛**，只是多几轮 diff 计算与 I2C。加防抖可省掉这些 churn。
  ⚠ 若要移植：sleep 是按主循环迭代次数累加（`RM_DISC_DEBOUNCE_THRESHOLD = 500`），
  而 1654 主循环末尾有 `SYS_WAIT_FOR_EVENT`，迭代频率不固定，**照搬会算不准**；
  应改用已有的 200ms `APP_Timer` 计 tick。

### 19.5 断开切回的过渡音量（先压到 5，2s 后回设定值）

RM 断开切回助听模式时**先以档位 5 出声，约 2s 后自动回到该程序的原设定值**。
仅作用于「跨程序」那条路径，且**只改 RAM 影子**，不动 Flash 里的用户设定。

**时序**（`rm_app.c`，`RM_Callback_StatusUpdate` → `LINK_DISCONNECTED`）：

| # | 动作 | 位置 |
|---|---|---|
| 1 | `bs300_mute()` 之后、发起异步切换**之前**：`rm_trans_volume_arm(saved)` 记下 `s_volumes[saved]` 到 `s_trans_vol_saved`，把该程序音量压成 `RM_TRANS_VOL_LEVEL`(5) | `rm_app.c` 断链分支 |
| 2 | 异步切换把音量 5 一起下发（目标音量取自 `s_volumes[prog]`，见 §19.1） | `bs300_switch_program_async()` |
| 3 | 切换完成回调里 `bs300_active()` 解除静音 —— 此时是 5 | `rm_bs300_switch_done()` |
| 4 | 起 2s 倒计时（`s_trans_vol_ticks = RM_TRANS_VOL_RESTORE_TICKS`(10)，200ms/tick），到点 `rm_restore_volume_cb()` 用 `bs300_set_volume_notone_async()`（不带提示音）回到原设定值 | `rm_app.c` / `APP_Timer` |

**为什么用 `APP_Timer` 而不是 `bs300_schedule_delayed_push`**：后者是与测听（§18）**共用的单槽**，
且任何 BS300 会话重装 `BS300_SYNC_TIMER` 都会把它的延时提前（重装成 2 tick），或在会话结束时
把它搁浅（会话结束那一路不再 re-arm 定时器）。`APP_Timer` 是开机自启、自我重装的 200ms 周期定时器
（`app_process.c`），独立且稳，代价是精度 ±200ms。`app.h` 里 `rm_trans_volume_tick()` 原型按
`#ifdef BS300_ENABLE` 声明。

**守卫**（`rm_restore_volume_cb`）—— 只在「还停在这个程序」且「音量仍是过渡值 5」时才回写：

- 2s 内 RM 重连（已切到程序3）或用户按键/手机改过音量 → **不覆盖当前发声**，
  只把影子状态里的 5 改回设定值，避免把用户设定冲成 5（那会顺着 §15.1 的断链落盘写进 Flash）；
- `rm_trans_volume_arm()` 对**同一程序重入不重记**设定值 —— RM 闪断会在 2s 窗口内重入，
  若每次重记就会把过渡值 5 当成用户设定值，恢复后**再也回不去**。

**范围 / 已知取舍**：

- `s_saved_prog_before_rm == 3` 那条分支（RM 前就在程序3，只 `bs300_active()`、没有跨程序切换）
  **不做**过渡音量；
- 2s 过渡窗口内若 BLE 断链，`bs300_settings_persist()`（§15.1）会把当时的 5 落盘
  → **该窗口内断链/掉电会丢原设定值**（窗口极短，暂未加保护）；
- 不推 `CMD_PUSH_VOLUME`：手机 UI 看不到这次 5 → 设定值的变化（这是刻意的）。

## 20. BLE 指令索引（Rempro / HDLC 验配协议）

本节是**全部已实现 BLE 指令的索引**，用于按 ID 快速定位代码与相关章节。逐条的行为细节分散在
§15–§19 各节，本节的「相关章节」列指向它们。

> 协议字段的**权威定义**在 `docs/瑞听听力产品控制接口文档(1).md`（⚠ 该文件只覆盖到 86 号，
> 60/61 有、89 号没有 —— 89 号的字段来自更新版接口文档，见 §20.6）。
> **代码里的 handler 是最终事实**：文档与本表冲突时以代码为准。

### 20.1 帧格式与传输

| 项 | 值 |
|---|---|
| 分帧 | HDLC：`7E` 起止，`7E→7D 5E`、`7D→7D 5D` 转义 |
| FCS | **8-bit 字节加和**（`hdlc_fcs`，[ble_rempro_cmd.c:66](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L66)），非 CRC |
| 上行帧体 | `SYS_ID(0) + CMD_ID(2, 小端) + data[] + FCS` |
| 响应帧体 | `SYS_ID(0) + CMD_ID(2, 小端) + Flag + data[] + FCS`；**Flag=0 表示成功** |
| 主动推送 | `SYS_ID=1`（`HDLC_SYS_ID_DEVICE`），见 §20.3 |
| 分块 Notify | 每块 **≤20B**，靠 `rempro_env.sentSuccess`（GATTC 完成事件）驱动 `rempro_tx_poll()`，**不用 ke_timer**（低功耗） |
| GATT | 命令写 ROLE 特征 / 推送走 ONOFF Notify（UUID 见 §15） |
| 入口 / 出口 | GATT 回调 `rempro_reasm_append()`（直接追加，避免单槽竞态）→ 主循环 `rempro_cmd_process()` |
| 缓冲 | 重装 `REASM_BUF_SIZE`=100B；发送 `TX_BUF_SIZE`=200B |

**通用保护**（多数写指令都有，本表「保护」列不再逐一重复）：

- `len` 不足 → 回 `Flag=1`，不做任何事；
- 会写 BS300 的指令先查 `bs300_sync_is_busy()`（有未完成的异步会话时回 `Flag=1`）；
- **`Device_Type` 字段一律忽略** —— 1654 是单耳设备，1/2/3/6/7/9/21/37/40/60/61/89 号等
  都只把它读进变量或直接不读，只有 2 号 `SetVolume` 用它区分左右（`dev_type==0||1` 才下发）。

### 20.2 指令总表（App → 设备，SYS_ID=0）

列含义：「请求」= HDLC 帧体 `data[]`（SYS_ID/CMD_ID 已由分帧层剥掉）；「响应」= 响应帧体的
`data[]`（Flag 单列，不算在内）。

| ID | 名称 | 请求 data[] | 响应 data[] | Handler | 存储/副作用 | 相关章节 |
|---|---|---|---|---|---|---|
| 2 | SetVolume | Device_Type, Volume, Volume2 | status=1 | [cmd_setvolume:324](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L324) | RAM `s_volumes[prog]` + `0x8060B2` 下发；断链落盘 | §15.1 |
| 3 | SetDeviceOnOff | Device_Type, OnOff | status=1 | [cmd_setdeviceonoff:349](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L349) | `bs300_active()` / `bs300_mute()` | §18 |
| 4 | GetBatteryInfo | — | Left_Battery, Right_Battery(=0) | [cmd_getbatteryinfo:490](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L490) | 只读；DIO3 ADC 现采 | §17 |
| 5 | SetFeedbackOnOff | Device_Type, Scene_ID, OnOff | status=1 | [cmd_setfeedbackonoff:397](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L397) | RAM `s_feedback_onoff` → 覆写 `dfbc_enable_mode` bit7；断链落盘 | §15.1 |
| 6 | SetGain | Device_Type, Scene_ID, (Spectrum, Raw)\* | — (Flag=0) | [cmd_setgain:827](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L827) | **程序 Flash**；`bin_gain = Raw-27` | — |
| 7 | SetMPO | Device_Type, Scene_ID, (Channel, Raw)\* | — (Flag=0) | [cmd_setmpo:873](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L873) | **程序 Flash**；`lmt_th = Raw+30` | — |
| 8 | SetCompressRatio | Device_Type, Scene_ID, Turn_Number, (Channel, Step)\* | — (Flag=0) | [cmd_setcompressratio:915](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L915) | **程序 Flash**；Turn 0→`kp1_r_idx`，否则`kp2_r_idx` | — |
| 9 | SetDenoise | Device_Type, Scene_ID, Level(0-5) | — (Flag=0) | [cmd_setdenoise:966](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L966) | RAM `s_denoise`（`max_att += level*3`）+ 重同步；断链落盘 | §15.1 |
| 10 | SetEqualizer | Device_Type, EQ_Type(0低/1中/2高), dB[-5,5] | status=1 | [cmd_setequalizer:590](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L590) | RAM `s_eq_*` + `bs300_set_eq_async()`；断链落盘 | §15.1 |
| 13 | SetPlayVoice | Device_Type, Spectrum(0-16), Decibel(20-100) | status | [cmd_setplayvoice:1003](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L1003) | 纯音：Mute → ITG 写 → Active | `docs/开发/测听功能.md` |
| 14 | SetStopVoice | — | status=1 | [cmd_setstopvoice:1032](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L1032) | Mute → ITG clear | `docs/开发/测听功能.md` |
| 15 | GetCurrentScene | — | prog + vol + denoise + EQ×2（12B） | [cmd_getcurrentscene:534](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L534) | **只读** | §18 白名单 |
| 16 | SetCurrentScene | Device_Type, Scene_ID | status=1 | [cmd_setcurrentscene:515](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L515) | `bs300_switch_program_async()` + 落盘 | §19 / §15.1 |
| 17 | GetFittingData | Device_Type, Scene_ID | Flash raw：gain[32]+CR[32]+MPO[16]（86B） | [cmd_getfittingdata:654](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L654) | **只读**（读程序 Flash） | — |
| 21 | SetMuteData | Device_Type, Mute(非0=静音) | status=1 | [cmd_setmutedata:375](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L375) | `bs300_mute()`/`active()`。⚠ **方向与 3 号相反** | §18 |
| 26 | GetDeviceConfig | — | 版本/程序数/MAC/Product_Type/Chip_Type…（32B） | [cmd_getdeviceconfig:559](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L559) | **只读**；`Product_Type` 与广播包必须一致 | §18 白名单 |
| 33 | GetDeviceOnOff | — | Left_OnOff, Right_OnOff | [cmd_getdeviceonoff:448](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L448) | **只读** RAM `s_device_on` | — |
| 34 | GetFeedbackOnOff | Device_Type, Scene_ID | Left_OnOff, Right_OnOff | [cmd_getfeedbackonoff:458](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L458) | **只读** | — |
| 37 | SetFittingStatus | Device_Type, Fitting_Status | ack=1 | [cmd_setfittingstatus:1084](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L1084) | 按状态**挂起/恢复 RM** | §18 / §18.1 |
| 40 | SetAudiometryStatus | Device_Type, Status(0进/1退) | ack=1 | [cmd_setaudiometrystatus:1112](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L1112) | 测听 enter/exit + 挂起 RM + 延时推送 | §18 / §18.1 |
| 60 | GetAGCOSettings | Device_Type, Scene_ID | Scene_ID, Enable, Thr, Atk(2), Rel(2)（7B） | [cmd_getagcosettings:698](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L698) | **只读**（读程序 Flash） | **§20.5（本次新增）** |
| 61 | SetAGCOSettings | Device_Type, Scene_ID, Enable, Thr, Atk(2), Rel(2) | — (Flag=0) | [cmd_setagcosettings:741](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L741) | **程序 Flash**（新增 AGCO 反编码） | **§20.5（本次新增）** |
| 78 | IICDataCommunity | Device_Type, Data_Number(=1), Data_Length(2), SUB_CMD_Type, payload | 原样回显 | [cmd_iicdatacommunity:1153](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L1153) | ⚠ **真 I2C 中转未实现，目前只回显** | — |
| 87 | SetFOTAStatus | Device_Type | Flag + status | [cmd_fota_status:1207](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L1207) | `CFG_FOTA` 开→`Sys_Fota_StartDfu(1)`；关→不支持 | §16 / §18 |
| 88 | GetStreamAddress | Device_Type | Stream_Address(3, 小端) | [cmd_getstreamaddress:819](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L819) | **只读**，回 accessword 的**高 24 位** | **§20.6（本次新增）** |
| 89 | SetStreamAddress | Device_Type, Stream_Address(3, 小端) | Flag + status | [cmd_setstreamaddress:847](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L847) | 改 `rm_param.accessword` + **立即落盘 Settings** → **复位**；开机回填 | **§20.6（本次新增）** |

\* `(X, Y)\*` 表示「可重复的 (X, Y) 对」，个数由 `len` 推出。

**写指令的两条路径**（选错会「设置不生效」）：

| 路径 | 代表指令 | 何时可见效果 |
|---|---|---|
| 程序 Flash（`fitting_commit(prog, false)`，[ble_rempro_cmd.c:629](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L629)） | 6/7/8/**61** | **下次切到该程序时**（写的是 Flash，不立即下发 DSP） |
| RAM 影子 + 异步下发 | 2/5/9/10 | 立即（`bs300_*_async()`），断链时落盘 |

### 20.3 设备 → App 主动推送（SYS_ID=1）

| ID | 名称 | 推送 data[] | 触发点 | 函数 |
|---|---|---|---|---|
| 4 | `CMD_PUSH_VOLUME` | prog, Device_Type=1, Volume, Volume2 | 按键短按音量 / SetVolume | [rempro_push_volume_change:281](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L281) |
| 5 | `CMD_PUSH_SCENE` | Scene_ID | 按键长按切程序 / RM 建链切 3 / RM 断开恢复原程序 | [rempro_push_scene_change:271](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L271) |
| 6 | `CMD_PUSH_INITIAL_STATUS` | Device_Type=1, Initial_Status(2=初始化完成 / 1=未初始化) | 测听进入 / 退出后延时 2s | [rempro_push_initial_status_done:296](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L296) / [rempro_push_audiometry_exit:309](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c#L309) |

推送均先查 `ble_env.state == APPM_CONNECTED`，未连接直接丢弃。

### 20.4 RM 推流期间的白名单

`app_env.audio_streaming`（RM 建立=1 / 断开=0）期间，分发侧只放行 **26 / 4 / 15** 三条**只读**指令，
其余**静默丢弃不回响应**（日志 `[REMPRO] RM streaming, CMD=%u dropped`）。判据是「不做 I2C、
不改 DSP 状态」—— **会写 BS300 的指令一律不进白名单**，原因见 §12.6 与 §18。

⚠ 因此 **60/61/88/89 在 RM 推流期间都收不到**（61/89 属写指令；60/88 虽只读，但未被加入白名单）。

### 20.5 本次新增（2026-09-23）：60 GetAGCOSettings / 61 SetAGCOSettings

把 AGCO（自动增益控制）作为**每程序的验配参数**开放给 App，走**程序 Flash** 路径（与 6/7/8 号
SetGain/MPO/Compress 同一条路，不是 9 号 SetDenoise 的 RAM 缓存路）。

**字段与换算**：

| 字段 | 宽度 | 说明 |
|---|---|---|
| Scene_ID | 1 | 程序 0-3，越界回 `Flag=1` |
| AGCO_Enable | 1 | 本次**只支持 1**；传 0 → 回 `Flag=1` 失败 ACK + 日志 `Enable=0 unsupported (待确认)`（见下） |
| AGCO_Threshold | 1 | **幅度** 0-30（表示 0 ~ -30 dB）；>30 回 `Flag=1` |
| AGCO_Attack | 2 | 1-2500，**小端**；越界钳位 |
| AGCO_Release | 2 | 1-2500，**小端**；越界钳位 |

**为落 Flash 补的反向编码**（原缺失，不补则 Set 会静默空写）：

- 新增 `encode_agco_flash()`（[bs300_param_encode.c:438](remote_mic_rx_coex_1654/code/bs300_param_encode.c#L438)）——
  逐行翻译 codegen `flash_write.py:encode_agco_flash()`，**6 字节**，threshold 存 `|dB|`；
- `bs300_struct_to_flash()` 增加 `cmd_data == 0x23` 分支（[bs300_param_encode.c:687](remote_mic_rx_coex_1654/code/bs300_param_encode.c#L687)）——
  此前该函数只反编码 WDRC(`0x12`) 与 ENR(`0x1C`)，**AGCO 根本不会写回 Flash**。

**用 ground truth 核对过的布局**：AGCO 模块目录字节 `0x23`、长度 `23 00 02` → 6 字节
（`skills/bs300/data/program_0.json` / `program_1.json` 两个程序一致）；
对拍 codegen 自测样本：`atk=1000, rel=0, thr=3` → `E8 03 00 03 00 00`。
设计计划见 `.claude/skills/bs300/docs/plans/agco_flash_encode.md`。

**⚠ 未实现 / 待确认**：

1. **`AGCO_Enable=0` 无处可存** —— `decode_agco_flash` 在模块存在时**硬编码 `agco_enable=1`**，
   Flash 里没有 enable 位。当前传 0 直接回**失败 ACK**（不静默假成功，符合「显式失败」原则），
   等手册/芯片侧确认 enable 的真实存储方式后再补。
2. 因此 `agco_enable` 被**复用为「AGCO 模块是否存在」的标志**：模块缺失时 Set 也回 `Flag=1` +
   日志 `has no AGCO module`，避免静默空写。
3. **未上板实测**（尤其「断电重启后值是否保持」= 唯一能证明反向编码正确的测试）。

**验证清单**：

1. Set：`Device_Type=1, Scene_ID=0, Enable=1, Thr=9, Atk=1000, Rel=100` → 日志
   `[REMPRO] SetAGCOSettings: ...` + `[FITTING] commit prog=0 active=0 sync=0`
2. 紧接着 Get（`Device_Type, Scene_ID=0`）→ 7 字节回包，值等于刚设的（**同时验证小端**：
   若 Atk 读回不是 `0x03E8`/1000，说明 App 用大端，需调）
3. **断电重启**再 Get → 值应保持 ← 关键项
4. `Enable=0` → 期望失败 ACK + `Enable=0 unsupported (待确认)`
5. 对没有 AGCO 模块的程序 Set → 期望失败 ACK + `has no AGCO module`
6. 回归：6/7/8 号 SetGain/MPO/Compress 仍正常（共用了 `bs300_struct_to_flash` 与 `s_fit_buf`）

> 注：本指令**不做 I2C 即时下发**（`fitting_commit(prog, false)`）。App 需在写完后切一次程序
> 或重新同步，AGCO 才会作用到 DSP —— 与 6/7/8 号行为一致。

### 20.6 本次新增（2026-09-23）：88 GetStreamAddress / 89 SetStreamAddress

设置 RM 音频流的 accessword **高 24 位**（即「音频流地址」），用于与 TX 端的流标识对齐。
**写 Flash 掉电保存 → 立即复位 → 开机重新读**，是本指令的完整闭环。

**accessword 的构成**（宏在 [app.h:80](remote_mic_rx_coex_1654/include/app.h#L80)）：

| 位 | 含义 |
|---|---|
| bit31-8 | **音频流地址**（24 位；`RM_STREAM_ADDR_MASK` 取出；默认 `0xF2CDE6`，`RM_STREAM_ADDR_DEFAULT`） |
| bit7-0 | **固定常量 `0x29`**（`RM_STREAM_ACCESSWORD_FIXED_LOW`） |

> ⚠ **与 RSL10 SDK 样例方向相反**：SDK 写的是 `0x00cde629 | (xx << 24)`
> （低 3 字节固定、高 1 字节随流变，xx = `0xF2`/`0x0D`）；本机按 App 约定改成
> **低 1 字节固定、高 3 字节可设**。两种拆法都能得到出厂值 `0xF2CDE629`，但 App 能改的
> 字节不同 —— 改动前必须与 App/TX 侧对齐，否则 RM 直接搜不到。
> 映射只在 `app.h` 的 `RM_STREAM_ADDR_TO_ACCESSWORD` / `RM_STREAM_ACCESSWORD_TO_ADDR`
> 两个宏里定义，其余代码不许重算。

#### 为什么必须复位 —— 库会把参数拷走

| 环节 | 位置 | 说明 |
|---|---|---|
| `RM_Configure()` 把 `param->accessword` **拷贝**进库内部 `rm_env.accessword` | `remote_micLib/rm_event.c:51` | `APP_RM_Init()` 里调用，**之后就与 `app_env.rm_param` 脱钩** |
| `RF_InitRegistersCustomMode()` 把 `rm_env.accessword` 写进 RF `PATTERN` 寄存器 | `remote_micLib/rm_pkt_hdl.c:855` | 只在 RF 初始化（RM 启动）时发生 |

所以改 `app_env.rm_param.accessword` 对已跑起来的 RM **毫无影响** —— 只能重启让
`APP_RM_Init()` 用新值重新 `RM_Configure()`。RM 推流期间该指令本来也会被白名单丢掉（§20.4）。

#### 执行流程

| # | 动作 | 位置 |
|---|---|---|
| 1 | 校验 `len >= 4`；`Device_Type` 忽略；`Stream_Address` 3 字节**小端**拼成 24 位 | `cmd_setstreamaddress()` |
| 2 | 写 `app_env.rm_param.accessword = RM_STREAM_ADDR_TO_ACCESSWORD(addr)`（`(addr << 8) \| 0x29`） | 同上 |
| 3 | **立即落盘**：`bs300_settings_persist()` → Settings 槽（含当前音量/EQ/降噪/DFBC） | `bs300_ram_sync.c` |
| 4 | 落盘**失败** → 回 `Flag=1`，**不复位**（不把失败伪装成成功） | 同上 |
| 5 | 成功 → 回 `Flag=0 + status=1`，置 `s_reset_pending` | 同上 |
| 6 | `rempro_tx_poll()` 里等 **ACK 最后一个分块被协议栈确认发出**（`!s_tx_in_progress && rempro_env.sentSuccess`）→ `NVIC_SystemReset()` | `ble_rempro_cmd.c` |
| 7 | 重启后 `APP_RM_Init()` 调 `bs300_settings_load_stream_addr()` 取回地址（无记录则用默认），**再** `RM_Configure()` | [rm_app.c:112](remote_mic_rx_coex_1654/code/rm_app.c#L112) |
| 8 | 该地址在 `app.c` 主函数里打印一行 `[RM] stream addr=0x...... (from flash\|default)` —— **不能在 `APP_RM_Init` 里打，那里中断未开会死锁**，见 §10 的 ⛔ 提示。`from flash`/`default` 由 `rm_stream_addr_from_flash()` 给出，用于一眼区分「回填成功」还是「用了默认」 | `app.c` + `rm_app.c` |

> **第 6 步为什么不能直接复位**：响应走分块 Notify，复位太早会把 ACK 掐断，App 会当成
> 「无响应」而重发。等 GATTC 完成事件是唯一可靠的信号。
> **第 7 步为什么在 `APP_RM_Init` 里而不是 BS300 的加载流程里**：`APP_RM_Init()` 在
> [app_init.c:303](remote_mic_rx_coex_1654/code/app_init.c#L303) 早期就被调用，早于
> `bs300_driver_init()` —— 等那边加载就太晚了，`RM_Configure()` 早把旧值拷走了。

> ⚠ **边角情况**：如果 App 在 ACK 发出前就断链，`sentSuccess` 不会置位 → **本次不复位**。
> 地址已落盘、下次开机自然生效，只是没立刻生效。刻意不加超时轮询 —— 那会在断链后
> 突然重启，行为更怪。

#### 88 GetStreamAddress —— 读回

请求只有 `Device_Type`（忽略，单耳）；响应 `Flag + Stream_Address(3, 小端：首字节=低位)。
**只读**：回 `RM_STREAM_ACCESSWORD_TO_ADDR(rm_param.accessword)` = 高 24 位，即 89 号写入的
那个「配置值」。

> **⚠ 回的是 `rm_param`（配置值），不是库里的 `rm_env`（在效值）**。两者只有在
> 「89 号已写、复位还没发生」这一小段窗口内不同 —— 此时回 `rm_param` 才能让 App 看到
> 自己刚设的值（Set→Get 往返一致）；若回 `rm_env` 会显示旧值，看起来像 Set 失败。

**本轮新增/改动**：Settings 槽加 `rm_stream_addr`(3B) 并把 `SETTINGS_VER` 4→5（见 §15.1 的
版本变更提示）；`bs300_settings_save/load` 各加一个参数；新增
`bs300_settings_load_stream_addr()`；`bs300_settings_persist()` 改为返回 `bool`。

**验证清单**：

1. 发 `Device_Type=1, Stream_Address = E6 CD F2`（**小端** → 值 `0xF2CDE6`）→
   日志顺序应为
   `[REMPRO] SetStreamAddress: dev=1 addr=0xF2CDE6 accessword=0xF2CDE629`
   → `[BS300] settings saved prog=N slot=M ...`
   → `[REMPRO] SetStreamAddress: reset to apply` → 设备重启
   （`accessword` 末字节恒为 `0x29` —— 那是固定的低字节，不随地址变）
2. **验证字节序**：小端才对得上 `addr=0xF2CDE6`。若 App 发的是 `F2 CD E6`（大端写法），
   日志会变成 `addr=0xE6CDF2` —— 说明与固件的小端约定不一致
3. **验证读回**：发 88 号（只带 `Device_Type`）→ 应回 3 字节 = `E6 CD F2`（小端），日志
   `[REMPRO] GetStreamAddress: dev=1 addr=0xF2CDE6`。**Set→Get 往返应一致**
4. **验证持久化**（核心）：复位后开机日志应出现
   `[RM] stream addr=0xF2CDE6` —— 且**要带 `(from flash)`**；若显示 `(default)` 说明
   值虽然对，但用的是出厂默认、**没存住**（两者取值恰好相同，只能靠这个标签区分）
5. **验证掉电**：设一个**非默认**地址（如 `12 34 56`，小端发 `56 34 12`）→ 直接拔电再上电，
   开机日志应是 `[RM] stream addr=0x123456 (from flash)`。用非默认值才能和「默认回退」区分开
6. **端到端**（唯一能证明地址真被 RF 采用的测试）：设新地址 → 重启 → TX 用同一 accessword
   则能连上，不同则搜不到
7. 字段 < 4 字节（89）/ 无 payload（88）→ 失败 ACK；89 失败时**不复位**
8. 回归：`SetVolume` 等改动 + 断链 → 生效且**断链后**日志出现 `settings saved`（确认新增字段
   没破坏原有保存路径）；RM 推流期间发 88/89 应被静默丢弃

### 20.7 本次（2026-09-23）改动的文件清单

| 文件 | 改动 |
|---|---|
| [include/ble_rempro_cmd.h](remote_mic_rx_coex_1654/include/ble_rempro_cmd.h) | 新增 `CMD_GETAGCOSETTINGS 60` / `CMD_SETAGCOSETTINGS 61` / `CMD_GETSTREAMADDRESS 88` / `CMD_SETSTREAMADDRESS 89` |
| [code/ble_rempro_cmd.c](remote_mic_rx_coex_1654/code/ble_rempro_cmd.c) | 新增 4 个 handler（`:698` / `:741` / `:819` / `:847`）+ 分发 switch 4 个 case（`:1418` 起）；新增 `s_reset_pending` 与 `rempro_tx_poll()` 里的延迟复位 |
| [code/bs300_param_encode.c](remote_mic_rx_coex_1654/code/bs300_param_encode.c) | 新增 `encode_agco_flash()`（`:438`）；`bs300_struct_to_flash()` 加 `0x23` 分支（`:687`） |
| [code/bs300_storage.c](remote_mic_rx_coex_1654/code/bs300_storage.c) / [.h](remote_mic_rx_coex_1654/include/bs300_storage.h) | Settings 槽加 `rm_stream_addr`(3B)，`SETTINGS_VER` 4→5；`save/load` 加参数；新增 `bs300_settings_load_stream_addr()`；抽出 `settings_find_latest()` |
| [code/bs300_ram_sync.c](remote_mic_rx_coex_1654/code/bs300_ram_sync.c) / [.h](remote_mic_rx_coex_1654/include/bs300_ram_sync.h) | 两处 `settings_save` 带上流地址（`cur_stream_addr()`）；`bs300_settings_persist()` 改为返回 `bool` |
| [code/bs300_driver.c](remote_mic_rx_coex_1654/code/bs300_driver.c) | 两处 `bs300_settings_load()` 补 `NULL`（BS300 不关心流地址） |
| [include/app.h](remote_mic_rx_coex_1654/include/app.h) | 新增 `RM_STREAM_ACCESSWORD_FIXED_LOW` / `RM_STREAM_ADDR_DEFAULT` / `_MASK` + 两个映射宏 `RM_STREAM_ADDR_TO_ACCESSWORD` / `RM_STREAM_ACCESSWORD_TO_ADDR` |
| [code/rm_app.c](remote_mic_rx_coex_1654/code/rm_app.c) | `APP_RM_Init()` 的 accessword 改为**从 Flash 读**（带默认回退 + 宏化前缀） |
| [app.c](remote_mic_rx_coex_1654/app.c) | 主函数启动日志后加一行 `[RM] stream addr=0x%06lX (from flash\|default)`（`APP_RM_Init` 里不能打印，见 §10 ⛔） |
| [code/rm_app.c](remote_mic_rx_coex_1654/code/rm_app.c) | 另加 `rm_stream_addr_from_flash()`（+ `app.h` 声明）供上一条日志区分来源 |
| `.claude/skills/bs300/docs/plans/agco_flash_encode.md` | 新增：AGCO Flash 反编码接口计划（含 ground truth 核对记录） |

**编译状态**：仅改动源码，**未编译、未上板**。验证项见 §20.5 / §20.6 各自的清单。
