# remote_mic_tx_coex 开发记录：从 Demo 到 OK

## 目标

TX 通过 BLE 连接 Sleep 设备，发送 RM_ONOFF=1 指令让 Sleep 进入 RM 接收模式，然后 TX 切 RM 发送 DMIC 音频。

---

## 第一阶段：BLE 扫描 → 直连

### 原始状态
- TX 扫描 BLE 广播，匹配 peer 的 Custom Service UUID（`aabb0e6e-...`）
- 连接后向 7 个 characteristic 写入 RM 配置参数
- 对端地址硬编码 `0xC01111111111`
- 输入接口：`SPI_RX_CODED_INPUT`

### 改动
1. **改为连接 Sleep 工程**
   - Custom Service UUID 对齐 Sleep：`e093f3b5-00a3-a9e5-9eca-40016e0edc24`
   - 只匹配 RM_ONOFF characteristic（UUID byte4=0x04）
   - 删掉 7 参数写入，只写 1 字节 `0x01`

2. **扫描方式演进**
   - 先尝试广播中匹配 128-bit Service UUID → Sleep 广播里没放 UUID，失败
   - 改为匹配设备名 `"cbtest"` → 有效但需要扫描
   - 最终改为**硬编码 MAC 直连**：`00 12 09 00 80 09`（`SLEEP_BD_ADDRESS`），跳过扫描

3. **BLE 消息结构修正**（RSL10 SDK 3.9.1182）
   - `gapm_start_scan_cmd`：`scan_interval` → `interval`，`scan_window` → `window`，无 `filter_duplic`
   - `gapm_adv_report_ind`：数据在 `param->report.data`，地址在 `param->report.adv_addr`（`struct bd_addr_t`）

### 涉及文件
- [ble_std.h](../remote_mic_tx_coex/include/ble_std.h) — UUID、MAC、状态枚举
- [ble_std.c](../remote_mic_tx_coex/code/ble_std.c) — `DirectConnect()`、`GAPM_AdvReportInd`
- [ble_custom.h](../remote_mic_tx_coex/include/ble_custom.h) — Service/Char UUID
- [ble_custom.c](../remote_mic_tx_coex/code/ble_custom.c) — 写 RM_ONOFF 逻辑
- [app_process.c](../remote_mic_tx_coex/code/app_process.c) — Timer 触发写 + RM 切换

---

## 第二阶段：添加 DMIC 输入

### 原始状态
- TX 只支持 `SPI_RX_CODED_INPUT`、`SPI_RX_RAW_INPUT`、`PCM_RX_RAW_INPUT`
- 无 DMIC 相关定义和初始化代码

### 改动（参考 `remote_mic_tx_raw` 工程）
1. **app.h**：新增 `DMIC_RX_RAW_INPUT 4`，`INPUT_INTRF` 默认 DMIC，DMIC DMA 配置（`DMA_SRC_DMIC`），硬件宏（`DMIC_CFG`、`DMIC_AUDIO_CFG`、`DECIMATE_BY_200`），`SUBFRAME_LENGTH=64`

2. **app_init.c**：DMIC 初始化（AUDIOCLK `/5`=3.2MHz、DMIC DIO、DMIC audio config、DSP IRQ、DMIC DMA），**关键是 `Sys_DMA_ChannelEnable(RX_DMA_NUM)` 必须在 DMIC 分支显式调用**

3. **app_func.c**：
   - 新增 `dmic_buffer_in[]` 和 `Port_rx_dmic_dma_isr()`（解包 32-bit DMIC 数据→左右声道→直送 encoder queue，**跳过 ASRC**）
   - ASRC/ASCC 代码加 `#if != DMIC` 保护
   - DSP encoder pipeline（queue、LPDSP32 编码、tx_data_fifo）对所有 raw 模式共享

4. **rm_app.c**：`RM_Callback_TRX` 的 `#if` 保护扩展为包含 `DMIC_RX_RAW_INPUT`

