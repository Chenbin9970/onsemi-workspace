---
name: bs300
description: BS300 助听器 DSP 协议实现。I2C 通信协议、Flash/Param 双路径编码、校准数据、验配参数。当用户需要写/改 Param 或 Flash 编码器、生成 C 代码、分析协议字段、调试 I2C 指令差异、运行交叉验证、修改 bs300_codegen.py 或其子模块（proto/calib/flash_read/flash_write/math_utils/param）时使用。
---

# BS300 助听器 DSP 协议实现

基于杰理 BS300 芯片的 I2C 通信协议实现，覆盖校准数据解析、Program Burn Flash 编解码、验配参数 Param I2C 指令生成。

## 0. 计划前置 — 先收集信息再定方案

**如果你正在制定 BS300 相关接口的计划，先确认以下信息再开始设计。这些是 skill 后续执行必需的前置条件，缺了任何一个后面都会被打回来重问。**

### 0.1 必须从用户获取的信息

| # | 问题 | 为什么需要 | 如果没有 |
|---|------|-----------|---------|
| 1 | 数据从哪来？格式是什么？ | Step 0 输入格式确认 | 无法写解析代码，猜格式必出错 |
| 2 | I2C 接口签名是什么？（硬件 I2C / 软件模拟 / 上层封装） | C 函数签名设计 | 写出的 `i2c_write()` 调用方不存在 |
| 3 | 目标芯片地址？I2C 从机地址？ | 帧构建 | 用错地址芯片不响应 |
| 4 | 只需要发送指令（单向写），还是需要读回数据（双向）？ | 决定是否实现读流程 + 轮询 FURPROC | 单向写丢响应，双向读没轮询会读到垃圾 |
| 5 | 有没有可用的 ground truth 做交叉验证？ | 验证闭环 | 没有参照数据时只能做 roundtrip 自洽，可信度打折扣 |

### 0.2 信息收集后再定方案

确认以上 5 条后，再决定：
- 走 Flash 路径还是 Param 路径（§3 决策树）
- 生成 Python（codegen 扩展）还是 C（固件集成）还是两套都要
- 复用现有模块（`proto.py` / `flash_read.py` / `param.py`）还是从头写

### 0.3 产出：写入 `docs/plans/` 目录

计划确定后，按接口名分文件存入 `docs/plans/` 目录（如 `docs/plans/flash_read.md`、`docs/plans/calib_write.md`）。每个文件使用标准模板（见下方），agent 必须逐字段填写。

**计划文件模板**：

```markdown
---
interface: <接口名称>
status: draft | confirmed | implemented | stale
last_updated: <YYYY-MM-DD>
---

# <接口名称> 接口设计计划

## 1. 前置信息

| # | 问题 | 答案 | verified |
|---|------|------|:--:|
| 1 | 数据来源和格式 | <用户确认的格式> | no |
| 2 | I2C 接口签名 | <函数签名> | no |
| 3 | 芯片地址 | <地址值> | no |
| 4 | 单向写 / 双向读写 | <选择> | no |
| 5 | ground truth 可用？ | <yes/no，文件路径> | no |

## 2. 设计方案

- **路径**: Flash / Param / 校准
- **语言**: Python / C / 两套
- **复用模块**: <列出>

## 3. 验证方式

- [ ] crossval_c_vs_py.py 通过
- [ ] 端到端对比通过
- [ ] ground truth 字段级匹配
```

### 0.4 计划文件使用规则

**规则 0.4.1 — status 生命周期**：
- `draft`：信息尚未收齐，有 `verified: no` 或用户答"不知道"的字段。代码中对应位置加 `// TODO: confirm with upstream` 注释。
- `confirmed`：全部 5 条确认完毕，可以开始实现。
- `implemented`：代码完成且交叉验证通过，`verified` 全部更新为 `yes`。
- `stale`：代码变更后未更新计划。agent 每次加载 plan 时对比 `last_updated` 和关联代码文件的 git log，发现代码更新但 plan 未更新时标记为 stale。

**规则 0.4.2 — verified 字段**：
- `no`：用户口头确认，未经过代码验证。agent 对此决策持保留态度，交叉验证失败时优先怀疑这些项。
- `yes`：已经通过交叉验证或实际跑通。
- `how`：非标准值，记录验证方式（如 "用 logic analyzer 抓过实际 I2C 波形"）。

**规则 0.4.3 — 多接口隔离**：
每个接口一个独立 plan 文件，放在 `docs/plans/` 目录下。禁止多个接口共用一个 plan 文件，防止字段混淆。

