# FOTA 固件空中升级

## 架构

```
Flash 布局 (380KB):
0x00100000 ┌──────────────┐
           │ Bootloader   │  (首次通过 UART 烧录)
0x00130800 ├──────────────┤
           │ Application  │  助听器固件 (~121KB)
0x0015C800 ├──────────────┤
           │ BS300 数据   │  程序参数存储
0x0015F000 └──────────────┘
```

## 触发方式

App 通过 Custom Service RX 特征值写入 `0xFD`，设备调用 `Sys_Fota_StartDfu(1)` 进入 DFU 模式。

```
App → Custom Service RX: [0xFD]
设备 → 断连 BLE，重启
设备 → 广播 "RSL FOTA"
App/Fota.Console → 连接 DFU Service → 发送 .fota 固件
设备 → 自动重启到新固件
```

## 改动清单

### 启动/链接
- `RTE/sections.ld` — `__rom_start=0x00130800`, `__image_size`, FOTA sections
- `RTE/startup_rsl10.S` — image_descriptor, vector[7/8], `SystemFotaInit` 调用

### 运行时配置
- `rteconfig` — 移除 BLE Stack + Kernel，添加 Fota 组件
- `.cproject` — `libfota.a` 替换 `libblelib.a`, `-DCFG_FOTA=1`, post-build 生成 `.fota`

### 应用代码
- `include/app.h` — `CFG_FOTA`, 版本号, `sys_fota.h`/`sys_boot.h`
- `code/ble_std.c` — `SYS_FOTA_VERSION(...)` 版本声明
- `code/ble_custom.c` — RX `0xFD` 触发 `Sys_Fota_StartDfu(1)`
- `code/fota_system.c` — `SystemFotaInit()` → `fota_init()` 初始化 FOTA Stack

### 新增文件
- `include/sys_fota.h` — FOTA 公共 API
- `include/sys_boot.h` — Bootloader 接口
- `include/fota_system.h` — FOTA 系统配置
- `RTE/Device/RSL10/fota.bin` — FOTA Stack 二进制
- `RTE/Device/RSL10/mkfotaimg.py` — 生成 .fota 镜像

## 编译 & 烧录

```bash
# 1. IDE 编译，自动生成 Debug/peripheral_server_sleep.fota

# 2. 首次烧录（设备需在 bootloader 模式：UPDATE_GPIO 拉低 + 复位）
python updater.py COM6 peripheral_server_sleep.fota

# 3. 之后可通过 BLE OTA 升级，写 0xFD 到 Custom Service RX 触发
```

## Build ID 不匹配问题

post-build 步骤 `mkfotaimg.py` 会检查 `fota.bin`（FOTA stack）和 `app.bin`（应用）的 Build ID 是否一致：

```
AssertionError: Build ID do not match
```

**原因**: 工作区 `RTE/Device/RSL10/fota.bin` 和 CMSIS Pack 的 `libfota.a` 不是同一批构建的。

**修复**: 从 CMSIS Pack 复制匹配的 `fota.bin` 覆盖工作区版本：

```powershell
Copy-Item "$env:LOCALAPPDATA\Arm\Packs\ONSemiconductor\RSL10\3.9.1182\lib\Release\fota.bin" `
    "RTE\Device\RSL10\fota.bin" -Force
```

> `fota.bin` 和 `libfota.a` 是 FOTA 构建流程的一对产物，Build ID 嵌入在 `.rodata.fota.build-id` 段中，必须同源。

## 依赖

```bash
pip install ecdsa pyserial
```
