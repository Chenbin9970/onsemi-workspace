# RSL10 Peripheral Server Sleep — 编译指南

## 环境要求

| 组件 | 来源 | 路径 |
|------|------|------|
| ARM GCC 10.2.1 | ON Semiconductor IDE V4.3.1 | `C:\Program Files (x86)\onsemi\IDE_V4.3.1.132\arm_tools\bin` |
| RSL10 CMSIS-Pack 3.7.606 | ARM Pack Manager | `%LOCALAPPDATA%\Arm\Packs\ONSemiconductor\RSL10\3.7.606` |
| GNU Make | Git Bash / MSYS2 | — |

---

## 工程结构

```
peripheral_server_sleep/
├── app.c                         # 主入口 + Main_Loop
├── include/                      # 头文件
│   ├── app.h                     #   主配置 + APP_RM_ENABLE 在此定义
│   ├── ble_std.h                 #   BLE GAP/连接参数
│   ├── ble_custom.h              #   Custom Service（含远程麦控制特征值）
│   ├── ble_bass.h                #   电池服务
│   ├── calibration.h             #   校准策略 + 电压目标
│   ├── rm_pkt.h                  #   远程麦协议 API（RTE Remote_Mic 组件提供）
│   ├── dsp_pm_dm.h               #   LPDSP32 G722 解码器常量
│   └── queue.h                   #   环形缓冲
├── code/                         # 源码
│   ├── app_init.c                #   硬件 + BLE + 音频初始化
│   ├── app_process.c             #   消息处理 + Sleep + AUDIOSINK ISR
│   ├── ble_std.c                 #   BLE GAP/GATT 事件处理
│   ├── ble_custom.c              #   Custom Service 读写处理
│   ├── ble_bass.c                #   电池服务
│   ├── calibration.c             #   电压校准
│   ├── wakeup_asm.S              #   Sleep 唤醒汇编
│   ├── audio_func.c              #   LPDSP32 解码 ISR + ASRC + OD 输出
│   ├── rm_app.c                  #   远程麦应用初始化 + RX 回调
│   ├── dsp_pm_dm.c               #   G722 解码器 LPDSP32 程序/数据镜像
│   ├── queue.c                   #   环形缓冲实现
│   ├── rm_event.c                #   [RTE] 远程麦协议状态机
│   ├── rm_pkt_hdl.c              #   [RTE] 远程麦数据包处理
│   └── config_data.c             #   [RTE] RF 寄存器 + 频率表
├── syslib/                       # 系统库副本（Makefile 用，IDE 排除）
│   ├── rsl10_sys_clocks.c
│   ├── rsl10_sys_power.c
│   ├── rsl10_sys_power_modes.c
│   ├── rsl10_sys_rffe.c
│   ├── rsl10_sys_dma.c
│   ├── rsl10_sys_flash.c
│   └── rsl10_sys_timers.c
├── RTE/                          # CMSIS RTE 运行时环境
│   ├── Device/RSL10/
│   │   ├── RTE_Device.h          #   驱动配置（DMA, GPIO, Retention trim）
│   │   ├── sections.ld           #   链接脚本（含 .dsp section）
│   │   ├── startup_rsl10.S
│   │   ├── system_rsl10.c
│   │   └── rsl10_protocol.c
│   └── RTE_Components.h
├── Makefile                      # 命令行构建
├── .cproject                     # IDE 工程配置（排除列表 + APP_RM_ENABLE）
├── .project                      # IDE 工程描述
└── peripheral_server_sleep.rteconfig  # RTE 组件配置
```

> **`[RTE]`** 标记的文件由 CMSIS-Pack RTE 系统管理，IDE 自动编译。本地副本仅供 Makefile 使用，已通过 `.cproject` 排除。

---

## 构建系统

工程支持 **两种构建方式**，各有不同文件来源：

| | Makefile | IDE (Eclipse CDT + RTE) |
|---|---|---|
| 应用代码 `code/*.c` | ✓ | ✓（排除 RM 和 syslib） |
| 远程麦库 `rm_*.c` | ✓（本地副本） | ✓（RTE Remote_Mic 组件） |
| 系统库 `rsl10_sys_*.c` | ✓（`syslib/` 本地副本） | ✓（RTE System 组件） |
| 校准库 `rsl10_calibrate_*.c` | ✗ | ✓（RTE Calibrate 组件） |
| RTE 配置 | ✗ | ✓（自动根据 `.rteconfig` 引入） |

### 方式一：Makefile（推荐 CLI 使用）

```bash
cd peripheral_server_sleep
make          # 编译（Debug，-O0 -g3）
make clean    # 清理
```

产物：`build/peripheral_server_sleep.elf`

#### 编译的文件（共 19 个 C + 2 个 ASM）

