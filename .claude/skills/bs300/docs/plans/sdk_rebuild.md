---
interface: bs300_sdk_rebuild
status: implemented
last_updated: 2026-05-29
---

# BS300 SDK 接口重建计划

## 1. 前置信息

| # | 问题 | 答案 | verified |
|---|------|------|:--:|
| 1 | 数据来源和格式 | Flash 480B → decode → 结构化值 → VM → encode → Param I2C | yes |
| 2 | I2C 接口签名 | `soft_iic_dev iic` + soft_iic_* API (GPIO 模拟 I2C) | yes |
| 3 | 芯片地址 | 0x02 (7-bit) | yes |
| 4 | 单向写 / 双向读写 | 双向（读校准/Flash + 写 Param/Burn） | yes |
| 5 | ground truth 可用？ | yes: param_commands_0/1.json, program_0/1.json, param_values_0/1.json | yes |

## 2. 设计方案

- **路径**: Param 路径为主（验配参数 → BS300 RAM），Flash 路径为辅（首次上电 decode）
- **语言**: C（SDK 固件集成），Python 做交叉验证参照
- **复用模块**: proto.py, calib.py, flash_read.py, param.py, math_utils.py

## 3. 问题诊断

现有 C 代码未经 BS300 skill 流程，存在以下问题：

1. **Bin Gain 公式缺少 Volume/EQ 叠加** — 设计文档 §Volume/EQ 运行时修改 明确要求
2. **`bs300_set_volume()` / `bs300_set_eq()` 缺失** — 设计文档 API 定义已列出
3. **`bs300_param_modify()` 基于 Flash buffer 偏移而非结构体字段偏移** — 与设计文档 vm_offset 语义不一致
4. **语音提示输入切换缺失** — 设计文档 API 定义已列出
5. **所有 encode 函数未跑过 C vs Python 交叉验证** — 无法保证编码正确性
6. **模块开关状态机不完整** — 仅 ENR 有 ON→OFF disable 帧

## 4. 实施步骤

### Step 1: C vs Python 交叉验证（修复现有 encode 函数）

对每条 Param encode 函数：
1. 用同一组输入（param_values_0.json / param_values_1.json）分别跑 C 和 Python
2. 对比 48B data section，byte 级差异 ≤ ±1 为 TOLERATED，> ±1 为 bug
3. 两套 Program 全部通过才算完成

```
验证命令：
  py -X utf8 bs300_codegen.py                    # Python 基线
  py -X utf8 scripts/crossval_c_vs_py.py         # C vs Python 对比
```

### Step 2: 修复 Bin Gain 公式

在 `bs300_encode_wdrc_bin_gain()` 中加入：
- `volume_level * 5` 整体增益
- `eq_gain(band_i)` 三段均衡器增益
- EQ band 判定：根据 freq_table 判断 band 频率落在 <500 / 500-2000 / >2000 Hz

```
bin_gain_final[i] = baseline[i] + (volume_level × 5) + eq_gain(band_i) - gain_cal[i] + igd
结果 clamp 到 int8 [-128, 127]
```

### Step 3: 新增 `bs300_set_volume()` / `bs300_set_eq()`

```c
int bs300_set_volume(soft_iic_dev iic, u8 level);   // 写 VM → 重编 Bin Gain → 发 1 条 I2C
int bs300_set_eq(soft_iic_dev iic, s8 low, s8 mid, s8 high);
```

### Step 4: 重构 `bs300_param_modify()`

改为结构化字段偏移：
```c
int bs300_param_modify(soft_iic_dev iic, u8 prog_idx, u16 offset,
                       const u8 *val, u8 len);
// offset: 在 bs300_prog_struct_t 中的字节偏移
// 内部: 写 VM → 依赖拓扑判定 → 重编受影响命令 → 发送
```

### Step 5: 新增语音提示输入切换

```c
u8  bs300_voice_prompt_input_switch(soft_iic_dev iic, u8 target_input);
int bs300_voice_prompt_input_restore(soft_iic_dev iic, u8 original_input);
```

### Step 6: 完善模块开关状态机

对所有模块统一实现 OFF→ON（发全部命令）/ ON→OFF（发 disable 帧）/ ON→ON（增量对比）。

## 5. 验证方式

- [x] Step 1: crossval_c_vs_py.py 全部 32 条命令通过（AGCO ±1, DDM2 ±1 TOLERATED）
- [x] Step 2: Bin Gain 编码与 Python codegen 输出逐 byte 对比（vol=0,eq=0 基线一致）
- [x] Step 3: bs300_set_volume / bs300_set_eq 实现完成
- [x] Step 4: bs300_param_modify 改为结构体偏移 + 模块级增量重编
- [x] Step 5: bs300_voice_prompt_input_switch/restore 实现完成
- [x] Step 6: 全部模块 ON/OFF 状态机完成（WDRC Lmt, Volume, ENR, DFBC, ISS, WNR, AGCO, MM Plus, DDM2, TC/DAI）
- [ ] 硬件烧录测试待确认
