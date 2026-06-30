# Debug UART printf — 功能说明

`#define DEBUG_UART_ENABLE`（[app.h](peripheral_server_sleep/include/app.h)）控制 demo 的调试打印和低功耗行为。

---

## 开关控制汇总

| 代码位置 | 原行为 | `DEBUG_UART_ENABLE` 时 | 原因 |
|----------|--------|------------------------|------|
| 校准 `Load_Trim_Values_And_Calibrate_MANU_CALIB` | 从 NVR4 加载电压 trim | `#ifndef` 跳过 | NVR4 读取破坏 DMA 状态，导致 printf 无输出 |
| `BLE_Power_Mode_Enter` | 进入深度睡眠 | `#ifndef` 跳过 | 深度睡眠断开 SWD，无法再次烧录 |
| `SYS_WAIT_FOR_INTERRUPT` | WFI 暂停 CPU | `#ifndef` 跳过 | WFI 后只有 BLE 事件才唤醒，10.24s 广播下计数器迭代太慢 |
| DIO4/DIO5 禁用 | 关闭空闲 IO 省电 | `#ifndef` 跳过 | DIO5 = UART TX，关闭后 printf 无输出 |

---

## 主循环打印与休眠互斥

主循环结构（[app.c](peripheral_server_sleep/app.c) `Main_Loop`）：

```c
while (true) {
    Kernel_Schedule();          // BLE 事件处理
    Sys_Watchdog_Refresh();     // 喂狗

    // 非阻塞 tick 计数器（调试打印）
    static uint32_t tick_cnt = 0;
    if (++tick_cnt >= 10000) {  // 阈值可调
        tick_cnt = 0;
        PRINTF("tick\r\n");
    }

    // BLE 连接处理 ...

#ifdef DEBUG_UART_ENABLE
    // 调试模式：跳过低功耗
#else
    BLE_Power_Mode_Enter();     // 深度睡眠（调试时跳过）
    SYS_WAIT_FOR_INTERRUPT;     // WFI（调试时跳过）
#endif
}
```

### 循环计时与 tick 阈值

有 `SYS_WAIT_FOR_INTERRUPT` 时，CPU 只在 BLE 基带定时器到期时醒来：

| 状态 | 唤醒间隔 | tick_cnt=100 时 tick 间隔 |
|------|----------|--------------------------|
| 广播 (10.24s) | 10.24s | 100 × 10.24s ≈ 17 分钟 |
| 广播 (1s) | 1s | 100 × 1s = 1 分 40 秒 |
| 已连接 (500ms) | 500ms | 100 × 500ms = 50 秒 |

- `tick` 间隔 = `cnt 阈值` × `唤醒间隔`
- 阈值太小 → tick 过于密集，UART DMA 阻塞循环
- 阈值太大 → tick 间隔过长，不实用

### 调试模式（关闭休眠 + 跳过 WFI）

- CPU 全速运行，tick 间隔 ≈ `cnt 阈值 / 循环速率`（几百 ms 到数秒）
- BLE 连接正常（事件在中断中入队，调度器立即处理）
- SWD 始终可用，无需断电重插即可反复烧录
- **功耗较高**（全速运行），调试完需注释 `DEBUG_UART_ENABLE` 恢复低功耗

---

## 调试流程

1. 确保 `#define DEBUG_UART_ENABLE`（[app.h](peripheral_server_sleep/include/app.h)）
2. IDE 中添加 RTE Utility 组件 → Build → 生成 `Debug/` 目录
3. CLI 编译下载（见 [BUILD.md](BUILD.md)）
4. 串口终端 115200 8N1 → 看到 `DEVICE INITIALIZED` + 周期性 `tick`
5. 调试完毕 → 注释 `DEBUG_UART_ENABLE` → 编译下载 → 恢复低功耗

---

## 相关文件

| 文件 | 作用 |
|------|------|
| [app.h](peripheral_server_sleep/include/app.h) | `DEBUG_UART_ENABLE` 宏定义 |
| [app.c](peripheral_server_sleep/app.c) | `Main_Loop` 中 tick 计数、休眠/WFI 开关 |
| [app_init.c](peripheral_server_sleep/code/app_init.c) | 校准跳过、`printf_init` 调用 |
| [app_process.c](peripheral_server_sleep/code/app_process.c) | `Continue_Application` 中 DIO4/5 禁用 |
| [BUILD.md](BUILD.md) | 编译下载流程 |
| [hw_config_reference.md](hw_config_reference.md) | 硬件参数配置参考 |
