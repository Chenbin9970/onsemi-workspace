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
| 断线重连延迟 | 10s（RM 活跃时跳过） |

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
  └─ APPM_READY（断线）→ 等 10s → DirectConnect（RM 活跃时跳过）

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

## 第四阶段：双设备连接

### 目标

TX 先后连接两个 Sleep 设备，分别发 RM_ONOFF，都就绪后切 RM。

### 参考

`ble_central_client_bond` 多连接 demo：所有连接状态用数组 `[conidx]` 索引，`KE_IDX_GET(src_id)` 路由 GATT 事件。

### 架构设计

**顺序连接**：先连 peer 0 走完完整流程，断连后再连 peer 1。两个都 CS_PEER_CONFIGURED 后切 RM。

**环境结构**：
- `ble_env` — 保持单实例（GAPM 层全局状态）
- `cs_env[PEER_COUNT]` — 每个 peer 独立的状态机
- `basc_support_env[PEER_COUNT]` — 每个 peer 独立的电池服务
- `current_peer` — 当前正在连接的 peer 索引（0 或 1）

### MAC 地址

```c
#define PEER_COUNT 2
#define SLEEP_BD_ADDRESS_0  { 0x09, 0x80, 0x00, 0x09, 0x12, 0x00 }  // 00 12 09 00 80 09
#define SLEEP_BD_ADDRESS_1  { 0x07, 0x7b, 0x00, 0xbf, 0xc0, 0x60 }  // 60 c0 bf 00 7b 07
```

### 关键 Bug：conidx 复用

BLE 栈顺序连接时复用 `conidx`（peer 0 断开后 peer 1 也用 conidx=0）。GATT handler 里不能用 `KE_IDX_GET(src_id)` 作为 `cs_env[]` 索引——两个 peer 的 GATT 事件路由到同一个 `cs_env[0]`。

**修复**：GATT handler 里用 `current_peer` 索引 `cs_env[]`：
```c
uint8_t conidx = KE_IDX_GET(src_id);  // BLE API 调用用
uint8_t idx = current_peer;            // cs_env 索引用
cs_env[idx].state = CS_ALL_ATTS_DISCOVERED;
...
GATTC_DiscAllChar(conidx, ...);        // BLE API 用 conidx
```

### 完整流程

```
DirectConnect(0) → 连 MAC0 → 服务发现 → 1s → 写01
  → GATTC_CmpEvt: cs_env[0].state = CS_PEER_CONFIGURED
  → Timer: configured_count=1 → 主动断开 peer 0
  → GAPC_DisconnectInd → 1s → DirectConnect(1)

DirectConnect(1) → 连 MAC1 → 服务发现 → 1s → 写01
  → GATTC_CmpEvt: cs_env[1].state = CS_PEER_CONFIGURED
  → Timer: configured_count=2 → 1s → 切 RM
```

### 涉及文件

- [ble_std.h](../remote_mic_tx_coex/include/ble_std.h) — `PEER_COUNT 2`、两个 MAC、`current_peer` extern
- [ble_std.c](../remote_mic_tx_coex/code/ble_std.c) — `DirectConnect(idx)`、`BLE_SetServiceState` 用 `cs_env[conidx]`
- [ble_custom.h](../remote_mic_tx_coex/include/ble_custom.h) — `cs_env[PEER_COUNT]`
- [ble_custom.c](../remote_mic_tx_coex/code/ble_custom.c) — GATT handler 用 `current_peer` 索引
- [ble_basc.h](../remote_mic_tx_coex/include/ble_basc.h) — `basc_support_env[PEER_COUNT]`
- [ble_basc.c](../remote_mic_tx_coex/code/ble_basc.c) — 所有引用加 `[ble_env.conidx]`
- [app_process.c](../remote_mic_tx_coex/code/app_process.c) — timer 遍历双 slot、计数 `configured_count`
- [app.c](../remote_mic_tx_coex/app.c) — 电池读加 `[ble_env.conidx]`
