# ble_central_peripheral Demo 分析报告

> 基于 `d:/projects/onsemi-workspace/ble_central_peripheral/` 完整源码阅读，2026-08-06

## 文件清单

### 应用层

| 文件 | 职责 |
|------|------|
| `app.c` | 入口。初始化设备、服务、消息处理器，运行事件循环 |
| `include/app.h` | 主头文件。任务消息枚举、BD 地址（Peripheral + Central + 4 个 peer）、角色常量、服务计数、函数原型 |
| `source/app_config.c` | 硬件初始化、GAPM 命令 struct（`devConfigCmd`/`connectionCmd`/`advertiseCmd`）、广播/扫描数据、连接确认参数、`Device_Param_Prepare` |
| `source/app_msg_handler.c` | 所有应用层消息处理器 + `connection_Mode` 全局变量 + `APP_StartAirOperation` |
| `source/app_basc.c` | `BASC_BATT_LEVEL_IND` 处理器（打印对端电量） |
| `source/app_bass.c` | ADC 电量读取、BATMON 报警、低电 trap |
| `source/app_customss.c` | 自定义服务：`att_db[]` 属性表、`CUSTOMSS_MsgHandler`、RX 回调、定时通知 |
| `include/app_basc.h` | `APP_BASC_BattLevelInd_Handler` 声明 |
| `include/app_bass.h` | BASS 常量 + 函数声明 |
| `include/app_customss.h` | UUID、属性枚举、环境 struct、timer 消息 ID |

### BLE 抽象层（RTE/CMSIS-Pack）

| 文件 | 职责 |
|------|------|
| `RTE/Device/RSL10/ble_gap.c` | GAP 层状态机、`GAPM_ResetCmd`/`SetDevConfigCmd`/`StartAdvertiseCmd`/`StartConnectionCmd`/`CancelCmd`/`ProfileTaskAddCmd`、BondList（NVR2 flash 管理） |
| `RTE/Device/RSL10/ble_gap.h` | `GAP_Env_t`、`GAPM_State_t` 枚举、默认时序常量、函数声明 |
| `RTE/Device/RSL10/ble_gatt.c` | `GATTM_AddAttributeDatabase`/`GetHandle`/`MsgHandler`、`GATTC_SendEvtCmd`/`DiscByUUIDSvc`、读写请求分发（callback 机制） |
| `RTE/Device/RSL10/ble_gatt.h` | `att_db_desc` struct、`GATT_Env_t`、Service/Characteristic 声明宏 |
| `RTE/Device/RSL10/msg_handler.c` | 泛用消息订阅机制：单链表 `MsgHandler_Add`/`Remove`/`Notify` |
| `RTE/Device/RSL10/msg_handler.h` | `MsgHandler_t` struct + 函数声明 |
| `RTE/Device/RSL10/ble_bass.c` | 电池服务端：初始化、profile 添加、使能、电量更新、定时通知、消息处理 |
| `RTE/Device/RSL10/ble_bass.h` | `BASS_Env_t` + 函数声明 |
| `RTE/Device/RSL10/ble_basc.c` | 电池客户端：初始化、使能(含 service discovery)、读取、通知配置、定时请求、消息处理 |
| `RTE/Device/RSL10/ble_basc.h` | `BASC_Env_t` + 函数声明 |

---

## BLE 初始化流程

