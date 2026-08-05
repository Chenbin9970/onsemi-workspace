# TX 端 RFX2401C + 扫描 修改记录

> 2026-08-05 | 涉及项目：`remote_mic_tx_coex`

## 涉及文件

| 文件 | 改动 |
|------|------|
| `remote_mic_tx_coex/include/app.h` | RFX2401C DIO 定义 |
| `remote_mic_tx_coex/include/ble_std.h` | Scan_SendStartCmd 声明 + MAC 列表 + PEER_COUNT |
| `remote_mic_tx_coex/code/app_init.c` | RFX2401C GPIO 初始化（直接写 DIO->CFG 寄存器） |
| `remote_mic_tx_coex/code/app_process.c` | BLE 就绪自动扫描 + 扫描持续 |
| `remote_mic_tx_coex/code/ble_std.c` | MAC 匹配扫描 + GAPM_CmpEvt 扫描超时处理 |

---

## 详细改动

### 1. `include/app.h` — RFX2401C 引脚定义

在 `LED_DIO_NUM` 之后新增：

```c
/* RFX2401C front-end module control pins */
#define RFX2401C_RXEN_DIO               10
#define RFX2401C_TXEN_DIO               11
```

### 2. `include/ble_std.h` — 声明 + MAC + 计数

#### 2.1 Scan_SendStartCmd 声明

在 `extern void DirectConnect(uint8_t peer_idx);` 之后：

```c
extern void Scan_SendStartCmd(void);
```

#### 2.2 PEER_COUNT 和 MAC 地址

```c
#define PEER_COUNT                      6

#define SLEEP_BD_ADDRESS_0  { 0x91, 0x76, 0x00, 0xbf, 0xc0, 0x60 }  /* 60:C0:BF:00:76:91 */
#define SLEEP_BD_ADDRESS_1  { 0x76, 0x76, 0x00, 0xbf, 0xc0, 0x60 }  /* 60:C0:BF:00:76:76 */
#define SLEEP_BD_ADDRESS_2  { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB }  /* AB:89:67:45:23:01 */
#define SLEEP_BD_ADDRESS_3  { 0x09, 0x80, 0x00, 0x09, 0x12, 0x00 }  /* 00:12:09:00:80:09 */
#define SLEEP_BD_ADDRESS_4  { 0xAF, 0x94, 0xD8, 0xBF, 0xC0, 0x60 }  /* 60:C0:BF:D8:94:AF */
#define SLEEP_BD_ADDRESS_5  { 0x94, 0x11, 0x22, 0xFF, 0xFF, 0xF5 }  /* F5:FF:FF:22:11:94 */
```

### 3. `code/app_init.c` — RFX2401C GPIO 初始化

在 `RF_SwitchToBLEMode();` 之后，`Sys_DIO_Config(DIO_SYNC_PULSE, ...)` 之前：

```c
/* Init RFX2401C: default RX mode for BLE scanning
 * DIO->CFG[x] layout: [15:14]=DRIVE [12]=LPF [11:10]=PULL [9:0]=MODE
 * 0xC002 = 6X_DRIVE | NO_PULL | GPIO_OUT_0
 * 0xC003 = 6X_DRIVE | NO_PULL | GPIO_OUT_1 */
DIO->CFG[RFX2401C_TXEN_DIO] = 0xC002;  /* TXEN LOW  */
DIO->CFG[RFX2401C_RXEN_DIO] = 0xC003;  /* RXEN HIGH */
```

> 使用直接寄存器写入绕过宏，确保 IO10/IO11 正确拉低/拉高。
> 注意：`DIO_MODE_GPIO_OUT_0` 等宏在 DIO 10/11 上可能不生效（RTE 默认 weak pull-up），所以用原始寄存器值。

### 4. `code/app_process.c` — 扫描触发

在 `APP_Timer` 的 `TX_BLE_IDLE` 分支：

```c
case TX_BLE_IDLE:
    if (ble_env.state == APPM_READY)
    {
        Scan_SendStartCmd();   // BLE 就绪后自动扫描
    }
    break;
```

> 去掉了原来的 `ad_detected → TX_CONNECTING` 跳转，纯扫描不连接。

### 5. `code/ble_std.c` — 核心改动

#### 5.1 peer_macs 数组扩展

```c
static const uint8_t peer_macs[PEER_COUNT][BDADDR_LENGTH] = {
    SLEEP_BD_ADDRESS_0
#if PEER_COUNT > 1
    , SLEEP_BD_ADDRESS_1
#endif
#if PEER_COUNT > 2
    , SLEEP_BD_ADDRESS_2
#endif
#if PEER_COUNT > 3
    , SLEEP_BD_ADDRESS_3
#endif
#if PEER_COUNT > 4
    , SLEEP_BD_ADDRESS_4
#endif
#if PEER_COUNT > 5
    , SLEEP_BD_ADDRESS_5
#endif
};
```

#### 5.2 GAPM_AdvReportInd — MAC 匹配

改为纯 MAC 匹配（不再匹配设备名），匹配后只打印，不取消扫描、不连接：

```c
int GAPM_AdvReportInd(...)
{
    uint8_t p;
    if (ble_env.state != APPM_SCANNING)
        return (KE_MSG_CONSUMED);

    for (p = 0; p < PEER_COUNT; p++)
    {
        if (memcmp(peer_macs[p], param->report.adv_addr.addr, BDADDR_LENGTH) == 0)
        {
            PRINTF("__SLEEP DEVICE FOUND peer=%d MAC=%02X:...\n", p, ...);
        }
    }
    return (KE_MSG_CONSUMED);
}
```

#### 5.3 GAPM_CmpEvt — 扫描超时重新扫 + CANCEL 处理

扫描完成事件（GAPM_SCAN_ACTIVE / GAPM_SCAN_PASSIVE）重置状态为 APPM_READY，定时器下一 tick 重新扫描：

```c
case (GAPM_SCAN_ACTIVE):
case (GAPM_SCAN_PASSIVE):
{
    ble_env.state = APPM_READY;   // timer will re-scan
}
break;
```

#### 5.4 删除 scanned_peer

`static struct gap_bdaddr scanned_peer;` 删除（纯扫描不连接，不再需要保存扫描到的地址）。

---

## TX 端模式切换

### 测试模式：纯扫描

- `tx_state = TX_BLE_IDLE`，只扫描不连接
- 去掉了音频检测 → TX_CONNECTING 跳转
- 去掉了 TX_CONNECTING 和 TX_RM_ACTIVE 的完整逻辑

### 恢复连接模式

需要恢复 `app_process.c` 中的 TX_CONNECTING / TX_RM_ACTIVE 状态机逻辑。

### RFX2401C TX 模式

需要推流时改 RFX2401C：
```c
DIO->CFG[RFX2401C_TXEN_DIO] = 0xC003;  /* TXEN HIGH */
DIO->CFG[RFX2401C_RXEN_DIO] = 0xC002;  /* RXEN LOW  */
```
