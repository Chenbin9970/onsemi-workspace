# ASHA 配对 + 音频通道修复 — 进度总结

## 现状

手机配对成功、加密通过、GATT 读正常、连接参数匹配 demo（interval=16/20ms）。但手机**读完 ReadOnlyProperties 后不开 L2CAP**，30 秒后断连（reason=19, 用户主动断连）。

Demo 流程：配对 → 读 ReadOnlyProperties **和** LE_PSM → interval=16 → L2CAP → AudioControlPoint(STATUS) → 音频

我们流程：配对 → 读 ReadOnlyProperties → interval=16 → **卡住** → 30s 断连

---

## 已完成的修改（8 个文件）

### 1. ASHA 默认模式 — `asha_app.c`
- `asha_active = true`（开机直接 ASHA）
- `ASHA_App_Process` 状态监控 + AudioControlPoint 写入通知

### 2. 冷启动跳 RM — `app.c`
- RM 冷启动加 `if (!asha_active)` 守卫
- ASHA 连接期间防睡眠
- `ASHA_App_Process` 移入 while 循环

### 3. ASHA 初始化时序 — `app_init.c`
- `ASHA_Initialize` 移到 `App_Initialize` 内部（匹配 demo：服务注册前初始化）

### 4. 配对处理器 — `ble_std.c` + `ble_std.h`
- `GAPC_BondReqInd`：PAIRING_REQ / LTK_EXCH / TK_EXCH / IRK_EXCH / CSRK_EXCH 全处理
- 内联 `bond_cfm_send`（`GAPC_BondCfm` 在排除编译的 `ble_gap.c` 里）
- **KE_MSG_ALLOC → KE_MSG_ALLOC_DYN** 修复 buffer 溢出（根因：`ble_gap.c` 排除后内核消息池不知道正确大小）
- 加密处理 `GAPC_EncryptReqInd` / `GAPC_EncryptInd`（内联实现）
- LTK 保存 `saved_ltk[KEY_LEN]`
- `cfm->ltk_present = false`
- 连接参数更新 `accept=1`
- 配对模式 `GAPM_PAIRING_LEGACY`、`GAP_PAIRING_BOND_UNAUTH`
- SEC_CON conditional 支持

### 5. GATT 读取修复 — `ble_asha_wrap.c` + `ble_custom.c`
- `ASHA_GATTC_ReadReqHandler` 传 `toData=NULL` → callback 做 `memcpy(NULL,...)` → HardFault
- 修复：传 buffer 输出参数 + **KE_MSG_ALLOC_DYN** 分配足够空间
- `ASHA_GATTM_AddSvcRspHandler` start_hdl 设置验证

### 6. 写入通知 — `ble_asha_wrap.c` + `asha_app.c`
- AudioControlPoint 写入 → 主循环发 `ASHA_NotifyStatusPoint`

---

## 关键发现

| # | 发现 | 影响 |
|---|------|------|
| 1 | `ble_gap.c`/`ble_gatt.c` 等被 `.cproject` 排除编译 | 所有 RTE abstraction 层函数不可用，必须内联到项目文件 |
| 2 | `KE_MSG_ALLOC` 无 `ble_gap.c` 时分配空间不足 | 内存溢出破坏 LTK → MIC failure → 断连 |
| 3 | `ASHA_GATTC_ReadReqHandler` 传 `toData=NULL` | memcpy NULL → HardFault → 看门狗重启 |
| 4 | `cfm->ltk_present` 未初始化 | 栈随机值让 BLE 栈误以为有 LTK |
| 5 | 手机读完 ReadOnlyProperties 后不继续 | **核心未解问题** |
| 6 | L2CAP 根本就没被手机发起 | `[L2CAP]` 打印从未出现 |

---

## 当前日志特征（首次配对）

```
[ASHA] svc_added=1 start_hdl=34
[ASHA] state=0
[BLE] connected: interval=36
[BOND] req=0 → req=7 → req=6          // 配对完成
[BLE] param_update: 6 → 36 → 16        // 参数更新 ← 匹配 demo
[GATT] read handle=36                  // ReadOnlyProperties ✓
[ASHA] GATT read data=01 01 62 03...   // 数据正确 ✓
// ← 30 秒空白，手机不做任何操作
[BLE] disconnect reason=19             // 手机主动断连
```

---

## 待确认方向

1. **Secure Connections** — 配对 `pairing_lvl` 是否需要 `GAP_PAIRING_BOND_SECURE_CON`（已改，待测）
2. **服务发现** — 手机能否通过 GATT discovery 发现全部 ASHA 特征值
3. **`ble_gap.c` 编译** — 是否需要从排除列表移除，让内核消息池知道正确大小
4. **参考完整 demo** — demo 的 `.cproject`/Makefile 和我们差哪些编译选项
