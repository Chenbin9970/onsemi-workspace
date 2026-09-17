# RSL10 MAC 地址与 NVR3 —— 为什么所有解锁板 MAC 都一样

> 记录于 2026-09-17。结论已用 J-Link 在实机上逐条验证，不是推测。

## 1. 结论速览

| 问题 | 答案 |
|---|---|
| 本机 MAC 存哪 | NVR3 的 `DEVICE_INFO_BLUETOOTH_ADDR`，绝对地址 `0x00081000`，6 字节（**小端**） |
| 为什么 1654 正常 | 没被解锁过，NVR3 完好，各自 MAC 不同 |
| 为什么 1664 / 7160SL 板 MAC 全一样 | 解锁 7160SL 时整片擦除**含 NVR3**，固件回退到常量 `co_default_bdaddr` |
| 那个常量是什么 | `01 23 45 67 89 AB`（小端）→ MAC **AB:89:67:45:23:01** |
| 常量在哪 | SDK 的 BLE 内核库 `libkelib.a` 里，**每个 build 都一样**，所以所有板必然同 MAC |
| 怎么改 | 把 NVR4 里的产线备份写回 NVR3，见 §7（已实测通过） |

## 2. 固件侧的取值链路

`code/ble_std.c` `BLE_Initialize()`（1664 与 1654 逻辑相同）：

```c
#if (BD_ADDRESS_TYPE == BD_TYPE_PUBLIC)
    bdaddr_type = GAPM_CFG_ADDR_PUBLIC;
    if (Device_Param_Read(PARAM_ID_PUBLIC_BLE_ADDRESS, (uint8_t *)&bdaddr))
    {
        /* 读到就直接用 */
    }
    else
    {
        memcpy(bdaddr, &co_default_bdaddr, sizeof(uint8_t) * BDADDR_LENGTH);  /* ← 擦过 NVR3 的板子走这里 */
    }
#endif
```

`Device_Param_Read()`（`RTE/Device/RSL10/rsl10_protocol.c`）在 `DEVICE_INFO_BLUETOOTH_ADDR` 为**全 `FF`** 或**全 `00`** 时返回 `valueExist = 0`：

```c
ptr = (uint8_t *) DEVICE_INFO_BLUETOOTH_ADDR;
if (((memcmp(all_ff_bytes, ptr, 6) == 0) || (memcmp(all_00_bytes, ptr, 6)) == 0))
{
    valueExist = 0;          /* → 上层回退 co_default_bdaddr */
}
```

这个 MAC 被外部看到的两条路：

- 广播 **scan response** 的厂商段（`ble_std.c` `Advertising_Start()`，`company_id[12..17]` 逐字节倒序填入 `bdaddr`）
- Rempro `GetDeviceConfig`（ID:26）响应的 `Address_Left` / `Address_Right`（`code/ble_rempro_cmd.c`，直接 `memcpy(bdaddr, 6)`）

## 3. 证据（三条独立互证）

### 3.1 常量本体

在 `libkelib.a`（RSL10 SDK 的 BLE 内核库）里直接搜到 `01 23 45 67 89 AB` 这 6 字节，紧跟在代码段之后，即 `co_default_bdaddr` 的定义：

```
... 01 20 10 bd 00 20 fc e7 | 01 23 45 67 89 ab | 00 00 00 00 00 00 ...
                            ^^^^^^^^^^^^^^^^^^^ co_default_bdaddr
```

### 3.2 符号地址与 hex 内容对得上

用 `arm-none-eabi-nm` 查各工程 ELF 的 `co_default_bdaddr` 地址，该地址在各工程 hex 里**正好就是那 6 字节**：

| 工程 | `nm` 得到的地址 | 该工程 hex 中 AB89 的位置 |
|---|---|---|
| `remote_mic_rx_coex_1664` | `0x0012324E` | `smart1654_0914.hex` @ `0x0012324E` ✔ |
| `remote_mic_rx_coex_1654` | `0x0012324E` | 同上 ✔ |
| `peripheral_server_sleep7160test` | `0x00128B9B` | 同工程 hex @ `0x00128B9B` ✔ |
| `peripheral_server_sleep` | `0x0013B0BF` | 同工程 hex @ `0x0013B0BF` ✔ |

> 地址随 build 变，**值永远是 `01 23 45 67 89 AB`**。不要拿地址当判断依据，拿值当依据。

### 3.3 板上实测

| 地址 | 板 A | 板 B |
|---|---|---|
| NVR3 `0x00081000`（MAC） | 全 `FF`（已擦） | 全 `FF`（已擦） |
| NVR4 `0x00081A20` | `d4 a8 b1 46 00 f4 00 00` | `48 a5 b1 46 00 f4 00 00` |
| NVR3 里的 `co_null_bdaddr` 位置 | 全 `FF` | 全 `FF` |

两板 NVR3 均确认被擦 → 都会回退 → MAC 必然同为 `AB:89:67:45:23:01`。

## 4. NVR 区结构

来自 `rsl10_map.h` / `rsl10_map_nvr.h`：

