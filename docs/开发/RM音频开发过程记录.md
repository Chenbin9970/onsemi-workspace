# RM 音频 OD 输出开发过程记录

## 版本节点

| 提交 | 说明 |
|------|------|
| `5d83d9a` | **基线**：RM+BLE+低功耗，无音频 |
| `a2c8543` | **IDE 验证版**：用户调通的 RM 音频 OD 输出 |
| `e581abf` | **整理版**：清理所有 `#if 0` 死代码 |
| `b4bddf1` | 加 `RM_CP_MODE_INIT` 宏控制 CP/BLE 模式 |
| *(HEAD)* | **渐进式音频集成 checkpoint**：音频基础设施就位，BLE↔RM 切换正常，DSP 固件加载待启用 |

## 文件变更（5d83d9a → a2c8543）

### 新建
| 文件 | 用途 |
|------|------|
| `app_func.c` | 音频管线 ISR（DSP 解码/ASRC/OD），含两个版本（`#if 0` / `#else`） |
| `dsp_pm_dm.c/h` | G.722 LPDSP32 固件数据 |
| `queue.c/h` | 缓冲管理 |

### 修改
| 文件 | 改动要点 |
|------|----------|
| `app.h` | 加音频定义：OD/ASRC/DSP，`#if 0` SPI 版 / `#else` OD+DMIC 版 |
| `app_init.c` | `App_RM_BLE_Initialize` 独立音频初始化；`App_Initialize` 内 `#if 0` 包音频代码 |
| `rm_app.c` | TRX 调 `Rendering_func`；STATUS 回调控制音频 IRQ；带调试打印 |
| `app_process.c` | 外层 `#if 0` 包原版，`#else` 为简化版；Sleep_Mode_Configure 的 AudioSink 用 `#if 1` |

## 调试关键发现

### 1. `app_func.c` 两个版本的差异

| 差异 | `#if 0` 块（正确） | `#else` 块（有问题，当前在用） |
|------|---|---|
| `Ascc_period_isr` 分流 | `if(app_env.audio_streaming)` | **`if(1)`** → 功耗根因 |
| PERIOD 计数器 | 不在音频路径重启 | **每次都重启** |
| `Ascc_phase_isr` | `&& audio_sink_period_cnt != 0` 保护 | **无保护** |
| `Asrc_reconfig` | `if (Ck != 0)` 除零保护 | **无保护** |
| 额外代码 | 无 | `sample_in[160]`、DMIC 缓冲、DMIC ISR |

### 2. 功耗根因

`Ascc_period_isr` 中 `if(1)` 导致始终走音频路径，每次 ISR 都 `PERIOD_CNT_START` 重启 AUDIOSINK 计数器，RC OSC 测量路径永不执行，`NVIC_DisableIRQ` 永不被调用，硬件持续运行无法进入深度睡眠。

### 3. 崩溃根因

`Asrc_reconfig` 中 `Ck = audio_sink_cnt` 可能为 0，无保护直接除导致 HardFault。

### 4. Sleep_Mode_Configure 顺序

必须在音频 AudioSink 初始化之前调用，否则会覆盖音频的时钟源配置（SAMPL_CLK → STANDBYCLK）。

### 5. DMA 通道冲突

- `MEMCPY_DMA_NUM=0`（RM memcpy）与 sleep 的 BB 寄存器保存 DMA0 冲突
- `OD_DMA_NUM=1` 与 `Sys_PowerModes_Sleep_Init_2Mbps` 的 RF 备份 DMA1 冲突
  - `Sys_PowerModes_Sleep_Init_2Mbps` 内部用 `Sys_DMA_ChannelConfig(DMA1, ...)` 备份 RF 寄存器，覆盖 OD 音频输出 DMA 配置 → 无声
  - `Sys_PowerModes_Wakeup_2Mbps` 中 DMA1 硬编码，不可改 → 只能挪 `OD_DMA_NUM`
- `ASRC_IN_IDX=3` 与 RM memcpy DMA3 在编译时共存（运行时不同时使用）
- **最终方案**：`MEMCPY_DMA_NUM=2`，`OD_DMA_NUM=5`，避开 sleep 的 DMA0/1

