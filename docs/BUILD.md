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

## 已知问题

1. **IDE 和 Makefile 文件来源不同** — `syslib/` 和 RM 源文件在 `.cproject` 中被排除，IDE 使用 RTE 版本。修改这些文件时需注意版本一致性。
2. **`APP_RM_ENABLE` 在 `app.h` 中硬编码** — 如需禁用远程麦功能，注释掉 `include/app.h:29` 的 `#define APP_RM_ENABLE`。
3. **IDE 命令行编译要求 IDE 未运行** — 否则报 `Workspace already in use!`。
4. **`syslib/` 文件从 CMSIS-Pack 复制** — 更新 SDK 版本时需同步，命令：
   ```bash
   SDK=/c/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606
   for f in rsl10_sys_clocks.c rsl10_sys_power.c rsl10_sys_power_modes.c \
            rsl10_sys_rffe.c rsl10_sys_dma.c rsl10_sys_flash.c rsl10_sys_timers.c
   do cp "$SDK/source/firmware/syslib/code/$f" syslib/; done
   ```
5. **HTTPS 推送不可用** — GitHub 推送请用 SSH：`git@github.com:Chenbin9970/onsemi-workspace.git`
6. **`.cproject` 会被 IDE 覆盖** — 不要在 IDE 打开时手动编辑 `.cproject`，如需修改排除列表，先关 IDE。
