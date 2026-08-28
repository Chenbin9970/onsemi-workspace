# 7160test — PCM slave 输出实现步骤

> 工程：`peripheral_server_sleep7160test`
> 目标：把解码音频经 **PCM 从机接口** 输出给 7100 DSP（参照 `remote_mic_rx_rawtest1` 已验证实现移植）
> 状态：**2026-08-28 完成，数据流打通**（7100 做时钟主机，RSL10 从机移位输出）
> 前提：7100 I2C 控制已通（`docs/7100协议.md`），只补音频传输通道。

## 0. 主机接口规格（唯一不变锚点）

| 参数 | 值 |
|------|-----|
| 时钟角色 | **7100 是 clock master**，RSL10 做 PCM slave |
| BCLK | 384 kHz（7100 提供，不可改） |
| FS | 12 kHz（7100 提供，50% 占空比方波，I2S 特征） |
| 采样率 | 12 kHz/声道 |
| 数据 | 16-bit 立体声：word0=左、word1=右，**7100 读 word1（右声道）** |

## 1. 数据流

> RM 接收 → G722 解码(16k) → `Dsp2CmBuff0dec` → DMA ch3 → **ASRC(DEC_MODE2 16k→12k)** → ch4 → `pcm_tx_buf`(双缓冲) → ch5 → `PCM->TX_DATA` → **SERO(DIO14)** → 7100

每个 10ms 帧：ch4 从 `ASRC->OUT` 采 120 个 16-bit 采样（`PCM_FRAME_WORDS = 3*FRAME_LENGTH/4 = 120`）写进 `pcm_tx_buf`，ch5 把整帧 32-bit 字流到 `PCM->TX_DATA`（每字 = 高16 空 + 低16 采样 = word1 右声道）。

## 2. 引脚冲突与处理

| DIO | 原用途 | 现用途 | 处理 |
|-----|--------|--------|------|
| 2 | 空闲（按键已注释） | **PCM BCLK 输入** | 直接占用 |
| 3 | 电池 ADC | **PCM FS 输入** | 电池采样停用（`BAT_ADC_ENABLE` 宏，见 §5） |
| 4 | UART RX | **PCM SERI 输入**（从机不回传，未用） | UART RX 移到 DIO6 |
| 14 | JTAG TDI | **PCM SERO 输出** | `App_Initialize` 运行时已关 JTAG data/TRST，释放后配成 SERO |
| 6 | LED | UART RX | LED 删除（`LED_DIO` 及其全部调用） |
| 7 | 恢复按钮 | 不变 | — |

## 3. 改动步骤（按文件）

### 3.1 `include/app.h`

1. 电池采样总开关（默认关）：
   ```c
   //#define BAT_ADC_ENABLE   /* DIO3 让给 PCM FS，取消注释开启需先挪 FS 脚 */
   ```
2. 删 `LED_DIO`。
3. PCM 引脚 + 时钟源（放在 `SAMPL_CLK` 之前）：
   ```c
   #define PCM_CLK_DO       2
   #define PCM_FRAME_SYNC   3
   #define PCM_SER_DI       4
   #define PCM_SER_DO       14
   #define SAMPL_CLK        PCM_FRAME_SYNC   /* ASCC 改测 DIO3 的 12k FS */
   ```
4. `PCM_CFG_TX`（WORD_SIZE_32、MULTIWORD_2、SUBFRAME_ENABLE、SLAVE，同 test1）。
5. DMA：`OD_DMA_NUM 5` → `PCM_DMA_NUM 5`；删 `RX_DMA_OD`；加 `PCM_FRAME_WORDS (3*FRAME_LENGTH/4)`、`PCM_DOUBLE_BUFFER 1`。
6. `RX_DMA_ASRC_OUT` 改 PCM 版：`P_TO_M / SRC16 / DEST16 / LIN / COMPLETE_INT_ENABLE`（DEST16 写 32-bit 字低 16 位）。
7. 新增 `RX_DMA_PCM_STEREO`：`DMA_DEST_PCM / M_TO_P / 32→32 / LIN / COMPLETE_INT_ENABLE`。
8. extern：`pcm_tx_buf[2][PCM_FRAME_WORDS]`、`pcm_fill/pcm_ready/pcm_waiting`。

