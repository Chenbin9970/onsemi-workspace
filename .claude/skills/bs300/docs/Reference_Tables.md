# BS300 查找表 & 枚举参考

## 1. 常见查找表

### Table 2-2: Attack/Release Time (122 条目, index → ms)
- 0–20ms 步进 1ms, 21–50ms 步进 2-5ms, [121]=16000ms
- `_TIME_TABLE`: list[int], `_time_to_index(ms)`: ms → 最近索引

### Table 2-3: WDRC Ratio (128 条目, index → ratio)
- 单调递增, [0]=0, [1]=0.22, [32]=1.0, [127]=20.0
- `_RATIO_TABLE`: list[float], `_ratio_to_index(ratio)`: float → 最近索引

### 频率表 (32 条目, index → Hz)
- [0]=0, [1]=125, 步进 250Hz, [31]=7625

### 枚举映射

| 枚举 | 映射 |
|------|------|
| DFBC Mode | Slow FBC=0x01, Slow Weak=0x03, Slow Strong=0x07, Fast FBC=0x09, Fast Weak=0x0B, Fast Strong=0x0F |
| WNR Preset | Minimal=1, Light=3, Moderate=6, Strong=9, Maximum=12 |
| Input Cmd | FrontMic=0x03, RearMic=0x04, Telecoil=0x05, DAI=0x06, MM_Plus=0x17, DDM2=0x1B, DualMic=0x1E |

## 2. 未实现模块（按优先级）

**高优**：基本控制 (Mute/Active/IsConnect 等 8 条)、WDRC Display (4 条)、Device Config (3 包)

**中优**：系统配置 (Digital VC/Standby/Volume Learning 等 12 条)、信号发生器 (Stimulate/ToneGen 3 条)、WDRC Acclim Param (4 条)、Flash Noise Gen2 + WDRC Acclim

**低优**：Tune Alerts (28 条)、Voice Prompt (~60 条)

## 4. DDM2 专用查表 (经验证 @2026-06-03)

### 4.1 Flash polar → Param frac24

| Flash uint3 | Polar Mode | Param frac24 |
|-------------|-----------|-------------|
| 0 (0b000) | Bi-directional | `0x000000` |
| 1 (0b001) | Hyper-cardioid | `0x200000` |
| 2 (0b010) | Super-cardioid | `0x300000` |
| 3 (0b011) | Cardioid | `0x400000` |
| 4 (0b100) | Omni-directional | `0x7FFFFF` |

ADM 模式下强制 Omni = `0x7FFFFF`。

### 4.2 Omni Threshold frac48 查表 (121 条)

索引 `idx = round((avg_mic1 - omni_mt) * 10) / 10`，范围 [0, 120]:

| idx | frac48 | idx | frac48 | idx | frac48 |
|-----|--------|-----|--------|-----|--------|
| 0-12 | `0x800000000000` (max) | 50 | `0xA28B0E` | 90 | `0x2249E6` |
| 13 | `0x669CCFD683FF` | 60 | `0x085B09` | 94 | `0x0DA639` |
| 20 | `0x147878FD6F87` | 70 | `0x667224` | 100 | `0x036D96` |
| 30 | `0x020BED392EE6` | 80 | `0x56F733` | 110 | `0x0057BD` |
| 40 | `0x0034616F7BF4` | 85 | `0x6C7159` | 120 | `0x0008C5` |

完整表见 `bs300_param.c:bs300_omni_frac48_l[121]`。

### 4.3 DDM2 Flash 字节布局

```
Byte 0: 0x00 (reserved)
Byte 1: omni_threshold (uint8, I2C encoded)
Byte 2: [7]=1 [6]=open_ear [5]=mode(FDM/ADM) [3]=apply_omni [2:0]=polar(uint3)
Byte 3-5: cutoff_frequency (uint24 LE)
```

### 4.4 Omni 公式

- `omni_mt = (I2C_value - 2) / 4 + 40` (I2C→MT 转换)
- `cal = avg(mic1_band[1:31])` (不是 avg_output)
- `frac48 = 2^47 / 10^(0.10001 * (cal - omni_mt) - 1.20412)`

交叉验证: `data/ddm2_validation.json`
