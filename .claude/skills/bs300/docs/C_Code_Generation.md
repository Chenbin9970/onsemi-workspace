# BS300 C 代码生成注意事项

> **详细记录**: 项目根目录 `PYTHON_TO_C_ISSUES.md`，以下是从 Python → C 转换中踩过的坑。

## 1. 历史 bug 清单（C 生成相关）

| # | 模块 | bug | 根因 | 修复 |
|---|------|-----|------|------|
| 1 | **全模块** | 交叉验证 32/32 通过，但设备实际输出错误 | `crossval_c_vs_py.py` 只验证 encode 公式，不验证 Flash decode → struct → encode 完整数据流 | 增加端到端验证：Flash 读回 → decode → encode → 设备输出对比 |
| 2 | **WNR** | 全量同步 WNR 指令未发送 | `sync_program` 未做 `\|= 0x01`，encode 函数内部 `if (!(enable & 0x01)) return 0` 提前返回 | `sync_program` 和 `decode_wnr_flash` 中加 `\|= 0x01` |
| 3 | **WNR** | 增量切换 WNR 误发 disable 帧 | `switch_program` 未做 `\|= 0x01`，VM 中 bit0=0 → 走进 `o_ena==1 && n_ena==0` 分支 → 发全 0 帧关掉 WNR | `switch_program` 中加防御性 `\|= 0x01` |
| 3b | **WNR** | `dual_mic_mode_sel` 语义被覆盖 | 协议定义 bit0 为双麦模式开关（仅双麦场景使用），代码将其重新解释为 WNR 总开关。`\|= 0x01` 是有意的设计选择（Module Directory 存在即启用），不是 bug fix。单麦设备上可能向芯片发送了错误的 `dual_mic_mode_sel=1` | 暂接受，未来如需支持单麦 WNR 需拆分字段 |
| 4 | **WNR** | 金色参照对比假阳性 | `param_values_1.json` 中 preset 值与 `program_burn_write_1.json` 实际烧录值不一致 | 对比前确认输入参数（preset、calib、input_type）与 Flash 实际内容一致 |
| 5 | **全模块** | `gen_golden_ref.py` ImportError | 工具脚本使用旧 API 名，主库 API 签名已大幅变动，无法简单改名修复 | ✓ 已移除，功能由 `crossval_c_vs_py.py` 替代 |
| 6 | **全模块** | 修改 .h 后 make 不重新编译 | SDK Makefile 头文件依赖追踪不完整 | 关键 .h 修改后 `touch` 对应 .c 强制重编译，或 `make clean && make` |
| 7 | **全模块** | `const` 编译错误 | `sync_program` 需要修改传入 struct 的 enable 位，但参数声明为 `const` | 全量同步函数不加 `const`，它可能需要修改 struct |
| 8 | **全模块** | hex 对比定位延迟 | 手动从设备 log 复制 hex 做对比，多次构造出错误长度（59B、56B 而非 48B） | 用脚本自动提取 I2C payload（跳过 4B 帧头 + 1B 校验和），禁止手动复制粘贴 |
| 9 | **WNR** | Setup 与 Band/SingleMic 的 SSP 映射方式不一致 | Setup 用 `wnr_preset_to_ssp[preset]` 查 ssp_value 算 word3 阈值 (6)，Band 用 `preset-1` 做 ssp_level 查 offset 表列。两者是不同映射体系：ssp_value∈{0,1,3,6,12} vs ssp_level∈{0,1,2,3} | 已确认正确，见 §5 完整映射表 |

## 2. C 代码生成强制规则

**规则 1 — 端到端验证**：交叉验证（`crossval_c_vs_py.py`）通过只证明 encode 公式的整数算术化正确。C 代码生成完成后，必须额外验证 Flash → struct → encode → 设备输出的完整链路。仅靠交叉验证通过不能判定任务完成。

**规则 2 — VM 缓存**：`bs300_driver.c` 初始化优先从 VM 加载 struct。当 Flash 数据格式或内容升级时，必须 bump `BS300_STRUCT_VERSION` 触发重新读取。版本号是唯一可靠的缓存失效机制。

**规则 3 — enable 标志来源**：Flash 格式中，模块是否启用的权威来源是 Module Directory（模块在目录中存在 = 启用），不是模块数据字节中的 enable 位。Flash decode 必须从目录存在性推断 enable。

