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

## 13. 验证步骤

1. Eclipse 导入/编译 `remote_mic_rx_coex_1654` Debug，确认链接通过（bs300 新增文件自动入编）。
2. 烧录后用 UART DIO5(115200) 观察：`started` → `[BS300] …` → `BS300_INIT_OK/FAIL`。
3. 与已配对发射机建链：应听到 OD 输出音频；断链应静音；串口出现 `RM_LINK_ESTABLISHED/DISCONNECTED`。
4. 手机/工具扫描应看到名为 **Smart1654** 的可连接广播；用主动扫描可读到 scan response 里的厂商段（含 MAC/耳侧）。
5. 示波器查 DIO0/DIO1（OD 差分）与 DIO8/DIO7（BS300 I2C）波形。
   **DIO7 上不应再出现采样钟**（已改到 DIO10）。
6. **RM 重连回归测试（必测）**：建链 → 断开 → **反复重连 5~10 次**，每次音频都应正常，
   不应出现失真/断续。这是 §12.6 那个 bug 的验证入口 —— 该 bug 曾是 100% 复现。
   同时确认播放过程中**无间歇性噗噗声**（那是误用内部时钟源的副作用）。
6. **设置掉电保存（见 §15.1）**：手机连上后改音量 / 切模式 / 拉 EQ / 改降噪 / 开关 DFBC →
   串口应在**断链那一刻**出现 `[BS300] settings saved prog=N slot=M vol=[...]` →
   断电重启应出现 `[BS300] settings loaded from flash` / `settings restored prog=N` /
   `boot cache: prog=... vol=... denoise=...`，且听感与断电前一致。

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

- 文件：`code/ble_rempro.c`（服务 DB）、`code/ble_rempro_cmd.c`（HDLC 验配协议：SetVolume/Scene/Gain/MPO/EQ/Denoise/Feedback/Audiometry/GetFitting/电池等，分块 Notify 发送），verbatim 自 sleep。
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

**存储**（[bs300_storage.c:285-407](remote_mic_rx_coex_1654/code/bs300_storage.c#L285-L407)）：
Main Flash **Settings sector `0x0015C800`**（2KB），**64B append-only 槽 ×32**，写满才擦一次扇区。
槽内 = `active_prog(1) + volume(4) + eq_low/mid/high(各4) + denoise(4) + feedback_onoff(4)` + magic `"BSST"`
+ CRC16-XMODEM + version。**读取时从后往前扫，取最新有效槽**（避免擦写抖动）。

**恢复**（[bs300_driver.c:92-118](remote_mic_rx_coex_1654/code/bs300_driver.c#L92-L118)，`bs300_driver_init()` Step 4）：
`bs300_settings_load()` → `bs300_restore_settings()` 灌入 RAM 影子状态 →
`bs300_cache_boot_state()` 把值应用到 DSP 状态（音量 / EQ / 降噪 max_att 偏移 / DFBC 覆盖位）。

**落盘时机 —— 两条路**（与 sleep 行为一致）：

| 来源 | 时机 | 位置 |
|---|---|---|
| **按键**（短按音量+1 / 长按切程序） | 动作发起后**立即**同步落盘 | [app.c:96](remote_mic_rx_coex_1654/app.c#L96) |
| **Rempro 手机命令** | 命令 handler 只改 RAM（注释 *"Flash persist deferred to BLE disconnect"*），**延后到 BLE 断链**时统一落盘 | [ble_std.c](remote_mic_rx_coex_1654/code/ble_std.c) `GAPC_DisconnectInd` |

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
`remote_mic_rx_coex_1654.fota`、`text≈115KB` 自 `0x130800` 起结束低于 `0x0015C800`（bs300 高位区）。默认状态 = **OFF**。

## 17. 电池 DIO3(IO) 采样 & GetBatteryInfo 保护

- **采样方式**（参考 peripheral_server_sleep）：电池经 **DIO3** 进 ADC，每读前重配
  `ADC_NORMAL | ADC_PRESCALE_1280H`、输入 `ADC_POS_INPUT_DIO3`（channel 0）。
- **量程**（app.h）：`BAT_ADC_DIO=3`、`BAT_ADC_MIN=6950`(≈3.0V)、`BAT_ADC_MAX=9374`(≈4.4V)、`BAT_LVL_MAX=100`。
- **周期采样**：`APP_Timer`(200ms) 经 `read_battery_raw()` 采样，16 次平均 → `app_env.batt_lvl`，
  每 ~3.2s 打 `__BATT n%`（暂无 BLE 上报，供后续使用）。
- **按需读取**：Rempro `GetBatteryInfo` 命令走同一 `read_battery_raw()`。
- **保护**：`cmd_getbatteryinfo` 算完百分比后 `if (pct==0) pct=1;` —— 低于阈值/取整到 0 时**最低报 1%**，不回 0。
- 注：假定板子电池分压接 DIO3（同 sleep）；脚位/分压不同则改 `BAT_ADC_DIO` 与量程。

## 18. RM 与 BLE/按键互斥 + 特殊 Rempro 命令

**RM 连接(流)期间互斥**（app.c）
- `app_env.audio_streaming`（RM LINK_ESTABLISHED=1 / DISCONNECTED=0）期间：
  - **BLE 指令不处理**：主循环 `rempro_cmd_process()` 被跳过并 `rempro_reasm_reset()` 丢弃残留 RX 帧；
  - **按键无效**：`Button_Process()` 动作条件加 `&& !app_env.audio_streaming`（音量/切程序不响应）。

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
