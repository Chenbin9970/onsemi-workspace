---
interface: agco_flash_encode
status: implemented
last_updated: 2026-09-23
---

# AGCO Flash 反向编码（encode_agco_flash）接口设计计划

用于 1654 工程 BLE ID:60/61（Get/SetAGCOSettings）把 AGCO 参数写回程序 Flash。

## 1. 前置信息

| # | 问题 | 答案 | verified |
|---|------|------|:--:|
| 1 | 数据来源和格式 | `bs300_modules_t` 的 `agco_enable / agco_threshold_db / agco_attack_01ms / agco_release_01ms` | yes |
| 2 | I2C 接口签名 | 不涉及 I2C；本地 Flash 480B buffer 就地改 | yes |
| 3 | 芯片地址 | 不涉及 | yes |
| 4 | 单向写 / 双向读写 | 写 Flash（`bs300_storage_write_program`） | yes |
| 5 | ground truth 可用？ | `skills/bs300/data/program_0.json` / `program_1.json`（模块目录含 `23 00 02`） | yes |

## 2. 设计方案

- **路径**: Flash（Program Burn）
- **语言**: C（`remote_mic_rx_coex_1654/code/bs300_param_encode.c`）
- **复用模块**: 复用 codegen `flash_write.py:encode_agco_flash()`，逐行翻译；不新造公式

### 模块布局（已核对 ground truth）

模块目录条目首字节：`0x12`=WDRC / `0x1C`=ENR / `0x1D`=ISS / `0x1F`=WNR / **`0x23`=AGCO**
AGCO 长度字 `23 00 02` → `length_words=2` → **6 字节**（program_0/1 均是）。

| 字节 | 内容 |
|------|------|
| 0 | attack bits[7:0] |
| 1 | attack bits[11:8]（低 4 位） \| release bits[3:0]（高 4 位） |
| 2 | release bits[11:4] |
| 3 | threshold = **\|dB\|**（uint8，非带符号） |
| 4-5 | `0x00 0x00` |

对拍样本：`encode_agco_flash(atk=1000, rel=0, thr=3)` → `E8 03 00 03 00 00`，
与 `flash_write.py` 自测注释一致；`decode_agco_flash` 逆运算回 1000/0/3 ✓。

### 关键约定

- **threshold 存绝对值**：codegen 写 `abs(threshold_db)`，C 编码器同样取 abs；
  C 侧 `decode_agco_flash` 用 `(int8_t)data[3]` 读，二者对 `|dB| ∈ [0,30]` 自洽。
- **enable 无 Flash 位**：解码器在模块存在时硬编码 `agco_enable=1`，故 `agco_enable`
  被复用为「模块存在」标志；BLE Set 在模块缺失时显式失败，不静默空写。

## 3. 验证方式

- [x] 与 codegen 自测样本逐字节一致（`E8 03 00 03 00 00`）
- [x] encode → decode roundtrip 自洽（手工推演）
- [ ] 上板实测（用户侧）：设 AGCO → 断电重启 → GetAGCOSettings 值保持
