# rm_rx（remote_mic_rx_coex）7100 I2C 通讯调试流程

> 复盘：把 7160test 里能正常读 7100 的 I2C 初始化搬到 remote_mic_rx_coex（rm rx），
> 期间"读全 0"问题的定位与解决过程。结论一句话：
> **rx_coex 的 App_Initialize 里那段 flash overlay（PRAM memcpy + FLASH_OVERLAY_CFG=0xf）
> + loop cache 会导致 7100 I2C 读全 0，删掉即正常。**

> **2026-09-08 关键进展**：
> 1. 找到**上电握手**：DIO13(7100→RSL10) 上电拉低；RSL10 等 DIO13 低后在 **DIO11** 发低脉冲应答，之后才能发 I2C（详见 `docs/7100协议.md` §2）。缺握手会导致 7100 长期忙(65)/不应答。
> 2. 去掉 flash overlay + 握手正确后，同步初始化到 **Packet 13..118** 复刻成功（A6/A8、读配置、A1/A2 参数，`ok=1`）。
> 3. **A7 两段读规则已定并跑通**：A7 写端字节 3/4 = 数据长度(len16)，应答 = 3B 头 `46 <len_lo> <len_hi>` + len16 数据。读取=先读 3B 头，头长度 == 写端长度且状态 46，再读该长度数据，最后 `04 82`。
> 4. **7 组 A7 可正确读取并推进循环**：`dsp_7100_a7_seq_tick()`（200ms tick）按 7 组（A7 01 00 06/02/03/26/0A/0C…）逐组两段读校验，通过才 `04 82` 进下一组，7 组循环。
> 5. 心跳 `{0x88,0x01}` 每 5s；I2C ISR 在读 ACK 前加了 ~3µs 人为延时，便于对齐参考时序。

## 1. 背景与目标

- 设备：RSL10 + Ezairo 7100（DSP），同一套硬件上 **7160test 读 7100 正常，rx_coex 读全 0**。
- 目标：让 rx_coex 开机对 7100 做与 7160test 相同的初始化发送（A6/A8 握手、读配置、A1 参数写），
  并能读到真实数据。
- 关键前提：两工程的 `i2c_7100_hal.c/h` 与 `dsp_7100_init.c/h` **逐字节一致**（差异仅注释），
  因此问题不在 I2C 驱动/协议代码本身。

## 2. 硬件与总线事实

| 项 | 值 |
|---|---|
| 从机 | Ezairo 7100 DSP |
| I2C | RSL10 硬件 I2C0 master，中断驱动 |
| SCL / SDA | DIO0 / DIO1（强上拉+6x+LPF） |
| 从机地址 | 7bit = 0x02（总线写地址字节 = 0x04，Sys_I2C_StartWrite 内部左移，传 0x02） |
| 速度 | prescale 12 → ~410kHz |

## 3. rx_coex 中 7100 I2C 相关代码构成

| 文件 | 作用 |
|---|---|
| `code/i2c_7100_hal.c/h` | 硬件 I2C0 中断驱动：`i2c_7100_write/read/delay_ms`、`I2C_7100_ADDR=0x02` |
| `code/dsp_7100_init.c/h` | 分阶段上电初始化发送（表驱动）：A6/A8 握手 → 04 82 读配置 → A1 参数写；`DSP7100_INIT_MAX_STAGE=3`；从 7160test 移植，加了 TX ok 打印 |
| 心跳 | `APP_7100_HB_Handler`（app_process.c）每 5s 写 `{0x88,0x01}`；BLE ready 后由 ble_std.c `ke_timer_set` 启动 |

触发时机（当前，同 7160test 放置方式）：
```
main():
  App_Initialize()          // 不再做 flash overlay（关键修复）
  PRINTF [RESET] wdt/sw/lockup/acs   // 复位原因诊断
  Sys_Delay_ProgramROM(3 * SystemCoreClock)   // 3s 上电稳定
  dsp_7100_boot_init()      // 阻塞跑 stage1..3，UART 打印 [7100] TX/RX ok
  DIO4/DIO5 disable（同 7160test）
  while(1){ Kernel_Schedule; RM_StatusHandler; ... }   // 心跳在 BLE ready 后 5s 起
```

## 4. 遇到的问题与根因

### 4.1 读全 0 —— 根因：flash overlay + loop cache（本次主案）
- 现象：`[7100] RX` 内容形如 `07 07 00 00 ...`，全 0；7160test 同一步读到真实数据。
- 根因：`App_Initialize` 里这段：
  ```c
  memcpy((uint8_t *)PRAM0..3_BASE, FLASH_MAIN_BASE..., ...);
  SYSCTRL->FLASH_OVERLAY_CFG = 0xf;      // 把代码区 overlay 到 PRAM
  SYSCTRL->CSS_LOOP_CACHE_CFG = CSS_LOOP_CACHE_ENABLE;
  ```
  overlay 打开后，CPU 从被覆盖的 flash 地址取指/取数据改从 PRAM 读；紧接着要执行的
  `dsp_7100_init`/I2C 路径若被错误内容覆盖，就会悄悄跑乱 → 7100 读全 0。
