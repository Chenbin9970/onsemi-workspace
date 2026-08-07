# Sleep 设备双角色（主+从）对侧耳机连接

## 概览

`peripheral_server_sleep` 从纯 BLE Peripheral 升级为 BLE 双角色：
- **从机**：广播 "cbtestfota"，接受手机 App 连接（GATT Server）
- **主机**：主动连接对侧耳机（Central 角色，硬编码 MAC）

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

## 修改文件

| 文件 | 改动 |
|------|------|
| `include/ble_std.h` | +55 行：对侧 MAC 宏、连接参数、`peer_ear_state` 枚举、`ble_env_tag` 扩展、函数声明 |
| `code/ble_std.c` | +150 行：`GAP_ROLE_ALL`、`DirectConnect_PeerEar()`、`PeerEar_TryConnect()`、GAPM_CANCEL 处理、双连接分派 |
| `app.c` | +30 行：200ms 定时器驱动倒计时、RM 停止后自动恢复广播 |
| `code/ble_custom.c` | GATTC 读写使用 `src_id` 适配多连接 |

## 对侧 MAC 配置

在 `ble_std.h` 中配置（BLE little-endian 格式）：

```c
#define PEER_EAR_BD_ADDRESS_LEFT    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define PEER_EAR_BD_ADDRESS_RIGHT   { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB }
```

当前设备根据 `ear_side` 自动选择对侧 MAC：左耳 → 连右耳 MAC，右耳 → 连左耳 MAC。

### 对侧连接参数

| 参数 | 值 | 含义 |
|------|-----|------|
| interval | 400 (500ms) | 连接间隔 |
| latency | 0 | 不跳连接事件，快速检测断连 |
| timeout | 600 (6s) | 监督超时，断电后 ~6s 检测到 |
| scan_interval | 100 (62.5ms) | 连接扫描间隔 |
| scan_window | 50 (31.25ms) | 连接扫描窗口 |

## 关键设计决策

### 定时器 vs 迭代计数

最初用 Main_Loop 迭代计数（~100ms/轮），但开打印时 CPU 不睡导致迭代速度差几十倍。最终用 `ke_timer_set` 内核定时器（200ms），在睡眠和空转两种模式下均准确。

### GAPM_CANCEL 响应

`GAPM_CANCEL_CMD` 的 `CMP_EVT` 返回的是**被取消的操作码**（如 `GAPM_ADV_UNDIRECT=13`），不是 `GAPM_CANCEL(0x02)`。因此在 `default` 分支匹配 `status == GAP_ERR_CANCELED`，而非 `case (GAPM_CANCEL)`。

### 连接超时机制

`GAPM_CONNECTION_DIRECT` 在对侧不在线时，BLE 堆栈可能一直扫描不返回 `CMP_EVT`，导致状态机卡死在 `PEER_EAR_CONNECTING`。解决方案：发起连接时启动 3 秒超时（200ms × 15），超时后发 `GAPM_CANCEL` 中止，进入重试等待。

### BLE 参数合规校验

协议要求 `supervision_timeout > (1 + latency) × conn_interval × 2`（单位按各自量纲换算）。原来 `latency=10, timeout=3200(32s)` 合规（`32000ms > 11000ms`），改为 `timeout=600(6s)` 后不合规（`6000 < 11000`），被 BLE 堆栈拒绝（`LL_ERR_INVALID_HCI_PARAM=162`）。修复：`latency=10 → 0`。

### RM 冷启动冲突

冷启动时 `RF_SwitchToCPMode()` 抢占 RF，导致 BLE 广播在 CP 模式下"空转"。此时发 `GAPM_CANCEL` 必然失败，形成每 200ms 重试的死循环。修复：在 `app_env.audio_streaming == 1` 时跳过对侧连接。

### 华为手机 40 秒断连

特定手机（`interval=24/30ms, timeout=5s`）连接约 40 秒后触发监督超时。排查结论：

- **不是设备 bug**：Main_Loop 心跳持续到断连前 84ms，ARM 未挂起
- **不是参数协商问题**：设备主动请求放宽参数（500ms/32s），手机接受但仍断
- **不是冷启动 RM 干扰**：禁用后仍断
- **手机特异性**：换一台手机（`timeout=0.72s`）完全稳定

缓解措施：连接后主动请求参数更新为 500ms 间隔 / 32s 超时。对问题手机延长了存活时间（45s→73s）但未完全解决，判定为 RSL10 协议栈与该手机 BLE 实现的兼容性边界。

## 编译配置

- `GAP_ROLE_ALL` 启用 Central 角色
- `CFG_CON=2` 限制最大 2 连接
- `CFG_FOTA=0`（调试用，与双角色无关）

## 日志说明

| 前缀 | 含义 |
|------|------|
| `[PEER_EAR] stopping advertising` | 暂停广播，准备连接对侧 |
| `[PEER_EAR] advertising cancelled` | 广播已停，开始发连接 |
| `[PEER_EAR] DirectConnect to` | 正向对侧 MAC 发起连接 |
| `[PEER_EAR] connected: conidx=0` | 对侧连接成功 |
| `[PEER_EAR] connection failed` | 对侧连接失败，5s 后重试 |
| `[PEER_EAR] connect timeout, cancelling` | 连接超时，取消尝试 |
| `[PEER_EAR] connect cancelled, retrying later` | 超时取消完成，进入重试 |
| `[PEER_EAR] disconnected` | 对侧断连 |
| `[DISCONNECT] reason=0x%02X` | 手机断连原因码 |
| `[PARAM_UPDATED] intv=...` | 连接参数已更新 |