**规则 0.4.4 — 未知项不伪装**：
用户答不出的问题填 `unknown`，不要猜测填入伪装成已确认。对应 C 代码中加 `// TODO: confirm`，Python 代码中加 `raise NotImplementedError("待确认: xxx")`。

**规则 0.4.5 — 计划变更（plan changed）**：
用户中途修改计划时，agent 必须执行以下流程，不能只改 plan 文件就当没事发生：

```
用户说"XX 要改成 YY"
        │
        ▼
1. 更新 plan 文件:
   - 修改对应字段
   - status 回退（implemented → confirmed, confirmed → draft）
   - 被改的决策项的 verified 回退到 no
   - last_updated 更新为当天

2. 找出受影响代码:
   - grep 搜索旧值/旧格式在代码中的引用
   - 列出所有需要修改的文件和函数，让用户过目

3. 修改代码:
   - 逐一修改并标注 "plan v<N> 变更: <旧值> → <新值>"
   - 如果旧逻辑和新逻辑并存（过渡期），加注释说明条件

4. 重新验证:
   - 之前通过的交叉验证可能不再有效
   - 重新跑 py -X utf8 bs300_codegen.py 确认基线
   - 如果 ground truth 也变了，先确认新 ground truth 可用再跑
```

**举例**：plan 记录帧格式为 54 字节，status=implemented。用户说"不对，新固件去掉了 Len 字段，现在是 53 字节"。
- agent 更新 plan：status → confirmed，verified → no
- grep 所有 `raw[5:53]` / `len == 54` / `0x10`（Len 字段值）
- 列出受影响文件，用户确认后修改
- 重新跑交叉验证——如果 ground truth 还是旧的 54 字节数据，交叉验证会失败，此时不能标记 implemented，需要等待新 ground truth

**规则 0.4.6 — status 完整生命周期**（补充 0.4.1）：
```
draft ──→ confirmed ──→ implemented
  ↑          ↑              │
  │          │              │ 代码变更但 plan 未更新
  │          │              ▼
  │          └─────────── stale
  │                          │
  └──────── 用户修改计划 ─────┘
  (implemented/confirmed 均可回退到 draft/confirmed)
```

---

> **核心约束 1：任何涉及 BS300 协议的代码，必须先执行下方 §1 的 6 步流程，再读 `docs/BS300 Protocol Handbook v3.md` 确认字段布局。手册是唯一权威来源，禁止凭记忆或推测实现。**
>
> **核心约束 2：任何代码输出必须经过验证闭环——用 `program_*.json`（Flash 路径）或 `param_commands_*.json`（Param 路径）做逐字段/逐 byte 交叉验证，两套 Program (0+1) 全部通过才算完成。没有芯片参照数据的模块，至少做 encode→decode roundtrip 自洽验证。**
>
> **核心约束 3（铁律）：以下交叉验证数据源文件来自芯片回读或 MT 验配软件导出，是 ground truth。严禁任何形式的修改，只读访问。**
> ```
> program_0.json / program_1.json          — Flash 回读数据（芯片）
> param_values_0.json / param_values_1.json — MT value_in_MT 配置
> param_values_raw_0.json / param_values_raw_1.json — MT 原始 dB 值
> param_commands_0.json / param_commands_1.json — Param I2C 指令（芯片回读）
> calibration.json / calibration_values.json — 校准数据（芯片回读）
> ```
> **如果代码适配需要修改这些文件的字段名或结构，只能向开发者提出建议，由开发者手动修改。Agent 不得自行写入。改 ground truth 等于改考试答案，破坏验证闭环的可信度。**

## 1. Agent 执行流程（6 步强制）

