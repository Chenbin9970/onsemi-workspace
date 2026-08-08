# peripheral_server_sleep 开发记录

---

## 一、RM 退 BLE 后 4s 出现 1mA 脉冲

> 2026-08-05 | 修复

### 现象

冷启进 RM → 30s 超时退回 BLE → 4s 后功耗跳到 1mA。脉冲持续 ~500ms，每 4s 重复一次。

### 根因

`DEBUG_UART_ENABLE` 关闭后设备可进入深度休眠，`Continue_Application` → `Enable_Audiosink_Measurement` 每 40 次 BLE 广播唤醒（4s）触发一次 RC 振荡器校准。校准期间 `low_power_enable = false`（关深眠）+ 开 AUDIOSINK 中断。

ASHA 移植（`7064db1`）把 `AUDIOSINK_PERIOD_IRQHandler` 从 `Ascc_period_isr` 换成了 ASHA 空壳 ISR——只读 `PERIOD_CNT`，不关中断、不恢复 `low_power_enable`。中断风暴导致永远无法深眠，1mA 常驻。

**为什么之前没发现**：`DEBUG_UART_ENABLE` 开着时 `BLE_Power_Mode_Enter` 被跳过，深眠从未进入，RC 校准路径从未触发。

### 修复

1. 注释 `APP_ASHA_ENABLE`（ASHA 暂停开发），恢复 `Ascc_period_isr` 为 `AUDIOSINK_PERIOD_IRQHandler`
2. 删除全部 ASHA 文件及引用（17 个文件，975 行）
3. 移除 `MsgHandler_Notify` 调用（`5e0a36c` 引入但 msg_handler.c 未编译，导致链接失败）
4. `rm_stop` 路径中 `audio_streaming = 0` 移到 `RM_Disable()` 之前

### 涉及文件

| 文件 | 改动 |
|------|------|
| `include/app.h` | 注释 `APP_ASHA_ENABLE`，删 ASHA include/define/service |
| `app.c` | 删 ASHA handler，`audio_streaming = 0` 前移 |
| `code/app_func.c` | ISR 别名 `#ifndef APP_ASHA_ENABLE` 守卫（define 删后恒真） |
| `code/app_process.c` | 去 `MsgHandler_Notify` + `msg_handler.h` |
| `code/ble_custom.c` | 去 ASHA service/read/write handler |
| `code/ble_std.c` | 去 ASHA 广播数据、L2CAP CoC、MsgHandler_Notify |
| `code/asha_app.c` | 删除 |
| `code/asha_queue.c` | 删除 |
| `code/ble_asha_wrap.c` | 删除 |
| `include/asha_*.h` | 删除（4 个） |
| `RTE/Device/RSL10/ble_asha.c/h` | 删除 |
