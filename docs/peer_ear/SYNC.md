# 双耳同步方案：程序 & 音量

## 架构

两只耳机各运行同一固件，通过 BLE 连接后自动同步：

| 角色 | `ear_side` | 行为 |
|------|-----------|------|
| 左耳（主机） | `RM_LEFT` | Central，主动连右耳，发现 GATT → 订阅 TX 通知 → 可 Write RX |
| 右耳（从机） | `RM_RIGHT` | Peripheral，等左耳连入（MAC 识别）→ 接 TX 通知订阅 → 发 TX 更新 |

## 同步机制

```
左耳按键切程序 ──Write RX[0x01, prog]──→ 右耳 RX → 右耳处理
右耳按键切程序 ──TX Notify[cnt,prog,vol]──→ 左耳订阅 → 左耳处理
```

| 方向 | 机制 | 数据 |
|------|------|------|
| 本机→对侧 | GATT Write 对侧 RX | `[0x01, prog]` 或 `[0x02, vol]` |
| 对侧→本机 | GATT Notify 对侧 TX | `[counter, prog, vol, 0, 0]` 5 字节 |

### 防回环（int 计数器）

`sync_from_remote` 用 int 计数器替代 bool：
- GATTC_EvtInd / WriteReqInd（收到远程指令）：`sync_from_remote++`
- 按键立即同步：`sync_from_remote++`
- BS300 回调：`if (sync_from_remote > 0) { sync_from_remote--; } else { send sync; }`

多个异步 I2C 操作完成不会竞态清零导致 echo。

### 按键即发同步

按键时立即设 `cs_env.tx_value_changed = 1` 并预填 `cs_env.tx_value[1..4]` 为新值，不等 I2C 完成。TX 通知块只填 byte[0]（计数器），byte[1..4] 由调用方预设。

### 通知发往对侧

`CustomService_SendNotification` 原只发 `ble_env.conidx`（手机）。新增对侧连接后同时发 `ble_env.peer_ear_conidx`。通知块触发条件也加上 `|| ble_env.peer_ear_connected`（手机 CCCD 为 0 时仍能发）。

### 断连后自动恢复广播

Peripheral 侧对侧断开时 `GAPC_DisconnectInd` 中无条件调用 `Advertising_Start()`，确保对侧重连时能搜到广播。

## TX 通知格式

```
原来: [counter, counter, counter, counter, counter]
改为: [counter, prog,    vol,      0,        0    ]
        ↑ phone兼容     ↑ ear同步  ↑ ear同步
```

## GATT 发现（跳过特征扫描）

两边同固件，特征句柄在 service 内偏移固定，`DISC_BY_UUID_SVC` 找到 service handle 后直接计算：

```c
tx_hdl      = svc_start + CS_IDX_TX_VALUE_VAL + 1;
tx_cccd_hdl = svc_start + CS_IDX_TX_VALUE_CCC + 1;
rx_hdl      = svc_start + CS_IDX_RX_VALUE_VAL + 1;
```

省去 `DISC_ALL_CHAR`（左耳发现该步骤返回 `0x51` 错误，跳过）。

## 修改文件

| 文件 | 改动 |
|------|------|
| `include/ble_std.h` | +20 行：`cs_peer_env_tag` 结构体、`peer_ear_gatt_ready` 标志、`CS_Peer_Enable/WriteRX` 函数声明 |
| `code/ble_std.c` | +20 行：对侧连接后调用 `CS_Peer_Enable`（Central）/ MAC 识别入站连接（Peripheral）；断连时重置 GATT 状态 |
| `include/ble_custom.h` | +15 行：`GATTC_DISC_SVC_IND`、`GATTC_DISC_CHAR_IND`、`GATTC_EVENT_IND` 消息处理器声明 |
| `code/ble_custom.c` | +100 行：`CS_Peer_Enable`（服务发现+直接算句柄+订阅）、`CS_Peer_WriteRX`（发 `[cmd,arg]`）、`GATTC_EvtInd`（收 TX 通知并执行本地切换）、`GATTC_WriteReqInd` 中对侧识别设 `sync_from_remote` |
| `app.c` | +30 行：按键按下即发同步（不等 I2C 完成）、TX 通知格式改为 `[cnt,prog,vol,0,0]`、`sync_from_remote` 防回环 |
| `include/app.h` | +3 行：`sync_from_remote` 标志 |

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
  Central 订阅 TX CCCD → TX 通知发送 [cnt,prog,vol,0,0]
  按键 → cs_env.tx_value_changed → TX 通知发往 Central
  收到 Central Write → cs_env.rx_value_changed → Main_Loop 处理
```

## 编译

左右耳同固件，切换 `app.h` 中 `APP_RM_AUDIO_CHANNEL`：

- 左耳: `RM_LEFT`（主动连对侧）
- 右耳: `RM_RIGHT`（等对侧连入）