### 关键 Bug
| Bug | 现象 | 修复 |
|-----|------|------|
| DMIC DMA 未 enable | DMIC 无数据 | `Sys_DMA_ChannelEnable(RX_DMA_NUM)` 被 `#if != DMIC` 包住 → 移出 |
| RM_Callback_TRX 不服务 DMIC | RM 链路通但无音频 | `Read_buffer()` 调用被 `#if SPI/PCM` 包住 → 扩展条件 |
| AccessWord 溢出 | 编译 warning | `0xF200CDE629` → `(0x00cde629 \| (0xf2 << 24))` |

### 涉及文件
- [app.h](../remote_mic_tx_coex/include/app.h)
- [app_init.c](../remote_mic_tx_coex/code/app_init.c)
- [app_func.c](../remote_mic_tx_coex/code/app_func.c)
- [rm_app.c](../remote_mic_tx_coex/code/rm_app.c)

---

## 第三阶段：RM 时序调试

### 问题演进

| 阶段 | 现象 | 根因 | 修复 |
|------|------|------|------|
| 1 | Sleep 收到 01 后重启 | TX 写 01 后 Timer 里等 `APPM_CONNECTED`，但 Sleep 立即 `RF_SwitchToCPMode` 导致 BLE 断开，`ble_env.state` 变 `APPM_READY`，RM 切换被跳过 | RM 切换逻辑移到 `GATTC_CmpEvt` 写完成回调 |
| 2 | BLE 断开后 `cs_env.state` 被清 | `BLE_SetServiceState(false)` 无条件 `cs_env.state = CS_INIT` | 加保护：`CS_PEER_CONFIGURED` 时不重置 |
| 3 | 连接后 164ms 断开 | 连接间隔 7.5ms 太密 | 改为 30ms（匹配调试助手） |
| 4 | 连接后立刻断 | Supervision timeout 300（3s）与调试助手 72（720ms）不匹配 | `CON_SUP_TIMEOUT` 改为 72 |
| 5 | RM 播一下就断 | `audio_streaming` 未设 1，重连计时器在 RM 运行时发 BLE 连接干扰 | `RM_Enable` 后设 `app_env.audio_streaming = 1` |
| 6 | 调试过程中反复重启 | 电池读取 `send_batt_req` 在写 01 后仍向 Sleep 发 GATT 请求 | `cs_env.state >= CS_CONFIGURING` 时停止 |

### 最终时序参数

| 参数 | 值 |
|------|-----|
| 连接间隔 | 24~32（30~40ms） |
| Supervision timeout | 72（720ms） |
| 连接后写 01 延迟 | 1s（5×200ms timer） |
| 写完切 RM 延迟 | 1s（5×200ms timer） |
| 断线重连延迟 | 1s（5×200ms timer，RM 活跃时跳过） |
| 重连守卫 | `DirectConnect` 入口 + timer 层双重检查 `ble_env.state != APPM_CONNECTING`，防止打断正在进行的连接 |

### 最终 BLE 连接参数

```
#define CON_INTERVAL_MIN    24      // 30ms
#define CON_INTERVAL_MAX    32      // 40ms
#define CON_SLAVE_LATENCY   0
#define CON_SUP_TIMEOUT     72      // 720ms
```

---

## 最终完整流程

```
上电
  → 硬件初始化（DMIC、DSP encoder flash copy、DSP message setup）
  → BLE_Initialize + App_Env_Initialize
  → APP_RM_Init（配置 RM 参数）
  → RF_SwitchToBLEMode
  → DirectConnect（GAPM_CONNECTION_DIRECT, MAC: 09 80 00 09 12 00）

BLE 连接建立
  → 电池服务发现
  → Custom Service 发现（UUID: e093f3b5-...）
  → 找到 RM_ONOFF characteristic → CS_ALL_ATTS_DISCOVERED

Timer（200ms）:
  ┌─ CS_ALL_ATTS_DISCOVERED → 等 1s → 写 0x01
  ├─ 写完成 → CS_PEER_CONFIGURED（GATTC_CmpEvt）
  ├─ CS_PEER_CONFIGURED → 等 1s → RF_SwitchToCPMode + RM_Enable
  │   └─ app_env.audio_streaming = 1，停电池读、停重连
  └─ APPM_READY（断线）→ 等 1s → DirectConnect（RM 活跃时跳过；APPM_CONNECTING 守卫防打断）

RM 运行中
  → DMIC → DMA → 解包 → queue → LPDSP32 G.722 编码 → tx_data_fifo
  → RM_Callback_TRX → Read_buffer → RM 协议发送
```

