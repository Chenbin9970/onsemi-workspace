# remote_mic_rx_raw PCM 输出改动清单（自 198e51e 起）

> 文档版本：2026-08-24
> 用途：**回退到提交 198e51e 后，按此清单逐步加回改动**，定位"连 RM 重启"问题。
> 相关：验证结论见 [pcm_output_config.md](pcm_output_config.md)。

## 背景

- 提交 198e51e 是 Path B 初版（32-bit 写、双缓冲 raw），当时"能出声但不稳定"。
- 之后为排查 PCM 输出做了大量改动。现在**连上 RM 就重启**，需回退后逐步加回定位。
- 涉及文件：`remote_mic_rx_raw/include/app.h`、`code/app_init.c`、`code/app_func.c`。

---

## 改动 A：PCM 寄存器配置（app.h `PCM_CFG_TX`）

**已验证的可用配置**（测试音自测确认）：

| 字段 | 旧（198e51e） | 新（当前） | 效果 |
|------|--------------|-----------|------|
| `WORD_SIZE` | 16 | **32** | 16 会导致 DMA/PCM 错位、50% 欠载 |
| `FRAME_ALIGN` | LAST | **FIRST** | LAST 会让数据从第 2 个 BCLK 才开始（1 BCLK 滞后） |
| `SUBFRAME` | DISABLE | **ENABLE** | DISABLE 时帧结构异常 |
| `SLAVE/MASTER` | SLAVE | SLAVE | 真实路径从机（测试时临时改 MASTER） |

```c
PCM_WORD_SIZE_32 | PCM_FRAME_ALIGN_FIRST | PCM_SUBFRAME_ENABLE | 其余不变
```

> 注意：当前工作区 `PCM_CFG_TX` 是 `PCM_SELECT_SLAVE`（真实路径）。测试时改成 `PCM_SELECT_MASTER`。

## 改动 B：32-bit DMA 写（关键）

**RSL10 的 PCM TX_DATA 必须 32-bit 写**（一次写一帧 2×16-bit）。16-bit 写导致 50% 帧欠载。

`RX_DMA_PCM_STEREO`（ch5，真实路径）：已是 `WORD_SIZE_32` ✓（198e51e 就对了，不用改）。

打包方向（`DMA_IRQ_FUNC(ASRC_OUT_IDX)`，app_func.c）：
```c
// 旧：pcm_tx_buf[pcm_fill][i] = (s << 16) | s;   // 两个字都=s
// 新：pcm_tx_buf[pcm_fill][i] = s;              // word0=s(低16)，word1=0(高16)
// 输出 0xSSSS0000
```

## 改动 C：DMA 重武装用完整 `Sys_DMA_ChannelConfig`

**怀疑是"连 RM 重启"的原因**：旧重武装只 `Set_ChannelSourceAddress/DestAddress + Enable`，完成后的 LIN DMA 重使能时计数器可能没重载 → 立即完成 → 中断风暴 → 主循环饿死 → 看门狗复位。

```c
// DMA4 (ASRC_OUT_IDX)：
Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT, PCM_FRAME_WORDS, 0,
                      (uint32_t)&ASRC->OUT, (uint32_t)&pcm_raw_buf[pcm_raw_active][0]);
Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

// DMA5 (PCM_DMA_NUM)：
Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                      (uint32_t)&pcm_tx_buf[pcm_active][0], (uint32_t)&PCM->TX_DATA);
Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
Sys_DMA_ChannelEnable(PCM_DMA_NUM);
```

## 改动 D：测试音（`PCM_TEST_TONE`，验证用，非真实路径）

1. `PCM_TEST_BUF_LEN`：24 → **240**（诊断缓冲加大，验证欠载是否随缓冲缩放）。
2. `pcm_test_buf`：`int16_t` → **`int32_t`**，值 `0x5555`（低16=数据，高16=0）。
3. `RX_DMA_PCM_TEST`：`WORD_SIZE_16→32`、`ADDR_CIRC→LIN`、`COMPLETE_INT_DISABLE→ENABLE`。
4. 测试路径启动顺序（SAI 式）：禁 PCM → 预载 TX_DATA → ch5 DMA → 开 DMA5 NVIC → 最后 Enable PCM。
5. `Initialize_Raw_PCM_Output_Type` 加 `#if (PCM_TEST_TONE)` 主机模式分支（USRCLK /42 → BCLK~381k）。

> 注意：当前工作区测试路径的缓冲填充循环是 `0xAAAAAAAA`（最后一次调试改动），静态初始化是 `0x5555`。两者不一致，恢复测试音时注意。

## 改动 E：未验证/存疑

- **ASRC_Reconfig / ASRC_CFG**：DEC_MODE2 公式未改动（198e51e 就有）。
- **信号发生器模拟主机**：外部方波不满足 PCM 从机时序，数据乱。用主机模式自测更可靠。
- **"连 RM 重启"根因未确认**：改动 C（DMA 重武装）是最新尝试，需验证是否解决。

---

## 回退/逐步加回建议顺序

1. 回退 3 个文件到 198e51e（`git checkout -- <files>`）。
2. 先加 **改动 B**（打包 `s`）—— 一行，无风险。
3. 加 **改动 A**（WORD_SIZE_32 / FRAME_ALIGN_FIRST / SUBFRAME_ENABLE）—— 硬件配置。
4. 加 **改动 C**（DMA 重武装完整重配）—— 若重启消失，则根因在此。
5. 测试音（改动 D）只在需要自测时开，不进真实路径。

## 其它未提交内容（非 rx_raw，勿误回退）

- `ble_android_asha/`：asha 工程（已暂存未提交，含 SPI→PCM 改造）。
- 波形文件：`wave*.csv`、`wavetone*.csv`（逻辑分析仪抓包）。
- 文档：`docs/pcm/pcm_output_config.md`（本清单 + 验证结论）。
