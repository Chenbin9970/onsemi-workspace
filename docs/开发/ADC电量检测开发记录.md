# ADC 电池电量检测开发记录

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
| DIO 模式 | `DIO_MODE_DISABLE \| DIO_NO_PULL` |

初始化位置（2 处）：

| 位置 | 触发时机 |
|------|---------|
| `code/app_init.c` → `App_Initialize()` 末尾 | 冷启动 |
| `code/app_process.c` → `Continue_Application()` | 每次深度睡眠唤醒 |

## 三、校准常数（`app.h`）

以 prescale 1280H 实测校准：

```c
#define BAT_ADC_DIO      3
#define BAT_ADC_CHANNEL  0
#define BAT_ADC_MIN      6950   /* raw=6950 → 电池 3.0V (  1%) */
#define BAT_ADC_MAX      9374   /* raw=9374 → 电池 4.4V (100%) */
#define BAT_LVL_MAX      100
```

> 校准数据来源于硬件实测：4.4V 时 raw=9374，3.0V 时 raw=6950。

## 四、查询逻辑（`code/ble_rempro_cmd.c`）

仅在收到 **REMPRO CMD=4（GetBatteryInfo）** 时读取 ADC：

1. 读 `ADC->DATA_TRIM_CH[0]` 获取 raw 值
2. 边界保护：
   - `raw ≤ BAT_ADC_MIN` → 返回 **1%**（不返回 0%，避免误判）
   - `raw ≥ BAT_ADC_MAX` → 返回 **100%**
3. 线性换算：`pct = (raw - BAT_ADC_MIN) * 100 / (BAT_ADC_MAX - BAT_ADC_MIN)`
4. 通过 HDLC 响应返回 `[Left_Battery, Right_Battery=0]`

**不取样、不平均、不发 BLE 通知**。仅在 APP 主动查询时读取一次。

## 五、踩坑记录

### 5.1 uint32_t 下溢（2026-07-22）

`raw - BAT_ADC_MIN` 在 `raw < BAT_ADC_MIN` 时用 `uint32_t` 做减法会下溢为巨大值，导致 `pct` 远超 100% 后被 clamp 到 100%。

**现象**：电池明明 3.0V 以下，却显示 100%。

**修复**：加 `if (raw <= BAT_ADC_MIN) { pct = 1; }` 边界保护。

### 5.2 DIO2 vs DIO3 调试

曾切换到 DIO2 调试，最终回到 DIO3。结论：两者电路相同（1M+360k 分压），只需改 `Sys_ADC_InputSelectConfig` 的 `ADC_POS_INPUT_DIOx` 宏即可切换。

### 5.3 Prescale 选择

- 最初用 prescale 200（快速）/ 6400（低功耗），不同初始化路径混用
- 统一为 **1280H**，所有路径一致，避免不同路径 ADC 读数差异

### 5.4 DIO 模式

`DIO_MODE_DISABLE` vs `DIO_MODE_GPIO_IN_0`：
- ADC 走内部模拟通路，两种都能工作
- 当前使用 `DIO_MODE_DISABLE`，已验证正常

## 六、文件变更清单

| 文件 | 改动 |
|------|------|
| `include/app.h` | 定义 `BAT_ADC_DIO`、`BAT_ADC_CHANNEL`、`BAT_ADC_MIN`、`BAT_ADC_MAX` |
| `code/app_init.c` | `App_Initialize()` 末尾初始化 ADC |
| `code/app_process.c` | `Continue_Application()` 处初始化 ADC |
| `code/ble_rempro_cmd.c` | `cmd_getbatteryinfo()` 读 ADC 换算百分比 |
| `code/app_process.c` | 删除 `Measure_Battery_Level()`（原 16 次平均逻辑） |
| `app.c` | 删除 Main_Loop 中的 `Measure_Battery_Level()` 调用和 BASS 通知 |