---

## Sleep 工程改动

仅一处：[app.h](../peripheral_server_sleep/include/app.h) 打开 `#define DEBUG_UART_ENABLE`（调试用，可关闭）。

---

## 关键经验

1. **BLE 连接参数要匹配对端** — 间隔不能太密，timeout 参考调试助手的值
2. **BLE 断线会清状态** — `BLE_SetServiceState(false)` 会重置 `cs_env.state`，需要保护关键状态
3. **radio 不能同时跑 BLE 和 RM** — 顺序切换，RM 跑起来后禁止 BLE 重连
4. **DMIC DMA 必须显式 enable** — `DMA_RX_CONFIG` 中的 `DMA_ENABLE` 位不够
5. **RM 回调的输入保护要覆盖所有 input 类型** — `#if` 条件漏了 DMIC 导致无声

---

## 第四阶段：双设备顺序连接

### 目标

TX 先后连接两个 Sleep 设备，分别发 RM_ONOFF，都就绪后切 RM。

### 架构设计

**顺序连接**（初版）：先连 peer 0 走完完整流程，断连后再连 peer 1。两个都 CS_PEER_CONFIGURED 后切 RM。

**环境结构**：
- `ble_env` — 保持单实例（GAPM 层全局状态）
- `cs_env[PEER_COUNT]` — 每个 peer 独立的状态机
- `current_peer` — 当前正在连接的 peer 索引（0 或 1）

### MAC 地址

```c
#define PEER_COUNT 2
#define SLEEP_BD_ADDRESS_0  { 0x09, 0x80, 0x00, 0x09, 0x12, 0x00 }  // 00 12 09 00 80 09
#define SLEEP_BD_ADDRESS_1  { 0x07, 0x7b, 0x00, 0xbf, 0xc0, 0x60 }  // 60 c0 bf 00 7b 07
```

### 关键 Bug：conidx 复用

BLE 栈顺序连接时复用 `conidx`（peer 0 断开后 peer 1 也用 conidx=0）。GATT handler 里用 `current_peer` 而非 `KE_IDX_GET(src_id)` 索引 `cs_env[]`。

### 涉及文件

- [ble_std.h](../remote_mic_tx_coex/include/ble_std.h)、[ble_std.c](../remote_mic_tx_coex/code/ble_std.c)
- [ble_custom.h](../remote_mic_tx_coex/include/ble_custom.h)、[ble_custom.c](../remote_mic_tx_coex/code/ble_custom.c)
- [app_process.c](../remote_mic_tx_coex/code/app_process.c)

---

## 第五阶段：同时双连接（替换顺序连接）

### 目标

**两个从机同时保持 BLE 连接**，不再连一个断一个。

### 架构改动

新增 per-peer 连接跟踪：

```c
// ble_std.h
extern uint8_t peer_conidx[PEER_COUNT];     // peer → BLE conidx 映射
extern bool   peer_ble_connected[PEER_COUNT]; // 每个 peer 的 BLE 连接状态
extern uint8_t ConidxToPeer(uint8_t conidx); // conidx → peer 反向映射
```

**关键修改**：