```
Step 0 — 外部输入确认（前置必做，不可跳过）:
  任何涉及芯片回读数据、固件 log hex、外部工具导出数据的任务，格式不是代码内部闭环的，
  在动手前必须：
  1. 列出你假设的字节布局（每帧多少字节？有无帧头/帧尾？payload 偏移量？）
  2. 向用户展示一帧的结构图
  3. 用户确认后再继续
  如果用户也不确定格式 → 不要猜，先读数据文件分析特征字节（找 0x02 帧头、试不同偏移
  的校验和、检查 48B 边界对齐），把分析结果作为假设提出，让用户确认。

Step A — 判断任务类型:
  ├─ "写 Flash 编码" / "Program Burn" → Flash 路径 (docs/Protocol_QuickRef.md §2 模块表 Flash 列)
  ├─ "写 Param 指令" / "验配参数" / "I2C 命令" → Param 路径 (docs/Protocol_QuickRef.md §2 模块表 Param 列)
  ├─ "读校准数据" / "CalibData" → 校准路径 (手册 §校准数据)
  ├─ "生成 C 代码" / "Python → C" / "C 编码器" → C 生成路径 (docs/C_Code_Generation.md + PYTHON_TO_C_ISSUES.md)
  └─ "对比/分析/调试差异" → 分析路径 (docs/Task_Templates.md §1.3)

Step B — 读手册对应章节:
  py -X utf8 bs300_codegen.py  # 先跑一遍现有测试确认基线
  → 在 BS300 Protocol Handbook v3.md 中搜索模块名/命令字
  → 确认字段布局、位宽、字节序

Step C — 对照 §2 已知陷阱自查:
  → 找到本任务对应的公式 → 确认取整规则
  → 确认是否有 cal_offset / 特殊映射

Step D — 实现:
  → 在 bs300_codegen.py 中添加编码函数
  → 运行 py -X utf8 bs300_codegen.py 验证

Step E — 验证闭环（强制，不可跳过）:
  ├─ Flash 路径:
  │   1. param_values_*.json → encode → program_burn_write_*.json (I2C 写帧)
  │   2. program_burn_write_*.json → decode → 对照 program_*.json (芯片回读)
  │   3. 对照 param_values_*.json (MT 配置全字段)
  ├─ Param 路径 → 用 param_commands_0.json + param_commands_1.json 做逐 byte 对比
  │    **±1 容忍规则**: byte 级差异 ≤ ±1 视为 rounding tolerance，标记为 TOLERATED，不阻塞通过。
  │    只有 max byte-diff > 1 才算真实 mismatch。根因是芯片使用 float_32/int_16t 运算
  │    后 saturate 到 int8_t，不同运算类型导致 ±1 差异，无法在 Python 端完全消除。
  ├─ 校准解析 → 用 calibration.json + calibration_values.json 做字段对比
  └─ 两套 Program 数据必须全部通过，缺一不可
```

**警告：跳过 Step 0/B/C/E 直接实现，已导致 2+ 个公式 bug 和多次 hex 对比字节数错误。任何未经输入确认和交叉验证的输出视为未完成。**

## 2. 已知陷阱（前置必读）

### 2.1 取整规则矩阵（最易出错！）

| 场景 | 规则 | 示例 |
|------|------|------|
| `|dB| * 65536 / 6.02` (AGCO Thr) | **ceil** | `ceil(abs(-9) * 65536 / 6.02)` |
| `dB * 65536 / 6.02` (Volume gain) | **truncation** | `int(6 * 65536 / 6.02)` |
| `SNR * 32 / 6.02` (ENR SNR Th) | **round** | `round(32 / 6.02 * snr_th_db)` |
| `sum / N` 平均数 | **ceil** / **floor** / **round** | WNR: ceil, WDRC KP: floor, ISS: round |
| `frac48` 转换 (ISS) | **round** | `round(...)` 不是 `int(...)` |
| `1.0 / (10**exp) * (1<<47)` (ISS) | **round** | `round(1.0 / (10**exp) * (1<<47))` |

**Python 实现速查**：
```python
# ceil:  (N * 327680 + 300) // 301    # 65536/6.02 = 327680/301
# floor: (N * 327680) // 301
# round: round(N * 327680 / 301)
```

### 2.2 历史 bug 清单

| # | 模块 | bug | 根因 | 修复 |
|---|------|-----|------|------|
| 1 | **AGCO Threshold** | 参数值偏差 | 用了 truncation 而非 ceil | `ceil(abs(thr) * 65536 / 6.02)` |
| 2 | **ENR SNR Threshold** | 参数值偏差 | 用了 `int()` 而非 `round()` | `round(32/6.02 * snr_th_db)` |
| 3 | **WDRC KP/Lmt Threshold** | per-channel 个别通道偏差 | CH3/CH6 cal_offset=+1 (P0 TC), P1 FM 已全部在 ±1 容忍范围内 | ✓ (±1 容忍) |

### 2.3 其他已知问题

- **ENR NT / UNT** (0x8040C2 / 0x8050C2): 两个 Program 均不匹配，根因是芯片内部 `SNR_Frequency_Spacing[]` 数组与 ENR 频段划分命令中的 band 区间不一致，导致 mic1Cal 差异（最大 ~3）。公式 `NT*10 = round(5.307 * (noise_th + 130 - mic1Cal - igd) - 371.2)` 已确认正确。当前暂时接受此差异，等待芯片端提供数组真值后修复。
- **band_0 数据不可用**: 所有校准 band 计算从 band_1 开始。
- **WDRC KP Threshold + AGCO**: byte-level 差异均在 ±1 内（rounding tolerance），已验证通过。

