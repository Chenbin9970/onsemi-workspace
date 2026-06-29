# RSL10 Peripheral Server Sleep — 编译指南

## 环境要求

| 组件 | 来源 | 路径 |
|------|------|------|
| ARM GCC 10.2.1 | ON Semiconductor IDE V4.3.1 | `C:\Program Files (x86)\onsemi\IDE_V4.3.1.132\arm_tools\bin` |
| RSL10 CMSIS-Pack 3.7.606 | ARM Pack Manager | `%LOCALAPPDATA%\Arm\Packs\ONSemiconductor\RSL10\3.7.606` |
| GNU Make | 任意（MSYS2 / Git Bash / Cygwin） | — |

## 方式一：命令行 Makefile（推荐，不依赖 IDE）

```bash
cd peripheral_server_sleep
make          # 编译（Debug，-O0 -g3）
make clean    # 清理
```

产物：`build/peripheral_server_sleep.elf`

### Release 编译

修改 Makefile 中的 CFLAGS：`-O0 -g3` → `-Os`

### 编译的文件

- **应用层**（9 个）：`app.c`, `code/app_*.c`, `code/ble_*.c`, `code/calibration.c`
- **系统层**（6 个）：`syslib/rsl10_sys_*.c`（从 CMSIS-Pack 复制）
- **RTE**（2 个）：`RTE/Device/RSL10/system_rsl10.c`, `rsl10_protocol.c`
- **汇编**（2 个）：`code/wakeup_asm.S`, `RTE/Device/RSL10/startup_rsl10.S`

### 链接的库

| 库 | 用途 |
|----|------|
| `libblelib.a` | BLE 协议栈核心 |
| `libkelib.a` | 内核调度器 |
| `libbass.a` | 电池服务 Profile |
| `libweak_prf.a` | 其余 Profile 弱符号（避免未定义引用） |

## 方式二：IDE 命令行（需关闭 IDE）

```bash
# 关掉 IDE 后执行
"C:\Program Files (x86)\onsemi\IDE_V4.3.1.132\eclipse\eclipsec.exe" \
  -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data "C:\Users\admin\onsemi-workspace4.5" \
  -build "peripheral_server_sleep/Debug"
```

配置名可选 `Debug` 或 `Release`。

## 方式三：IDE 图形界面

Eclipse 中打开项目 → Project → Build Project（Ctrl+B）

## 已知问题

1. **IDE 命令行编译要求 IDE 未运行**，否则报 `Workspace already in use!`
2. **syslib 源文件**是从 CMSIS-Pack 复制到项目的 `syslib/` 目录的，更新 SDK 版本时需同步
3. HTTPS 连接 GitHub 不可用，推送请使用 SSH：`git@github.com:Chenbin9970/onsemi-workspace.git`
