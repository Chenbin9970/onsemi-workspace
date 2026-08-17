# ADC 电池电量检测开发记录

> **2026-08-17 更新**：调试期间采样引脚试过 DIO2，**正式定为 DIO3**；每次读取前重新配置 ADC；新增低电量检测（Main_Loop 定时 + 提示音）。

## 一、硬件电路

电池（4.4V 满电）→ 1MΩ → **DIO3** → 360kΩ → GND

DIO3 电压范围：

| 电池电压 | DIO3 电压 |
|---------|-----------|
| 3.0V（低电） | 0.794V |
| 4.4V（满电） | 1.165V |

## 二、ADC 配置

| 项目 | 值 |
|------|-----|
| 输入引脚 | DIO3 |
| ADC 通道 | Channel 0 |
| 正输入 | `ADC_POS_INPUT_DIO3` |
| 负输入 | `ADC_NEG_INPUT_GND` |
| 预分频 | `ADC_PRESCALE_1280H` |
| DIO 模式 | `DIO_MODE_DISABLE \| DIO_NO_PULL`（`App_sleep_Initialize`/`App_RM_BLE_Initialize` 用 `DIO_MODE_GPIO_IN_0`） |

初始化位置（4 处，均配置到 DIO3）：

| 位置 | 触发时机 |
|------|---------|
| `code/app_init.c` → `App_sleep_Initialize()` | 睡眠初始化 |
| `code/app_init.c` → `App_RM_BLE_Initialize()` | RM/BLE 初始化 |
| `code/app_init.c` → `App_Initialize()` 末尾 | 冷启动 |
| `code/app_process.c` → `Continue_Application()` | 每次深度睡眠唤醒 |

### 2.1 每次读取前必须重新配置

实测发现：**每次读取 ADC 采样值前都要重新执行**以下两行，否则 `DATA_TRIM_CH` 读到的是旧值：

```c
Sys_ADC_Set_Config(ADC_NORMAL | ADC_PRESCALE_1280H);
Sys_ADC_InputSelectConfig(0, ADC_POS_INPUT_DIO3 | ADC_NEG_INPUT_GND);
```

统一封装在 `read_battery_raw()`（`code/ble_rempro_cmd.c`），所有读取路径（GetBatteryInfo、低电量检测）都走它，避免采样逻辑重复。

## 三、校准常数（`app.h`）

以 prescale 1280H 实测校准：

```c
#define BAT_ADC_DIO      3
#define BAT_ADC_CHANNEL  0
#define BAT_ADC_MIN      6950   /* raw=6950 → 电池 3.0V (  1%) */
#define BAT_ADC_MAX      9374   /* raw=9374 → 电池 4.4V (100%) */
#define BAT_LVL_MAX      100
```

> 校准数据来源于硬件实测：4.4V 时 raw=9374，3.0V 时 raw=6950。DIO2/DIO3 电路相同（1M+360k 分压），校准常数不随引脚切换变化。

## 四、查询逻辑（`code/ble_rempro_cmd.c`）

**REMPRO CMD=4（GetBatteryInfo）**：

1. 调 `read_battery_raw()`：重新配置 ADC → 读 `ADC->DATA_TRIM_CH[0]` 获取 raw 值
2. 边界保护：
   - `raw ≤ BAT_ADC_MIN` → 返回 **1%**（不返回 0%，避免误判）
   - `raw ≥ BAT_ADC_MAX` → 返回 **100%**
3. 线性换算：`pct = (raw - BAT_ADC_MIN) * 100 / (BAT_ADC_MAX - BAT_ADC_MIN)`
4. 通过 HDLC 响应返回 `[Left_Battery, Right_Battery=0]`

**不取样、不平均**。仅在 APP 主动查询时读取一次。

## 五、低电量检测（2026-08-17 新增）

在 `Main_Loop` 里按状态累积运行时间，到 `LOW_BATT_CHECK_MS` 间隔检查一次电池电压，低于 `BAT_ADC_MIN` 就播低电提示音。