```
main()
  ├─ Device_Initialize()          // 硬件初始化 + BLE stack (Kernel_Init + BLE_InitNoTL)
  ├─ APP_SetAdvScanData()         // 配置广播/扫描响应数据
  ├─ BASS_Initialize()            // 电池服务端：注册 5 个 handler
  ├─ BASS_NotifyOnBattLevelChange(1s)  // 每秒检测电量变化
  ├─ BASS_NotifyOnTimeout(6s)     // 每 6s 通知一次电量
  ├─ BASC_Initialize()            // 电池客户端：注册 5 个 handler
  ├─ BASC_RequestBattLevelOnTimeout(5s) // 每 5s 读取对端电量
  ├─ CUSTOMSS_Initialize()        // 自定义服务：注册 2 个 handler
  ├─ CUSTOMSS_NotifyOnTimeout(6s) // 每 6s 发送递增通知
  ├─ MsgHandler_Add() ×7          // 注册应用层 7 个 handler
  ├─ GAPM_ResetCmd()              // 重置 GAP manager
  │   └─► GAPM_CMP_EVT / GAPM_RESET
  │       └─► APP_GAPM_GATTM_Handler
  │           └─ GAPM_SetDevConfigCmd(&devConfigCmd)
  │               └─► GAPM_CMP_EVT / GAPM_SET_DEV_CONFIG
  │                   ├─► APP_GAPM_GATTM_Handler
  │                   │   └─ GATTM_AddAttributeDatabase(att_db)
  │                   ├─► BASS handler
  │                   │   └─ GAPM_ProfileTaskAddCmd(TASK_ID_BASS)
  │                   └─► BASC handler
  │                       └─ GAPM_ProfileTaskAddCmd(TASK_ID_BASC)
  │
  ├─ 等待所有 profile + service 添加完成
  │   (GAPM_GetProfileAddedCount() == 2 && GATTM_GetServiceAddedCount() == 1)
  │
  ├─ connection_Mode = APP_BLE_PERIPHERAL_ROLE
  ├─ APP_StartAirOperation(PERIPHERAL)
  │   └─ GAPM_StartAdvertiseCmd(&advertiseCmd)    // 开始广播
  ├─ ke_timer_set(APP_LED_TIMEOUT, 200ms)
  └─ ke_timer_set(APP_SWITCH_ROLE_TIMEOUT, 10s)   // 10s 后切 Central
  
  while(1) {
      Kernel_Schedule();      // 分发所有内核消息
      Sys_Watchdog_Refresh();
      SYS_WAIT_FOR_EVENT;     // 等待下个事件（不休眠）
  }
```

### 关键配置

```c
// app_config.c:40
.role = GAP_ROLE_ALL           // 支持 Peripheral + Central 双角色

// app_config.c:63-96 — Central 连接参数
connectionCmd = {
    .op.code     = GAPM_CONNECTION_AUTO,  // 自动扫描+连接
    .scan_interval = 100,                 // 62.5ms
    .scan_window   = 50,                  // 50% 占空比
    .con_intv_min  = 20,                 // 20ms 连接间隔
    .con_intv_max  = 20,
    .con_latency   = 0,
    .superv_to     = 300,                // 3s 超时
    .nb_peers      = 4,                  // 4 个 peer
    .peers[0..3]   = { CENTRAL1..4 }     // 预配置 MAC
};
```

---

## 双角色时间切片

### 状态机

```
connection_Mode = 0 (Peripheral)
      │
      ├── 10s timeout ──► GAPM_CancelCmd()
      │                      │
      │                      └── GAPM_CMP_EVT / GAP_ERR_CANCELED
      │                          └── APP_StartAirOperation(CENTRAL)
      │                              └── GAPM_StartConnectionCmd()
      │
connection_Mode = 1 (Central)
      │
      ├── 10s timeout ──► GAPM_CancelCmd()
      │                      │
      │                      └── GAPM_CMP_EVT / GAP_ERR_CANCELED
      │                          └── APP_StartAirOperation(PERIPHERAL)
      │                              └── GAPM_StartAdvertiseCmd()
      │
      └──► 循环...
```

### `APP_StartAirOperation()` — 调度核心

```c
void APP_StartAirOperation(uint8_t currentRole)
{
    if (GAPC_GetConnectionCount() < APP_NB_PEERS) {
        if (currentRole == APP_BLE_PERIPHERAL_ROLE) {
            GAPM_StartAdvertiseCmd(&advertiseCmd);    // 广播
        } else {
            GAPM_StartConnectionCmd(&connectionCmd);  // 扫描+连接
        }
    }
}
```