### 3.2 `code/app_init.c`

1. 定义 `uint32_t pcm_tx_buf[2][PCM_FRAME_WORDS];`。
2. 新增 `Initialize_Raw_PCM_Output_Type()`：
   ```c
   Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP, PCM_CLK_DO,
                     PCM_FRAME_SYNC, PCM_SER_DI, PCM_SER_DO, DIO_MODE_INPUT);
   Sys_PCM_Config(PCM_CFG_TX);
   Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                         (uint32_t)&pcm_tx_buf[0][0], (uint32_t)&PCM->TX_DATA);
   ```
3. `Audio_Init`：删 OD DMA 配置；ch4 改 `ASRC->OUT → pcm_tx_buf[0]`，长度 `PCM_FRAME_WORDS`；末尾双缓冲换手 + 武装（顺序：设 `pcm_fill=0/pcm_ready=0xFF/pcm_waiting=1` → 使能 ch4 ISR → 使能 ch4 → 使能 ch5 ISR → `Sys_PCM_Enable()`）。
4. `Audio_Resume`：同步改（ch5→PCM、ch4→pcm_tx_buf、重武装）。
5. 3 处电池 ADC 配置（`App_sleep_Initialize`/`App_RM_BLE_Initialize`/`App_Initialize`）包 `#ifdef BAT_ADC_ENABLE`。

### 3.3 `code/app_func.c`（活动块，注意有 `#if 0` 死代码副本）

1. 加全局量：`pcm_fill / pcm_ready / pcm_waiting`。
2. `Asrc_reconfig`：`ASRC_DEC_MODE1`→`ASRC_DEC_MODE2`、相位 `<<29`→`<<28`（16k→12k，0.75 只在 MODE2 有效）。
3. 新增 ch4/ch5 完成中断处理 + 向量表别名：
   - ch4（`DMA4_IRQHandler`）：ASRC 采满 → `pcm_ready=pcm_fill`、`pcm_fill=1-pcm_fill`、重武装 ch4；若 `pcm_waiting` 则启动 ch5。
   - ch5（`DMA5_IRQHandler`）：流完 → 若 `pcm_ready!=0xFF` 续流，否则 `pcm_waiting=1`。
   ```c
   void __attribute__ ((alias("Pcm_asrc_out_dma_isr"))) DMA_IRQ_FUNC(ASRC_OUT_IDX)(void);
   void __attribute__ ((alias("Pcm_tx_dma_isr")))        DMA_IRQ_FUNC(PCM_DMA_NUM)(void);
   ```

### 3.4 `app.c` / `app_process.c` / `ble_rempro_cmd.c`

- `app.c`：`rm_stop_requested` 里 `Sys_DMA_ChannelDisable(OD_DMA_NUM)`→`PCM_DMA_NUM`；低电检查块包 `#ifdef BAT_ADC_ENABLE`；删 LED。
- `app_process.c`：电池 ADC 配置包宏；删 LED（含 APP_Timer 指示灯）。
- `ble_rempro_cmd.c`：`read_battery_raw()` 关采样时 `return BAT_ADC_MAX`（固定 100%，不碰 DIO3，避免每 60s 重配 DIO3 打断 PCM）。

### 3.5 pack `printf.c`（`...\RSL10\3.9.1182\source\firmware\printf\printf.c`）

- `UART_RX 4`→`6`（DIO4 让给 PCM SERI；影响所有用 pack printf 的 sleep 工程，RX 本未用）。

## 4. 关键实现点（易错）

- **ASRC 用 DEC_MODE2 + `<<28`**：16k→12k 的 0.75 比例只在 MODE2 有效，相位粒度也不同（MODE1 用 `<<29`）。
- **SAMPL_CLK 必须跟着 FS 走**：`Sys_Audiosink_InputClock` 测 DIO3 的 12k FS，ASRC 输出速率据此锁定。
- **ch4/ch5 换手**：ch5 不在初始化时使能，由 ch4 完成中断在 `pcm_waiting` 时启动，避免首帧竞争。
- **数据在 word1（右声道）**：采样放 32-bit 字低 16 位，`PCM_TX_ALIGN_LSB` + WORD_SIZE_32 时 7100 从右声道读到（同 test1 主机行为）。
- 烧录顺序：`App_Initialize` 先运行时关 JTAG（释放 DIO14），`Audio_Init` 在 RM 启动时 `Sys_PCM_ConfigClk` 把 DIO14 配成 SERO。

