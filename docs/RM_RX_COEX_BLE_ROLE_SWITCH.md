# remote_mic_rx_coex — BLE 控制 RM 角色切换分析

> 基于 `d:/projects/onsemi-workspace/remote_mic_rx_coex/` 完整源码 + RM 协议栈固件源码，2026-08-06

## 总架构

RSL10 同时跑 BLE Peripheral 和 RM (Remote Mic) 接收，通过 BBIF_COEX 硬件仲裁共享 RF。BLE Custom Service 提供 7 个 characteristic 直接读写 RM 参数，其中 ON_OFF 触发 RM 的启停。

```
App (Central)
  │
  ├─ BLE Write ROLE      → 存到 app_env.rm_param.role（不立即生效）
  ├─ BLE Write ON_OFF=1  → RM_Configure() + RM_Enable()（★ 生效点）
  ├─ BLE Write CHNLSIDE  → 存到 app_env.rm_param.audioChnl
  ├─ BLE Write VOLUME    → 存到 app_env.volume
  ├─ BLE Write MODIDX    → 存到 app_env.rm_param.mod_idx
  ├─ BLE Write HOPLIST   → 存到 app_env.rm_param.hopList
  └─ BLE Write HOPSIZE   → 存到 app_env.rm_param.numChnlInHopList
```

## Custom Service 结构

文件：`include/ble_custom.h:89-121`

7 个 characteristic，全部 WRITE 权限，全部直接映射到 `app_env.rm_param` 字段：

| Characteristic | UUID 字节 4 | 映射变量 |
|---|---|---|
| ROLE | 0x02 | `app_env.rm_param.role` |
| ON_OFF | 0x03 | `app_env.RM_on_off` |
| CHANNEL_SIDE | 0x04 | `app_env.rm_param.audioChnl` |
| MOD_INDEX | 0x05 | `app_env.rm_param.mod_idx` |
| VOLUME | 0x06 | `app_env.volume` |
| HOPPING_LIST | 0x07 | `app_env.rm_param.hopList` |
| HOPPING_SIZE | 0x08 | `app_env.rm_param.numChnlInHopList` |

## 初始化流程

```
App_Initialize()                                [app_init.c:39]
  ├─ 硬件初始化、时钟、DSP 固件加载
  ├─ BLE_Initialize()                            // GAP_ROLE_PERIPHERAL
  ├─ App_Env_Initialize()                        // ke_task_create + CustomService_Env_Initialize
  ├─ APP_RM_Init(ear_side)                      [rm_app.c:79]
  │   ├─ rm_param.role = RM_SLAVE_ROLE           // 固定从机
  │   ├─ rm_param.audioChnl = side               // 左/右耳
  │   ├─ rm_param.accessword = 0x0dcde629       // 匹配 TX 的 access word
  │   ├─ 设置跳频表、调制指数、连接参数等
  │   └─ RM_Configure(&rm_param, callback)       // ★ 写入 RM 硬件
  └─ RF_SwitchToBLEMode()                         // 默认 BLE 模式
```

- `ear_side` 定义在 `app_func.c`，默认 `APP_RM_AUDIO_CHANNEL = RM_LEFT = 0`
- `APP_RM_Init()` 只调一次（`SIMUL != 1` 时），之后通过 BLE 指令控制 RM

## BLE Write 处理流程

### 参数类 characteristic（ROLE/CHNLSIDE 等）

文件：`code/ble_custom.c:394-396`

```
GATTC_WriteReqInd()
  └─ case CS_REMPRO_IDX_ROLE_VALUE_VAL:
       valptr = &app_env.rm_param.role
       memcpy(valptr, value, len)     // ★ 仅写 RAM，不做任何硬件操作
       ke_msg_send(cfm)               // 回复 write 确认
```

**不调用 RM_Configure，不重启 RM。新值在下次 ON_OFF=1 时生效。**

### ON_OFF characteristic（触发 RM 启停）

文件：`code/ble_custom.c:401-475`