| 类别 | 文件 | 说明 |
|------|------|------|
| 应用入口 | `app.c` | main() + Main_Loop |
| BLE 栈 | `code/ble_std.c`, `ble_bass.c`, `ble_custom.c`, `ble_custom.h` | GAP/GATT + 电池 + 远程麦控制 |
| 硬件初始化 | `code/app_init.c`, `code/app_process.c` | 时钟/校准/Sleep/音频/ISR |
| 校准 | `code/calibration.c` | 电压校准 |
| 远程麦 | `code/rm_event.c`, `rm_pkt_hdl.c`, `config_data.c`, `rm_app.c` | 远程麦协议栈 |
| 音频 DSP | `code/audio_func.c`, `dsp_pm_dm.c`, `queue.c` | G722 解码/ASRC/OD 输出 |
| 系统库 | `syslib/rsl10_sys_*.c` (7 个) | 时钟/DMA/Flash/电源/Timer |
| RTE | `RTE/Device/RSL10/system_rsl10.c`, `rsl10_protocol.c` | 系统初始化 |
| 汇编 | `code/wakeup_asm.S`, `RTE/Device/RSL10/startup_rsl10.S` | 唤醒 + 启动向量 |

#### 编译宏 (`-D` flags)

```
RSL10_CID=101  CFG_CON=8  CFG_BLE=1  CFG_SLEEP  CFG_HW_AUDIO
CFG_ALLROLES=1  CFG_APP  CFG_APP_BATT  CFG_ATTS=1  CFG_EMB=1
CFG_HOST=1  CFG_RF_ATLAS=1  CFG_ALLPRF=1  CFG_PRF=1  CFG_NB_PRF=2
CFG_CHNL_ASSESS=1  CFG_SEC_CON=1  CFG_EXT_DB  CFG_PRF_BASS=1  _RTE_
```

> **`APP_RM_ENABLE`** 不在命令行定义，而是直接写在 `include/app.h:29`，确保 IDE 和 Makefile 行为一致。

#### 链接的库

| 库 | 用途 |
|----|------|
| `libblelib.a` | BLE 协议栈核心 |
| `libkelib.a` | 内核调度器 |
| `libbass.a` | 电池服务 Profile |
| `libweak_prf.a` | 其余 Profile 弱符号 |

### 方式二：IDE (Eclipse CDT)

IDE 中打开工程 → **Refresh (F5)** → **Clean** → **Build Project (Ctrl+B)**

IDE 编译时：
- 通过 RTE 从 CMSIS Pack 引入 `rm_event.c`, `rm_pkt_hdl.c`, `config_data.c`
- 通过 RTE 从 CMSIS Pack 引入所有 `rsl10_sys_*.c`
- 通过 RTE 从 CMSIS Pack 引入 `rsl10_calibrate*.c`
- `.cproject` 排除了本地 `syslib/` 和 `code/rm_*.c`，防止重复链接
- RTE 组件配置在 `peripheral_server_sleep.rteconfig` 中

#### RTE 组件列表

| 组件 | 用途 |
|------|------|
| `Device::Bluetooth Core::BLE Stack` (release) | BLE 协议栈 `libblelib.a` |
| `Device::Bluetooth Core::Kernel` (release) | 内核 `libkelib.a` |
| `Device::Bluetooth Profiles::BASS` (release) | 电池服务 `libbass.a` |
| `Device::Libraries::System` (source) | syslib 驱动源码（时钟/DMA/Flash/电源/Timer） |
| `Device::Libraries::Calibrate` (source) | 电压校准库 |
| **`Device::Libraries::Remote_Mic` (source)** | 远程麦协议栈 `rm_pkt.h` + 源码 |
| `Device::Startup` (source) | 启动文件 + 链接脚本 |

### 方式三：IDE 命令行（headless，需关 IDE）

```bash
"C:\Program Files (x86)\onsemi\IDE_V4.3.1.132\eclipse\eclipsec.exe" \
  -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data "C:\Users\admin\onsemi-workspace4.5" \
  -build "peripheral_server_sleep/Debug"
```

---

## Release 编译

修改 Makefile 中 `CFLAGS` 的 `-O0 -g3` → `-Os`，或在 IDE 中切换到 Release 配置。

---

## ble_android_asha — CLI 编译 + 下载

ble_android_asha（ASHA 助听器 BLE 工程）使用 Eclipse 自动生成的 Makefile，无需 IDE 即可编译下载。

### 环境

同 `peripheral_server_sleep`，额外需要 J-Link 用于下载：

