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

## 4. 构建与总开关

- IDE：ON Semi Eclipse（GNU ARM，arm-none-eabi-gcc，`-mcpu=cortex-m3`）。
- `.cproject` sourceEntries 按目录整收：**新增到 `code/` 的 .c、`include/` 的 .h 自动参与编译**，无需改工程文件。
- 总开关（include/app.h）：
  - `OUTPUT_INTRF = OD_OUTPUT`（解码直出 OD）；可选 `SPI_TX_CODED_OUTPUT` / `SPI_TX_RAW_OUTPUT`。
  - `BS300_ENABLE`：BS300 子系统总开关（定义即启用）。
  - `OUTPUT_INTERFACE`（在 pack 的 printf.h，未在本工程覆盖 → 默认 UART）：打印出口选择。

## 5. 引脚分配

| 功能 | 引脚 | 说明 |
|------|------|------|
| OD_P / OD_N（受话器，差分） | DIO0 / DIO1 | `DIO_MODE_OD_P`，照 peripheral_server_sleep |
| BS300 I2C SCL / SDA（bit-bang） | DIO8 / DIO7，addr 0x01 | 与 sleep 一致；DIO7 同时作 audiosink 采样钟输入(SAMPL_CLK) |
| 调试 UART TX / RX | DIO5 / DIO6（**pack printf.c 内硬编码**） | 115200；当前调试口在 DIO5，改引脚需改 pack 的 printf.c（影响所有工程） |
| 按键（active low，上拉） | DIO12 | 短按音量+1 / 长按切程序；参考 sleep；由原打印脚让出 |
| 采样/audiosink 时钟输入 | DIO7 | `Sys_Audiosink_InputClock(SAMPL_CLK…)`，无条件配置 |
| DIO_SYNC_PULSE | DIO8 | 复用为 BS300 SCL；GPIO 默认输出 |
| 上电暂停/恢复(recovery) | DIO13 | 接地暂停便于重刷，勿占用 |
| 空闲/预留 | DIO2/3/4/9/10/14 | 调试 DIO15/11 为 GPIO 输出 |

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

## 11. 关键文件清单

修改（相对 commit `14811e3`）：
- include/app.h、code/app_init.c、code/app_func.c、code/app_process.c、code/rm_app.c、app.c
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

## 13. 验证步骤

1. Eclipse 导入/编译 `remote_mic_rx_coex_1654` Debug，确认链接通过（bs300 新增文件自动入编）。
2. 烧录后用 UART DIO5(115200) 观察：`started` → `[BS300] …` → `BS300_INIT_OK/FAIL`。
3. 与已配对发射机建链：应听到 OD 输出音频；断链应静音；串口出现 `RM_LINK_ESTABLISHED/DISCONNECTED`。
4. 手机/工具扫描应看到名为 **Smart1654** 的可连接广播；用主动扫描可读到 scan response 里的厂商段（含 MAC/耳侧）。
5. 示波器查 DIO0/DIO1（OD 差分）与 DIO8/DIO7（BS300 I2C）波形。

## 14. BLE 配置（参考 sleep：单设备连接）

- **单设备连接**：沿用原有 peripheral 单连接（`APP_IDX_MAX = 1`）；**未移植** sleep 的左右耳 peer/REMPRO、BLE Central、双耳 GATT 同步等双连逻辑。
- **设备名**：`APP_DFLT_DEVICE_NAME = "Smart1654"`（include/ble_std.h）。
- **地址配置**（同 sleep）：`BD_ADDRESS_TYPE = BD_TYPE_PUBLIC`；`PRIVATE_BDADDR`、`APP_PUBLIC_BDADDR`、`RADIO_CLOCK_ACCURACY(500)` 均照 sleep。
- **广播**：ADV 放设备名，可发现模式 `GAP_GEN_DISCOVERABLE`，广播间隔 160×0.625ms≈100ms（与 sleep 一致）；
  公司厂商段 18B 用 sleep 的 `APP_COMPANY_ID_DATA`，并把“耳侧 + 设备 MAC”编入 company data（同 sleep 的 `Advertising_Start`）。
  因名字 Smart1654 为 9 字符、ADV 放不下 18B 厂商段，MAC/耳侧数据改放 **scan response**（主动扫描可读）。
- **bdaddr 修正**（code/ble_std.c）：原 1654 在 PUBLIC 分支里 `Device_Param_Read(PARAM_ID_PUBLIC_BLE_ADDRESS)` **成功后又用 `PRIVATE_BDADDR` 覆盖 bdaddr**
  （原 demo 只走 PRIVATE 分支，坏逻辑未暴露）。已照 sleep 改为：**读到即保留**，读不到回退 `co_default_bdaddr`。
- ⚠ 注意：PUBLIC 实际 MAC 来自 NVR3 / APP 参数里已存的公共地址（sleep 为 `33:44:44:22:22:11`）；
  1654 板若要与 sleep 同 MAC，需预烧 NVR（或后续加“为空则写入 `APP_PUBLIC_BDADDR`”的一次性逻辑，暂未加）。