- 本工程非 FOTA，运行时不需要 overlay；7160test 从不做 overlay。
- **修复：删除该块。**（loop cache 仅是省电微优化，一并删除无影响）
- 定位方法：拿"能跑的 `App_testInitialize`"做基线（它没做 overlay），
  与原版 `App_Initialize` 逐特征用 `#if` 开关做 build-up/teardown 二分，最终锁定该块。
- 顺带说明：强制 LDO 一行（`ACS_VCC_CTRL->BUCK_ENABLE_ALIAS = VCC_LDO_BITBAND`，
  同 7160test）保留；若确认非必需可删。

### 4.2 看门狗复位（wdt=1）
- 现象：开机 `[RESET] wdt=1 sw=0 lockup=0 acs=0`，循环重启。
- 原因：开机长 `Sys_Delay_ProgramROM(8s)` 阻塞期间**从不喂狗** → WDT 复位。
- 修法：长延时用 1ms 步进 + `Sys_Watchdog_Refresh()` 喂狗。
  （当前 main 用 3s 阻塞 + 7160test 同款，3s < WDT 阈值，OK；如再加长必须配喂狗循环）

### 4.3 复位原因打印（诊断工具）
- main 开机打印并清除粘滞标志：`[RESET] wdt/sw/lockup/acs`。
  - wdt=1 → 长阻塞没喂狗；
  - acs=1 → 模拟/供电域复位（掉电/buck 不稳等），与代码逻辑关系小；
  - lockup=1 → CPU 锁死/硬错误。

### 4.4 写 vs 读 判定
- `dsp_7100_init` 的 TX 加了 `ok=`（`i2c_7100_write` 返回值），RX 自带 `ok=`：
  - `ok=0` → I2C 传输超时/失败（总线、地址、中断向量问题）；
  - `ok=1` 但内容全 0 → 7100 ACK 了、在回真 0（从机状态 / overlay 这类代码/执行问题）。

### 4.5 已顺带与 7160test 对齐的 IO（均非读 0 根因，但统一了行为）
- DIO8(DIO_SYNC_PULSE)/DIO15/DIO11 原被配 GPIO OUT **拉低** → 7160test 不驱动，删除；
- `NVIC_EnableIRQ(DIO0_IRQn)`（按键残留）→ 7160test 无，关闭；
- `RECOVERY_DIO` 13→7；
- PCM/SAMPL_CLK 对齐：`SAMPL_CLK = PCM_FRAME_SYNC = DIO3`（audiosink 参考脚）；
- DIO4/5 `DISABLE | DIO_NO_PULL`（main 里，同 7160test）；
- 电池 VBAT ADC 采样删除（`BAT_ADC_ENABLE` 本就不开）。

## 5. 当前工作流（时序）

1. 上电 → `App_Initialize()`（含强制 LDO；**无 flash overlay**）。
2. main 打印 `[RESET]` 复位原因 → 3s 延时 → `dsp_7100_boot_init()`：
   stage1 A6/A8 握手写 → stage2 04 82 读配置（读块） → stage3 A1 参数写；
   每步 `[7100] TX ok=... / RX (..B ok=..)` 打印，可与 7160test / star.csv 参考波形逐条对照。
3. `dsp_7100_boot_init()` 返回后 DIO4/5 disable，进主循环。
4. BLE ready（GAPM reset 完成）后：`APP_TEST_TIMER` + 心跳 200ms tick，
   心跳每 5s（25×200ms）向 7100 写 `{0x88,0x01}`。

## 6. 遗留 / 待清理
- （已完成）`App_testInitialize` 及其 `ADD_*` 二分脚手架死代码已删除。
- 强制 LDO 一行是否长期保留待确认（当前保留以与 7160test/实测可用配置一致）。
- 后续如需 7100 A7 参数读回（降噪/DFBC/WDRC gains），参考 7160test `dsp_7100_cmd`/`DSP7100_READBACK_ENABLE`。

## 7. 关键文件
- `remote_mic_rx_coex/code/i2c_7100_hal.c`、`include/i2c_7100_hal.h`
- `remote_mic_rx_coex/code/dsp_7100_init.c`、`include/dsp_7100_init.h`
- `remote_mic_rx_coex/code/app_init.c`（App_Initialize 修复点）
- `remote_mic_rx_coex/app.c`（触发时序）
- `remote_mic_rx_coex/code/app_process.c`、`code/ble_std.c`、`include/app.h`（心跳）