## 5. 开关与后续

| 宏 | 默认 | 作用 |
|----|------|------|
| `//#define BAT_ADC_ENABLE` | 关 | 电池 ADC；开启需先给 PCM FS 换脚（DIO3 只支持 DIO0-3 ADC） |
| `PCM_DOUBLE_BUFFER` | 1 | 双缓冲（1）/ 单缓冲（0） |
| `ASRC_DITHER` | 未移植 | test1 的静音蚊蚊修复（±8 LSB dither）；7160test DSP 输出在 DSP RAM，移植方式不同，有蚊蚊再做 |
| `//#define OD_DIO12_OUTPUT` | 关 | 备用 OD 输出模式（见 §6），与 PCM/UART 打印互斥 |

## 6. 备用：OD 输出模式（`OD_DIO12_OUTPUT`）

> 完整文档见 **[`7160test_od_output.md`](7160test_od_output.md)**（数据流、改动文件、踩坑、验证记录）。本节为速览。

调试 OD（内部输出驱动）单端输出：**DIO12 做 OD_P**。与 PCM 输出、UART 打印（DIO12）互斥。

**打开方式**：`app.h` 取消注释 `//#define OD_DIO12_OUTPUT`（同时 `DEBUG_UART_ENABLE` 关打印）。

**互斥处理**：
- `OUTPUT_INTERFACE = OUTPUT_DISABLED` → printf.h 里 `PRINTF`/`printf_init` 全部变空宏，DIO12 不配成 UART TX。
- `Audio_Init`/`Audio_Resume` 走 OD 分支：`Sys_DIO_Config(12, ...|DIO_MODE_OD_P)` + OD DMA（`BufferOut→AUDIO->OD_DATA`，CIRC）+ ASRC OUT→BufferOut。
- PCM 分支整体 `#else` 掉（不配 PCM、不使能 ch4/ch5 完成中断、不 `Sys_PCM_Enable`）。

**数据流**：RM → G722(16k) → `Dsp2CmBuff0dec` → ch3 → ASRC(**DEC_MODE1**，内部 DMIC/OD 时钟锁速) → ch4 → BufferOut(CIRC) → ch5(OD DMA) → AUDIO->OD_DATA → **DIO12**。

**与 PCM 模式差异**：
| 项 | PCM | OD |
|----|-----|-----|
| 采样钟 `SAMPLING_CLK_SRC` | `SAMPL_CLK<<...` = DIO3 12k FS | `AUDIOSINK_CLK_SRC_DMIC_OD`（**内部时钟**，不依赖外部引脚） |
| ASRC | `DEC_MODE2` + `<<28`（16k→12k） | `DEC_MODE1` + `<<29` |
| ASRC OUT DMA | LIN + 完成中断 → pcm_tx_buf | CIRC 无中断 → BufferOut |
| 输出 | ch5 → PCM->TX_DATA → DIO14 | ch5 → OD_DATA → DIO12 |
| 打印（DIO12） | UART TX | 关闭（DIO12 让给 OD_P） |

> **2026-08-28 踩坑**：OD 采样钟一开始照搬 sleep 用 DIO7 外部时钟，但 7160 测试时 DIO7 无有效时钟 → ASRC 锁不上速 → **一顿一顿**。改用内部时钟 `AUDIOSINK_CLK_SRC_DMIC_OD`（remote_mic_rx_raw 的 OD 做法）解决。DIO7 仍是恢复按钮（启动时），不再兼作采样钟。

## 参考

- 验证基准工程：`remote_mic_rx_rawtest1`（`docs/rx_rawtest1_pcm_output.md`）
- OD 完整实现参考：`peripheral_server_sleep`（`Audio_Init` 的 OD 段）
- IO 分配 / 调试总览：`docs/7160调试过程.md`