### 6. `wakeup_cfg` 差异

| | 原版 (`#if 0`) | 简化版 (`#else`) |
|---|---|---|
| DIO 唤醒 | DIO3/2/1/0 全部 `_DISABLE` | `WAKEUP_DIO0_ENABLE` |
| 影响 | 不会误唤醒 | DIO0 是 OD 输出脚，OD 数据变化会触发唤醒 |

## 已确认工作的路径

1. **CP 模式**：`RF_SwitchToCPMode()` + `RM_Enable()`，音频硬件在 `App_Initialize` 中一次性初始化，全程不进入 sleep
2. **BLE 模式**：`RF_SwitchToBLEMode()`，RM 通过 BLE 命令启动，需配合 `audio_streaming` 开关控制 sleep
3. **OD 输出管线**：RM RX → `Rendering_func` → DSP G.722 → ASRC → `BufferOut` → OD DMA → 耳机
4. **低功耗 sleep**：仅在 BLE 模式 + `audio_streaming=0` + 正确 `wakeup_cfg` 时生效

## 未完成

- ~~BLE 模式 + RM 动态开/关音频硬件实现低功耗 + 音频共存~~ 已解决 (2026-07-07)
- ~~`app_func.c` 的 `#else` 块 bug 修复~~ 已解决 (2026-07-07)

---

## BLE 低功耗 → RM 音频切换（2026-07-07 完整调试记录）

> 背景：开机 BLE 低功耗模式，通过 BLE 指令切换到 RM，RM 连上后无声音。
> 直接 RM 模式（不进低功耗）音频正常。根因分三层。

### 层一：DMA 通道冲突（无声）

| DMA 通道 | 原占用 | Sleep 占用 | 冲突 |
|----------|--------|-----------|------|
| 0 | MEMCPY_DMA_NUM | BB 寄存器备份（SDK 硬编码） | 无声 |
| 1 | OD_DMA_NUM | RF 寄存器备份（SDK 硬编码） | 无声 |

**修复**：`MEMCPY_DMA_NUM=2`，`OD_DMA_NUM=5`

### 层二：DSP 内存 sleep 断电（崩溃）

`Sys_PowerModes_Sleep` 通过 `SYSCTRL->MEM_POWER_CFG` 关掉未保留内存。原始只保留了 `DRAM0/1/2` 和 `BB_DRAM0`，DSP 专用内存全部断电：

- `DSP_PRAM0`（固件代码）
- `DSP_DRAM0/1`（固件数据 bank 0-1）
- `DSP_DRAM4`（G722 配置消息区）
- `DSP_DRAM5`（解码器输入缓冲 `Cm2DspBuff0dec`）

导致 wakeup 后 CPU 写 `MEM_MESSAGE`（DSP_DRAM4）或 `memcpy` 到 DSP_DRAM5 触发 BusFault → HardFault → 重启。

**修复**：在 `Sleep_Mode_Configure` 的 `mem_power_cfg` 和 `mem_power_cfg_wakeup` 加入上述 DSP 内存电源位。

### 层三：DSP 核 sleep 后停止（最隐蔽）

DSP 内存保留了，但 DSP **核**在 sleep 时随系统时钟一起停止了。`Continue_Application` 恢复了系统时钟和 BB/RF，但**没有恢复 `SYSCTRL->DSS_CTRL`**。DSP 核停留在 stopped 状态，收到 `DSS_CMD_1` 命令无响应。

**诊断过程**（LED GPIO 逐级定位）：

| 现象 | 结论 |
|------|------|
| LED 灭 | `Rendering_func` 未被调用（RM 数据未到） |
| LED 常亮 | `Rendering_func` 调用，`DspDec_isr` 未触发 → **DSP 核无响应** |
| LED 185µs 高→低 | DSP 响应正常 |

**修复**：在 `Continue_Application` 末尾加入：
```c
SYSCTRL->DSS_CTRL = DSS_LPDSP32_RESUME;
```
每次 sleep wakeup 后恢复 DSP 核运行。

### `app_func.c` `#else` 块 bug 修复（同步完成）

