# BLE 多连接改造（设计文档）

## 目标

将 `peripheral_server_sleep` 从 1 路 BLE 连接扩展为支持 3 路同时连接。

## 参考实现

`peripheral_server/` 目录下的 ON Semiconductor 官方 demo，支持 4 路连接。

## 架构方案

### 对比：单连接 vs 多连接

| 层面 | 单连接（现状） | 多连接（目标） |
|------|--------------|--------------|
| `ble_env` | 单结构体 | `ble_env[NUM_MASTERS]` 数组 |
| 状态 | 全局 `ble_env.state` | 每槽位独立的 `ble_env[i].state` |
| `cs_env` | 单结构体 | `cs_env[NUM_MASTERS]` 数组 |
| `rempro_env` | 单结构体 | `rempro_env[NUM_MASTERS]` 数组 |
| `bass_support_env` | 单结构体 | `bass_support_env[NUM_MASTERS]` 数组 |
| `APP_IDX_MAX` | 1 | NUM_MASTERS (3) |
| 事件路由 | 直接用 `ble_env.conidx` | `Find_Connected_Device_Index(KE_IDX_GET(src_id))` |
| 连接管理 | 连上就停广播 | 满 3 路才停广播 |

### 槽位生命周期

```
APPM_INIT → APPM_CREATE_DB → APPM_READY → APPM_ADVERTISING → APPM_CONNECTED
                                                                    ↓ (断开)
                                                               APPM_READY
```

初始化时所有槽位同步推进（0→CREATE_DB→READY→ADVERTISING），建立连接后各自独立变化。

### 关键辅助函数

```c
// 已连接数
uint8_t Connected_Peer_Num(void);

// conidx → device_indx 映射
signed int Find_Connected_Device_Index(uint8_t conidx);

// 发送连接确认
void Send_Connection_Confirmation(uint8_t device_indx);
```

## 代码改动清单

### 1. `include/ble_std.h`

```diff
- #define APP_IDX_MAX    1
+ #define NUM_MASTERS_MIN  1
+ #define NUM_MASTERS_MAX  3
+ #define NUM_MASTERS      NUM_MASTERS_MAX
+ #define APP_IDX_MAX      NUM_MASTERS
+ #define INVALID_DEV_IDX  -1

- extern struct ble_env_tag ble_env;
+ extern struct ble_env_tag ble_env[];

+ extern signed int Find_Connected_Device_Index(uint8_t conidx);
+ extern uint8_t Connected_Peer_Num(void);
+ extern void Send_Connection_Confirmation(uint8_t device_indx);

- extern void BLE_SetServiceState(bool enable, uint8_t conidx);
+ extern void BLE_SetServiceState(bool enable, uint8_t device_indx);
```

### 2. `code/ble_std.c` — 事件处理器（重写）

**全局变量数组化**：
```diff
- struct ble_env_tag ble_env;
+ struct ble_env_tag ble_env[NUM_MASTERS];
```

**新增辅助函数**：
- `Find_Connected_Device_Index(conidx)` — 扫描所有 `state==APPM_CONNECTED` 的槽位，匹配 `conidx`
- `Connected_Peer_Num()` — 统计 `state==APPM_CONNECTED` 的槽位数
- `Send_Connection_Confirmation(device_indx)` — 封装 `GAPC_CONNECTION_CFM` 消息发送

**`BLE_Initialize`** — 循环 `memset` 初始化所有 `NUM_MASTERS` 个槽位，设置初始状态

**`Service_Add`** — `ble_env.next_svc` → `ble_env[0].next_svc`（初始化只在槽位 0 操作）

**`Advertising_Start`** — 改为遍历所有槽位，将 `APPM_READY` 改为 `APPM_ADVERTISING`；发一次 `GAPM_START_ADVERTISE_CMD` 即可

**`GAPM_ProfileAddedInd` / `GAPM_CmpEvt(SET_DEV_CONFIG)`** — 所有服务添加完成后，循环设置所有槽位为 `APPM_READY`

**`GAPC_ConnectionReqInd`**：
```c
if (Connected_Peer_Num() < NUM_MASTERS) {
    // 扫描找第一个 state != APPM_CONNECTED 的槽位
    for (device_indx = 0; ...; device_indx++) { ... break; }
    ble_env[device_indx].conidx = KE_IDX_GET(src_id);
    ble_env[device_indx].state = APPM_CONNECTED;
    // ... 保存连接参数 ...
    Send_Connection_Confirmation(device_indx);
    BLE_SetServiceState(true, device_indx);
}
```

**`GAPC_DisconnectInd`**：
```c
signed int device_indx = Find_Connected_Device_Index(KE_IDX_GET(src_id));
if (device_indx == INVALID_DEV_IDX) return;
ble_env[device_indx].state = APPM_READY;
BLE_SetServiceState(false, device_indx);  // 内部会检查是否需要恢复广播
// ... bs300_settings_persist / rempro_reasm_reset ...
```

**`GAPC_ParamUpdatedInd` / `GAPC_ParamUpdateReqInd`** — 通过 `Find_Connected_Device_Index` 定位槽位后更新对应参数

**`BLE_SetServiceState`** — 参数从 `conidx` 改为 `device_indx`；末尾检查 `Connected_Peer_Num() < NUM_MASTERS` 决定是否恢复广播

