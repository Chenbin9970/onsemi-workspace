# TX CONNECT_IND 发送逻辑设计说明

## 1. 背景

TX 通过 RFX2401C FEM 芯片强化了 BLE 发送功率（PA），但**未强化接收（无 LNA）**。这导致非对称链路：TX 能发到 Sleep，但 Sleep 的回复在稍远距离处 TX 收不到。

## 2. Sleep 侧行为

### 2.1 两个 Sleep 设备

- peer 0：左耳或设备 0
- peer 1：右耳或设备 1

### 2.2 双模式设计（功耗驱动）

| 模式 | 功耗 | 用途 |
|------|------|------|
| BLE 广播/连接 | ~100μA | 低功耗待机，等待 TX 唤醒 |
| RM 搜索/接收 | ~400μA | 接收 DMIC 音频流 |

### 2.3 模式切换逻辑

```
冷启 → RM 搜索模式（30s 窗口）
  ├─ 30s 内 RM 连上 → 切程序 3（音频模式）
  ├─ 30s 内未连上 → 退 BLE 低功耗，广播等待
  └─ RM 断连 → 消抖 1s → 重新 RM 搜索 → 30s 超时退 BLE

BLE 模式收到 TX 的 CONNECT_IND（识别 TX MAC）→ 直接切 RM（跳过 BLE 握手）
```

**30s RM 窗口的设计意图**：TX 和 peer 0 已在 RM 连接中，peer 1 后开机 — peer 1 的 30s RM 窗口让它能直接加入已有 RM 会话，无需 TX 额外操作。

**省电是核心动机**：RM 模式比 BLE 模式多 300μA，所以 Sleep 在不需音频时尽快退回 BLE。

## 3. TX 侧发送逻辑

### 3.1 TX 状态机（音频门控）

TX 不建立 BLE 连接，只在有音频时发送 CONNECT_IND 唤醒 Sleep：

```
TX_BLE_IDLE ──[DMIC 检测到音频]──▶ TX_CONNECTING ──[序列完成]──▶ TX_RM_ACTIVE
     ▲                                │                               │
     │                                │ 音频中途消失                    │
     │                                └──▶ TX_BLE_IDLE                 │
     │                                                                │
     └───────────────────[音频丢失 3s]────────────────────────────────┘
```

| 状态 | TX 行为 | Sleep 侧 |
|------|---------|----------|
| `TX_BLE_IDLE` | BLE 主机待机，DMIC 持续检测 | BLE 低功耗（已超时退）或 RM 搜索（冷启 30s 内） |
| `TX_CONNECTING` | 串行发 CONNECT_IND 到 peer 0 → CANCEL → peer 1 → CANCEL | 收到 CONNECT_IND → 识别 TX MAC → 切 RM |
| `TX_RM_ACTIVE` | RM 推流，持续检测静音 | RM 接收 → 切程序 3 音频输出 |

### 3.2 DMIC 音频检测

- **检测到音频**：EMA 能量 > 阈值(5000) 连续 3 tick（600ms）→ 触发 CONNECT_IND
- **音频丢失**：能量 < 阈值连续 15 tick（3s）→ 退出 RM 回 BLE
- 检测**始终运行**，`BLE_IDLE` 和 `RM_ACTIVE` 都需要

### 3.3 CONNECT_IND 的本质

CONNECT_IND **不是要建立 BLE 连接**。它是一个**单向唤醒信号**：

- TX 用 RFX2401C PA 强化发送 → Sleep 收到 → 识别 TX MAC → 直接切 RM
- Sleep 返回的 `gapc_connection_cfm` → TX **收不到**（RFX2401C 无 LNA，非对称链路）
- TX 用 PA 发出 CONNECT_IND 强信号，但 TX 自身的 RX 无 LNA 强化

### 3.4 为什么必须串行

GAPM（BLE 协议栈全局管理器）**一次只处理一个连接请求**。`nb_peers > 1` 在 RSL10 stack 上不工作。两个 Sleep 必须串行：peer 0 → CANCEL → peer 1。

