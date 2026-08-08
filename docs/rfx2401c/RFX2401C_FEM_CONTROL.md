# RFX2401C FEM 控制方案

## 硬件

- **芯片**：RFX2401C，2.4GHz FEM，集成 PA + LNA，单天线端口带内部 TR 开关
- **连接**：DIO11 → TXEN（PA enable），DIO10 → RXEN（LNA enable），高电平使能

### 真值表

| TXEN | RXEN | 模式 |
|------|------|------|
| 0 | 0 | 休眠，所有通路关 |
| 1 | 0 | TX 模式，PA 开 |
| 0 | 1 | RX 模式，LNA 开 |
| 1 | 1 | **非法**，不应使用 |

### 关键时序

- PA 开启时间：~1μs
- LNA 开启时间：~1μs
- TX→RX 切换时间：~1μs

---

## 验证记录

### 环境

- TX：RSL10 + RFX2401C
- Sleep：无 FEM，近场（桌面级距离）
- BLE 连接参数：interval 32（40ms），latency 0，supervision 72（720ms）

### 测试 1：共存中断切 TX/RX

**方案**：`BLE_COEX_RX_TX_IRQ` ISR 读 `BBIF_COEX_STATUS`，按 `BLE_TX`（bit 4）控 DIO11，按 `BLE_RX`（bit 0）控 DIO10。

**结果**：全部 `reason=0x3E`（连接建立失败），无一次成功交换数据包。

**原因**：共存中断在射频起止边缘才触发，ISR 延迟 2-5μs。BLE 连接事件内 TX↔接收在 150μs（T_IFS）里完成，ISR 的 GPIO 响应跟不上槽切换——PA/LNA 不能在射频开始前就位，包头部丢失。

### 测试 2：只开 RXEN

**方案**：TXEN=LOW，RXEN=HIGH 常开，测 LNA 接收通路。

**结果**：能收到 Sleep 的 CONNECT_IND 响应（LNA 通路 OK），但 TX 无 PA 发不出数据包 → 连接失败（0x3E）。

### 测试 3：只开 TXEN

**方案**：TXEN=HIGH，RXEN=LOW 常开，测 PA 发送通路。

**结果**：BLE 连接、服务发现、RM_ONOFF 写入、RM 推流全链路正常。证明：
- PA 通路工作正常
- 近场场景下，RX 通路即使无 LNA 也有足够信号强度
- 连接持续 16s+，RM 成功启动

---

## 方案对比

### 方案 A：ISR 控制（已验证可用）

用 `BLE_IN_PROCESS` 信号控制 TXEN：

```
BLE连接事件窗口 → IN_PROCESS=1 → ISR拉高TXEN
BLE空闲        → IN_PROCESS=0 → ISR拉低TXEN
RM推流         → 手动TXEN常高
```

RXEN 不用（近场不需要 LNA）。在整个窗口 PA 开，不在 TX↔RX 槽间切换。

**优点**：改动小，已验证
**缺点**：PA 在 RX 槽也开着，浪费少量功耗（连接事件仅 ~1ms/500ms = 0.2% 占空比，影响不大）；远场场景可能需 LNA

### 方案 B：硬件 GPIO 映射（待验证）

RSL10 的 `DIO_RF_GPIO03_SRC` / `DIO_RF_GPIO47_SRC` 寄存器可能支持 BBIF 共存信号直连 DIO，消除 ISR 软件延迟。需查阅 RSL10 数据手册确认 RF GPIO 可用的信号源列表。

**优点**：零延迟，硬件级别切换，可同时控制 TXEN 和 RXEN
**缺点**：需要确认硬件是否支持，可能需要特定 DIO 脚

### 方案 C：GROSSTGTIM 预触发

`BLE_GROSSTGTIM_IRQ` 在连接事件前 ~100μs 触发，在此 ISR 中预先拉高 TXEN。

**优点**：有足够提前量
**缺点**：无法区分 TX 还是 RX 槽，需配合共存中断做细粒度切换；增加软件复杂度

---

## 寄存器速查

```
BBIF_COEX_STATUS  @ 0x4000140C  (RO)
  bit 8  BLE_IN_PROCESS  整个射频窗口
  bit 4  BLE_TX          TX 忙
  bit 0  BLE_RX          RX 忙

BBIF_COEX_INT_CFG @ 0x40001410  (RW)
  bits 9:8   IN_PROCESS 触发沿  0=无 1=上升 2=下降 3=双边
  bits 5:4   TX 触发沿
  bits 1:0   RX 触发沿

BBIF_COEX_CTRL    @ 0x40001408  (RW)
  bit 4  TX  软件控制 TX 共存输出
  bit 0  RX  软件控制 RX 共存输出
```

### 中断配置示例

```c
// IN_PROCESS 双边沿触发
(*(volatile uint32_t *)0x40001410) = (0x3 << 8);

// 仅 TX 双边沿触发
// (*(volatile uint32_t *)0x40001410) = (0x3 << 4);

// IN_PROCESS + TX + RX 全触发
// (*(volatile uint32_t *)0x40001410) = (0x3 << 8) | (0x3 << 4) | (0x3 << 0);
```

### ISR 模板

```c
#define COEX_STATUS             (*(volatile uint32_t *)0x4000140C)
#define COEX_STATUS_IN_PROCESS  (1 << 8)
#define COEX_STATUS_TX          (1 << 4)
#define COEX_STATUS_RX          (1 << 0)

void BLE_COEX_RX_TX_IRQHandler(void)
{
    // 方案 A：整个窗口开 PA
    if (COEX_STATUS & COEX_STATUS_IN_PROCESS)
        TXEN = 1;
    else
        TXEN = RXEN = 0;

    // 方案 A 变体（远场）：TX/RX 独立控制
    // if (COEX_STATUS & COEX_STATUS_TX) TXEN = 1; else TXEN = 0;
    // if (COEX_STATUS & COEX_STATUS_RX) RXEN = 1; else RXEN = 0;
}
```

---

## 注意

- `rsl10_hw_nonCMSIS.h` 和 `rsl10_hw.h` 共用 `RSL10_HW_H` include guard，不能同时 include。如需共存寄存器，直接在代码中写地址或只 include nonCMSIS 版本
- DIO10/DIO11 可能与调试输出或 SPI/PCM 引脚冲突，使用前检查 `app.h` 的 DIO 分配
- RM 推流阶段 BLE 共存信号不触发，需手动控制 GPIO