| 文件 | 改动 |
|------|------|
| [ble_std.c](../remote_mic_tx_coex/code/ble_std.c) | `GAPC_ConnectionReqInd`：存 per-peer conidx，不覆盖已有连接；`GAPC_DisconnectInd`：只在该 peer 断开时处理，全部断开才设 `APPM_READY`；`BLE_SetServiceState` 用 `ConidxToPeer` 索引 `cs_env` |
| [ble_custom.c](../remote_mic_tx_coex/code/ble_custom.c) | GATT handler 用 `ConidxToPeer(KE_IDX_GET(src_id))` 替代 `current_peer` 索引 `cs_env[]` |
| [app_process.c](../remote_mic_tx_coex/code/app_process.c) | peer0 发现完毕后立即 `DirectConnect(1)`（保持 peer0 连接）；RM_ONOFF 写操作用 `peer_conidx[i]` 指定正确的 BLE 连接；断线重连遍历所有 peer |

### 流程

```
boot → DirectConnect(0) → conidx=0 连 peer0
  → 服务发现完毕 → DirectConnect(1)（peer0 保持连接！）
  → conidx=1 连 peer1 → 服务发现完毕
  → 写 RM_ONOFF 到两个 peer（通过各自 conidx）
  → 都 CS_PEER_CONFIGURED → 切 RM
```

---

## 第六阶段：DMIC 音频检测

### 目标

有音频输入才启动 RM，没音频时不触发。

### 检测架构

**计算在 ISR，判决在 Timer**（ISR 只做纯算术避免 hard fault）：

| 层 | 位置 | 职责 |
|----|------|------|
| ISR | [app_func.c](../remote_mic_tx_coex/code/app_func.c) `Port_rx_dmic_dma_isr()` | 计算左右声道绝对值和，写入 `audio_energy_left/right` |
| Timer | [app_process.c](../remote_mic_tx_coex/code/app_process.c) 200ms tick | 读能量 → EMA 低通滤波 → 阈值比较 → 连续计数 → 置 `ad_detected` |

### 滤波参数

```
#define AUDIO_ENERGY_THRESHOLD    5000   // 安静时能量 ~75，阈值 ≈ 66x 裕度
#define AUDIO_DETECT_CONSEC_CNT   3      // 连续 3 tick (600ms) 超阈值判定为有音频
```

**EMA 低通滤波**：α = 1/16（右移 4 位），平滑 DMIC 初始化瞬态尖峰（实测 69030 单次尖峰 → ema 峰值 4384，不触发）

### 检测条件

```c
if (peer_ble_connected[0] || peer_ble_connected[1]
    || app_env.audio_streaming)
```

BLE 连接期间和 RM 推流期间都持续检测。RM 断连后 BLE 重连，检测继续。

### 门控策略

- **RM_ONOFF 写**：卡 `ad_detected`（没音频不写）
- **RM 推流启动**：卡 `ad_detected` + 双 peer 都配置完
- 双连接保持不受影响（BLE 稳定在线等待音频）

### 完整流程

```
boot → BLE 双连接 peer0 + peer1
  → 服务发现完毕
  → [等 DMIC 检测到音频 — EMA 滤波，阈值 5000，连续 600ms]
  → 写 RM_ONOFF 到两个从机
  → 都配置完 → 启动 RM 推流
  → RM 推流期间 BLE 自然断开，DMIC 检测持续运行
```

### 涉及文件

- [app.h](../remote_mic_tx_coex/include/app.h) — `AUDIO_ENERGY_THRESHOLD`、`AUDIO_DETECT_CONSEC_CNT`、`extern audio_energy_*`
- [app_func.c](../remote_mic_tx_coex/code/app_func.c) — DMIC ISR 能量计算
- [app_process.c](../remote_mic_tx_coex/code/app_process.c) — 音频检测状态机 + 门控逻辑

---

## 当前完整架构

