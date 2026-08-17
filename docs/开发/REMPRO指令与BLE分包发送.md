# REMPRO 指令与 BLE 分包发送

> 记录 2026-08-17 新增的 REMPRO CMD=17（GetFittingData）指令实现，以及 BLE 响应分包发送机制（GATTC 完成事件驱动）。
> 指令字节级格式以 `docs/瑞听听力产品控制接口文档(1).md` 为准。

---

## 一、GetFittingData（CMD=17）获取验配数据

读取指定程序号的增益/压缩比/MPO，数据与 SET 指令（SetGain/SetMPO/SetCompressRatio）操作的是**同一份数据**。

### 1.1 请求

| 字段 | 长度 | 描述 |
|------|------|------|
| SYS_ID | 1 | 0 |
| CMD_ID | 2 | 17 |
| Device_Type | 1 | 0=左 1=右 |
| Scene_ID | 1 | 程序号 0 开始 |

`rempro_cmd_process` 解析后传给 `cmd_getfittingdata(data, len)`：`data[0]=Device_Type`、`data[1]=Scene_ID`。

### 1.2 响应 payload（`hdlc_response` 自动加 SYS_ID + CMD_ID + Flag）

| 偏移 | 字段 | 内容 |
|------|------|------|
| 0 | Scene_ID | 回显请求程序号 |
| 1 | LeftOrRight | 回显 Device_Type |
| 2 | Turn_Number | **2** |
| 3 | Gain_Number | **32** |
| 4 | Compress_Number | **16**（每拐点通道数） |
| 5 | MPO_Number | **16** |
| 6–37 | Gain[32] | `bin_gain[]` → Flash raw `= value_in_MT + 27` |
| 38–69 | Compress[32] | `kp1_r_idx[16]` + `kp2_r_idx[16]`（raw index，无偏移） |
| 70–85 | MPO[16] | `lmt_th_db[]` → Flash raw `= value_in_MT - 30` |

共 **86 字节**。压缩比数组 32 个值 = 2 拐点 × 16 通道，`Compress_Number` 字段填每拐点通道数 16。

> **数值格式**：增益/MPO 返回 **Flash raw**（与 SET 指令收发一致，可 round-trip）；压缩比本来就是 raw index，无偏移。不要返回 value_in_MT 原始值。

### 1.3 实现要点（`code/ble_rempro_cmd.c` → `cmd_getfittingdata`）

1. 校验：`len>=2`、`scene_id<4`、`bs300_sync_is_busy()` 时返回失败。
2. 数据加载与 SET 指令完全一致：
   ```c
   bs300_prog_struct_t fit;
   bs300_storage_load_program(scene_id, bs300_work_buf);
   bs300_flash_to_struct(bs300_work_buf, &fit);
   ```
   用局部 `fit` 读取，不污染 SET 指令共享的 `s_fit_buf`。
3. 编码：`(uint8_t)(fit.wdrc.bin_gain[i] + 27)`、`(uint8_t)(fit.wdrc.lmt_th_db[i] - 30)`。

### 1.4 未实现的相关指令

文档中还有 GetGainData(22)/GetMPOData(23)/GetCompressRatio(24)/GetDenoise(25) 等单类型读取，当前未实现，需要时参考 17 号同样模式。

---

## 二、BLE 响应分包发送（GATTC 完成事件驱动）

### 2.1 问题背景

- `RemproService_SendNotification` 只是 `ke_msg_send(GATTC_SEND_EVT_CMD)` **排队**，实际发送要等内核调度。
- 忙等延时（`bs300_delay_ms`）期间内核不跑，通知全压队列，返回主循环后**一次性突发发出** → App 收到的是全部打包，不是分包间隔。
- `ke_timer_set` 逐包调度在**低功耗下会出问题**（每次 timer 触发都唤醒 CPU，破坏 WFI 休眠）。

### 2.2 方案：GATTC 完成事件驱动

每发一包，等**上一条通知真正发送完成**再发下一包：

- `GATTC_CmpEvt`（`code/ble_custom.c`）在通知发送完成时把 `rempro_env.sentSuccess` 置 1（已有逻辑）。
- `hdlc_response` 发第 1 包并清零 `sentSuccess`。
- `Main_Loop` 每轮调 `rempro_tx_poll()`（`app.c`，`Kernel_Schedule()` 之后）：若 `sentSuccess` 置位则发下一包再清零。

每包天然间隔一个连接事件，**无 ke_timer、无额外唤醒**，断开自动中止。

```c
void rempro_tx_poll(void)
{
    if (!s_tx_in_progress) return;
    if (ble_env.state != APPM_CONNECTED) { s_tx_in_progress = false; return; }
    if (!rempro_env.sentSuccess) return;

    rempro_env.sentSuccess = 0;
    rempro_tx_send_next();   /* 发 ≤20B 分包，发完清 s_tx_in_progress */
}
```

### 2.3 文件改动

| 文件 | 改动 |
|------|------|
| `code/ble_rempro_cmd.c` | `hdlc_response` 改异步；新增 `rempro_tx_send_next()`、`rempro_tx_poll()`；`TX_BUF_SIZE` 100→200 |
| `app.c` | Main_Loop 加 `rempro_tx_poll()` |
| `include/ble_rempro_cmd.h` | 声明 `rempro_tx_poll()` |

> **`TX_BUF_SIZE` 100→200**：GetFittingData 的 86B payload + 帧头/FCS = 91B 未转义，转义后最坏 ~182B，原 100 会越界。200 覆盖所有指令的最坏情况。

> **`hdlc_push`**（设备主动推送，单包 ≤20B）保持同步立即发送，不参与分包延时。

### 2.4 已知边界

若分包发送期间恰好有 cs 推送通知完成（按键/状态变更触发），`GATTC_CmpEvt` 会把 `sentSuccess` 置位导致下一分包提前一帧发送。HDLC 以 0x7E 定界，不影响组帧正确性，实际测试中很少遇到。

---

## 三、read_battery_raw() 共用采样逻辑

`code/ble_rempro_cmd.c`，所有电池 ADC 读取（GetBatteryInfo、低电量检测）共用：

```c
uint32_t read_battery_raw(void)
{
    Sys_ADC_Set_Config(ADC_NORMAL | ADC_PRESCALE_1280H);
    Sys_ADC_InputSelectConfig(0, ADC_POS_INPUT_DIO3 | ADC_NEG_INPUT_GND);
    return ADC->DATA_TRIM_CH[BAT_ADC_CHANNEL];
}
```

每次读取前重新配置 ADC，否则读到旧值。详见 `ADC电量检测开发记录.md`。
