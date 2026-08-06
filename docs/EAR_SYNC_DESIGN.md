# BLE 双耳直连开发文档

## 概述

两个 sleep 耳机（RSL10 + BS300）通过 BLE 直连实现信息互通。主耳（左）承担 Central+Peripheral 双角色，定期在广播和扫描之间时间切片；副耳（右）保持纯 Peripheral。

## 架构

```
主耳 (GAP_ROLE_ALL)                      副耳 (GAP_ROLE_PERIPHERAL)
  ├─ [0-3s]  广播 ◄── 手机                  ├─ 广播 ◄── 手机
  ├─ [3-3.5s] 扫描 ──► 连接副耳               ├─ 广播 ◄── 主耳（连接请求）
  │           写 GATT ──► 同步数据             │   接收 GATT 写入，应用变更
  ├─ [3.5-6s] 广播 ◄── 手机                  └─ 广播 ◄── 手机
  └─ ...
```

## 文件清单

### 新增

| 文件 | 说明 |
|------|------|
| `code/ear_sync.c` | 核心状态机：角色切换 timer、Central 扫描、GATT 写入、远程数据应用 |

### 修改

| 文件 | 变更 |
|------|------|
| `include/app.h` | `EAR_SYNC_ENABLE`、配置宏、timer 枚举、状态字段、函数声明、消息处理器列表 |
| `include/ble_std.h` | `APP_IDX_MAX` 根据 `EAR_SYNC_ENABLE` 条件设置 |
| `include/ble_custom.h` | 耳间同步 UUID、`CS_IDX_EAR_SYNC_*` 枚举、长度宏、struct 字段 |
| `code/ble_custom.c` | GATT 属性表添加 ear-sync characteristic、读写 handler、环境初始化 |
| `code/ble_std.c` | 主耳 `GAP_ROLE_ALL`（待启用）、`GAPM_CANCEL` 处理、`GAPC_ConnectionReqInd` 耳间连接拦截 |
| `code/app_func.c` | `ear_side` 可通过 `-DEAR_SIDE=1` 编译配置 |
| `code/ble_rempro_cmd.c` | CMD 26 返回对耳 MAC、`cmd_setvolume/setcurrentscene/setdeviceonoff` 添加 `EarSync_PushChange` |
| `app.c` | Main_Loop 处理 ear-sync 接收数据 + pending 重发 + BLE RX/按钮 PushChange |
| `code/app_init.c` | `App_Env_Initialize` 末尾调用 `EarSync_Init()` |

## 配置宏（`include/app.h`）

```c
#define EAR_SYNC_ENABLE                    // 总开关

#define EAR_SYNC_PRIMARY_SIDE     0        // 主耳：0=左 1=右
#define EAR_SYNC_PEER_BDADDR      { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB }  // 对耳 MAC

#define EAR_SYNC_ADV_DURATION_MS   3000    // 广播窗口时长
#define EAR_SYNC_SCAN_DURATION_MS   500    // 扫描窗口时长

#define EAR_SYNC_CON_INTV_MIN      400     // 耳间连接间隔 (500ms)
#define EAR_SYNC_CON_INTV_MAX      400
#define EAR_SYNC_CON_LATENCY       0
#define EAR_SYNC_SUPERV_TO          3200   // 超时 32s
```

## 同步数据协议

GATT Write 2 字节到 ear-sync characteristic：

| Byte 0 (CMD) | Byte 1 (DATA) | 含义 |
|--------------|---------------|------|
| `0x01` | 0-3 | 切换程序/场景 |
| `0x02` | 0-9 | 音量变更 |
| `0x03` | 0/1 | 设备开关 |

## 状态变更推送触发点

| 变更来源 | 代码位置 |
|----------|----------|
| App REMPRO SetCurrentScene (CMD 16) | `cmd_setcurrentscene()` 后 |
| App REMPRO SetVolume (CMD 2) | `cmd_setvolume()` 后 |
| App REMPRO SetDeviceOnOff (CMD 3) | `cmd_setdeviceonoff()` 后 |
| App Custom RX (cmd=0x01) | `Main_Loop()` BLE RX 处理 |
| App Custom RX (cmd=0x02) | `Main_Loop()` BLE RX 处理 |
| 按键长按（程序切换） | `Main_Loop()` 按钮处理 |
| 按键短按（音量+1） | `Main_Loop()` 按钮处理 |

## 编译说明

左右耳同一份代码，编译宏区分：

```
左耳（主耳）:  默认编译
右耳（副耳）:  编译时加 -DEAR_SIDE=1
```

## 调试记录

### 2026-08-06 — 直接在 sleep 工程上加 ear-sync 导致 BLE 不广播

**现象**：`EAR_SYNC_ENABLE` 加在 `peripheral_server_sleep` 后，编译通过但上电 BLE 搜不到。

**排查过程**：

| 步骤 | 操作 | 结果 |
|------|------|------|
| 1 | 注释 `EAR_SYNC_ENABLE`，仅保留 `DEBUG_UART_ENABLE` | BLE 正常 |
| 2 | 启用 `EAR_SYNC_ENABLE`，`GAP_ROLE_ALL` → `GAP_ROLE_PERIPHERAL` | BLE 不行 |
| 3 | 启用，但 `APP_IDX_MAX` 锁为 1 | BLE 不行 |
| 4 | 启用，屏蔽 `app.c`/`ble_custom.c`/`ble_std.c`/`ble_rempro_cmd.c` 中所有 `#ifdef EAR_SYNC_ENABLE` 代码块 | BLE 不行 |
| 5 | 步骤 4 + 屏蔽 `app_init.c` 中 `EarSync_Init()` 调用 + 屏蔽 `ear_sync.c` 全部 + 屏蔽 `ble_custom.h` 改动 + 去掉 handler list 中的 `EarSync_TimerHandler` | 待确认 |

**初步结论**：即使把几乎所有运行时代码都禁用，仅保留 `app.h` 中的 struct 字段扩展和 enum 值，BLE 仍然不行。根因尚未定位，可能涉及内存布局、链接脚本或堆栈边界问题。

**下一步**：改用 `ble_central_peripheral` demo 作为基座，该 demo 已验证 Central+Peripheral 双角色可正常工作，在其基础上增量添加 ear-sync 功能（BS300 I2C 驱动、GATT 数据通道、状态同步逻辑）。
