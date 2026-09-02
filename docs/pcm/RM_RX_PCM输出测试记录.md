# RM RX PCM 输出 — 测试记录

> 本文档按时间记录每次代码改动 → 硬件实测现象，避免重复测试。最后更新：2026-08-20

## 已验证的事实（不再重复测）

| # | 事实 | 验证方式 | 状态 |
|---|------|---------|------|
| 1 | **PCM 帧格式正确**：16-bit×2 立体声、I2S（`FRAME_ALIGN_LAST`+`FRAME_WIDTH_LONG`）、slave、时钟极性正常 | 1kHz 测试音直通 PCM，主机端听到**干净的 1kHz 单音** | ✅ 确定 |
| 2 | 主机 FS=12kHz、BCLK=384kHz、**不能改 16k**（用户确认） | 逻辑分析仪实测 + 用户确认 | ✅ 确定 |
| 3 | DSP 解码输出 `Buffer.output` = **打包 int16**（每子帧 8 样本 = 16 字节） | RSL10 手册 DMA 语义：M_TO_P 长度=目标字数 | ✅ 确定 |
| 4 | DMA 语义：**M_TO_P 长度=目标(外设)字数；P_TO_M/P_TO_P 长度=源字数**；字长不等时自动打包/解包，存储右对齐 | RSL10 Hardware Reference Manual 12.2.3.3 | ✅ 确定 |
| 5 | ASRC 模式约束：`DEC_MODE1` 要求 f_sink ∈ (0.875,1.125)×f_src；`DEC_MODE2` 要求 f_sink ∈ (0.4,1)×f_src；`INT_MODE` 要求 f_sink > f_src | `rsl10_sys_asrc.h` `Sys_ASRC_CheckInputConfig` | ✅ 确定 |
| 6 | 16k→12k 重采样：**DEC_MODE2 才是有效模式**（12/16=0.75 ∈ (0.4,1)）；DEC_MODE1 无效（0.75 ∉ (0.875,1.125)） | 由 #5 推导 | ✅ 确定 |
| 7 | ASRC 是**单声道**重采样器，不理解左右声道 | RSL10 文档/代码 | ✅ 确定 |
| 8 | **PCM 传输基频正确**：扫频 1k→10k 各频点基频正确送到主机，人耳可听到跳频变化 | T12 扫频测试，主机喇叭收听 | ✅ 确定 |
| 9 | **固定 1kHz 梳伪频**：24-word 循环 DMA 每 1ms wrap 产生周期性扰动，在 1k/2k/3k/4k/5k…处有峰；**不跟随基频**（发 2k 峰与发 1k 相同）、**幅度无关**（6000→1500 仍 80+dB） | T13/T14 仪器测量 | ✅ 确定 |
| 10 | 24-word 循环 DMA 的 1kHz 梳是**测试音专属**：生产 Path B 用 240-word 双缓冲+ISR 重装（wrap 10ms=100Hz），不受影响 | 由 #9 + 机制对比推导 | ✅ 确定 |
| 11 | **主机 = 16-bit 立体声**（word0=左、word1=右，24k 字/秒）。此前"立体声"是 BCLK/FS 比值的**推导假设**，本次用 word0=0、word1=1kHz 实测：主机**出声** → 读 word1 → 立体声坐实 | T19 格式验证实验 | ✅ 确定 |
| 12 | **32-bit mono 已被排除**：若 word0/word1 拼成 32-bit 样本，T5 的负 sin 值会错拼成巨大正数，不可能"干净 1kHz" → word0 是独立 16-bit 有符号 | 由 T5 + 补码分析反证 | ✅ 确定 |

## 关键速率关系

- 解码：16kHz mono（160 样本 / 10ms 帧，20 子帧 × 8 样本）
- 单声道数据直接进 2 字帧 → 每帧第二个字欠载 → 内容/速率都不对
- 立体声复制（L=R=单声道样本）是让内容正确的必要条件

## 测试记录（按时间顺序）

