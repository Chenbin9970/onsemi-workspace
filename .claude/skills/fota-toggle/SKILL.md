---
name: fota-toggle
description: peripheral_server_sleep FOTA 固件空中升级开关。当用户说"编译带FOTA"、"编译不带FOTA"、"开关FOTA"、"切换FOTA"、"关闭FOTA"、"打开FOTA"时使用。
---

# FOTA 开关

`peripheral_server_sleep` 工程的 FOTA（Firmware Over-The-Air）功能开关。通过替换 5 个文件 + `app.h` 宏实现切换。

## 工作原理

FOTA 开关涉及 5 个维度：
1. **启动文件** — FOTA 版有 `image_descriptor`、向量[7/8]、`SystemFotaInit()`，非 FOTA 版没有
2. **链接脚本** — FOTA 版 ROM 从 `0x00130800`（bootloader 之后）开始，非 FOTA 版从 `0x00100000` 开始
3. **RTE 配置** — FOTA 版含 `Device.Bluetooth Core.Fota` 组件；非 FOTA 版含 `BLE Stack` + `Kernel`
4. **编译配置** — FOTA 版有 `CFG_FOTA=1`、链接 `libfota.a`、post-build 生成 `.fota`；非 FOTA 版链接 `libblelib.a`+`libkelib.a`
5. **编译宏** — `app.h` 中 `#define CFG_FOTA` 控制 C 代码的条件编译

## 涉及文件

```
peripheral_server_sleep/
├── .cproject                                    ← 活跃的编译配置
├── .cproject_fota                               ← FOTA 版本备份
├── .cproject_nofota                             ← 非 FOTA 版本备份
├── include/app.h                                ← #define CFG_FOTA 开关
├── peripheral_server_sleep.rteconfig            ← 活跃的 RTE 配置
├── peripheral_server_sleep_fota.rteconfig       ← FOTA 版本备份
├── peripheral_server_sleep_nofota.rteconfig     ← 非 FOTA 版本备份
└── RTE/Device/RSL10/
    ├── startup_rsl10.S                          ← 活跃的启动文件
    ├── startup_rsl10_fota.S                     ← FOTA 版本备份
    ├── startup_rsl10_nofota.S                   ← 非 FOTA 版本备份
    ├── sections.ld                              ← 活跃的链接脚本
    ├── sections_fota.ld                         ← FOTA 版本备份
    └── sections_nofota.ld                       ← 非 FOTA 版本备份
```

## 检测当前状态

```
看 app.h 中 #define CFG_FOTA 是否被注释：
  - 未注释 → FOTA ON
  - 已注释 → FOTA OFF
```

## 切换步骤

### FOTA ON（启用）

```bash
cd peripheral_server_sleep

# 1. 编译配置
cp .cproject_fota .cproject

# 2. RTE 配置
cp peripheral_server_sleep_fota.rteconfig peripheral_server_sleep.rteconfig

# 3. 启动文件
cd RTE/Device/RSL10
cp startup_rsl10_fota.S startup_rsl10.S

# 4. 链接脚本
cp sections_fota.ld sections.ld
```

然后在 `include/app.h` 中确保 `#define CFG_FOTA` 未被注释。

### FOTA OFF（禁用）

```bash
cd peripheral_server_sleep

# 1. 编译配置
cp .cproject_nofota .cproject

# 2. RTE 配置
cp peripheral_server_sleep_nofota.rteconfig peripheral_server_sleep.rteconfig

# 3. 启动文件
cd RTE/Device/RSL10
cp startup_rsl10_nofota.S startup_rsl10.S

# 4. 链接脚本
cp sections_nofota.ld sections.ld
```

然后在 `include/app.h` 中注释掉 `#define CFG_FOTA`（改为 `//#define CFG_FOTA`）。

## 执行后自检

切换完成后用 grep 确认：
```bash
# 确认启动文件正确
grep -c "SystemFotaInit" peripheral_server_sleep/RTE/Device/RSL10/startup_rsl10.S
# FOTA ON → 返回 1    FOTA OFF → 返回 0

# 确认链接脚本正确
grep -c "__rom_start" peripheral_server_sleep/RTE/Device/RSL10/sections.ld
# FOTA ON → 返回 2    FOTA OFF → 返回 0

# 确认 RTE 配置正确
grep -c 'Csub="Fota"' peripheral_server_sleep/peripheral_server_sleep.rteconfig
# FOTA ON → 返回 1    FOTA OFF → 返回 0

# 确认编译配置正确
grep -c "libfota\|libblelib" peripheral_server_sleep/.cproject
# FOTA ON → 有 libfota    FOTA OFF → 有 libblelib+libkelib

# 确认 app.h 宏正确
grep "^#define CFG_FOTA" peripheral_server_sleep/include/app.h
# FOTA ON → 有输出    FOTA OFF → 无输出
```

## 注意事项

- `fota.bin` 和 `libfota.a` 需来自同一 CMSIS Pack 版本（Build ID 匹配），否则 post-build 报错
- 非 FOTA 版 `sections.ld` 保留了 `DRAM_DSP_CM3` 和 `.shared`（ASHA 需要）