| 组件 | 路径 |
|------|------|
| ARM GCC 10.2.1 | `C:\Program Files (x86)\onsemi\IDE_V4.3.1.132\arm_tools\bin` |
| RSL10 CMSIS-Pack 3.7.606 | `%LOCALAPPDATA%\Arm\Packs\ONSemiconductor\RSL10\3.7.606` |
| J-Link 7.20b | `C:\Program Files (x86)\SEGGER\JLink\JLink.exe` |

### 编译

```bash
export PATH="/c/Program Files (x86)/onsemi/IDE_V4.3.1.132/arm_tools/bin:$PATH"
cd ble_android_asha/Debug
make clean
make all
```

编译宏 (`-D`) 与管理文件由 IDE 的 `.cproject` 生成，保存在 `Debug/subdir.mk` 中：

```
RSL10_CID=101  RTE_BLE_L2CC_ENABLE=1  EZAIRO_71XX_DIO_CFG=7100
L2C_CONNECTION_MAX=1  ASHA_CAPABILITIES_SIDE=ASHA_CAPABILITIES_SIDE_LEFT
SECURE_CONNECTION  APP_BONDLIST_SIZE=28  CFG_BOND_LIST_IN_NVR2=true
CFG_BLE=1  CFG_ALLROLES=1  CFG_APP  CFG_APP_BATT  CFG_ATTS=1
CFG_CON=8  CFG_EMB=1  CFG_HOST=1  CFG_RF_ATLAS=1  CFG_ALLPRF=1
CFG_PRF=1  CFG_NB_PRF=8  CFG_CHNL_ASSESS=1  CFG_SEC_CON=1
CFG_EXT_DB  CFG_PRF_BASS=1  CFG_PRF_DISS=1  _RTE_
```

链接的库（`Debug/objects.mk`）：

| 库 | 用途 |
|----|------|
| `libblelib.a` | BLE 协议栈核心 |
| `libkelib.a` | 内核调度器 |
| `libbass.a` | 电池服务 Profile |
| `libdiss.a` | 设备信息服务 Profile |

产物：

| 文件 | 说明 |
|------|------|
| `Debug/ble_android_asha.elf` | 可执行文件 |
| `Debug/ble_android_asha.hex` | HEX 固件 |
| `Debug/ble_android_asha.map` | 内存映射 |

### 下载（J-Link 命令行）

1. 创建 J-Link 命令脚本 `flash.jlink`：

```
erase
loadfile "C:/Users/admin/onsemi-workspace4.5/ble_android_asha/Debug/ble_android_asha.hex"
r
g
exit
```

2. 执行下载：

```bash
"/c/Program Files (x86)/SEGGER/JLink/JLink.exe" \
  -device RSL10 \
  -if SWD \
  -speed 4000 \
  -autoconnect 1 \
  -CommanderScript flash.jlink
```

3. 删除脚本：`rm flash.jlink`

预期输出（关键行）：

```
Device "RSL10" selected.
Found Cortex-M3 r2p1, Little endian.
Erasing done.
Downloading file [...]... O.K.
ResetTarget() end
```

### 一次搞定（编译 + 下载）

```bash
export PATH="/c/Program Files (x86)/onsemi/IDE_V4.3.1.132/arm_tools/bin:$PATH"
cd ble_android_asha/Debug && make clean && make all && \
cat > flash.jlink <<'EOF'
erase
loadfile "C:/Users/admin/onsemi-workspace4.5/ble_android_asha/Debug/ble_android_asha.hex"
r
g
exit
EOF
"/c/Program Files (x86)/SEGGER/JLink/JLink.exe" \
  -device RSL10 -if SWD -speed 4000 -autoconnect 1 \
  -CommanderScript flash.jlink && \
rm flash.jlink
```

### Debug UART 打印

ble_android_asha 自带 UART printf（`app_trace.h`），开箱即用：

| 配置 | 值 |
|------|-----|
| UART TX | DIO5 |
| UART RX | DIO4 |
| 波特率 | 115200 |
| DMA 通道 | 7 |
| 宏 | `PRINTF(...)`，已启用 (`RSL10_DEBUG=DBG_UART`) |

启动时会打印版本号和时间戳。连接串口终端（115200 8N1）即可看到输出。

---

## peripheral_server_sleep — CLI 编译 + 下载 + printf

Demo 工程与 asha 一样是 Eclipse CDT 项目，使用 IDE 生成的 `Debug/` Makefile。

### 前置条件

先在 IDE 中添加 **RTE Utility 组件**（提供 `printf.c/h`），然后 **Build 一次** 生成 `Debug/` 目录及其子 makefile。

### 编译下载（一行脚本）