### T1 — 提交版 e158a78（初始 PCM 输出）
**配置**：`ASRC→PCM 直通`，单声道塞进立体声帧；PCM slave 16-bit×2 I2S；CLK=2 FRAME=3 SERO=14 SERI=4
**现象**：有声音了，但声音不大对
**结论**：基础通路通，但单声道内容/速率不对

### T2 — 方案3：绕过 ASRC 直通 16k
**配置**：解码子帧 DMA 直接送 PCM（每子帧重新武装 ch4）
**现象**：不行
**结论**：撤回。ASRC 是必要的（速率转换）

### T3 — 立体声复制 ping-pong v1（后置）
**配置**：ch4 `ASRC→mono 缓冲`(16→16) + counter 中断；ch5 连续循环 `stereo→PCM`
**现象**：周期性咔哒，音频很小声听不清
**结论**：ch5 连续读和 ISR 填充不同步 → 读到未填充半区（相位竞争）

### T4 — ping-pong + ch5 re-arm
**配置**：ch5 只由 ISR 武装发送已填充半区
**现象**：一样，甚至音乐声都没有了
**结论**：ch4 用 `DEST_WORD_SIZE_16` 的 ASRC→内存可能没传（对比 OD 路径用 32），疑点记录

### T5 — 回退提交版 + 测试音验证 ✅
**配置**：`PCM_TEST_TONE=1`，1kHz 正弦（L=R）直通 PCM，绕开解码/ASRC
**现象**：**1kHz 单音正常**
**结论**：**帧格式完全正确**（事实 #1）

### T6 — pre-ASRC 复制（int32 提取）
**配置**：DSP0_IRQ 里把单声道复制成 `[s,s]` 到 pcm_stage，翻倍喂 ASRC（ch3 长度 8→16）
**现象**：声音糊，速度像是差不多
**结论**：ASRC 输入翻倍（16/子帧）→ 超载；且 int32 提取样本布局错了

### T7 — pre-ASRC 复制（packed int16 修正）
**配置**：按打包 int16 读 Buffer.output（修正 T6 的布局错误）
**现象**：感觉差不多，且有些时候没声
**结论**：pre-ASRC 翻倍输入仍是核心问题（ASRC 超载 → 时断）。撤回

### T8 — 后置复制 ping-pong v2（正确 DMA 语义）
**配置**：ch4 `ASRC→BufferOut`(DEST=32，同 OD 路径) + counter 中断；ch5 由 ISR 武装
**现象**：直接就变成"嘚嘚嘚嘚"的声音，音频糊的根本听不清
**结论**：此路不通。**停止所有 ping-pong/counter 中断方案**

### T9 — ASRC 模式修正 DEC_MODE2
**配置**：ASRC 从 `DEC_MODE1` 改为 `DEC_MODE2`（16k→12k 唯一有效模式），相位增量 `<<29`→`<<28`；输出保持提交版 ASRC→PCM 直通
**现象**：**变音了，且有些时候无声**
**结论**：DEC_MODE2 生效（确认之前 DEC_MODE1 确实不对）。但 12k mono → 24k PCM 欠载仍在（单声道内容问题未解决），且有间歇无声

### T10 — post-ASRC ping-pong 重试（DEC_MODE2 基础上）
**配置**：T9（DEC_MODE2）+ T8 的 ping-pong（ch4 `ASRC→BufferOut` DEST=32 + counter 中断；ch5 由 ISR 武装送立体声）
**原理**：T8 是在 ASRC 错误模式（DEC_MODE1）下测的，现在 ASRC 输出正确的 12k mono，ping-pong 应稳定
**状态**：**待测试**

### T11 — ASRC INT_MODE 升采样到 24k（当前，待测）
**配置**：ASRC 从 12k 输出改为 **24k mono**（`INT_MODE`，`Ck = 2×audio_sink_cnt`，16k→24k 升采样）；ch4 保持 `ASRC→PCM 直通`
**原理**：PCM 需 24k 字/秒，ASRC 直出 24k → **无欠载**（消掉鞭炮/时无声）；L=s_k, R=s_{k+1}，每声道 12k
**状态**：**待测试**

