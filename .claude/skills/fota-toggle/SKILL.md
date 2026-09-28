---
name: fota-toggle
description: RSL10 工程 FOTA 固件空中升级开关（通用，非仅 sleep）。当用户说"编译带FOTA"、"编译不带FOTA"、"开关FOTA"、"切换FOTA"、"关闭FOTA"、"打开FOTA"时使用。切换由 fota_toggle.py 一条命令完成。
---

# FOTA 开关

RSL10 工程的 FOTA（Firmware Over-The-Air）开关，由同目录的 `fota_toggle.py` 完成：
替换 4 个活跃文件（`.cproject` / `<项目>.rteconfig` / `startup_rsl10.S` / `sections.ld`）+
切换 `app.h` 的 `CFG_FOTA` 宏 + 合并 `.cproject` 的 exclude + 跑自检。

**不要用 cp 手动切**。手动 cp 会踩「exclude 丢失 → 链接失败」的坑（见下），脚本已处理。

## 用法

```bash
python .claude/skills/fota-toggle/fota_toggle.py <项目> on|off        # 切换
python .claude/skills/fota-toggle/fota_toggle.py <项目> on|off --dry-run   # 只看会改什么
python .claude/skills/fota-toggle/fota_toggle.py --list              # 所有带 FOTA 变体的工程及状态
python .claude/skills/fota-toggle/fota_toggle.py --status [<项目>]   # 只报当前状态
```

- `<项目>` 用工程**目录名**（如 `remote_mic_rx_coex_1654`），不是 rteconfig 前缀
- 目标工程取用户明确说的那个；用户没说时看当前打开的文件属于哪个工程，仍不确定就问 ——
  **不要默认某个工程**
- 脚本幂等：已是目标状态会直接退出
- 四个目标文件有未提交改动时**拒绝执行**（退出码 3），确认要覆盖才加 `--force`；
  回滚用 `git checkout`
- 退出码：`0` 成功且自检全过 / `1` 自检有 FAIL / `2` 参数或工程有误 / `3` 脏树被拦

## 执行后

脚本已经打印自检结果。**全 PASS 才算切换成功**。有 FAIL 就按提示查根因，别当成功。
不要替用户编译 —— 编译在 Eclipse 里由用户做；也**不自动 commit**。

## FOTA 开关的 5 个维度（脚本改的就是这些）

1. **启动文件** — FOTA 版有 `image_descriptor`、向量[7/8]、`SystemFotaInit()`，非 FOTA 版没有
2. **链接脚本** — FOTA 版 ROM 从 `0x00130800`（bootloader 之后）开始，非 FOTA 版从 `0x00100000` 开始
3. **RTE 配置** — FOTA 版含 `Device.Bluetooth Core.Fota` 组件
4. **编译配置** — FOTA 版链接 `libfota.a`、post-build 用 `mkfotaimg.py` 生成 `.fota`
5. **编译宏** — `app.h` 中 `#define CFG_FOTA` 控制 C 代码的条件编译

## 涉及文件

```
<项目>/
├── .cproject                          ← 活跃编译配置（被 .cproject_{fota,nofota} 覆盖）
├── <项目名>.rteconfig                 ← 活跃 RTE 配置
├── include/app.h                      ← #define CFG_FOTA 开关行
└── RTE/Device/RSL10/
    ├── startup_rsl10.S                ← 活跃启动文件
    ├── sections.ld                    ← 活跃链接脚本
    └── *_fota.S / *_nofota.S / sections_{fota,nofota}.ld   ← 变体备份（不参与编译）
```

## exclude 合并（脚本的核心，为什么不能裸 cp）

`.cproject_{fota,nofota}` 备份可能**落后或本身不完整**。实测：

- `remote_mic_rx_coex_1664` 的 `_fota` 备份 config2、`_nofota` 备份两个 config **都没排除变体文件**
- `remote_mic_rx_coex_1654` 活跃 `.cproject` 两个 config 的 exclude 互不一致

裸 cp 会把排除项弄丢 → `startup_rsl10.S` 与 `startup_rsl10_fota.S`/`_nofota.S` 同时入编 →
`Reset_Handler` / `ISR_Vector_Table` 重复定义 → **链接失败**。

脚本的做法：

- 以备份为底，**并入活跃版独有的排除项**（保留工程特有配置）
- 强制保证不变量：4 个变体文件 + `mkfotaimg.py` + `fota.bin` **必排**；
  `startup_rsl10.S` / `sections.ld` **必留**
- 只有真正随 FOTA 切换的项（如 `rsl10_protocol.c`）跟随目标变体

脚本会把每个 config 的 exclude 增删打印出来。出现**删除项**会判 FAIL —— 那代表备份落后到需要人工确认。

## 自检项（脚本自动跑，无需手敲 grep）

| 检查 | FOTA ON | FOTA OFF |
|---|---|---|
| `startup_rsl10.S` 含 `SystemFotaInit` | ≥1 | 0 |
| `sections.ld` 含 `__rom_start` | 3 | 0 |
| `<项目>.rteconfig` 含 `Fota` 组件 | 1 | 0 |
| `.cproject` 含 `libfota.a` | 有 | 无 |
| `app.h` `#define CFG_FOTA` | 生效（未注释） | 被注释 |
| `.cproject` exclude 完整性 | 变体必排、活跃文件必留 | 同 |

> **旧版本 skill 的错误**：曾说「FOTA ON → 无 libblelib / libkelib」。
> 实测 `peripheral_server_sleep` 与 `peripheral_server_sleep7160test` 的 FOTA 版**照样链**
> libblelib + libkelib（叠加式），只有 `remote_mic_rx_coex_1654` / `1664` 是替换式。
> **可靠判别是 `libfota.a` 存在与否**，不是 blelib/kelib 缺席。

## 注意事项

- **`fota.bin` 与 `libfota.a` 需来自同一 CMSIS Pack 版本**（Build ID 匹配），否则 post-build 报错
- FOTA 版 app 重定位到 `0x00130800`，前部预留给 boot + `fota.bin` 子镜像；
  烧录/调试方式与非 FOTA 版不同
- 非 FOTA 版 `sections.ld` 可能额外保留 `DRAM_DSP_CM3` / `.shared` 等段（如 ASHA 需要），
  这是两版链接脚本的正常差异，别当成错误