### 关键保证

**`GAPM_CancelCmd()` 不丢已有连接。** 它只取消当前空口操作（广播或扫描），已建立的 BLE 连接不受影响。`ble_gap.c:100-102` 注释明确说明：

> Cancel an ongoing air operation such as scanning, advertising or connecting. It has no impact on other commands.

当连接数达到 `APP_NB_PEERS=4` 时，`APP_StartAirOperation` 直接跳过，不再尝试建立新连接。

---

## Central 连接流程

### 发起连接

```
APP_StartAirOperation(CENTRAL)
  └─ GAPM_StartConnectionCmd(&connectionCmd)
      └─ gap_env.gapmState = GAPM_STATE_STARTING_CONNECTION
      └─ Stack 开始扫描 + 匹配 peer MAC 列表
          │
          └─ 发现 peer ──► 自动连接
              └─ GAPC_CONNECTION_REQ_IND
                  ├─ GAPC_MsgHandler: connectionCount++, 记录连接信息
                  ├─ APP_GAPC_Handler:
                  │   ├─ 打印 peer 地址
                  │   ├─ 如已绑定 → GAPM_ResolvAddrCmd() 解析地址
                  │   ├─ GAPC_ConnectionCfm() 确认连接
                  │   └─ APP_StartAirOperation(connection_Mode) // 继续当前角色
                  ├─ BASS handler: BASS_EnableReq(conidx) // 服务端使能
                  └─ BASC handler: BASC_EnableReq(conidx) // 客户端发现
```

### BASC 服务发现（GATT 客户端）

```
BASC_EnableReq(conidx, PRF_CON_DISCOVERY)
  └─► BASC_ENABLE_REQ 发送到 Stack
      └─► Stack 内部做 GATT primary service discovery (UUID 0x180F)
          + characteristic discovery (UUID 0x2A19)
          └─► BASC_ENABLE_RSP
              ├─ 保存 discovered handles → basc_env.bas[conidx]
              ├─ basc_env.enabled[conidx] = true
              ├─ BASC_BattLevelNtfCfgReq() — 使能对端通知
              └─ 启动定时读取 timer
```

---

## 自定义服务 (Custom Service)

### 属性表 (`app_customss.c:26-92`)

| Characteristic | 方向 | 权限 | 大小 |
|---|---|---|---|
| TX_VALUE | Peripheral → Central | RD + NTF | 20B |
| RX_VALUE | Central → Peripheral | RD + WRITE | 20B |
| TX_LONG_VALUE | Peripheral → Central | RD + NTF | 40B |
| RX_LONG_VALUE | Central → Peripheral | RD + WRITE | 40B |

### RX_LONG_VALUE 回显机制

Demo 的演示功能：Central 写入 `RX_LONG_VALUE` → Peripheral 取反后存到 `TX_LONG_VALUE`：

```c
uint8_t CUSTOMSS_RXLongCharCallback(...)
{
    memcpy(to, from, length);
    if (operation == GATTC_WRITE_REQ_IND) {
        for (i = 0; i < CS_LONG_VALUE_MAX_LENGTH; i++)
            to_air_buffer_long[i] = 0xFF ^ from_air_buffer_long[i];
    }
}
```

---

## 消息处理器架构

### 链表订阅模型

与 `peripheral_server_sleep` 的静态状态机不同，demo 使用动态链表：

