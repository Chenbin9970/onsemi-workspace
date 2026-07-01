# BLE 配置

> 所有 BLE 协议层参数：广播、连接、GATT 服务、特征值。定义位置主要在 `include/ble_std.h` 和 `include/ble_custom.h`。

---

## 一、广播参数 (`ble_std.h`)

| 参数 | 值 | 说明 |
|------|-----|------|
| 广播信道 | 37, 38, 39 (`0x07`) | 全信道 |
| 广播模式 | Connectable | 可连接非定向 |
| 广播间隔 | **10.24s** (16384 × 0.625ms) | BLE 规范上限，最低功耗 |
| 设备名称 | `"cbtest"` | 广播数据中可见 |
| 地址类型 | Public | 固定地址，便于识别 |

---

## 二、连接参数 (`ble_std.h`)

| 参数 | 值 | 说明 |
|------|-----|------|
| 最小连接间隔 | **500ms** (400 × 1.25ms) | 很长，让 CPU 能深度睡眠 |
| 最大连接间隔 | **500ms** (400 × 1.25ms) | 与最小值相同 |
| Slave Latency | **19** | 最多跳过 19 个连接事件 |
| 监督超时 | **32s** (3200 × 10ms) | BLE 规范上限 |
| 有效通信间隔 | **10s** | 500ms × (1 + 19) = 10s |
| 配对 | **禁用** | `GAPM_PAIRING_DISABLE` |

### 连接参数更新

`GAPC_ParamUpdateReqInd` 中 **`cfm->accept = 0`** — 拒绝主机端修改连接参数，强制使用上述参数。

---

## 三、GATT 服务 & 特征值

### 3.1 Battery Service (标准)

| 属性 | 值 |
|------|-----|
| UUID | `0x180F`（标准） |
| 特征值 | Battery Level (Notify) |
| 实现文件 | `code/ble_bass.c` |
| 测量方式 | ADC Channel 0 (VBAT/2)，16 样本滑动平均 |
| 量程 | 1.1V (0%) ~ 1.4V (100%) |

### 3.2 Custom Service (私有)

| 属性 | 值 |
|------|-----|
| UUID | `24dc0e6e-0140-ca9e-e5a9-a300b5f393e0` (128-bit) |
| 实现文件 | `code/ble_custom.c` |

**TX_VALUE** (UUID `...02`)：

| 属性 | 值 |
|------|-----|
| 权限 | Read + Notify |
| 最大长度 | 20 bytes |
| 通知长度 | 5 bytes |
| 通知间隔 | 每 10 个 sleep cycle |
| 行为 | 周期性发送模拟变化值 |

**RX_VALUE** (UUID `...03`)：

| 属性 | 值 |
|------|-----|
| 权限 | Read + Write + Write Command |
| 最大长度 | 20 bytes |
| 行为 | 接收主机写入的数据 |

### 3.3 RM 控制特征值 (`APP_RM_ENABLE` 启用时)

| 特征值 | UUID (`...XX`) | 权限 | 功能 |
|--------|---------------|------|------|
| ON_OFF | `...04` | RD/WR | 写 1 启动 RM，写 0 停止 |
| VOLUME | `...05` | RD/WR | 音量控制 (1 byte) |
| CHANNEL_SIDE | `...06` | RD/WR | 左/右声道 (0=左, 1=右) |

---

## 四、BLE 状态机

```
APPM_INIT → APPM_CREATE_DB → APPM_READY → APPM_ADVERTISING
                                              │
                                         [连接请求]
                                              │
                                              ▼
                                        APPM_CONNECTED
                                              │
                                         [断开连接]
                                              │
                                              ▼
                                        APPM_ADVERTISING
```

---

## 五、关键配置对比：Demo vs 当前

| 参数 | 原始 Demo | 当前 | 功耗影响 |
|------|----------|------|----------|
| 广播间隔 | 40ms | **10.24s** | 广播功耗降为 1/256 |
| 连接间隔 | 10ms | **500ms** | CPU 有更多睡眠窗口 |
| Latency | 0 | **19** | 跳过 19 个事件才超时 |
| 超时 | 2s | **32s** | 容忍极长时间无通信 |
| 设备名 | 空 | **"cbtest"** | 可在扫描列表识别 |
| 地址类型 | Private | **Public** | 固定，不复用地址 |
| 供电 | Buck | **LDO** | 省掉 Buck 电感和开关损耗 |
| RTC 时钟 | XTAL32K | **RC_OSC** | 省掉外部 32K 晶振 |

---

## 六、相关文件

| 文件 | 作用 |
|------|------|
| `include/ble_std.h` | 广播、连接、设备名、地址等参数定义 |
| `code/ble_std.c` | BLE GAP/GATT 事件处理、状态机、连接参数拒绝 |
| `include/ble_custom.h` | Custom Service UUID、特征值定义 |
| `code/ble_custom.c` | Custom Service 读写处理、RM 控制特征值 |
| `include/ble_bass.h` | 电池服务参数 |
| `code/ble_bass.c` | 电池服务实现 |