## 3. Flash vs Param 路径决策树

```
value_in_MT (验配软件的参数值)
    │
    ├─ 目标：写入 EEPROM，上电加载？
    │     └─ YES → Flash 路径 (Program Burn)
    │           ├─ 编码格式: bit-packed 紧凑排列
    │           ├─ 命令字: 0x800001~0x80D001 + 0x80Y021 (Burn End)
    │           ├─ 公式特点: 不做校准补偿，存原始值或简单偏移
    │           ├─ 例: WDRC KP Th = value_in_MT (原始值)
    │           └─ 例: bin_gain = 27 + value_in_MT (7-bit int7)
    │
    └─ 目标：立即生效 (写入 RAM)？
          └─ YES → Param 路径 (验配参数指令)
                ├─ 编码格式: word-aligned (24-bit), 零填充
                ├─ 命令字: 0x8NNNNN 各模块独立命令
                ├─ 公式特点: 含校准补偿 (需 CalibData)
                ├─ 例: WDRC KP Th = 60 + th - avg(mic1[fidx..+1]) - igd
                └─ 例: bin_gain = bin_gain - gain_cal + igd (8-bit int8)
```

**关键区分**：同名字段在两条路径中编码公式不同，切不可混用！

## 4. 文件地图

```
.claude/skills/bs300/
├── bs300_codegen.py            # Hub: import 所有子模块并 re-export
├── proto.py                    # Step 0: I2C 帧构建/解析/校验
├── calib.py                    # Step 1: 校准数据解析
├── flash_read.py               # Step 2: BitReader + Flash 解码
├── flash_write.py              # Step 3: BitWriter + Flash 编码 + 烧录
├── math_utils.py               # Step 4: 定点数学 + 查找表
├── param.py                    # Step 5: 32 条 Param I2C 编码器
├── docs/                       # 9 个 .md 文档 + plans/
│   ├── BS300 Protocol Handbook v3.md
│   ├── BS300_Implementation_Guide.md
│   ├── Program_Burn_Guide.md
│   ├── ENR_NT_Analysis.md
│   ├── Protocol_QuickRef.md
│   ├── Task_Templates.md
│   ├── Reference_Tables.md
│   ├── C_Code_Generation.md
│   └── plans/                # 接口设计计划（按接口名分文件）
├── scripts/                    # 4 个辅助 Python 脚本
│   ├── crossval_c_vs_py.py
│   ├── crossval_program_vs_mt.py
│   ├── gen_c_int_math.py
│   └── calc_mic1cal.py
├── data/                       # 12 个 JSON 数据文件（ground truth + 生成产物）
│   ├── calibration.json / calibration_values.json
│   ├── program_0.json / program_1.json
│   ├── param_values_0.json / param_values_1.json
│   ├── param_values_raw_0.json / param_values_raw_1.json
│   ├── param_commands_0.json / param_commands_1.json
│   └── program_burn_write_0.json / program_burn_write_1.json
└── output/                     # C 代码生成产出
    ├── bs300_encode_tables.h
    └── bs300_program_read.c
```

`PYTHON_TO_C_ISSUES.md` 位于项目根目录。

### 按需加载指引

| 场景 | 读取文件 |
|------|---------|
| **每次加载 skill 时优先检查** | `docs/plans/*.md`（如果存在，按 status 过滤） |
| 写编码器，查命令字/公式/取整规则 | `docs/Protocol_QuickRef.md` |
| 输入切换 (input_selection) 影响哪些 I2C 指令 | `docs/Protocol_QuickRef.md` §4 |
| 执行具体任务，查模板步骤 | `docs/Task_Templates.md` |
| 偶尔查表/枚举 | `docs/Reference_Tables.md` |
| 生成 C 代码 | `docs/C_Code_Generation.md` + 根目录 `PYTHON_TO_C_ISSUES.md` |
| 字段布局权威定义 | `docs/BS300 Protocol Handbook v3.md` |
| 验证清单自查 | `docs/BS300_Implementation_Guide.md` |
| Flash 读写操作 | `docs/Program_Burn_Guide.md` |
| 调试 ENR NT/UNT | `docs/ENR_NT_Analysis.md` |
| 运行交叉验证 | `py -X utf8 bs300_codegen.py` |
| Flash vs MT 全字段对比 | `py -X utf8 scripts/crossval_program_vs_mt.py` |
| C vs Python 交叉验证 | `py -X utf8 scripts/crossval_c_vs_py.py` |
