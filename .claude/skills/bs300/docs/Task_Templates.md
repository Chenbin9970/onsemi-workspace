# BS300 任务模板 & 编码器规范

## 1. 常见任务模板

### 1.1 新增 Param 编码器

```
1. 在手册中搜索模块名 → 找到命令字 + Data Section 字节布局
2. 在 bs300_codegen.py 中找参照函数 (如 encode_wdrc_kp_threshold_param)
3. 实现 encode_xxx(params, calib: CalibData) -> bytes[48]:
   - data = bytearray(48)
   - 按手册布局逐字段填入
   - 注意 packing 规则: int8/uint8 用 _pack_bytes, int12 用 _pack_int12_2pw, uint6 用 _pack_uint6_4pw
4. 确定取整规则：公式用到 65536/6.02 或 sum/N？→ 查 Protocol_QuickRef.md + ../bs300.md §2.1
5. 在 test_step5() 中添加单元测试
6. 若有芯片参照数据，在 test_step5_crossval() 中添加 _cmp_words()
7. 运行 py -X utf8 bs300_codegen.py 验证
8. 更新 Protocol_QuickRef.md 模块表中对应状态
```

### 1.2 新增 Flash 编码器

```
1. 在 Program_Burn_Guide.md 中查模块的 Flash 编码公式
2. 确认是 bit-packed 还是 byte-aligned：
   - bit-packed → 使用 BitWriter (LSB-first, 同 BitReader 反操作)
   - byte-aligned → 直接按字节填充
3. Flash 公式不做校准补偿 (不同于 Param 路径!)
4. 实现 encode_xxx_flash(...) -> bytes
5. 在 test_step3() 或独立测试中做 roundtrip: encode → decode → assert 字段一致
6. 若有芯片 readback 数据，做交叉验证
```

### 1.3 调试指令差异

```
1. 运行 py -X utf8 bs300_codegen.py 确认基线全绿
2. 用 _cmp_words("模块名", 0x8XXXXX, encoded) 定位差异 word
3. 对差异 word，逐字段拆解：
   - 字段值 → 逆向公式 → 对比手动计算 vs 芯片目标值
   - 确定是取整规则问题、cal_offset 问题、还是数组映射问题
4. 若涉及 mic1Cal band range，运行 py -X utf8 calc_mic1cal.py 分析
5. 若涉及 Flash，运行 py -X utf8 crossval_program_vs_mt.py 全字段对比
```

### 1.4 验证闭环（强制）

```bash
# 1. Flash 路径 — 芯片回读交叉验证 (Program 0 + 1)
py -X utf8 crossval_program_vs_mt.py       # 836 checks, 0 errors, 2 warnings ✓

# 2. Param 路径 — I2C 指令逐 byte 对比 (Program 0 + 1, 共 62 条指令)
py -X utf8 bs300_codegen.py                # 跑全部测试 + test_step5_crossval
# P0: 28 byte-exact, 1 tolerated (±1), 2 known diff (ENR NT/UNT)
# P1: 27 byte-exact, 2 tolerated (±1), 2 known diff (ENR NT/UNT)
# Tolerated: AGCO (±1, 两 Program 共通), WDRC KP Threshold (±1, P1 only)
# Known diff: ENR NT + ENR UNT, 根因 SNR_Frequency_Spacing, mic1Cal diff ≤3 暂接受

# 3. 校准 — 字段级对比 ✓
# calibration.json + calibration_values.json: mic1_band×32, output_band×32, 6 个短模块

# 4. 自洽性 — encode→decode roundtrip (无芯片参照时兜底)
```

**验证闭环数据流**：
```
param_values_raw_*.json   (原始 dB 值)
    │  raw → value_in_MT 翻译
    ▼
param_values_*.json        (验配参数值)
    │
    ├── Flash 路径 ──→ [encode] ──→ program_burn_write_*.json (I2C 写帧)
    │                      │
    │                      └──→ [decode] ──→ 对照 program_*.json (芯片回读)
    │                                      对照 param_values_*.json (MT 配置)
    │                      
    └── Param 路径 ──→ param_commands_*.json (I2C 指令) ──→ _cmp_words() ──→ ✓/✗
```

**验收标准**：两套 Program 数据全部通过才算完成，单套通过不算验证通过。

## 2. 编码器签名规范

```python
# 所有 Param 编码器统一签名，返回固定 48 字节
def encode_xxx(params, calib: CalibData = None) -> bytes:
    data = bytearray(48)
    # ...
    return bytes(data)

# 字段打包规则
# int8/uint8: _pack_bytes() — 连续字节排列
# int12/uint12: _pack_int12_2pw() — 2 CH per 24-bit word
# uint6: _pack_uint6_4pw() — 4 CH per word, MSB=ch1
# frac24/int24/uint24: bs300_set_word() — 1 值 per word
```
