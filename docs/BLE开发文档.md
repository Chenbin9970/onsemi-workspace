# BLE 开发文档 — peripheral_server_sleep

## 1. 架构总览

```
┌─────────────────────────────────────────────────────┐
│                    BLE Stack                        │
│                                                     │
│  Advertising (31B)                                  │
│  ├── Flags [02][01][06]   (3B, 自动)                │
│  └── Device Name "cbtest" (可变长, AD type 0x09)     │
│                                                     │
│  Scan Response (31B)                                │
│  └── Manufacturer Data   (31B, AD type 0xFF)         │
│                                                     │
│  GATT Services (3个)                                 │
│  ├── Battery Service   (0x180F, 标准)               │
│  ├── Custom Service    (128-bit UUID)                │
│  └── REMPRO Service    (128-bit UUID, RT App 专属)   │
└─────────────────────────────────────────────────────┘
```

## 2. MAC 地址

| 项目 | 值 |
|------|-----|
| 地址类型 | Public |
| 当前值 | NVR3 Flash 中的值 |
| 定义位置 | `ble_std.h:106` `APP_PUBLIC_BDADDR` |

## 3. 广播数据 (Advertising)

**文件**: `ble_std.c` → `Advertising_Start()`

3 字节 Flags 由 BLE 栈自动添加。广播数据区放设备名 `"cbtest"`。

**配置文件**: `ble_std.h`
```c
#define APP_DFLT_DEVICE_NAME   "cbtest"        // 设备名
#define APP_ADV_CHMAP          0x07            // 信道 37/38/39
#define CFG_ADV_INTERVAL_MS    100             // 广播间隔
```

## 4. 扫描响应 (Scan Response)

**文件**: `ble_std.h:124-128`

31 字节 Manufacturer Specific Data：

```
偏移  内容
0     [0x1E]      长度 (30 字节跟随)
1     [0xFF]      AD Type (Manufacturer Specific)
2-3   [D6 05]     Magic
4-8   [00×5]      Zero padding
9-14  [MAC 6B]    设备 MAC 地址
15-20 [00×6]      Zero padding
21    [0x20]      设备类型
22-30 [00×9]      Zero padding (C 自动补零)
```

```c
#define APP_COMPANY_ID_DATA {
    0x1E, 0xff, 0xD6, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x11, 0x11, 0x11, 0x11, 0x11, 0xC0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20
}
#define APP_COMPANY_ID_DATA_LEN  (0x1E + 1)  // = 31
```

## 5. GATT 服务详情

### 5.1 Battery Service (BASS)

| 项目 | 值 |
|------|-----|
| UUID | `0x180F` (标准) |
| 文件 | `ble_bass.c` / `ble_bass.h` |
| 通知 | `Batt_LevelUpdateSend(conidx, batt_lvl, 0)` |
| 电量范围 | 0-100%，ADC0 采样 VBAT/2 |

### 5.2 Custom Service (原有)

| 项目 | 值 |
|------|-----|
| UUID | `24dc0e6e-0140-ca9e-e5a9-a300b5f393e0` |
| 文件 | `ble_custom.c` / `ble_custom.h` |

**Characteristics:**

| 名称 | UUID (差异字节) | 权限 | 大小 | 用途 |
|------|---------------|------|------|------|
| TX_VALUE | `...02...` | RD + NTF | 20B | 设备→App 通知 |
| RX_VALUE | `...03...` | RD + WR_REQ + WR_CMD | 20B | App→设备 指令 |
| RM_ONOFF | `...04...` | RD + WR_REQ + WR_CMD | 1B | RM 开关控制 |

**RX 指令格式** (原始，无 HDLC):
```
[cmd(1B)] [arg(1B)]

cmd = 0x01 → 切换程序 (arg=0-3)
cmd = 0x02 → 设置音量 (arg=0-9)
cmd = 0xFE → 清缓存重载
```

### 5.3 REMPRO Service (RT App 专属)

| 项目 | 值 |
|------|-----|
| UUID | `fb349b5f-8000-0080-0010-000000af0000` |
| 文件 | `ble_rempro.c` / `ble_rempro.h` (服务层) |
|  | `ble_rempro_cmd.c` / `ble_rempro_cmd.h` (指令层) |

**Characteristics:**

| 名称 | UUID (差异字节) | 权限 | 大小 | 用途 |
|------|---------------|------|------|------|
| ROLE | `...01...af` | RD + WR_REQ + WR_CMD | 20B | App→设备 (Write CMD) |
| ONOFF | `...02...af` | RD + NTF | 20B | 设备→App (Notify) |

**GATT 路由实现** (`ble_custom.c`):
- `GATTM_AddSvcRsp`: 第一次存入 `cs_env.start_hdl`，第二次存入 `rempro_env.start_hdl`
- `GATTC_ReadReqInd` / `GATTC_WriteReqInd`: 按 handle 范围路由到 REMPRO 或 CS
- `GATTC_CmpEvt`: 通知完成同时设置两边的 `sentSuccess`

