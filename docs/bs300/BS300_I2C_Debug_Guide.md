# BS300 I2C 驱动调试指南

## 1. 硬件连接

| RSL10 引脚 | BS300 引脚 | 功能 |
|-----------|-----------|------|
| DIO8 | SCL | I2C 时钟 |
| DIO7 | SDA | I2C 数据 |

- **I2C 上拉**: SCL/SDA 各需外部 4.7kΩ 上拉到 VCC
- **电压**: BS300 和 RSL10 需共地，VCC 一致
- **冲突注意**: DIO7/8 与 RM (SAMPL_CLK/SYNC_PULSE) 复用，BS300 通信期间不能开 RM

## 2. I2C 地址

| 项目 | 值 | 说明 |
|------|-----|------|
| 7-bit 从机地址 | `0x01` | `0b0000001` |
| 写地址 (bus byte) | `0x02` | `(0x01 << 1) \| 0` |
| 读地址 (bus byte) | `0x03` | `(0x01 << 1) \| 1` |

**易错点**: 代码里设的是 7-bit 地址 `0x01`，HAL 内部自动左移。手册里写的 `0x02` 是总线上的写地址字节，不是 7-bit 地址。

## 3. 软件 I2C 实现要点

### 3.1 GPIO 控制

```c
// SCL 始终输出，直接翻转
Sys_DIO_Config(SCL_PIN, DIO_MODE_GPIO_OUT_0);
Sys_GPIO_Set_High(SCL_PIN);  // 或 Set_Low

// SDA 在 INPUT (释放) 和 OUTPUT_0 (拉低) 间切换，模拟开漏
Sys_DIO_Config(SDA_PIN, DIO_MODE_GPIO_IN_0 | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);  // 释放
Sys_DIO_Config(SDA_PIN, DIO_MODE_GPIO_OUT_0);  // 拉低
```

### 3.2 关键时序

| 参数 | 值 | 说明 |
|------|-----|------|
| 数据位延时 | `delay_loop(25)` | ~25 次 WDT 刷新循环 |
| ACK 延时 | `delay_loop(125)` | **5x 数据位**，BS300 拉 ACK 需要额外时间 |
| 毫秒延时 | `Sys_Delay_ProgramROM(1000)` × 17 循环 | ~1ms |

### 3.3 易错点

1. **SDA 输入必须用 `DIO_WEAK_PULL_UP`**，不能 `DIO_NO_PULL`——如果外部上拉不够，内部弱上拉兜底
2. **ACK 位采样前延时 5x 数据位**——BS300 需要时间拉低 SDA 发 ACK
3. **Read Request 和 I2C Read 之间 delay 1ms**——BS300 需要时间准备响应数据
4. **地址字节由 HAL 自动发送**——frame payload 不要包含地址字节

## 4. 通信协议

### 4.1 帧格式

| 类型 | I2C 方向 | Payload | 总字节 |
|------|---------|---------|:--:|
| Simple Command | Write | `{0x00, Cmd[3], Chk}` | 5 |
| Read Request | Write | `{0x80\|len, Chk}` | 2 |
| Advanced Write | Write | `{0x10, Cmd[3], Data[48], Chk}` | 53 |

> 校验和: `Chk = 0xFF - (sum of payload bytes) & 0xFF`

### 4.2 读取时序

```
Simple Command (prepare)
  → 60~85ms → Read Request(0x00) → 1ms → Read 4B
  → 检查 FURPROC=0 + checksum
  → 60ms → Read Request(0x10) → 1ms → Read 52B
  → 取 Data[48B]
```

### 4.3 FURPROC 轮询

- 读取响应的 bit23 为 FURPROC 标志：0=就绪，1=忙
- checksum 校验失败或 FURPROC=1 时**重发整条命令**（最多 10 次，重试间隔额外 25ms）

## 5. 启动序列

上电后完整操作流程：

```
1. bs300_hal_init()     — I2C 引脚初始化
2. delay 800ms          — BS300 DSP 供电稳定
3. MUTE (0x800000)      — Simple Cmd, wait 85ms, 停止 DSP
4. delay 2ms
5. KEY_LOCK (0x801020)  — Simple Cmd, wait 85ms, 锁定按键
6. delay 2ms
7. VERIFY_COMM (0x800030)— Advanced Write, wait 60ms, 解锁 Flash
   Data[0..2] = {0x01, 0x29, 0x58}  // 大端序! 默认码 0x012958
8. GLOBAL_PROFILE (0x800071) — read_packet, 48B
9. READ_START (0x800031) — Simple Cmd, wait 85ms
10. Program packets ×10 (0x800011 ~ 0x809011) — each: read_packet, 48B
    → 共 480B program data
```

### 5.1 命令等待时间

| 命令类型 | 发完→轮询 | 调用函数 |
|---------|:--:|------|
| MUTE, KEY_LOCK, READ_START | 85ms | `bs300_send_simple_cmd()` |
| 读包 (0x800011+), 校准, GLOBAL_PROFILE | 60ms | `_send_simple_cmd(cmd, 60)` |
| VERIFY_COMM (Advanced Write) | 60ms | `bs300_advanced_write()` |
| Burn End (0x80Y021) | 150ms | (待实现) |

## 6. VERIFY_COMM 安全码

- 默认值: `0x012958`
- **大端序 (MSB-first)**: `{0x01, 0x29, 0x58}`
- ❌ 易错: 不是小端 `{0x58, 0x29, 0x01}`
- 手册参考: BS300 Protocol Handbook v3 §2.8

## 7. 代码文件结构

```
peripheral_server_sleep/
├── code/
│   ├── bs300_hal.c          — 软件 I2C 物理层 (GPIO bit-bang)
│   ├── bs300_hal.h          — I2C 地址/速率/引脚定义
│   ├── bs300_startup.c      — 协议层: 帧构建/轮询/启动/读包
│   ├── bs300_startup.h      — 协议层 API
│   ├── bs300_program_read.c — Flash 读取 + BitReader 解码
│   ├── bs300_test.c         — 测试入口 (printf dump)
│   └── bs300_test.h         — 测试声明
├── include/
│   ├── bs300_program_read.h — 解码数据结构体
│   └── app.h                — BS300_TEST_ENABLE, DEBUG_UART_ENABLE
└── app.c                    — main() 中调用 bs300_test_run()
```

## 8. 调试历史 (关键 bug)

| # | 现象 | 根因 | 修复 |
|---|------|------|------|
| 1 | 逻辑分析仪只看到 `0x7F 0x03` | 帧里多塞了地址字节，Chk 被挤掉 | 去掉 frame[0] 地址，HAL 单独发 |
| 2 | BS300 不 ACK | 7-bit 地址写成 0x02（应为 0x01）| `BS300_I2C_ADDR = 0x01` |
| 3 | 读到全是 0xFF | SDA 浮空，ACK 永远 NACK | `DIO_WEAK_PULL_UP` |
| 4 | 偶尔 NACK | ACK 采样太快，BS300 没来得及拉低 | ACK 延时 5x |
| 5 | 读回乱码 | Read Request → Read 之间无延时 | `delay_ms(1)` |
| 6 | VERIFY_COMM 失败 | security_code 字节序反了 | 大端 `{0x01,0x29,0x58}` |
| 7 | 读包超时一次就放弃 | 无命令级重试 | 整条 send+wait+poll 重试 10 次 |