```
msgHandlerHead → [Node: TASK_ID_GAPM → APP_GAPM_GATTM_Handler]
              → [Node: GATTM_ADD_SVC_RSP → APP_GAPM_GATTM_Handler]
              → [Node: TASK_ID_GAPC → APP_GAPC_Handler]
              → [Node: APP_LED_TIMEOUT → APP_LED_Timeout_Handler]
              → [Node: APP_BATT_LEVEL_LOW → APP_BASS_BattLevelLow_Handler]
              → [Node: APP_SWITCH_ROLE_TIMEOUT → APP_SwitchRole_Timeout]
              → [Node: BASC_BATT_LEVEL_IND → APP_BASC_BattLevelInd_Handler]
              → [Node: TASK_ID_BASS → BASS_MsgHandler]
              → [Node: GAPM_CMP_EVT → BASS_MsgHandler]
              → [Node: GAPC_CONNECTION_REQ_IND → BASS_MsgHandler]
              → [Node: GAPC_DISCONNECT_IND → BASS_MsgHandler]
              → [Node: GAPM_PROFILE_ADDED_IND → BASS_MsgHandler]
              → [Node: TASK_ID_BASC → BASC_MsgHandler]
              → ... (BASC, CUSTOMSS handlers)
```

### `MsgHandler_Notify` 分发逻辑

每个消息到达时，**两次遍历**：

1. **第一遍**：按 task ID 调用抽象层 handler（`GAPC_MsgHandler` / `GAPM_MsgHandler` / `GATTC_MsgHandler` / `GATTM_MsgHandler`）—— 更新内部状态
2. **第二遍**：遍历链表，匹配 `msg_id` 或 `task_id`，调用应用层回调

这意味着每个事件（如 `GAPC_CONNECTION_REQ_IND`）会被**多个 handler 依次处理**：先 GAPC 层更新连接计数，再应用层确认连接，再 BASS/BASC 各自使能服务。

---

## 与 peripheral_server_sleep 的核心差异

| 维度 | Demo | Sleep 工程 |
|------|------|-----------|
| BLE 角色 | `GAP_ROLE_ALL` | `GAP_ROLE_PERIPHERAL` |
| 多连接 | 最多 4 路（Peripheral + Central 各占） | 仅 1 路 Peripheral |
| Central 能力 | 扫描+连接+GATT 客户端发现 | 无 |
| 休眠 | `SYS_WAIT_FOR_EVENT`（不休眠） | `POWER_MODE_SLEEP`（深度休眠） |
| 消息架构 | `MsgHandler_Add` 链表订阅 | `app_process.c` 静态状态机 (`ble_env.state`) |
| GATT 客户端 | BASC（自动发现+读取对端服务） | 无 |
| 角色切换 | 10s 定时 `APP_SwitchRole_Timeout` | 无 |
| `GAPM_CancelCmd` | 用于角色切换 | 未使用 |
| 编译状态 | ✅ 正常（含 `GAP_ROLE_ALL`） | ❌ 我们的 `EAR_SYNC_ENABLE` 改动导致 BLE 异常 |

### Sleep 有而 Demo 没有的

- BS300 DSP 驱动（10+ 文件）
- 深度休眠（`BLE_Power_Mode_Enter`）
- RM 音频推流
- REMPRO (HDLC) 验配协议
- FOTA 固件升级
- 物理按键处理
- Flash 持久化（校准/参数存储）

---

## 以 Demo 为基座实现 ear-sync 的路线

1. **加 ear-sync characteristic**：在已有 `att_db[]` 末尾加一个 20B 的 characteristic（WRITE 权限），UUID 用新字节
2. **写 RX callback**：收到对端写入 → 解析 cmd+data → 应用状态变更
3. **改角色切换节奏**：`APP_SWITCH_ROLE_TIMER` 从 10s 改为 3s/0.5s 交替
4. **加 PushChange 逻辑**：本地状态变更 → 作为 Central 写入对端的 ear-sync characteristic
5. **迁 BS300 最小驱动**：`bs300_hal.c`（I2C GPIO）+ `bs300_driver.c`（init/sync）+ `bs300_ram_sync.c`（程序/音量切换）
6. **休眠暂不加**：Central 扫描期间必须保持唤醒