**服务注册** (`app.h`):
```c
#define SERVICE_ADD_FUNCTION_LIST
    Batt_ServiceAdd_Server,
    CustomService_ServiceAdd,
    RemproService_ServiceAdd
```
添加顺序: BASS → CS → REMPRO

## 6. HDLC 协议 (REMPRO 指令层)

### 6.1 帧格式

**请求** (App → 设备，写入 ROLE):
```
[7E] [SYS_ID=00] [CMD_ID_L] [CMD_ID_H] [Data...] [FCS] [7E]
 1B      1B          1B          1B      可变     1B    1B
```

**响应** (设备 → App，通知 ONOFF):
```
[7E] [SYS_ID=00] [CMD_ID_L] [CMD_ID_H] [Flag] [Data...] [FCS] [7E]
 1B      1B          1B          1B       1B     可变     1B    1B
```

**FCS**: `(SYS_ID + CMD_ID_L + CMD_ID_H + Data bytes) & 0xFF`

### 6.2 已实现指令

| ID | 名称 | 方向 | 请求数据 | 响应数据 | 实现 |
|----|------|------|---------|---------|------|
| 2 | SetVolume | W→R | `[DevType][Vol][Vol2]` | `[Flag=0][status=1]` | `bs300_set_volume_async` |
| 3 | SetDeviceOnOff | W→R | `[DevType][OnOff]` | `[Flag=0][status=1]` | 已实现框架 |
| 4 | GetBatteryInfo | W→R | (空) | `[Flag=0][L_Batt][R_Batt]` | 返回 `app_env.batt_lvl` |
| 16 | SetCurrentScene | W→R | `[DevType][SceneID]` | `[Flag=0][status=1]` | `bs300_switch_program_async` |
| 26 | GetDeviceConfig | W→R | (空) | `[Flag=0][10B config]` | 返回基本设备信息 |

### 6.3 新增指令模板

```c
// 1. 在 ble_rempro_cmd.h 添加 CMD ID
#define CMD_XXX   <新ID>

// 2. 在 ble_rempro_cmd.c 实现 handler
static void cmd_xxx(const uint8_t *data, uint8_t len)
{
    // 解析 data, 执行操作
    hdlc_response(CMD_XXX, 0, resp_data, resp_len);
}

// 3. 在 rempro_cmd_process() 的 switch 中添加 case
case CMD_XXX:
    if (data) cmd_xxx(data, data_len);
    else hdlc_response(CMD_XXX, 1, NULL, 0);
    break;
```

## 7. 文件清单

```
peripheral_server_sleep/
├── include/
│   ├── app.h                    # 服务注册 (SERVICE_ADD_FUNCTION_LIST)
│   ├── ble_std.h                # BLE 参数 (MAC, 广播, 连接)
│   ├── ble_custom.h             # Custom Service 定义
│   ├── ble_bass.h               # Battery Service 定义
│   ├── ble_rempro.h             # REMPRO Service 定义 (NEW)
│   └── ble_rempro_cmd.h         # REMPRO 指令定义 (NEW)
├── code/
│   ├── ble_std.c                # BLE 初始化, 广播, 连接管理
│   ├── ble_custom.c             # Custom Service 实现 + REMPRO 路由
│   ├── ble_bass.c               # Battery Service 实现
│   ├── ble_rempro.c             # REMPRO Service 实现 (NEW)
│   ├── ble_rempro_cmd.c         # REMPRO HDLC 解析 + 5 指令 (NEW)
│   ├── app_init.c               # 初始化 (含 REMPRO init)
│   └── app_process.c            # 消息分发
├── app.c                        # 主循环 (含 REMPRO 指令处理)
└── RTE/Device/RSL10/
    └── rsl10_protocol.c         # BLE 参数读取 (NVR3)
```

## 8. 关键函数速查

| 操作 | 函数 | 位置 |
|------|------|------|
| 发送 CS 通知 | `CustomService_SendNotification(conidx, CS_IDX_TX_VALUE_VAL, data, len)` | `ble_custom.c` |
| 发送 REMPRO 通知 | `RemproService_SendNotification(conidx, REMPRO_IDX_ONOFF_VALUE_VAL, data, len)` | `ble_rempro.c` |
| 发送 HDLC 响应 | `hdlc_response(cmd_id, flag, data, len)` | `ble_rempro_cmd.c` |
| 设置音量 | `bs300_set_volume_async(level, callback)` | `bs300_ram_sync.c` |
| 切换程序 | `bs300_switch_program_async(prog, callback)` | `bs300_ram_sync.c` |
| 发送电量通知 | `Batt_LevelUpdateSend(conidx, batt_lvl, 0)` | `ble_bass.c` |