```
上电
  → 硬件初始化（DMIC、DSP encoder flash copy）
  → BLE_Initialize + App_Env_Initialize + APP_RM_Init
  → DirectConnect(0)

BLE 双连接（同时在线）
  → peer0 服务发现完毕 → DirectConnect(1)
  → peer1 服务发现完毕

Timer（200ms）:
  ├─ DMIC 音频检测（EMA + 阈值 + 连续计数）
  ├─ 检测到音频 → 写 RM_ONOFF 到两个 peer（各 1s 延迟）
  ├─ 两个都 CS_PEER_CONFIGURED → 1s → 切 RM
  └─ 断线自动重连（RM 活跃时跳过；APPM_CONNECTING 守卫防打断）

RM 运行中
  → DMIC → DMA → 解包 → queue → LPDSP32 G.722 编码 → tx_data_fifo
  → RM_Callback_TRX → Read_buffer → RM 协议发送
  → DMIC 检测持续运行（条件含 audio_streaming）
```

---

## 更新后的关键经验

1. **BLE 连接参数要匹配对端**
2. **RSL10 支持 BLE 多连接**（参考 `ble_central_client_bond` demo），conidx 按连接顺序分配
3. **radio 不能同时跑 BLE 和 RM** — 互斥，RM 启动后 BLE 自然断
4. **ISR 里只做纯计算，状态机放 task 上下文** — 避免 hard fault
5. **DMIC 瞬态噪峰用 EMA 滤波** — 单次尖峰 69030 在 α=1/16 下不会触发
6. **GATT handler 用 conidx→peer 反向映射** — 多连接下事件路由正确
7. **DMIC 检测门控放 RM_ONOFF 和 RM 推流两层** — 从机没收到 RM_ONOFF 就不会切 RM 接收模式

---

## 第七阶段：调试日志 + 单设备双连接 bug

### 新增日志

| 位置 | 输出 |
|------|------|
| `GAPC_ConnectionReqInd` (TX) | `intv=32(40.0ms) lat=0 sup=72(720ms)` — 协商的连接参数 |
| `GAPC_DisconnectInd` (TX) | `reason=0x08` — 断开原因码 |
| `GATTC_DiscCharInd` | `__ALL_ATTS_DISCOVERED peer=0 conidx=0` — 服务发现完成 |
| `APP_Timer` 重连 | `__RECONNECT peer=1 (state=6)` — 重连触发时的状态 |

### 断连原因码速查

| reason | 含义 |
|--------|------|
| `0x08` | 连接超时（supervision timeout） |
| `0x13` | 远端主动断开 |
| `0x16` | 本地主动断开 |
| `0x3E` | 连接未完成就断了 |

### 单设备测试发现的 bug

**问题 1：reconnect_cnt 共享**

`reconnect_cnt` 是单例 static，peer 0 连接期间每 200ms 检查 peer 1 → `reconnect_cnt++`。累计 5 次后触发 `DirectConnect(1)` 去连不存在的设备。peer 0 断开后 reconnect_cnt 已到 4，只需再 1 tick 就重连（本该等 1s）。

**修复**：`reconnect_cnt` 改为 `reconnect_cnt[PEER_COUNT]` 数组，per-peer 独立计数。

**问题 2：PEER_COUNT=2 硬编码访问越界**

多处 `peer_ble_connected[0] \|\| peer_ble_connected[1]` 在 PEER_COUNT 变化时会越界。

**修复**：改为 for 循环遍历 `p < PEER_COUNT`。`peer_macs` 数组初始化加 `#if PEER_COUNT > 1` 保护。Step 0 加 `#if PEER_COUNT > 1` 保护。

**问题 3：GAPC_DisconnectInd 重启定时器打乱 delay_cnt**

任一 peer 断开时 `ke_timer_set` 重启 200ms 定时器，导致其他 peer 的 `delay_cnt` 计数节奏被打乱。peer 0 delay_cnt 先到 4，peer 1 断开后定时器重装，peer 0 从头等，最终 peer 1 先到 5 写 RM_ONOFF，peer 0 永远到不了 → `configured_count=1 < PEER_COUNT(2)` → RM 不启动。