### 3.5 为什么需要 CANCEL

GAPM 对不在线设备发送 CONNECT_IND 后，**永不主动超时返回**。CANCEL 是唯一能强制 GAPM 退出 CONNECTING 状态的机制，不 CANCEL 则：
- `ble_env.state` 永远卡在 `APPM_CONNECTING`
- 无法发下一个 CONNECT_IND
- 无法切 RM

### 3.6 为什么等 600ms

CONNECT_IND 发出去后，需要给 Sleep 时间：收到包 + 识别 MAC + 切 RF 模式。600ms（3 × 200ms tick）经验可靠。

## 4. 完整流程

```
TX Boot → BLE_Initialize → BLE 主机模式（TX_BLE_IDLE）
       → DMIC 持续监控音频

DMIC 检测到音频（EMA > 5000，连续 600ms）:
  TX_BLE_IDLE → TX_CONNECTING
    → DirectConnect(0) 发 CONNECT_IND 到 peer 0
    → 等 600ms → GAPM_CANCEL → 等 CANCEL 完成
    → DirectConnect(1) 发 CONNECT_IND 到 peer 1
    → 等 600ms → GAPM_CANCEL → 等 CANCEL 完成
    → 检查音频是否还在（ad_detected && !ad_lost）
      ├─ 还在 → APP_RM_Init → RF_SwitchToCPMode → RM_Enable → TX_RM_ACTIVE
      └─ 已消失 → 回 TX_BLE_IDLE

RM 推流中（TX_RM_ACTIVE）:
  → DMIC 持续检测 → 静音 3s → ad_lost
  → RM_Disable → RF_SwitchToBLEMode → TX_BLE_IDLE

Sleep 侧：
  冷启 → RM 搜索（30s 窗口）
    ├─ TX 的 CONNECT_IND 唤醒（如果已在 BLE 省电模式）
    ├─ 或直接 RM 连接（如果在 RM 搜索模式）
    └─ 30s 超时 → BLE 低功耗，等 CONNECT_IND 唤醒
```

## 5. 实现细节

### 5.1 CONNECT_IND phase 序列

```
phase < PEER_COUNT:
  ├─ cancelling=0, tick=0: DirectConnect(phase) → state=APPM_CONNECTING
  ├─ cancelling=0, tick<3: 等待（200ms × 3 = 600ms）
  ├─ cancelling=0, tick≥3: GAPM_CANCEL → cancelling=1
  └─ cancelling=1: 等待 CANCEL 生效 → phase++

phase ≥ PEER_COUNT && state != APPM_CONNECTING:
  → 检查音频 → RM START 或回 BLE_IDLE
```

### 5.2 RM → BLE 返回路径

```c
RM_Disable();
RF_SwitchToBLEMode();
NVIC_EnableIRQ(BLE_FINETGTIM_IRQn);
// 重置音频标志，下次检测到音频重新触发
```

## 6. 关键约束总结

| 约束 | 原因 |
|------|------|
| 有音频才发 CONNECT_IND | 省电：TX 在 BLE_IDLE 低功耗待机，Sleep 在冷启 30s 内已在 RM |
| 必须发 CONNECT_IND | Sleep 可能在 BLE 省电模式（超时后），需要单向唤醒 |
| 必须 CANCEL | GAPM 不超时，不 CANCEL 则永远卡 APPM_CONNECTING |
| 必须串行 | GAPM 一次只处理一个连接请求 |
| 等 600ms | 给 Sleep 足够时间收 CONNECT_IND + 切 RF |
| BLE 连接注定失败 | RFX2401C 只强化 TX，RX 收不到 Sleep 的回复 |
| CONNECT_IND 是单向唤醒 | Sleep 收到后直接切 RM，不走 BLE 握手 |
| 音频丢失 3s 退 BLE | 给 RM 链路消抖窗口，避免短暂静音误触发 |