**例外 — WNR `dual_mic_mode_sel`**：WNR Flash Byte 0 是 `dual_mic_mode_sel`（0=Disable, 1=Enable, 仅双麦使用），不是 WNR 模块总开关。代码将其重新解释为 WNR enable，并强制 `|= 0x01`。这是有意的设计选择，但意味着单麦设备可能收到错误的 dual_mic_mode=1 配置。详见 `PROGRAM_SWITCH_LOGIC.md`。

**规则 4 — 金色参照一致性**：用 `param_values_*.json` 生成金色参照前，必须确认其值与 `program_burn_write_*.json` 中实际烧录的内容一致。两套数据不同步会导致假阳性。

**规则 5 — API 同步**：重命名 `bs300_codegen.py` 或其子模块中的函数时，必须同步更新 `crossval_c_vs_py.py` 等所有调用方。改完用 `grep` 全局搜索旧函数名确认无遗漏。

**规则 6 — 构建验证**：修改 `.h` 文件后，SDK Makefile 可能不触发重新编译。用 `touch` 强制更新对应 `.c`，或直接 `make clean && make`。确认 ELF 时间戳更新后再烧录。

**规则 7 — const 使用**：全量同步函数（如 `sync_program`）可能需要修改传入 struct 的字段（如强制置位 enable），参数声明不加 `const`。

**规则 8 — hex 对比自动化**：设备 log 中的 I2C 帧包含 4B 帧头 + 1B 校验和。提取 48B payload 用脚本完成，禁止手动复制粘贴。对比也用脚本自动化，避免字节数错误。

**规则 9 — SSP 映射一致性**：同一模块的不同编码函数（如 Setup / Band / SingleMic）如果使用 SSP 查表，必须使用同一种映射方式。不一致时在代码中写注释说明原因。

**规则 10 — I2C 帧输入自适应 + 校验**：任何接收外部 I2C 帧数据的 C 函数，必须自带格式检测，不能硬编码假设一种帧格式。检测逻辑：尝试常见偏移量（0/1/4 字节帧头），计算校验和验证，定位 48B payload。失败时显式返回错误码（如 `-1`），禁止静默假设格式。例：

```c
// 好的做法
int bs300_parse_frame(const uint8_t *raw, int len, uint8_t *payload_out) {
    int off;
    for (off = 0; off <= 4; off++) {
        if (len >= off + 49 && bs300_checksum(raw + off, 49) == raw[off + 49]) {
            memcpy(payload_out, raw + off, 48);
            return 0;
        }
    }
    return -1;  // 格式不识别，显式报错
}
// 坏的做法
int bs300_parse_frame(const uint8_t *raw, uint8_t *payload_out) {
    memcpy(payload_out, raw + 1, 48);  // 硬编码假设偏移量=1
    return 0;
}
```

**规则 11 — 字段范围合理性检查**：解析出的关键字段必须在合理范围内，超出范围说明数据来源或格式有问题。至少检查：
- WDRC `total_channels` / `num_channels` ∈ [1, 16]
- ENR `nfsf` / `nhsf` / `nnsf` ∈ [0, 15]
- `kp_mode` ∈ {1, 2}（1KP 或 2KP）
- 校验和必须匹配（不匹配时数据已损坏，后续解析无意义）

```c
if (prog->wdrc.num_channels < 1 || prog->wdrc.num_channels > 16) {
    return -2;  // 数据损坏或格式错误
}
```

## 3. C 代码生成检查清单

在声称"C 代码生成完成"前逐条确认：

```
□ 1. crossval_c_vs_py.py 通过（32/32）？
□ 2. 端到端验证：Flash 读回 → decode → encode → 设备输出对比通过？
□ 3. 如有数据格式变更，BS300_STRUCT_VERSION 已 bump？
□ 4. Flash decode 中 enable 标志从 Module Directory 推断？
□ 5. 金色参照输入与 Flash 实际内容一致（preset/calib/input_type）？
□ 6. crossval_c_vs_py.py 等工具脚本 API 与 bs300_codegen.py 同步？
□ 7. 修改 .h 后确认 .c 被重新编译（检查 ELF 时间戳）？
□ 8. sync_program 参数未加 const（如需修改 struct）？
□ 9. hex 对比用脚本自动化，无手动复制粘贴？
□ 10. 同一模块多函数的 SSP/查表映射方式一致？
□ 11. I2C 帧输入函数有格式自适应检测（规则 10）？
□ 12. 关键字段有范围合理性检查（规则 11）？
```
