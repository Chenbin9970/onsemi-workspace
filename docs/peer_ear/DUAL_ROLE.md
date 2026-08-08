# 对侧耳机：BLE 双角色连接 + 程序/音量双向同步

## 架构

两只耳机各运行同一固件。左耳主动连右耳，连上后自动同步程序和音量。

| 角色 | `ear_side` | BLE 角色 | 行为 |
|------|-----------|---------|------|
| 左耳（主机） | `RM_LEFT` | Central + Peripheral | 主动连右耳，发现 GATT → 订阅 TX 通知 → Write RX 同步 |
| 右耳（从机） | `RM_RIGHT` | Peripheral | 等左耳连入（MAC 识别），收 TX 订阅 → 发 TX 通知同步 |

核心改动：`GAP_ROLE_PERIPHERAL` → `GAP_ROLE_ALL`

## 连接流程

```
Boot → 广播 → 1s 后暂停广播 → DirectConnect(对侧MAC) → 恢复广播
                   ↓ 成功                    ↓ 失败
              双连接并存             5s 后重试
```

- 始终优先广播，确保手机随时可连
- `GAPM_CONNECTION_DIRECT` 直连已知 MAC，无需扫描
- RM 音频流期间自动跳过（RF 在 CP 模式，BLE 不可用）
- 仅左耳（`RM_LEFT`）发起连接
- 右耳断连后自动重启广播

### 对侧连接参数

| 参数 | 值 | 含义 |
|------|-----|------|
| interval | 400 (500ms) | 连接间隔 |
| latency | 0 | 不跳事件，断电 ~6s 检测 |
| timeout | 600 (6s) | 监督超时 |

## 同步机制

```
左耳按键 → Write RX[0x01, prog] → 右耳处理
右耳按键 → TX Notify[cnt,prog,vol] → 左耳订阅 → 左耳处理
```

| 方向 | 机制 | 数据 |
|------|------|------|
| 本机→对侧 | GATT Write 对侧 RX | `[0x01, prog]` 或 `[0x02, vol]` |
| 对侧→本机 | GATT Notify 对侧 TX | `[counter, prog, vol, 0, 0]` 5 字节 |

### TX 通知格式

```
原来: [counter, counter, counter, counter, counter]
改为: [counter, prog,    vol,      0,        0    ]
```

手机看 byte[0] 照常工作，对侧解析 byte[1..2]。

### 按键即发

按键时立即设 `cs_env.tx_value_changed = 1` 并预填 `cs_env.tx_value[1..4]` 为新值，不等 I2C 完成。TX 通知块只填 byte[0]（计数器）。

### 通知发往对侧

`CustomService_SendNotification` 原只发 `ble_env.conidx`（手机）。新增 `|| ble_env.peer_ear_connected` 条件，且连接对侧后同时向 `peer_ear_conidx` 发通知。

### 防回环（int 计数器）

`sync_from_remote` 用 int 计数器替代 bool：
- GATTC_EvtInd / WriteReqInd（收到远程指令）：`sync_from_remote++`
- 按键立即同步：`sync_from_remote++`
- BS300 回调：`if (sync_from_remote > 0) { sync_from_remote--; } else { send sync; }`

### GATT 发现（跳过特征扫描）

两边同固件，特征句柄偏移固定，`DISC_BY_UUID_SVC` 找到 service handle 后直接计算：

```c
tx_hdl      = svc_start + CS_IDX_TX_VALUE_VAL + 1;
tx_cccd_hdl = svc_start + CS_IDX_TX_VALUE_CCC + 1;
rx_hdl      = svc_start + CS_IDX_RX_VALUE_VAL + 1;
```

省去 `DISC_ALL_CHAR`（返回 0x51 错误）。

## 消息流

```
Central 侧（左耳）:
  对侧连接 → GAPC_ConnectionReqInd(PEER_EAR_CONNECTING)
    → CS_Peer_Enable → DISC_BY_UUID_SVC
      → DiscSvcInd: 算句柄 + 写 TX CCCD + peer_ear_gatt_ready=true
  按键 → CS_Peer_WriteRX → 对侧处理 → 对侧 TX notify → EvtInd 收到（忽略）
  对侧按键 → 对侧 TX notify → EvtInd → cs_env.rx_value → Main_Loop 处理

Peripheral 侧（右耳）:
  入站连接 → GAPC_ConnectionReqInd(MAC 匹配 peer) → 保存 conidx
  Central 订阅 TX CCCD → TX 通知发送
  按键 → cs_env.tx_value_changed → TX 通知发往 Central
  收到 Central Write → cs_env.rx_value_changed → Main_Loop 处理
  断连 → Advertising_Start() → 等待重连
```

## 修改文件

| 文件 | 改动 |
|------|------|
| `include/ble_std.h` | 对侧 MAC、连接参数、`peer_ear_state` 枚举、`ble_env_tag` 扩展、`cs_peer_env_tag`、函数声明 |
| `code/ble_std.c` | `GAP_ROLE_ALL`、`DirectConnect_PeerEar()`、`PeerEar_TryConnect()`、GAPM_CANCEL 处理、双连接分派、对侧断连重启广播 |
| `include/ble_custom.h` | `GATTC_DISC_SVC_IND`、`GATTC_DISC_CHAR_IND`、`GATTC_EVENT_IND` 处理器声明 |
| `code/ble_custom.c` | `CS_Peer_Enable`（发现+算句柄+订阅）、`CS_Peer_WriteRX`、`GATTC_EvtInd`（收通知执行切换）、`GATTC_WriteReqInd` 中 MAC 识别 |
| `app.c` | 按键即发同步、TX 预填值、通知块条件扩展、对侧通知发送、计数器防回环 |
| `include/app.h` | `int sync_from_remote` 计数器 |

## 踩坑记录

**GAPM_CANCEL 响应码**：`CMP_EVT` 返回被取消的操作码（如 `GAPM_ADV_UNDIRECT=13`），不是 `GAPM_CANCEL(0x02)`。需在 `default` 分支匹配 `status == GAP_ERR_CANCELED`。

**RM 冷启动冲突**：`RF_SwitchToCPMode()` 抢 RF，BLE 广播在 CP 模式空转，`GAPM_CANCEL` 必然失败。修复：`audio_streaming == 1` 时跳过对侧连接。

**连接超时**：`GAPM_CONNECTION_DIRECT` 对侧不在线时堆栈可能一直扫描不返回。修复：3 秒超时自动 cancel。

**参数合规**：`timeout > (1+latency) × interval × 2`。`latency=10, timeout=6s` 不合规被拒（162）。修复：`latency=0`。

**通知块条件**：手机没连时 `cs_env.tx_cccd_value == 0` 导致整个通知块被跳过。修复：加 `|| ble_env.peer_ear_connected`。

**Peripheral 侧断连不广播**：对侧断开后 `ble_env.state` 未更新，广告不重启。修复：无条件 `Advertising_Start()`。

**华为手机 40s 断连**：特定手机 `timeout=5s` 的兼容性边界，非设备 bug。缓解：连后请求放宽参数。

## 编译

左右耳同固件，切换 `app.h` 中 `APP_RM_AUDIO_CHANNEL`：
- 左耳: `RM_LEFT`
- 右耳: `RM_RIGHT`

`CFG_CON=2`, `CFG_FOTA=0`