| 函数 | Bug | 修复 |
|------|-----|------|
| `Asrc_reconfig` | `Ck=0` 时直接除 → HardFault | 加 `if (Ck != 0)` |
| `Ascc_phase_isr` | `audio_sink_period_cnt=0` 时直接除 → HardFault | 加 `&& audio_sink_period_cnt != 0` |
| `Ascc_period_isr` | `if(1)` 始终走音频路径 → 功耗 | 改为 `if(app_env.audio_streaming)` |

### 新增 `Audio_Init()` 函数

`app_init.c` 新增 `Audio_Init()`，在 RM 启动前重配音频管线外围（不动 DSP 固件）：
- AudioSink 时钟源 + 计数器复位
- ASCC 中断使能
- ASRC IN/OUT DMA + OD DMA 重配（disable → config → enable）
- AUDIOCLK、OD 参数、TX Power
- BB_WAKEUP
- NVIC 优先级/使能

`App_Initialize` 和 `rm_start_requested` 均调用此函数。

### 修复涉及文件

| 文件 | 改动 |
|------|------|
| `app.h` | DMA 通道调整、`Audio_Init()` 声明 |
| `app_process.c` | DSP 内存保留、`Continue_Application` DSP resume |
| `app_func.c` | 3 个除零/分流 bug 修复 |
| `app_init.c` | 新增 `Audio_Init()`、`App_Initialize` 调用它 |
| `app.c` | `rm_start_requested` 调用 `Audio_Init()` |

---

## 渐进式音频集成 checkpoint（2026-07-07）

> 基于 `5d83d9a` 基线，分 5 步加音频，每步验证 BLE↔RM 切换。

### 当前状态

| 组件 | 状态 | 说明 |
|------|------|------|
| `app.h` 音频定义 | 已启用 | DMA/OD/ASRC/DSP 宏、extern 声明 |
| `app_func.c` / `dsp_pm_dm.c` / `queue.c` | 已加入 | ISR 别名与管线函数 |
| `rm_app.c` TRX 回调 | 无音频 | 只做错误计数，不调 `Rendering_func` |
| `rm_app.c` LINK_ESTABLISHED | 无音频 | 只打印，不初始化音频 ISR |
| `rm_app.c` LINK_DISCONNECTED | 正常 | RM_Disable → Stop timer → BLE 切换 |
| `app_process.c` DSP 保留 | 已启用 | `DSP_PRAM0/DRAM0/1/4/5` 在 sleep 中保留 |
| `app_process.c` DSS resume | 已启用 | `Continue_Application` 每次 wakeup 恢复 DSP |
| `app_init.c` DSP 固件加载 | **已启用** | `Sys_Flash_Copy` 正常，功耗无影响 |
| `app_init.c` Audio_Init() | 已定义未调用 | 重配音频管线外围，DSP 固件靠 sleep 保留 |
| `app.c` rm_start_requested | 无音频 | 直接 `RF_SwitchToCPMode` + `RM_Enable`，不调 `Audio_Init` |
| `app.c` rm_stop_requested | 正常 | 切 BLE 前 `DSS_LPDSP32_PAUSE` 停 DSP（修复 `RF_SwitchToBLEMode` 死机） |
| **功耗** | BLE 622µA | DSP 运行无额外功耗 |

### BLE↔RM 切换验证结果

| 方向 | 结果 |
|------|------|
| BLE → RM（指令 0x01） | 正常 |
| RM → BLE（指令 0x00） | 正常 |

### 功耗基准

| 模式 | 平均电流 | 说明 |
|------|---------|------|
| BLE 低功耗 | **622 µA** | DSP 固件未加载 (#if 0)，无音频硬件 |

### 下一步

1. ~~定位 DSP 固件加载为何导致 `RF_SwitchToBLEMode` 死机~~ → **已定位**：DSP **运行**（非 `Sys_Flash_Copy`）时切 RF 会死机，需在 `rm_stop_requested` 中先 `DSS_LPDSP32_PAUSE`
2. 启用 DSP 固件加载 + `DSS_LPDSP32_RESUME`，`rm_stop_requested` 加 `DSS_PAUSE` 保护
3. 启用 `Audio_Init()` 调用 → 音频 + BLE↔RM 切换完整工作