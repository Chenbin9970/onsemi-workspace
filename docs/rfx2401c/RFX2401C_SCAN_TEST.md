# RFX2401C 扫描距离测试报告

## 测试环境

- **测试日期**：2026-08-04
- **扫描程序**：TX 主机扫描程序（纯扫描，不连接）

### 扫描端（烧录 TX 程序）

| 编号 | 硬件 | RFX2401C | 备注 |
|------|------|----------|------|
| S1 | RSL10 开发板 | 无 | 内部 radio |
| S2 | Smart1604 充电盒蓝牙空板 | 有 | IO10=RXEN, IO11=TXEN，扫描时开 RX 使能 |

### 广播端（sleep 设备）

| 编号 | 硬件 |
|------|------|
| B1 | Smart1604 机身 |
| B2 | Smart1654 机身 |
| B3 | Smart1654 开发板 |

## 扫描程序配置

- IO10 → RXEN（LNA enable），IO11 → TXEN（PA enable）
- 开机默认 RX 模式（RXEN=HIGH, TXEN=LOW），持续 BLE 主动扫描
- 匹配方式：MAC 地址匹配（6 个 peer）
- 扫描超时后自动重启扫描

## TX 功率参数

| 位置 | 参数 | 值 |
|------|------|-----|
| `app.h:65` | `OUTPUT_POWER_6DBM` | `1`（启用） |
| `app_init.c:402` | `Sys_RFFE_SetTXPower(6)` | BLE 初始化后设 6dBm |
| `app_process.c:208` | `Sys_RFFE_SetTXPower(6)` | 进入 RM 推流时设 6dBm（受 `OUTPUT_POWER_6DBM` 宏控制） |
| `app_init.c:73` | `VDDPA_ENABLE` | `VDDPA_DISABLE_BITBAND`（内部 PA 关，使用外部 RFX2401C PA） |
| `app_init.c:74` | `VDDPA_SW_CTRL` | `VDDPA_SW_VDDRF_BITBAND` |

> S2（带 PA）：RSL10 内部 TX 功率 6dBm 作为 RFX2401C PA 输入。S1（无 PA）：内部 radio 直接输出 6dBm。

## 测试结果

| 扫描端 | 广播端 | 最大可扫描距离 |
|--------|--------|-------------|
| S1 RSL10 开发板（无 PA） | B1 Smart1604 机身 | ~8m |
| S1 RSL10 开发板（无 PA） | B2 Smart1654 机身 | ~5m |
| S1 RSL10 开发板（无 PA） | B3 Smart1654 开发板 | ~20m |
| S2 Smart1604 空板（有 PA，RX 使能） | B3 Smart1654 开发板 | ~23m |
| S2 Smart1604 空板（有 PA，RX 使能） | B1 Smart1604 机身 | ~11m |
| S2 Smart1604 空板（有 PA，RX 使能） | B2 Smart1654 机身 | ~8m |

## 备注

- 纯扫描模式，不发起连接
- 扫描到设备后仅打印 MAC，不停止扫描

---

## TX 发送测试

### 测试配置

- **发送端**：烧录 TX 程序，开机默认 RM TX 模式（TXEN=HIGH, RXEN=LOW）
- **接收端**：sleep 设备

### 测试结果

| 发送端 | 接收端 | 条件 | 结果 |
|--------|--------|------|------|
| S1 RSL10 开发板（无 PA） | B3 Smart1654 开发板 | 穿墙，预研办公室 | ~15m |
| S1 RSL10 开发板（无 PA） | B3 Smart1654 开发板 | 空旷场地 | ~56m |
| S1 RSL10 开发板（无 PA） | B1 Smart1604 机身 | 办公区无遮挡 | ~10m |
| S1 RSL10 开发板（无 PA） | B1 Smart1604 机身 | 空旷场地 | ~22m |
| S1 RSL10 开发板（无 PA） | B2 Smart1654 机身 | 办公区无遮挡 | ~10m |
| S1 RSL10 开发板（无 PA） | B2 Smart1654 机身 | 空旷场地 | ~20m |
| S2 Smart1604 空板（有 PA，TX 使能） | B1 Smart1604 机身 | 穿墙，研发办公室→厕所 | 通过 |
| S2 Smart1604 空板（有 PA，TX 使能） | B2 Smart1654 机身 | 穿墙，研发办公室→厕所 | 通过 |
| S2 Smart1604 空板（有 PA，TX 使能） | B3 Smart1654 开发板 | 穿墙，研发办公室→厕所 | 通过 |

> Smart1604/Smart1654 机身不支持穿墙（指 S1 无 PA 发送时）；S2 有 PA 时三个设备均支持穿墙。