**`Service_Enable`** — 内部调用 `Find_Connected_Device_Index(conidx)` 定位槽位

### 3. `include/ble_custom.h`, `code/ble_custom.c`

```diff
- struct cs_env_tag cs_env;
+ struct cs_env_tag cs_env[NUM_MASTERS];
```

- `CustomService_Env_Initialize` — 循环初始化所有槽位
- `GATTC_ReadReqInd` — 开头 `device_indx = Find_Connected_Device_Index(KE_IDX_GET(src_id))`，后续所有 `cs_env.` / `rempro_env.` 改为 `cs_env[device_indx].` / `rempro_env[device_indx].`
- `GATTC_WriteReqInd` — 同上；REMPRO 写入时传 `ble_env[device_indx].conidx` 给 `rempro_reasm_append`
- `GATTC_CmpEvt` — `cs_env[device_indx].sentSuccess` / `rempro_env[device_indx].sentSuccess`
- `GATTM_AddSvcRsp` — `cs_env[i].start_hdl` 和 `rempro_env[i].start_hdl` 设置为所有槽位相同值
- `CustomService_SendNotification` — `Find_Connected_Device_Index(conidx)` 获取 handle

### 4. `include/ble_rempro.h`, `code/ble_rempro.c`

```diff
- struct rempro_env_tag rempro_env;
+ struct rempro_env_tag rempro_env[NUM_MASTERS];
```

- `RemproService_Env_Initialize` — 循环初始化
- `RemproService_SendNotification` — `Find_Connected_Device_Index(conidx)` 获取 handle

### 5. `include/ble_rempro_cmd.h`, `code/ble_rempro_cmd.c`

```diff
- void rempro_reasm_append(const uint8_t *data, uint8_t len);
+ void rempro_reasm_append(const uint8_t *data, uint8_t len, uint8_t conidx);
```

- 新增 `static uint8_t reasm_conidx` — 记住当前组帧的连接
- `rempro_reasm_append` — 如果 conidx 变化（不同主机交叉写入）则清空重来
- `hdlc_response` — 响应发给 `reasm_conidx`（只回请求方）
- `hdlc_push` — 遍历所有 `APPM_CONNECTED` 槽位广播推送
- 所有 `ble_env.state != APPM_CONNECTED` → `Connected_Peer_Num() == 0`

### 6. `include/ble_bass.h`, `code/ble_bass.c`

```diff
- struct bass_support_env_tag bass_support_env;
+ struct bass_support_env_tag bass_support_env[NUM_MASTERS];
```

- `Bass_Env_Initialize` — 循环初始化
- `Batt_LevelNtfCfgInd` — `Find_Connected_Device_Index(KE_IDX_GET(src_id))` → `bass_support_env[device_indx].batt_ntf_cfg`
- `Batt_EnableRsp_Server` — 同上

### 7. `app.c` — 主循环

- `ble_env.state == APPM_CONNECTED` → `Connected_Peer_Num() > 0`
- BS300 RX 命令处理：`for (dev = 0; dev < NUM_MASTERS; dev++)` 遍历已连接槽位
- Custom Service TX 通知：遍历所有已连接槽位分别发送
- 新增 `app_notify_tx_changed()` 辅助函数，设置所有槽位的 `cs_env[dev].tx_value_changed = 1`
- `sentSuccess` 周期复位改为遍历所有槽位

### 8. `code/app_process.c`

- `Enable_Audiosink_Measurement` — `ble_env.state == APPM_CONNECTED` → `Connected_Peer_Num() > 0`；使用第一个已连接槽位的 `actual_con_interval` / `actual_con_latency`
- `APP_Timer` — `ble_env.state` → `Connected_Peer_Num()` / `ble_env[0].state`

## 运行时行为

### 连接管理

| 场景 | 行为 |
|------|------|
| 连接数 < 3 | 广播保持活跃，接受新连接 |
| 连接数 = 3 | 隐式停广播（不再调用 Advertising_Start） |
| 断开 1 路（非最后） | 槽位释放为 APPM_READY，恢复广播 |
| 断开最后 1 路 | 全部 APPM_READY，恢复广播，触发 bs300_settings_persist() |

### GATT 路由

所有 GATT 读/写/通知事件：`KE_IDX_GET(src_id)` → `Find_Connected_Device_Index` → `cs_env[device_indx]` / `rempro_env[device_indx]`

### REMPRO 协议

- 命令响应（hdlc_response）：只回请求方
- 状态推送（hdlc_push、按钮触发）：广播给所有已连主机
- HDLC 重装：单缓冲区 + conidx 跟踪，交叉写入自动清空

## 功耗影响

| 因素 | 影响 |
|------|------|
| 连接事件 ×3 | 唤醒频率上升，但 500ms + latency=10 下基础值很低 |
| 广播 | 满 3 路后自动停止，无额外功耗 |
| sleep 机制 | RSL10 协议栈内部管理多连接 wake-up 时序，应用层无改动 |
| 优化 | 如为固定主机，可将 `CFG_ADV_INTERVAL_MS` 从 100ms 拉大到 1000ms |

## 未实现 / 待办

- 配对与绑定（当前 `GAPM_PAIRING_DISABLE`）
- 白名单过滤（`adv_filt_policy` 仍为 0）
- 定向广播
- 每个连接的 HDLC 独立重装缓冲区（当前共享，交叉写入时清空）