| 区 | 范围 | 内容 | 解锁工具是否擦 |
|---|---|---|---|
| NVR1 | `0x00080000-0x000807FF` | 应用特定信息 | 是（整片擦） |
| NVR2 | `0x00080800-0x00080FFF` | 绑定设备地址与密钥 | 实测板 A 残留 72B，板 B 已清空 |
| NVR3 | `0x00081000-0x000817FF` | **本机蓝牙地址**（`+0x00`）、IP 保护配置（`+0x40`）、`MANU_INFO_INIT`（`+0x80`） | **是** |
| NVR4 | `0x00081800-0x00081FFF` | 出厂校准与制造信息，**产线编程、用户不可写** | 否（实测数据仍在） |

NVR4 分 4 个 256B 冗余页（`NVR4_0..3`），读的时候地址 bit8 不参与译码，所以两份看似"重复"的内容其实是冗余副本。

**16b 事实**：ON Semi 自己的 `Sys_ProgramROM_UnlockDebug()` 解锁调试口时，是"擦除**除 NVR3/NVR4 以外**的全部 flash"。而 E7160SL 原厂 Pre Suite 固件把 JTAG 锁密钥放在 NVR3，所以官方解锁工具（`unlock_RSL10_E7160SL.exe`）必须连 NVR3 一起擦——这是 MAC 丢失的直接原因，也解释了为什么 NVR4 还在。见 [7160SL_RSL10_解锁方法.md](../7160SL_RSL10_解锁方法.md)。

`default_MANU_INFO_INIT` 例程的恢复逻辑（SDK `source/samples/uv/default_MANU_INFO_INIT/app.c`）：

```c
/* NVR3 地址为空(全0)或全1 时，从 NVR4 读回 */
if (*(int64_t *)info.deviceAddr == 0x0 || *(int64_t *)info.deviceAddr == -1)
{
    Sys_ReadNVR4(MANU_INFO_BLUETOOTH_ADDR, INFO_ADDR_WORD_LEN, info.deviceAddr);  /* 8 字节 */
}
```

`MANU_INFO_BLUETOOTH_ADDR = FLASH_NVR4_1_BASE + 0x20 = 0x00081A20`，`INFO_ADDR_WORD_LEN = 2`（words）= 8 字节。

**固件手册确认**：`MANU_INFO_BLUETOOTH_ADDR` 就是「产线写入 `DEVICE_INFO_BLUETOOTH_ADDR` 的那个蓝牙公共地址的副本，用于设备信息扇区被意外擦除时恢复」。手册同时给出 CAUTION：`DEVICE_INFO_BLUETOOTH_ADDR` 在产线测试时被写成**每台唯一的 EUI-48**。

实测两板该处：

| 板 | NVR4 `0x00081A20` | 还原出的 MAC |
|---|---|---|
| A | `d4 a8 b1 46 00 f4` | `F4:00:46:B1:A8:D4` |
| B | `48 a5 b1 46 00 f4` | `F4:00:46:B1:A5:48` |

前 4 字节（`F4:00:46:B1`）相同、后 2 字节不同，是典型产线号段——**每台的唯一 MAC 一直都还在 NVR4 里**，解锁工具不擦 NVR4。

## 5. 影响面：对耳/对端地址撞车

`sleep` / `7160test` / `tx_coex` 里硬编码的

```c
#define PEER_EAR_BD_ADDRESS_LEFT  { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB }  /* AB:89:67:45:23:01 */
```

**就是本文件讨论的那个默认地址**——当初显然是从一台解锁后的板子上扫到、直接抄进代码的。

后果：拿它当对耳/对端地址，等于让设备去连接"一台 MAC 是默认值的设备"；而任何 NVR3 被擦的板子都符合这个条件，**所有解锁板会互相撞上**。

`docs/peer_ear/DUAL_ROLE.md` 里同一份信息还有另外两套值（`00:00:00:00:00:00` 和 `60:C0:BF:00:76:76/91`），三处不一致，需要统一。

## 6. 复现 / 排查命令

### 6.1 读 NVR3 / NVR4（纯读，无风险）

```
JLink.exe -NoGui 1 -Device RSL10 -If SWD -Speed 4000 -AutoConnect 1
J-Link> mem32 0x00081000, 0x08     # NVR3 的 MAC 区，全 F = 已擦
J-Link> mem32 0x00081A20, 0x08     # NVR4 的 MANU_INFO_BLUETOOTH_ADDR
J-Link> savebin D:\tmp\nvr.bin, 0x00080000, 0x2000
```

### 6.2 判断板子在跑哪个固件

把整片 flash dump 出来，搜设备名字符串，或看 `0x00100000` 处是否有向量表（`SP=0x2000xxxx` + 奇数 reset 向量）：

```
J-Link> savebin D:\tmp\flash.bin, 0x00100000, 0x5F000
```

已知设备名：`Smart1664` / `Smart1664FOTA`（1664）、`cb7160test`（7160test）、`cbtest`（sleep 系）。