**修复**：删除 `GAPC_DisconnectInd` 中的 `ke_timer_set`。200ms 定时器在 `APP_Timer` 顶部自己重装，不需要断开时干预。

### 涉及文件

- [app_process.c](../remote_mic_tx_coex/code/app_process.c) — reconnect_cnt 数组化、for 循环通用化
- [ble_std.c](../remote_mic_tx_coex/code/ble_std.c) — 连接/断开日志、for 循环通用化、删除 ke_timer_set
- [ble_std.h](../remote_mic_tx_coex/include/ble_std.h) — `#if PEER_COUNT > 1` 保护
- [ble_custom.c](../remote_mic_tx_coex/code/ble_custom.c) — 服务发现完成日志、PRINTF 修复

---

## 第八阶段：RFX2401C FEM 控制（已移除，待后续开发）

### 硬件连接

- RFX2401C：2.4GHz FEM，集成 PA + LNA，单天线端口 TR 切换
- DIO11 → TXEN（PA enable），DIO10 → RXEN（LNA enable）
- 真值表：TXEN/RXEN 不能同时为高（非法状态）

### 验证过程与结论

| 测试 | TXEN | RXEN | 结果 |
|------|------|------|------|
| 共存中断切 TX/RX | ISR 按 `BLE_TX`/`BLE_RX` 切 | ISR 按 `BLE_TX`/`BLE_RX` 切 | **失败** — 全部 reason=0x3E |
| RXEN 常开 | LOW | HIGH | 能收到 CONNECT_IND，TX 无 PA 发不出去 |
| **TXEN 常开** | **HIGH** | **LOW** | **成功** — BLE 全链路通，RM 启动 |

### 失败原因

共存中断 ISR 的 GPIO 响应延迟 2-5μs，对 RFX2401C 够用（芯片切换 ~1μs），但 BLE 的 TX↔RX 槽切换窗口仅 150μs，且 ISR 在射频起止边缘才触发，无提前量。PA/LNA 不能在槽切换前就位，导致包头部丢失 → 连接建立失败（0x3E）。

### 可行方案

用 `BLE_IN_PROCESS`（非 TX/RX 细粒度）控制 TXEN，整个 BLE 连接事件窗口 PA 常开。RXEN 不用（近场信号够强不需要 LNA）。RM 推流 TXEN 常开。

代码已从当前分支移除，详见 commit history 或 `app.h/app_init.c/app_func.c/app_process.c/rm_app.c` 中 `SIGNAL_BOOST` 相关的 commit。

### 后续开发方向

1. **硬件级别 GPIO 控制**：RSL10 `DIO_RF_GPIO03_SRC`/`DIO_RF_GPIO47_SRC` 可能支持 BBIF 信号直接映射到 DIO，消除 ISR 延迟
2. **用 BLE_GROSSTGTIM 提前开**：粗定时器在连接事件前 ~100μs 触发，可提前使能 FEM
3. **调 BLE 连接参数**：增大连接间隔、减小 supervision timeout 裕度，降低对切换速度的敏感度

---

## 第九阶段：MAC 地址更新

```c
// old (commented out):
// SLEEP_BD_ADDRESS_0 = 60:c0:bf:00:76:91
// SLEEP_BD_ADDRESS_1 = 60:c0:bf:00:76:76
// new:
#define SLEEP_BD_ADDRESS_0  { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB }  // AB:89:67:45:23:01
#define SLEEP_BD_ADDRESS_1  { 0x09, 0x80, 0x00, 0x09, 0x12, 0x00 }  // 00:12:09:00:80:09
```

BLE 地址在 C 代码中是小端序，所以倒序书写。

### 涉及文件

- [ble_std.h](../remote_mic_tx_coex/include/ble_std.h)

---

## 第十阶段：Sleep BLE→RM 直切

### 原理

TX 发送 CONNECT_IND → Sleep 在 `GAPC_ConnectionReqInd` 中检测对端 MAC → 匹配 TX 则跳过 BLE 握手（不发 `gapc_connection_cfm`），直接切 RM 模式。