### T12 — 1k→10k 阶跃扫频（24-word 循环 DMA + 定时器切基址）
**配置**：`PCM_TEST_TONE=1` `PCM_TEST_SWEEP=1`；10 个频点各 24-word 正弦表（24 words = 1ms，恰好整数周期），`ADDR_CIRC` 循环播放，**每个字都填正弦**（word0=word1）；定时器每 1s（后改 10s）只写 `SRC_BASE_ADDR` 切下一频点（不动 enable 状态，靠循环 wrap 重载基址）
**插曲**：初版用 TIMER1 → 与 RM 库冲突（`rm_pkt_hdl.c` 定义 `TIMER1_IRQHandler`）→ **链接失败**（最初"没声音"实为没烧进去）；改 **TIMER3**（本项目空闲：TIMER0/1=RM 库、TIMER2=解码节流）
**现象**：主机**能听到 1k→…→10k 跳频变化**（基频传输正确，事实 #8）；但仪器显示**任意频段内 1k-5k 同时有峰**
**结论**：传输层基频验证通过；发现固定 1kHz 梳伪频（待 T13/T14 定位）

### T13 — 单音 1kHz 仪器测量（word0-only 24-word 循环）
**配置**：`PCM_TEST_TONE=1` `PCM_TEST_SWEEP=0`；1kHz 正弦 word0 出音、word1=0，ADDR_CIRC 循环（与 T5 相同配置，无定时器/无 ISR）
**现象**（仪器接**主机喇叭**）：1k:88dB、2k:86、3k:92、4k:84、5k:69、6k:58、7k:52、8k:38，**3k 最高且强于基波**；幅值 6000→1500 后仍 80+dB
**结论**：峰不随幅值明显变化 → 非简单压缩电平效应；纯 word0-only 1kHz 在 0-8k 数学上只有 1k 一个峰，谐波不可能来自理想传输 → 有固定伪频

### T14 — 单音 2kHz 决定性测试
**配置**：同 T13，改 2kHz word0-only
**现象**：峰与发 1kHz 时**基本一致**（仍 1k-5k 有峰，含非 2k 倍数的 1k/3k/5k）
**结论**：峰**不跟随基频** → 排除"谐波跟随基频"（主机压缩）假设，确认为**固定 1kHz 梳**：24-word 循环 DMA 每 1ms wrap 的周期性扰动（事实 #9）。主机喇叭端测量还叠加主机压缩非线性（谐波跟随基频的部分）

### 扫频测试结论（测试音部分完结）
- **PCM 传输层基频验证通过**：扫频各频点基频正确，人耳可听跳频（事实 #8）——即"传输通、剩下是音频数据"
- **固定 1kHz 梳**是 24-word 小循环缓冲专属（事实 #9/#10）：wrap 率 = 24k/24 = 1kHz，任何 wrap 扰动都在 1k/2k/3k…处成梳；幅度无关、不跟随基频
- **生产 Path B 免疫**：240-word 双缓冲 + ISR 重装，wrap 10ms=100Hz，低于可听范围，不会成 1k 梳
- 测试音使命完成，切回生产路径验证真实 RM 音频

### T15 — 生产 Path B 软件重采样（word1=0）首次实测
**配置**：`PCM_TEST_TONE=0`，Path B：软件重采样 16k→12k（低通+线性插值）+ 240-word 双缓冲 + PCM DMA，word1 填 0
**现象**：**杂音大、音频很小声、变音**
**结论**：生产路径首次实测失败，三重症状待定位（数据格式？重采样？双缓冲？）

### T16 — Path B 改立体声复制（word1=word0）
**配置**：Path B 的 `dst[2i+1]` 从 0 改为 = word0（L=R 复制）
**现象**：**没变化**
**结论**：排除 word1 零填充镜像；问题不在帧结构