### 6.3 定位 `co_default_bdaddr`

```bash
arm-none-eabi-nm -n <工程>/Debug/<工程>.elf | grep -i default_bdaddr
```

## 7. 恢复唯一 MAC：把 NVR4 备份写回 NVR3（已实测通过）

### 7.1 先排除一条走不通的路：改固件没用

**公开地址以 NVR3 为准，固件里 `bdaddr` 填什么都不影响射频实际地址。**

验证过程：把 `code/ble_std.c` 的 PUBLIC 回退从 `co_default_bdaddr` 改成应用宏（`APP_PUBLIC_BDADDR`），编译烧录后：

| 观察点 | 结果 |
|---|---|
| app 侧 `bdaddr` 变量（`0x200008A0`） | `A0 76 00 BF C0 60` —— 新值，说明代码确实生效了 |
| 基带 `BB_BDADDRL/U`（`0x40001524`/`28`） | 仍是 `0x67452301`/`0x0000AB89` = `AB:89:67:45:23:01` |
| 手机扫描 | 仍是 `AB:89:67:45:23:01` |

该改动已回退。顺带得到两个结论：

- app 里的 `bdaddr` 只决定**广播厂商段**和 Rempro `GetDeviceConfig` 上报的 MAC，跟射频实际地址是两回事；
- `co_default_bdaddr`（`AB:89:67:45:23:01`）本质就是 **BLE IP 的上电默认图样**（`0x67452301` 是 SHA-1 的 H0 常数），所以 NVR3 一空，硬件默认值就原样出现在空中。

### 7.2 正确做法：写 NVR3

逐台流程（每台几分钟）：

**① 读该板的 NVR4 备份**
```
JLink.exe -NoGui 1 -Device RSL10 -If SWD -Speed 4000 -AutoConnect 1
J-Link> mem8 0x00081A20, 8
```
前 6 字节就是这台的 MAC（小端）。

**② 生成只含这 6 字节的 Intel HEX**
```
:020000040008F2           ; 扩展线性地址 = 0x0008
:06100000D4A8B14600F483   ; 0x00081000 起 6 字节（本例 MAC = F4:00:46:B1:A8:D4）
:00000001FF
```

> `0x00081000` 超过 16 位偏移，**必须带类型 04 的扩展线性地址记录**，否则会被当成 `0x00001000` 写到 PROM 上去。

**③ J-Link 烧进 NVR3**（J-Link V9.60 自带 RSL10 的 NVR bank，不必额外装 pack 里的设备定义）
```
J-Link> loadfile D:\tmp\nvr\nvr3_mac.hex
J-Link: Flash download: Bank 3 @ 0x00081000: 1 range affected (2048 bytes)
J-Link> mem8 0x00081000, 8      ; 回读确认
```

**④ 验证（不用扫码）**：读基带寄存器
```
J-Link> mem32 0x40001524, 0x02   ; BB_BDADDRL / BB_BDADDRU
```

### 7.3 实测结果

| 项 | 写入前 | 写入后 |
|---|---|---|
| NVR3 `0x00081000` | `FF FF FF FF FF FF` | `D4 A8 B1 46 00 F4` |
| app `bdaddr` 变量 | `A0 76 00 BF C0 60` | `D4 A8 B1 46 00 F4` |
| 基带 `BB_BDADDRL/U` | `0x67452301` / `0x0000AB89` → `AB:89:67:45:23:01` | `0x46B1A8D4` / `0x0000F400` → `F4:00:46:B1:A8:D4` |

### 7.4 注意事项

- J-Link 写 NVR3 会**整扇区擦除 2 KB**（`0x81000-0x817FF`）。这批板 NVR3 已是空的，无损；但若某台板 NVR3 里已有 `MANU_INFO_INIT`（`+0x80`）、JTAG 锁密钥（`+0x40`）等内容，**会一并被擦掉**，动手前先 dump 一份。
- 写完要复位才生效（`loadfile` 自带 reset）。
- 官方等价路径是跑 SDK 例程 `default_MANU_INFO_INIT`（`source/samples/uv/`，µVision 工程），逻辑就是「NVR3 地址为空 → 从 NVR4 读回」，用它可以但需要 Keil。
- 量产时这一步可固化成产线工装的一环。

## 8. 相关文件

| 文件 | 内容 |
|---|---|
| `remote_mic_rx_coex_1664/code/ble_std.c` | `BLE_Initialize()` 的地址分支、`Advertising_Start()` 的 company data |
| `remote_mic_rx_coex_1664/RTE/Device/RSL10/rsl10_protocol.c` | `Device_Param_Read()` 的全 FF/全 00 判定 |
| `remote_mic_rx_coex_1664/code/ble_rempro_cmd.c` | `GetDeviceConfig` 回 `Address_Left/Right` |
| `docs/7160SL_RSL10_解锁方法.md` | 解锁流程，含"NVR3 被擦、MAC 使用默认地址" |
| `docs/peer_ear/DUAL_ROLE.md` | 对耳 MAC 配置（三处值不一致） |