跳过了 service discovery + GATT write RM_ONOFF 全套 BLE 流程。

### Sleep 改动

`GAPC_ConnectionReqInd` 新增 MAC 检测（逐字节比较，不用 `memcmp`）：

```c
uint8_t tx_mac[BDADDR_LENGTH] = TX_BD_ADDRESS;
if (tx_mac[0] == param->peer_addr.addr[0] && ...)
{
    app_env.tx_connect_detected = 1;
    ble_env.state = APPM_READY;
    Advertising_Start();
    return;  // 不发送 gapc_connection_cfm
}
```

main loop 检测 `tx_connect_detected` → `APP_RM_Init` + `RM_Enable(500)`。

30s 超时退回 BLE 逻辑保持：`rm_timeout_ticks = RM_TIMEOUT_TICKS(150)`，每 200ms 减 1。

### TX MAC 地址

```c
#define TX_BD_ADDRESS { 0xFB, 0x6E, 0x84, 0xBF, 0xC0, 0x60 }  /* 60:C0:BF:84:6E:FB */
//#define TX_BD_ADDRESS { 0x14, 0x6A, 0x84, 0xBF, 0xC0, 0x60 }  /* 60:C0:BF:84:6A:14 */
```

### 涉及文件

- [app.h](../peripheral_server_sleep/include/app.h) — `TX_BD_ADDRESS`、`tx_connect_detected` 标志
- [ble_std.c](../peripheral_server_sleep/code/ble_std.c) — GAPC_ConnectionReqInd MAC 检测
- [app.c](../peripheral_server_sleep/app.c) — main loop tx_connect_detected 处理

---

## 第十一阶段：TX 简化 — 直接发 CONNECT_IND + CANCEL + RM

### 原理

TX 不再走 BLE 服务发现、GATT 写入等流程。Boot 后直接向两个 Sleep MAC 发送 CONNECT_IND，然后切 RM。

由于 GAPM 一次只能处理一个连接请求，按顺序发送：
peer 0 → 600ms → CANCEL → 等状态变 READY → peer 1 → 600ms → CANCEL → RM START。

CANCEL 通过 `GAPM_CancelCmd` 发送，`GAPM_CmpEvt(GAPM_CANCEL)` 中判断非扫描态则置 `APPM_READY`。

### 关键发现

- **GAPM 不超时**：对不在线设备发送 CONNECT_IND，GAPM 永不主动超时返回
- **GAPM_CANCEL 可用**：CANCEL 命令触发 `GAPM_CmpEvt(CANCEL)`，用于强制退出 CONNECTING 状态
- **不能同时发两个**：GAPM 同时只能处理一个连接请求
- **nb_peers 不支持**：`gapm_start_connection_cmd.nb_peers > 1` 在 RSL10 stack 上不工作

### Timer 核心逻辑

```c
if (phase < PEER_COUNT) {
    if (cancelling) {
        if (ble_env.state != APPM_CONNECTING)
            phase++, cancelling = 0, tick = 0;
    } else if (tick == 0) {
        DirectConnect(phase);
        tick = 1;
    } else if (++tick >= 3) {  // 600ms
        // send GAPM_CANCEL
        cancelling = 1;
        tick = 0;
    }
} else if (ble_env.state != APPM_CONNECTING) {
    // RM START
}
```

### TX 改动汇总

- `DirectConnect` 移除 `APPM_CONNECTING` 保护
- `GAPM_CmpEvt` 新增 `GAPM_CANCEL` 处理（非扫描态设 READY）
- 删除 Step 0-3（服务发现、RM_ONOFF 写入、configured_count）
- `reconnect_cnt` 数组化，per-peer 独立计数
- `GAPC_DisconnectInd` 删除 `ke_timer_set` 重启定时器
- 连接/断连检测用 for 循环替代硬编码 `[0]||[1]`