### T17 — Path B 改 int32 读取
**配置**：`Pcm_ResampleAndStore` 入参 `(int16_t*)`→`(const int32_t*)`，取低 16 位
**现象**：**没变化**
**结论**：排除解码数据读取格式（用户补充：解码后 OD 输出正常 → **解码本身没问题**）

### T18 — T11 方案：PCM 改走 ASRC INT_MODE 升采样 24k
**配置**：PCM 输出弃 Path B，改走 ASRC 路径（ASRC_IN + ASRC_OUT 连续 DMA）；`ASRC_Reconfig` 用 `INT_MODE` + `Ck=2×audio_sink_cnt`（16k→24k 升采样）
**现象**：**音频完全糊，但暂停时无杂音**
**结论**：**杂音来自 Path B 双缓冲 ISR 重装**（ASRC 连续 DMA 消除）；但 INT_MODE 升采样质量差（糊），且 word0/word1 取 24k 流的奇偶样本 → **L≠R**（相位错开 1 个 24k 样本），非真正的立体声复制

### T19 — 主机格式验证（word0=0, word1=1kHz）✅
**配置**：`PCM_TEST_TONE=1`，`pcm_test_buf` 改 word0 全 0、word1=1kHz 正弦（24-word 循环 DMA，走 T5 通路）
**现象**：主机**出声**（1kHz），但中间有概率顿一下
**结论**：**主机读 word1 → 16-bit 立体声确认**（事实 #11）。"顿一下"= 24-word 循环 wrap 扰动（1kHz 梳的时域表现，事实 #9/#10，测试音专属）

### 格式确认结论（关键转折）
- **主机确定是 16-bit 立体声**（word0=左、word1=右，24k 字/秒）——此前所有"欠载/糊"都源于对这个格式的不确定
- 生产路径正确目标：**16k mono → 12k 重采样 → L=R 复制 → 24k 字/秒**
- T9（DEC_MODE2 16k→12k）重采样质量好但欠载；T11（INT_MODE 16k→24k）填满但质量差+L≠R —— 两者都缺"L=R 复制"这一步
- **待验证**：RSL10 PCM 的 `SUBFRAME` 机制能否做"一个帧内 word0=word1 硬件复制"，若能则 DEC_MODE2 12k + 硬件复制彻底解决

## 已否决的方案（不再尝试）

| 方案 | 否决原因 |
|------|---------|
| 绕过 ASRC 直通 | 速率转换必须 ASRC |
| pre-ASRC 复制（翻倍喂 ASRC） | ASRC 超载 → 时断 |
| post-ASRC ping-pong（ch4 counter 中断 + ch5） | 三次尝试均失败（咔哒/无声/嘚嘚） |
| 主机改 16k | 用户确认主机动不了 |

## 下一步（按优先级）

1. **查 RSL10 PCM `SUBFRAME` 机制**：确认能否做"一个帧内 word0=word1 硬件复制"。若能，方案定为 **ASRC DEC_MODE2（16k→12k，质量好）+ 硬件复制（L=R）→ 24k 字/秒**。
2. 若 SUBFRAME 不能复制：评估 **ASRC DEC_MODE2 输出 12k → 内存 → 软件复制 L=R → PCM**（注意规避 Path B 双缓冲重装杂音，参考 T18 结论）。
3. 遗留：Path B 的"小声+变音"若与杂音无关，需单独查软件重采样质量（线性插值）与开环速率漂移。
4. 已验证但可回退：T11 ASRC INT_MODE（糊，暂弃）；测试音扫频/格式验证（已存档，勿重测）。

## 参考文件

- 方案文档：`docs/pcm/RM_RX音频PCM输出方案.md`
- RSL10 ASRC 驱动：`C:/Users/ViewSSS/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.9.1182/include/rsl10_sys_asrc.h`
- RSL10 DMA 手册：Hardware Reference Manual §12.2.3.3（词长/传输长度/打包）