```c
#define LOW_BATT_CHECK_MS  60000   /* 测试值 1 分钟，产品应为 600000=10min */
```

累积规则：
- **RM 模式**：每 200ms timer tick +200ms
- **BLE 模式**：每次唤醒 +当前唤醒间隔（广播 100ms / 连接 `(latency+1)×conn_interval×1.25ms`）

```c
if (low_batt_elapsed_ms >= LOW_BATT_CHECK_MS) {
    low_batt_elapsed_ms = 0;
    if (read_battery_raw() <= BAT_ADC_MIN) {
        bs300_play_low_batt_tone();
    }
}
```

配套改动：
- **200ms timer re-arm**：`app.c` 改为 RM 活跃时常开（`audio_streaming && !rm_stop_requested`），保证 RM 模式下低电量检测能持续跑（配合 `RM_TIMEOUT_TICKS=0`）。
- **`bs300_play_low_batt_tone()`**（`code/bs300_ram_sync.c`）：播最大音量提示音 `0xFD12F2`，BS300 I2C 忙时跳过。

## 六、踩坑记录

### 6.1 uint32_t 下溢（2026-07-22）

`raw - BAT_ADC_MIN` 在 `raw < BAT_ADC_MIN` 时用 `uint32_t` 做减法会下溢为巨大值，导致 `pct` 远超 100% 后被 clamp 到 100%。

**现象**：电池明明 3.0V 以下，却显示 100%。

**修复**：加 `if (raw <= BAT_ADC_MIN) { pct = 1; }` 边界保护。

### 6.2 DIO2 vs DIO3 调试

- **2026-08-17 调试期间用 DIO2 测试，正式版本改回 DIO3**（`BAT_ADC_DIO=3`）。
- 两者电路相同（1M+360k 分压），只需改 `Sys_ADC_InputSelectConfig` 的 `ADC_POS_INPUT_DIOx` 宏即可切换。
- 调试用 DIO2 时把 DIO2 从唤醒源移除；改回 DIO3 后已恢复 `WAKEUP_DIO2_ENABLE | WAKEUP_DIO2_FALLING`。

### 6.3 Prescale 选择

- 最初用 prescale 200（快速）/ 6400（低功耗），不同初始化路径混用
- 统一为 **1280H**，所有路径一致，避免不同路径 ADC 读数差异

### 6.4 DIO 模式

`DIO_MODE_DISABLE` vs `DIO_MODE_GPIO_IN_0`：
- ADC 走内部模拟通路，两种都能工作
- 当前 `App_sleep_Initialize`/`App_RM_BLE_Initialize` 用 `DIO_MODE_GPIO_IN_0`，`App_Initialize`/`Continue_Application` 用 `DIO_MODE_DISABLE`，均验证正常

### 6.5 读取前不重新配置读到旧值（2026-08-17）

`DATA_TRIM_CH` 只在配置后刷新，长时间不读会保留旧值。**每次读取前必须重跑 §2.1 的两行**，否则电量显示不准。

## 七、文件变更清单

| 文件 | 改动 |
|------|------|
| `include/app.h` | `BAT_ADC_DIO=3`；新增 `LOW_BATT_CHECK_MS` |
| `code/app_init.c` | 3 处 ADC 输入选择 DIO3；`Sys_DIO_Config(3, ...)`（调试期试过 DIO2） |
| `code/app_process.c` | `Continue_Application` DIO3 配置；`Sleep_Mode_Configure` 保持 DIO2 唤醒（调试 DIO2 时曾临时关闭） |
| `code/ble_rempro_cmd.c` | 新增 `read_battery_raw()`（读取前重配 ADC）；`cmd_getbatteryinfo` 改用它 |
| `app.c` | Main_Loop 低电量检测块 + 200ms re-arm 改动 |
| `code/bs300_ram_sync.c` | 新增 `bs300_play_low_batt_tone()` |