### 涉及文件

- [app_process.c](../remote_mic_tx_coex/code/app_process.c) — 简化为 CANCEL + RM 流程
- [ble_std.c](../remote_mic_tx_coex/code/ble_std.c) — CANCEL 处理、DirectConnect 去保护、MAC 比对 peer 识别
- [ble_std.h](../remote_mic_tx_coex/include/ble_std.h) — SLEEP_BD_ADDRESS 多组注释

---

## 第十二阶段：TX 音频门控 — BLE 待机 → 有音频才唤醒

### 原理

Phase 11 的 CONNECT_IND+CANCEL+RM 在 boot 后无条件触发，但 TX 开机时 Sleep 不一定需要被唤醒（Sleep 冷启已在 RM 搜索模式，30s 窗口）。真正需要 CONNECT_IND 的场景是：**Sleep 已超时退到 BLE 省电模式，TX 检测到 DMIC 音频后需要快速唤醒 Sleep**。

CONNECT_IND 本质是**单向唤醒信号**：
- TX 用 RFX2401C PA 强化发送 → Sleep 收到 CONNECT_IND
- Sleep 识别 TX MAC → 直接切 RM（跳过 BLE 握手，Phase 10）
- TX **收不到** Sleep 回复（RFX2401C 无 LNA 强化接收，非对称链路）
- BLE 连接注定建立失败

因此 CONNECT_IND 不是建立连接，只是一个"敲门"信号。

### TX 状态机

```
TX_BLE_IDLE ──[ad_detected]──▶ TX_CONNECTING ──[CANCEL 序列完成]──▶ TX_RM_ACTIVE
     ▲                              │                                      │
     │                              │ 音频中途消失                          │
     │                              └──▶ TX_BLE_IDLE                       │
     │                                                                     │
     └──────────────────────[ad_lost]──────────────────────────────────────┘
```

| 状态 | 行为 | 功耗 |
|------|------|------|
| `TX_BLE_IDLE` | BLE 主机模式待机，DMIC 持续检测音频 | 低功耗 |
| `TX_CONNECTING` | 串行发 CONNECT_IND 到两个 peer（各 600ms + CANCEL） | BLE 活跃 |
| `TX_RM_ACTIVE` | RM 推流，持续检测音频丢失 | RM 活跃 |

### 音频检测

- **检测到音频**：EMA 能量 > `AUDIO_ENERGY_THRESHOLD(5000)` 连续 3 tick（600ms）→ `ad_detected`
- **音频丢失**：能量 < 阈值连续 15 tick（3s）→ `ad_lost`
- 音频检测**始终运行**（去掉 `any_connected || audio_streaming` 守卫），`BLE_IDLE` 和 `RM_ACTIVE` 都需要

### 状态转换详情

1. **BLE_IDLE → CONNECTING**：`ad_detected` 触发，重置 phase/tick/cancelling
2. **CONNECTING**：沿用 Phase 11 的 phase/tick/cancelling 逻辑，串行两个 peer
   - 序列完成时检查音频是否还在（`ad_detected && !ad_lost`）→ 还在则切 RM，已消失则回 BLE_IDLE
3. **RM_ACTIVE → BLE_IDLE**：`ad_lost` 触发 → `RM_Disable()` → `RF_SwitchToBLEMode()` → 重开 BLE 中断 → 清音频标志 → 回 BLE_IDLE

### 删除的逻辑

- **reconnect 段**：BLE 连接不存在，CONNECT_IND 发送统一由音频检测驱动
- **phase 无条件触发**：CONNECT_IND 只在 `TX_CONNECTING` 状态下执行，不再 boot 后无条件跑

### 涉及文件

- [app.h](../remote_mic_tx_coex/include/app.h) — 新增 `AUDIO_LOST_CONSEC_CNT 15`
- [app_process.c](../remote_mic_tx_coex/code/app_process.c) — TX 状态机 + 音频门控 + 拆除 reconnect 段
