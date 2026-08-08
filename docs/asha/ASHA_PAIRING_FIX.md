# ASHA 配对问题修复方案

## 问题现象

ASHA 模式下，手机连接助听器后显示"设备拒绝配对"。

## 根因

Sleep 工程移植 ASHA 时沿用了 RM 模式的 BLE 配置，**配对功能被完全禁用**。

| 对比项 | ASHA Demo (`ble_android_asha`) | Sleep 工程 (`peripheral_server_sleep`) |
|--------|-------------------------------|----------------------------------------|
| `pairing_mode` | `GAPM_PAIRING_LEGACY` | **`GAPM_PAIRING_DISABLE`** |
| `pairing_lvl` | `GAP_PAIRING_BOND_UNAUTH` | **`GAP_AUTH_REQ_NO_MITM_NO_BOND`** |
| `GAPC_BOND_REQ_IND` handler | 有（完整 5 种 key exchange） | **无** |

Android ASHA 框架层强制要求配对/绑定（加密音频流），设备拒绝配对请求导致连接失败。

## 修复内容

涉及 2 个文件、3 处改动：

### 1. 启用配对模式 — `ble_std.c:166`

```diff
-    gapmConfigCmd->pairing_mode = GAPM_PAIRING_DISABLE;
+    gapmConfigCmd->pairing_mode = GAPM_PAIRING_LEGACY;
```

### 2. 连接确认声明允许绑定 — `ble_std.c:644`

```diff
-        cfm->pairing_lvl = GAP_AUTH_REQ_NO_MITM_NO_BOND;
+        cfm->pairing_lvl = GAP_PAIRING_BOND_UNAUTH;
```

`GAP_PAIRING_BOND_UNAUTH` 表示 Just Works 方式绑定（无需 MITM 保护），IO Capabilities 设为 NoInputNoOutput。

### 3. 新增配对请求处理器 — `ble_std.c`

在 `GAPC_ParamUpdateReqInd` 函数之后插入以下代码：

```c
int GAPC_BondReqInd(ke_msg_id_t const msg_id,
                    struct gapc_bond_req_ind const *param,
                    ke_task_id_t const dest_id,
                    ke_task_id_t const src_id)
{
    uint8_t conidx = KE_IDX_GET(src_id);

    switch (param->request)
    {
        case GAPC_PAIRING_REQ:
        {
            union gapc_bond_cfm_data pairingRsp = {
                .pairing_feat = {
                    .iocap     = GAP_IO_CAP_NO_INPUT_NO_OUTPUT,
                    .oob       = GAP_OOB_AUTH_DATA_NOT_PRESENT,
                    .auth      = GAP_AUTH_REQ_NO_MITM_BOND,
                    .sec_req   = GAP_NO_SEC,
                    .key_size  = KEY_LEN,
                    .ikey_dist = (GAP_KDIST_IDKEY | GAP_KDIST_SIGNKEY),
                    .rkey_dist = (GAP_KDIST_ENCKEY | GAP_KDIST_IDKEY | GAP_KDIST_SIGNKEY),
                }
            };
            GAPC_BondCfm(conidx, GAPC_PAIRING_RSP, true, &pairingRsp);
        }
        break;

        case GAPC_LTK_EXCH:
        {
            union gapc_bond_cfm_data ltkExch;
            ltkExch.ltk.ediv = co_rand_hword();
            for (uint8_t i = 0, i2 = GAP_RAND_NB_LEN; i < GAP_RAND_NB_LEN; i++, i2++)
            {
                ltkExch.ltk.randnb.nb[i] = co_rand_byte();
                ltkExch.ltk.ltk.key[i]   = co_rand_byte();
                ltkExch.ltk.ltk.key[i2]  = co_rand_byte();
            }
            GAPC_BondCfm(conidx, GAPC_LTK_EXCH, true, &ltkExch);
        }
        break;

        case GAPC_TK_EXCH:
            break; /* Just Works — TK is always 0 */

        case GAPC_IRK_EXCH:
        {
            union gapc_bond_cfm_data irkExch;
            memcpy(irkExch.irk.addr.addr.addr,
                   GAPM_GetDeviceConfig()->addr.addr, GAP_BD_ADDR_LEN);
            irkExch.irk.addr.addr_type = GAPM_GetDeviceConfig()->addr_type;
            memcpy(irkExch.irk.irk.key,
                   GAPM_GetDeviceConfig()->irk.key, GAP_KEY_LEN);
            GAPC_BondCfm(conidx, GAPC_IRK_EXCH, true, &irkExch);
        }
        break;

        case GAPC_CSRK_EXCH:
        {
            union gapc_bond_cfm_data csrkExch;
            Device_Param_Read(PARAM_ID_CSRK, csrkExch.csrk.key);
            GAPC_BondCfm(conidx, GAPC_CSRK_EXCH, true, &csrkExch);
        }
        break;
    }

    return (KE_MSG_CONSUMED);
}
```

### 4. 注册处理器 — `ble_std.h`

**`BLE_MESSAGE_HANDLER_LIST`** 末尾追加一行：

```diff
-    DEFINE_MESSAGE_HANDLER(GAPC_PARAM_UPDATE_REQ_IND, GAPC_ParamUpdateReqInd) \
+    DEFINE_MESSAGE_HANDLER(GAPC_PARAM_UPDATE_REQ_IND, GAPC_ParamUpdateReqInd),\
+    DEFINE_MESSAGE_HANDLER(GAPC_BOND_REQ_IND, GAPC_BondReqInd)                \
```

**函数声明** 追加在 `GAPC_ConnectionReqInd` 声明之后：

```c
extern int GAPC_BondReqInd(ke_msg_id_t const msgid,
                            struct gapc_bond_req_ind const *param,
                            ke_task_id_t const dest_id,
                            ke_task_id_t const src_id);
```

## 依赖

Handler 中使用了以下 SDK 函数，需确保相应源文件被编译：

| 函数 | 所在文件 | 来源 |
|------|----------|------|
| `GAPC_BondCfm` | `ble_gap.c` | RTE BLE Abstraction |
| `GAPM_GetDeviceConfig` | `ble_gap.c` | RTE BLE Abstraction |
| `Device_Param_Read` | `rsl10_protocol.c` | RTE System Library |
| `co_rand_byte` / `co_rand_hword` | BLE kernel | `libkelib.a` |

> **注意**：Sleep 工程的 `.cproject` 已将 `ble_gap.c`、`rsl10_protocol.c` 排除编译。需从排除列表中移除，或在 IDE 的 RTE 配置中重新启用 BLE Abstraction 和 System Library 的 protocol 组件。

## 配对流程

```
手机连接 → GAPC_CONNECTION_REQ_IND (pairing_lvl = BOND_UNAUTH)
         → 手机发起 SMP Pairing Request
         → GAPC_BOND_REQ_IND / GAPC_PAIRING_REQ (IO_CAP = NoInputNoOutput)
         → 密钥协商：TK(0) → LTK → IRK → CSRK
         → GAPC_PAIRING_SUCCEED
         → ASHA 音频流启动
```

## 不修改的部分

- **不启用 bond 存储**（`CFG_BOND_LIST_IN_NVR2`）：每次重连会重新配对，可正常跑通音频流。后续如需记住配对，参考 ASHA Demo 的 `app_msg_handler.c:304-385` 加入 bond list 逻辑。
- **不影响 RM 模式**：RM 检测到 TX MAC 后直接 return，不发送 `GAPC_CONNECTION_CFM`，配对流程不会触发。
- **不影响低功耗**：配对是一次性操作，连接参数（500ms interval、latency=10）不变。
