# RSL10 Peripheral Server Sleep — 硬件参数配置手册

## 一、射频发射功率 (`calibration.h`)

**`RF_TX_POWER_LEVEL_DBM`** — RF 输出功率

| 选项 | VCC 目标（MANU_CALIB） | 适用场景 |
|------|----------------------|----------|
| `0` | 1.10V | 低功耗、近距离 |
| `3` | 1.20V | 中等距离 |
| `6` | 1.20V + PA | 远距离（需启用 POWER_AMPLIFIER） |

当前值：**0 dBm**

---

## 二、校准策略 (`calibration.h`)

**`CALIB_RECORD`** — 电压校准方式

| 选项 | 说明 | 适用场景 |
|------|------|----------|
| `MANU_CALIB` | 从 NVR4 读取出厂校准值 | 量产芯片，无需自己校准 |
| `SUPPLEMENTAL_CALIB` | 从 NVR3 读取补充校准值 | 已用 supplemental_calibrate 工具校准过 |
| `USER_CALIB` | 运行时自动计算并校准 | 开发板、未校准芯片 |

当前值：**MANU_CALIB**

---

## 三、系统时钟 (`app.h`)

### 3.1 RF 主时钟

**`RFCLK_FREQ`** — 48M 晶振分频后的系统时钟

| 选项 | 典型用途 |
|------|---------|
| `8000000` (8 MHz) | 最低功耗 |
| `12000000` (12 MHz) | 平衡 |
| `16000000` (16 MHz) | 平衡 |
| `24000000` (24 MHz) | 较高性能 |
| `48000000` (48 MHz) | 最高性能 |

当前值：**8 MHz**

### 3.2 低功耗时钟源

**`RTC_CLK_SRC`** — Sleep 期间的 RTC 时钟

| 选项 | 精度 | 功耗 | 说明 |
|------|------|------|------|
| `RTC_CLK_SRC_XTAL32K` | 高 (ppm) | 较高 | 需外部 32K 晶振 |
| `RTC_CLK_SRC_RC_OSC` | 低 (±2%) | 最低 | 内部 RC 振荡器，会动态校准 |
| `RTC_CLK_SRC_DIO[0-3]` | 取决于外部源 | 最低 | 外部时钟输入 |

当前值：**RC_OSC**

### 3.3 XTAL32K 微调

| 参数 | 当前值 | 说明 |
|------|--------|------|
| `XTAL32K_ITRIM_VALUE` | `0xF` | 电流 trim（0x0–0xF） |
| `XTAL32K_CLOAD_TRIM_VALUE` | `0x38` | 负载电容 trim |

---

## 四、供电配置 (`app.h` + `calibration.h`)

### 4.1 Buck / LDO

**`VCC_BUCK_LDO_CTRL`**

| 选项 | 说明 |
|------|------|
| `VCC_BUCK_BITBAND` | DC-DC Buck（高效率） |
| `VCC_LDO_BITBAND` | LDO（低噪声） |

当前值：**VCC_LDO**

### 4.2 电压目标（MANU_CALIB 模式）

| 参数 | 当前值 | 说明 |
|------|--------|------|
| `VCC_TARGET` | 1.10V (0dBm) / 1.20V (3/6dBm) | 由 TX power 决定 |
| `VDDC_TARGET` | 0.92V | 数字核电压 |
| `VDDM_TARGET` | 1.05V | 存储器电压 |

### 4.3 Power Amplifier

| 参数 | 说明 |
|------|------|
| `POWER_AMPLIFIER_ON` | 仅 6dBm 时定义，启用 PA |

---

## 五、Sleep 模式配置 (`app_process.c: Sleep_Mode_Configure()`)

### 5.1 唤醒源 (`wakeup_cfg`)

| 唤醒源 | 当前状态 |
|--------|---------|
| BB_TIMER（BLE 基带定时器） | 自动启用（BLE Stack 设置） |
| RTC_ALARM | 自动启用 |
| WAKEUP_PAD (RESET 按键) | 启用，上升沿 |
| DIO0 | **禁用** |
| DIO1 | **禁用** |
| DIO2 | **禁用** |
| DIO3 | **禁用** |

若要启用 DIO 唤醒：`WAKEUP_DIO*_ENABLE | WAKEUP_DIO*_[RISING|FALLING]`

### 5.2 唤醒延迟

**`WAKEUP_DELAY`** — 唤醒后等待时钟稳定的时间

| 当前值 | 说明 |
|--------|------|
| `WAKEUP_DELAY_32` | ~1ms (32 × 31.25μs) |

可选: 1, 2, 4, 8, 16, 32, 64, 128

### 5.3 Memory Retention

当前保留: DRAM0, DRAM1, DRAM2（32KB retention）

---

## 六、Retention 调节器 Trim (`RTE_Device.h`)

| 参数 | 当前值 | 可选值 |
|------|--------|--------|
| `RTE_VDDTRET_TRIM_VALUE` | `0x3` | 0x1 (Consumer) / 0x3 (Automotive/RC_OSC) |
| `RTE_VDDMRET_TRIM_VALUE` | `0x1` | 0x1 (Consumer) / 0x3 (Automotive) |
| `RTE_VDDCRET_TRIM_VALUE` | `0x1` | 0x1 (Consumer) / 0x3 (Automotive) |

> **注意**: 若 RTC_CLK_SRC 使用 RC_OSC，VDDT retention 必须设为 0x3。

---

## 七、GPIO 功能定义 (`app.h`)

| 参数 | 当前值 | 说明 |
|------|--------|------|
| `LED_DIO` | DIO 6 | LED 指示灯（Sleep 唤醒闪烁） |
| `RECOVERY_DIO` | DIO 12 | 低电平时暂停启动，便于重新烧录；同时用作按键 GPIO 输入 |
| `BUTTON_DIO` | DIO 12 | 按键输入（与 RECOVERY_DIO 共用）；**非休眠唤醒源** |

---

## 八、BLE 广播 & 连接 (`ble_std.h` / `ble_std.c`)

| 参数 | 当前值 | 说明 |
|------|--------|------|
| 广播间隔 | **10.24s** (BLE 规范上限) | `CFG_ADV_INTERVAL_MS = 10240` (16384 × 0.625ms) |
| 连接间隔 | **500ms** | `PREF_SLV_MIN/MAX_CON_INTERVAL = 400` (×1.25ms) |
| 连接延迟 | **19** | 最多跳过 19 个连接事件 |
| 监督超时 | **32s** | `PREF_SLV_SUP_TIMEOUT = 3200` (×10ms)，BLE 规范上限 |
| 有效通信间隔 | **10s** | 500ms × (1 + 19) = 10s |
| 参数更新请求 | **拒绝** | `GAPC_ParamUpdateReqInd` 中 `cfm->accept = 0`，阻止主机修改连接参数 |
| 设备地址 | NVR3 public addr / Private fallback | 先尝试读取 NVR3 |
| 设备名称 | 空字符串 | 省电 |
