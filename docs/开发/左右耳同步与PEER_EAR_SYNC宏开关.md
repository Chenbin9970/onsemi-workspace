# 左右耳同步与 PEER_EAR_SYNC_ENABLE 宏开关

> 2026-08-18：新增 `PEER_EAR_SYNC_ENABLE` 总开关，用于一键关闭「左耳 BLE Central + 左右耳同步」，相关代码全部用 `#ifdef` 包裹保留。

---

## 一、功能概述

两个 sleep 耳机（RSL10 + BS300）通过 BLE 直连实现程序号互通：

- **左耳**以 `GAP_ROLE_ALL` 同时承担 Central + Peripheral 双角色，主动扫描并连接右耳（`DirectConnect_PeerEar`）。
- 连接后左耳作为 GATT Client（`cs_peer_env`）发现右耳的 Custom Service，订阅 TX 通知，通过写 RX 把程序切换推送过去；右耳收到后应用到本地 DSP。
- 只同步**程序号**，音量各耳独立。

## 二、宏开关

`include/app.h`：

```c
//#define PEER_EAR_SYNC_ENABLE   /* 左右耳同步 + 左耳 BLE Central 总开关；取消注释开启 */
```

- **默认关闭**（注释掉）：左耳变纯 Peripheral，取消双耳同步。
- **开启**：取消注释该行，恢复左耳 Central + 双耳同步。

用法与现有 `//#define CFG_FOTA` 惯例一致（注释即关闭）。

## 三、开启 / 关闭行为对比

| 项 | 开启 | 关闭（默认） |
|----|------|-------------|
| 左耳 GAP 角色 | `GAP_ROLE_ALL`（Central+Peripheral） | `GAP_ROLE_PERIPHERAL` |
| 主动连对耳 | `DirectConnect_PeerEar` + 重试/超时状态机 | 不连 |
| 对耳连接识别 | `GAPC_ConnectionReqInd` 按 MAC 匹配对耳 | 全部按手机连接处理 |
| 程序号同步推送 | 按键/切换时 `CS_Peer_WriteRX` 推对耳 | 不推 |
| GATT Client | 发现/订阅/读写对耳 Custom Service | 无（handler 空操作） |
| 回环抑制 | `sync_from_remote` 防对耳写入回弹 | 无（恒 0） |
| 睡眠 | 对耳连接时强制 `BB_WAKEUP` / 禁深睡 | 恒可深睡 |

## 四、代码范围（6 个文件）

所有 peer-ear 代码用 `#ifdef PEER_EAR_SYNC_ENABLE` 包裹；开启时与原来行为一致，关闭时编译为纯 Peripheral 单机固件。

| 文件 | Guard 内容 |
|------|-----------|
| `include/app.h` | 宏定义本身 |
| `include/ble_std.h` | `PEER_EAR_*` 宏、`enum peer_ear_state`、`BLE_CONN_TYPE_PEER_EAR`、`ble_env.peer_ear_*` 字段、`struct cs_peer_env_tag` 及 `CS_Peer_*` 声明 |
| `code/ble_std.c` | 角色配置（左耳 `GAP_ROLE_ALL`→统一 `GAP_ROLE_PERIPHERAL`）、初始化、`DirectConnect_PeerEar`、`PeerEar_TryConnect`、`GAPM_CmpEvt` 的直连/取消分支、`GAPC_ConnectionReqInd`/`GAPC_DisconnectInd` 的对耳分支 |
| `code/ble_custom.c` | `cs_peer_env` 定义、`CS_Peer_Enable`/`CS_Peer_WriteRX`、`GATTC_DiscSvcInd/DiscCharInd/EvtInd` 函数体（handler 保留、体为空操作）、`GATTC_WriteReqInd` 回环抑制分支 |
| `app.c` | 切程序/音量回调里的对耳推送、Main_Loop 对耳状态机/超时/连接判断、TX 推送对耳分支、按键切程序推送、睡眠条件（去掉"对耳连接禁深睡"） |
| `code/app_process.c` | `Continue_Application` 的 BB 唤醒分支（固定 `BB_DEEP_SLEEP`） |

## 五、验证

1. **关闭配置（默认）编译**：确认无 `PEER_EAR` 相关未定义引用，链接通过。
2. **开启配置编译**：取消注释宏，peer-ear 代码路径能正常编译（开关对称）。
3. **烧录关闭版到左耳**：左耳纯 Peripheral 广播，手机可正常连接；不再主动连右耳；按键切程序/音量不推对耳；日志无 `[PEER_EAR]`。
4. **烧录关闭版到右耳**：右耳本就是 Peripheral，行为不变。

## 六、备注

- `app_env.sync_from_remote` 字段保留不删，关闭时相关增减逻辑被 guard，恒为 0，无副作用。
- 关闭后左耳不再连右耳，**双耳程序号不再同步** —— 这是本开关的预期效果。
- 若日后重新量产双耳同步机型，只需取消注释宏，无需改代码。