```
GATTC_WriteReqInd()
  └─ case CS_REMPRO_IDX_ONOFF_VALUE_VAL:
       oldValue_onoff = app_env.RM_on_off    // 保存旧值
       valptr = &app_env.RM_on_off
       memcpy(valptr, value, len)            // 更新 RAM
       ke_msg_send(cfm)                      // 先回复 write 确认

       if (oldValue != newValue):            // ★ 值变化才触发动作
           if (app_env.RM_on_off == 1):
               RM_Configure(&rm_param, callback)  // 所有参数写入 RM
               RF_SwitchToCPMode()                 // RF 切到 Custom Protocol
               RM_Enable(1000)                     // 启动 RM（从机=RM_SEARCH）
           else:
               BBIF_COEX->RX=0, TX=0              // 停共存
               RM_Disable()                        // 停 RM
               RF_SwitchToBLEMode()                // RF 切回 BLE
```

## RM_Configure + RM_Enable 内部细节

源码：`C:/.../RSL10/3.9.1182/source/firmware/remote_micLib/rm_event.c`

### RM_Configure() [rm_event.c:33-140]

```c
rm_env.role           = param->role;         // 主/从角色
rm_env.audioChnl      = param->audioChnl;    // 左/右通道
rm_env.accessword     = param->accessword;   // 匹配码
rm_env.mod_idx        = param->mod_idx;      // 调制指数
rm_env.hoplist        = param->hopList;      // 跳频表
// ... 复制全部参数 ...

RemoteMic_Protocol_Init();                    // ★ 重置协议状态机
rm_env.linkStatus = LINK_DISCONNECTED;        // ★ 连接状态清零
BBIF_COEX_CTRL->RX = 0;                      // 停共存仲裁
BBIF_COEX_CTRL->TX = 0;
```

### RM_Enable() [rm_event.c:151-182]

```c
if (rm_env.role == RM_SLAVE_ROLE)
    rm_env.state = RM_SEARCH;     // 从机 → 扫描 TX
else
    rm_env.state = RM_READY;      // 主机 → 等待推流

// 启动 timer0（一次性，offset 微秒后触发），启动 RF 中断
```

## 角色切换流程对照

### 当前实现：ON_OFF 间接切换

```
① BLE Write ROLE = RM_MASTER    →  app_env.rm_param.role 更新（仅 RAM）
② BLE Write ON_OFF = 0          →  RM_Disable()、切回 BLE
③ BLE Write ON_OFF = 1          →  RM_Configure() ★ 读 role 写入硬件
                                   RF_SwitchToCPMode()
                                   RM_Enable(500)
```

### 可优化的直接切换

```
① BLE Write ROLE = RM_MASTER    →  app_env.rm_param.role 更新
                                   if (RM 正在跑):
                                       RM_Configure()  ← 跳过 RM_Disable
                                       RM_Enable(500)
```

`RM_Configure()` 内部已经做了 `linkStatus=DISCONNECTED` + `RemoteMic_Protocol_Init()`，等价于软件重置，不需要先 `RM_Disable`。代价是链接短暂断开后重搜重建。

## 关键文件索引

| 文件 | 行号 | 内容 |
|------|------|------|
| `include/ble_custom.h:89-121` | — | Custom Service attribute 枚举 |
| `code/ble_custom.c:357-475` | — | `GATTC_WriteReqInd` 全部 7 个 characteristic 的写处理 |
| `code/ble_custom.c:456-475` | — | ON_OFF 变化 → RM_Configure/RM_Disable |
| `code/rm_app.c:79-138` | — | `APP_RM_Init()` 初始化 RM 参数 |
| `code/rm_app.c:245-314` | — | `RM_Callback_StatusUpdate()` 连接状态回调 |
| `code/app_init.c:39-324` | — | `App_Initialize()` 硬件+BLE+RM init |
| `code/app_init.c:275-277` | — | `APP_RM_Init(ear_side)` + `RF_SwitchToBLEMode()` |
| `app.c:33-54` | — | 主循环：`Kernel_Schedule` + `RM_StatusHandler` + `SYS_WAIT_FOR_EVENT` |
| SDK `rm_event.c:33-140` | — | `RM_Configure()` 源码 |
| SDK `rm_event.c:151-182` | — | `RM_Enable()` 源码 |
