# 打印 / 低功耗 / RM 配置指南

## 宏开关

所有行为由两个宏控制，统一在 `include/app.h`：

| 宏 | 作用 |
|----|------|
| `DEBUG_UART_ENABLE` | 启用 UART printf 打印 |
| `APP_RM_ENABLE` | 启用远程麦功能 |

## 配置组合

### 调试开发（当前）
```c
// #define DEBUG_UART_ENABLE    ← 注释掉
#define APP_RM_ENABLE
```
- **RM 活跃时**：WFE 等待事件，CPU 不深度睡眠
- **RM 关闭时**：深度睡眠 + WFI，最低功耗
- **无打印输出**（UART 关闭省电）

### 调试打印
```c
#define DEBUG_UART_ENABLE
#define APP_RM_ENABLE
```
- **RM 活跃时**：WFE，打印正常
- **RM 关闭时**：CPU 空转（无等待），打印正常
- **功耗高**（CPU 不休眠），仅调试用

### 纯低功耗（无 RM）
```c
// #define DEBUG_UART_ENABLE
// #define APP_RM_ENABLE
```
- 原始低功耗模式：深度睡眠 + WFI

## RM 生命周期

```
上电 → App_RM_BLE_Initialize
         ├─ printf_init + 打印
         ├─ APP_RM_Init(0)     ← 初始化参数 + RM_Configure（配 RF）
         ├─ RF_SwitchToCPMode
         └─ RM_Enable(500)     ← 启动搜索
              │
         ┌────┴────┐
         │ 主循环   │
         │ WFE等待  │ ← RM 活跃时不睡眠
         └─────────┘
              │
         BLE 写 ONOFF=0 → RM_Disable → 可进入睡眠
         BLE 写 ONOFF=1 → RM_Enable  → 恢复搜索
```

## 时钟策略

- **始终 16MHz**（`RFCLK_FREQ=16000000` + 硬编码分频）
- **不动态切换**（切换时钟会导致 UART 波特率变化和外设不稳定）
- 16MHz 是 RM 2Mbps 协议稳定运行的最低要求

## 打印规则

- `printf.h` 在需要打印的 `.c` 文件中单独 `#include <printf.h>`，不在 `app.h` 统一引入
- `printf_init()` 在 `App_RM_BLE_Initialize` 中调用一次
- 切时钟后如需打印，需再次 `printf_init()` 重配 UART

## 已知约束

1. **RM_Configure 必须在 BLE 广告之前调用**（否则 RF 冲突导致崩溃）
2. **16MHz 是必须的**（8MHz 导致 bad CRC/频繁断连）
3. **深度睡眠时 SWD 不可用**（需 DIO12 接 GND + RESET 恢复烧录）
4. **调试打印和低功耗互斥**（开打印 = CPU 不休眠 = 功耗高）