```bash
export PATH="/c/Program Files (x86)/onsemi/IDE_V4.3.1.132/arm_tools/bin:$PATH"
cd peripheral_server_sleep/Debug && make clean && make all && \
cat > flash.jlink <<'EOF'
erase
loadfile "C:/Users/admin/onsemi-workspace4.5/peripheral_server_sleep/Debug/peripheral_server_sleep.hex"
r
g
exit
EOF
"/c/Program Files (x86)/SEGGER/JLink/JLink.exe" \
  -device RSL10 -if SWD -speed 4000 -autoconnect 1 \
  -CommanderScript flash.jlink && \
rm flash.jlink
```

### Debug UART printf

通过 `#define DEBUG_UART_ENABLE`（[app.h](peripheral_server_sleep/include/app.h)）控制：

| 配置 | 值 |
|------|-----|
| UART TX | DIO5 |
| UART RX | DIO4 |
| 波特率 | 115200 |
| 宏 | `PRINTF(...)`，通过 SDK `printf.h` 提供 |

**启用时**：
- 跳过 `Load_Trim_Values_And_Calibrate_MANU_CALIB()`（NVR4 读取会破坏 DMA 状态，导致 printf 无输出）
- `App_Initialize()` 末尾调用 `printf_init()` + `PRINTF("DEVICE INITIALIZED")`
- 不关闭 DIO4/5（`app.c` 和 `app_process.c` 的 `Continue_Application`）

**关闭时**（注释掉宏）：
- 恢复原始低功耗行为：校准、DIO4/5 关闭、零代码膨胀

### UART 打印根因

`Load_Trim_Values_And_Calibrate_MANU_CALIB()` 内部通过 `Sys_GetTrim()` 读取 NVR4 Flash，会污染 DMA 控制器状态。后续 `printf_init()` 中 `Sys_DMA_ChannelConfig(DMA7, ...)` 虽然重新配置了通道，但 DMA 控制器全局状态已被破坏，UART TX DMA 无法触发。

**验证过程**：从可工作的 `App_RM_BLE_Initialize` 出发，逐个加回 demo 的函数调用，最终定位到校准函数是唯一导致 printf 失效的调用。其他函数（`BLE_LLD_Sleep_Params_Set`、`Sys_RFFE_SetTXPower`、`Sys_Clocks_OscRCCalibratedConfig`、`Sleep_Mode_Configure`、`BLE_Is_Awake_Flag_Set`）均无影响。

### 主循环打印与休眠互斥

启用 `DEBUG_UART_ENABLE` 时，demo 的主循环 `Main_Loop()` 会跳过以下三个低功耗行为：

| 代码位置 | 原行为 | `DEBUG_UART_ENABLE` 时 | 原因 |
|----------|--------|------------------------|------|
| `BLE_Power_Mode_Enter()` | 进入深度睡眠 | `#ifndef` 跳过 | 深度睡眠断开 SWD，无法再次烧录 |
| `SYS_WAIT_FOR_INTERRUPT` | WFI 暂停 CPU | `#ifndef` 跳过 | WFI 后只有 BLE 事件才唤醒，10.24s 广播下迭代太慢（100 次要 17 分钟才打一条 tick） |
| DIO4/DIO5 禁用 | 关闭空闲 IO 省电 | `#ifndef` 跳过 | DIO5 用于 UART TX，关闭后 printf 无输出 |

**打印与广播间隔的关系**：

循环末尾 `SYS_WAIT_FOR_INTERRUPT` 使 CPU 等待 BLE 基带定时器。广播模式每 10.24s 醒一次，连接模式每 500ms 醒一次。循环计数器 `tick_cnt` 每醒一次加 1：

```
tick 间隔 = cnt 阈值 × 唤醒间隔
广播: 100 × 10.24s ≈ 17分钟
连接: 100 × 500ms  = 50秒
```

**调试时的行为**（`DEBUG_UART_ENABLE` 打开）：
- 休眠关闭、WFI 跳过 → CPU 全速运行，tick 每 ~N 毫秒（N = cnt / CPU 频率）
- BLE 连接正常（事件在中断中入队，全速调度器立即处理）
- SWD 始终可用，无需断电重插
- **功耗较高**（全速运行），调试完需关闭宏

---

## 已知问题

1. **printf 与校准冲突** — `Load_Trim_Values_And_Calibrate_MANU_CALIB` 的 NVR4 读取会破坏 DMA 状态。启用 `DEBUG_UART_ENABLE` 时自动跳过校准。
2. **HTTPS 推送不可用** — GitHub 推送请用 SSH：`git@github.com:Chenbin9970/onsemi-workspace.git`
3. **IDE 命令行编译要求 IDE 未运行** — 否则报 `Workspace already in use!`。
4. **`.cproject` 会被 IDE 覆盖** — 不要在 IDE 打开时手动编辑 `.cproject`。
5. **J-Link 偶发首次编程失败** — 重试一次即可，属于 J-Link OB 的已知问题。
