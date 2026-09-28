#!/usr/bin/env python3
"""RSL10 工程 FOTA 开关。

确定性实现：切换、exclude 合并、自检全部在代码里，不依赖 AI 判断。

用法:
  python fota_toggle.py <项目> on|off [--dry-run] [--force]
  python fota_toggle.py --status [<项目>]
  python fota_toggle.py --list
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

WS_ROOT = Path(__file__).resolve().parents[3]

# (活跃文件, FOTA 变体, 非 FOTA 变体)；{p} 换成工程名
FILE_MAP = (
    (".cproject", ".cproject_fota", ".cproject_nofota"),
    ("{p}.rteconfig", "{p}_fota.rteconfig", "{p}_nofota.rteconfig"),
    ("RTE/Device/RSL10/startup_rsl10.S",
     "RTE/Device/RSL10/startup_rsl10_fota.S",
     "RTE/Device/RSL10/startup_rsl10_nofota.S"),
    ("RTE/Device/RSL10/sections.ld",
     "RTE/Device/RSL10/sections_fota.ld",
     "RTE/Device/RSL10/sections_nofota.ld"),
)

# 切换后 .cproject 必须排除 / 必须保留的文件（四个工程实测一致）
MUST_EXCLUDE = ("startup_rsl10_fota.S", "startup_rsl10_nofota.S",
                "sections_fota.ld", "sections_nofota.ld",
                "mkfotaimg.py", "fota.bin")
MUST_INCLUDE = ("startup_rsl10.S", "sections.ld")
EXCL_PREFIX = "RTE/Device/RSL10/"

DEFINE_RE = re.compile(r"^([ \t]*)(//[ \t]*)?#define[ \t]+CFG_FOTA[ \t\r]*$", re.M)
EXCLUDING_RE = re.compile(r'excluding="([^"]*)"')


def read_text(path: Path) -> str:
    with open(path, "r", encoding="utf-8", newline="") as fh:
        return fh.read()


def write_text(path: Path, text: str) -> None:
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)


def map_paths(proj: str):
    """返回 [(活跃, 变体on, 变体off), ...] 绝对路径三元组。"""
    out = []
    for active, on, off in FILE_MAP:
        out.append((
            WS_ROOT / proj / active.format(p=proj),
            WS_ROOT / proj / on.format(p=proj),
            WS_ROOT / proj / off.format(p=proj),
        ))
    return out


def app_h(proj: str) -> Path:
    return WS_ROOT / proj / "include" / "app.h"


def discover() -> list[str]:
    found = []
    for child in sorted(WS_ROOT.iterdir()):
        if not child.is_dir():
            continue
        if (child / f"{child.name}_fota.rteconfig").is_file():
            found.append(child.name)
    return found


def variants_complete(proj: str) -> list[str]:
    """返回缺失的变体文件名（空 = 完整）。"""
    missing = []
    for _, on, off in map_paths(proj):
        for path in (on, off):
            if not path.is_file():
                missing.append(path.name)
    return missing


def detect_state(proj: str) -> str | None:
    """'on' / 'off'，无 define 行返回 None。"""
    match = DEFINE_RE.search(read_text(app_h(proj)))
    if not match:
        return None
    return "off" if match.group(2) else "on"


def set_define(text: str, on: bool) -> tuple[str, bool]:
    """切换 CFG_FOTA 行；返回 (新文本, 是否命中)。"""
    count = 0

    def repl(match: re.Match) -> str:
        nonlocal count
        count += 1
        pad = match.group(1)
        return f"{pad}#define CFG_FOTA" if on else f"{pad}//#define CFG_FOTA"

    new = DEFINE_RE.sub(repl, text)
    return new, count == 1


def excl_lists(text: str) -> list[list[str]]:
    return [value.split("|") for value in EXCLUDING_RE.findall(text)]


def _matches(item: str, name: str) -> bool:
    return item == name or item.endswith("/" + name)


def _is_invariant(item: str) -> bool:
    return any(_matches(item, n) for n in MUST_EXCLUDE + MUST_INCLUDE)


def merge_one(active: list[str], backup: list[str], toggle: set[str]) -> list[str]:
    """备份为底 + 活跃版独有项，再强制保证不变量（变体必排、活跃文件必留）。"""
    backup_set = set(backup)
    toggle_set = {x for x in toggle if not _is_invariant(x)}

    merged = list(backup)
    for item in active:
        if item not in backup_set and item not in toggle_set:
            merged.append(item)

    for name in MUST_EXCLUDE:
        if not any(_matches(x, name) for x in merged):
            merged.append(EXCL_PREFIX + name)

    return [x for x in merged if not any(_matches(x, n) for n in MUST_INCLUDE)]


def merge_excludes(active_text: str, backup_text: str, on_text: str, off_text: str):
    """把活跃版独有的排除项并入备份的 excluding；只有 FOTA 相关项跟随变体。

    返回 (新文本, 每个 config 的 (新增项, 删除项), 警告列表)。
    """
    active = excl_lists(active_text)
    backup = excl_lists(backup_text)
    on_l = excl_lists(on_text)
    off_l = excl_lists(off_text)
    warnings: list[str] = []
    changes: list[tuple[list[str], list[str]]] = []

    if not (len(active) == len(backup) == len(on_l) == len(off_l)):
        warnings.append(
            "excluding 条目数不一致（活跃 %d / 备份 %d），跳过合并，按备份原样写入"
            % (len(active), len(backup)))
        return backup_text, changes, warnings

    toggle = [set(on_l[i]) ^ set(off_l[i]) for i in range(len(on_l))]
    index = 0

    def repl(match: re.Match) -> str:
        nonlocal index
        i = index
        index += 1
        merged = merge_one(active[i], backup[i], toggle[i])
        before = set(backup[i])
        changes.append(([x for x in merged if x not in before],
                        [x for x in backup[i] if x not in set(merged)]))
        return 'excluding="%s"' % "|".join(merged)

    return EXCLUDING_RE.sub(repl, backup_text), changes, warnings


def git_dirty(proj: str, files: list[Path]) -> tuple[bool, str]:
    proj_dir = WS_ROOT / proj
    rel = [str(p.relative_to(proj_dir)) for p in files]
    try:
        proc = subprocess.run(
            ["git", "status", "--short", "--", *rel],
            cwd=str(proj_dir), capture_output=True, text=True)
    except OSError as exc:
        return False, f"无法执行 git（{exc}）"
    if proc.returncode != 0:
        return False, "git status 失败，跳过脏树检查"
    return bool(proc.stdout.strip()), proc.stdout.strip()


def verify(proj: str, target: str) -> list[tuple[str, bool, str]]:
    on = target == "on"
    results: list[tuple[str, bool, str]] = []

    startup = read_text(WS_ROOT / proj / "RTE/Device/RSL10/startup_rsl10.S")
    n = startup.count("SystemFotaInit")
    results.append(("startup SystemFotaInit", (n >= 1) == on, f"={n}"))

    sections = read_text(WS_ROOT / proj / "RTE/Device/RSL10/sections.ld")
    n = sections.count("__rom_start")
    results.append(("sections __rom_start", (n > 0) == on, f"={n}"))

    rte = read_text(WS_ROOT / proj / f"{proj}.rteconfig")
    n = rte.count("Fota")
    results.append(("rteconfig Fota 组件", (n > 0) == on, f"={n}"))

    cproj = read_text(WS_ROOT / proj / ".cproject")
    n = cproj.count("libfota.a")
    results.append(("cproject libfota.a", (n > 0) == on, f"={n}"))

    state = detect_state(proj)
    results.append(("app.h CFG_FOTA", state == target, f"={state}"))

    ok_excl = True
    detail = []
    for value in EXCLUDING_RE.findall(cproj):
        items = value.split("|")
        missing = [f for f in MUST_EXCLUDE if not any(_matches(x, f) for x in items)]
        bad = [f for f in MUST_INCLUDE if any(_matches(x, f) for x in items)]
        if missing or bad:
            ok_excl = False
            detail.append("缺:%s 误排:%s" % (",".join(missing) or "-", ",".join(bad) or "-"))
    results.append(("cproject exclude 完整性", ok_excl, "; ".join(detail) or "ok"))

    return results


def describe_excludes(changes) -> list[str]:
    lines = []
    for i, (added, removed) in enumerate(changes, start=1):
        parts = []
        if added:
            parts.append("+%d %s" % (len(added), ",".join(added)))
        if removed:
            parts.append("-%d %s" % (len(removed), ",".join(removed)))
        if parts:
            lines.append("  config%d exclude: %s" % (i, "  ".join(parts)))
    return lines


def report(proj: str, target: str, changes, warnings: list[str]) -> bool:
    for line in describe_excludes(changes):
        print(line)
    print("自检:")
    all_ok = True
    for name, ok, detail in verify(proj, target):
        all_ok = all_ok and ok
        print("  [%s] %-26s %s" % ("PASS" if ok else "FAIL", name, detail))
    for warning in warnings:
        print("  [WARN] " + warning)
    if any(removed for _, removed in changes):
        all_ok = False
        print("  [FAIL] exclude 有删除项，见上（可能是备份落后，需人工确认）")
    print("结果: %s — %s -> FOTA %s" % ("OK" if all_ok else "有问题", proj, target.upper()))
    return all_ok


def cmd_list() -> int:
    projects = discover()
    if not projects:
        print("未发现带 FOTA 变体的工程")
        return 1
    for proj in projects:
        state = detect_state(proj) or "?"
        missing = variants_complete(proj)
        note = "备份完整" if not missing else "缺: " + ",".join(missing)
        print("%-34s FOTA %-3s  %s" % (proj, state.upper(), note))
    return 0


def cmd_status(project: str | None) -> int:
    projects = [project] if project else discover()
    for proj in projects:
        if not (WS_ROOT / proj / "include" / "app.h").is_file():
            print("%s: 不是有效的工程目录" % proj)
            continue
        state = detect_state(proj)
        print("%s: FOTA %s" % (proj, (state or "?").upper()))
    return 0


def cmd_toggle(project: str, target: str, dry_run: bool, force: bool) -> int:
    proj_dir = WS_ROOT / project
    if not (proj_dir / "include" / "app.h").is_file():
        print("错误: %s 下没有 include/app.h，先确认工程名" % project)
        return 2

    missing = variants_complete(project)
    if missing:
        print("错误: %s 缺少 FOTA 变体文件: %s" % (project, ", ".join(missing)))
        return 2

    current = detect_state(project)
    if current is None:
        print("错误: %s/include/app.h 中找不到 #define CFG_FOTA 行" % project)
        return 2
    if current == target:
        print("%s 当前已是 FOTA %s，无需切换" % (project, target.upper()))
        return 0

    paths = map_paths(project)
    active_files = [a for a, _, _ in paths]

    dirty, detail = git_dirty(project, active_files)
    if dirty and not force:
        print("拒绝执行: 以下文件有未提交改动，切换会就地覆盖。")
        print(detail)
        print("确认要覆盖请加 --force（回滚用 git checkout）。")
        return 3

    print("切换 %s: FOTA %s -> %s%s" %
          (project, current.upper(), target.upper(), "（dry-run）" if dry_run else ""))

    # 读取活跃 .cproject 的排除项（cp 之前），供合并使用
    cproj_active = active_files[0]
    active_text = read_text(cproj_active)
    on_text = read_text(paths[0][1])
    off_text = read_text(paths[0][2])
    backup_text = on_text if target == "on" else off_text

    merged, changes, warnings = merge_excludes(active_text, backup_text, on_text, off_text)

    if dry_run:
        print("将替换: %s" % ", ".join(str(p.relative_to(proj_dir)) for p in active_files))
        print("将把 app.h CFG_FOTA %s" % ("取消注释" if target == "on" else "注释掉"))
        for line in describe_excludes(changes):
            print(line)
        for warning in warnings:
            print("  [WARN] " + warning)
        return 0

    for active, on, off in paths:
        shutil.copyfile(on if target == "on" else off, active)
    write_text(cproj_active, merged)

    new_text, hit = set_define(read_text(app_h(project)), target == "on")
    if not hit:
        print("错误: 未能切换 app.h 的 CFG_FOTA 行")
        return 2
    write_text(app_h(project), new_text)

    print("已替换 4 个文件并切换 app.h")
    return 0 if report(project, target, changes, warnings) else 1


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8")
        except (AttributeError, OSError):
            pass

    parser = argparse.ArgumentParser(description="RSL10 工程 FOTA 开关")
    parser.add_argument("project", nargs="?", help="工程目录名")
    parser.add_argument("state", nargs="?", choices=["on", "off"], help="目标状态")
    parser.add_argument("--list", action="store_true", help="列出所有带 FOTA 变体的工程")
    parser.add_argument("--status", action="store_true", help="只报当前状态")
    parser.add_argument("--dry-run", action="store_true", help="只显示将做哪些改动")
    parser.add_argument("--force", action="store_true", help="脏树时仍然执行")
    args = parser.parse_args(argv)

    if args.list:
        return cmd_list()
    if args.status:
        return cmd_status(args.project)
    if not args.project or not args.state:
        parser.print_help()
        return 2
    return cmd_toggle(args.project, args.state, args.dry_run, args.force)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
