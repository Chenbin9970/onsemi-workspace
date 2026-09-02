# RSL10 PCM 输出配置（已验证）

> **已过时**：本文档的 `WORD_SIZE_32` 结论已被推翻。最终正确配置见 [pcm_config_final.md](pcm_config_final.md)。

> 文档版本：2026-08-24
> 基于 `remote_mic_rx_raw` 工程测试音的实测验证（逻辑分析仪 + 主机模式自测）。
> 这份文档记录**经过验证可用的** PCM 输出配置，以及调试过程中排除的无效配置。

## 1. 验证结论（一句话）

**RSL10 的 PCM TX 必须用 32-bit DMA 写 `PCM->TX_DATA`（一次写一帧 2×16-bit），配合 `WORD_SIZE_32 + MULTIWORD_2 + SUBFRAME_ENABLE + FRAME_ALIGN_FIRST`。** 16-bit 写会导致 50% 帧欠载。

## 2. 硬件连接

| PCM 信号 | DIO | 方向（从机） | 方向（主机） |
|---------|-----|-------------|-------------|
| BCLK | DIO2 | 输入（外部主机 384k） | 输出（RSL10 产生） |
| FRAME/FS | DIO3 | 输入（外部主机 12k） | 输出（RSL10 产生） |
| SERI | DIO4 | 输入（未用） | 输入（未用） |
| **SERO** | **DIO14** | **输出（数据）** | **输出（数据）** |

> 注意：DIO14 原为 Ezairo 复位（RF_INT），PCM 模式下让给 SERO。

## 3. 寄存器配置 `PCM_CFG_TX`

```c
#define PCM_CFG_TX  (PCM_BIT_ORDER_MSB_FIRST |   // 线上 MSB 先出
                     PCM_TX_ALIGN_LSB |          // 数据字内 LSB 对齐
                     PCM_WORD_SIZE_32 |          // ★ 字宽 32-bit（关键）
                     PCM_FRAME_ALIGN_FIRST |     // ★ FS 对齐到帧第一位（关键）
                     PCM_FRAME_WIDTH_LONG |      // FS 高半帧（LONG）
                     PCM_MULTIWORD_2 |           // 每帧 2 个字
                     PCM_SUBFRAME_ENABLE |       // ★ 每字独立子帧（关键）
                     PCM_CONTROLLER_DMA |        // DMA 驱动数据
                     PCM_DISABLE |               // 初始关闭，后 Enable
                     PCM_SELECT_MASTER)          // 主机/从机（真实路径用 SLAVE）
```

### 3.1 各字段作用

| 字段 | 值 | 作用 |
|------|----|------|
| `BIT_ORDER` | MSB_FIRST | 每字从 MSB 开始移出 |
| `TX_ALIGN` | LSB | 数据在字内 LSB 对齐 |
| `WORD_SIZE` | **32** | **字宽必须 32**。16 会导致 DMA/PCM 错位欠载 |
| `FRAME_ALIGN` | **FIRST** | **FS 对齐到帧第一位**。LAST 会让数据滞后 1 BCLK（从第 2 个 BCLK 才开始） |
| `FRAME_WIDTH` | LONG | FS 高半帧 |
| `MULTIWORD` | 2 | 每帧 2 个字 |
| `SUBFRAME` | **ENABLE** | **每字独立子帧**。DISABLE 时帧结构异常 |
| `CONTROLLER` | DMA | 数据由 DMA 写 TX_DATA |
| `SLAVE/MASTER` | 按实际 | 从机=外部时钟，主机=RSL10 自己产生 |

## 4. 数据路径与打包

```
解码/测试数据(int32_t)
  → ch5 DMA (32-bit, LIN, 完成中断重武装) → PCM->TX_DATA
  → PCM 按 BCLK 移出 → SERO(DIO14)
```

### 4.1 打包格式（关键）

每帧 = 一个 **32-bit 值**，一次 DMA 写：

```c
// int32_t 值：低 16bit = 数据 s，高 16bit = 0
uint32_t frame = (uint32_t)(uint16_t)s;   // = s（低16=s，高16=0）
```

线上输出：**word0（先出）= s，word1（后出）= 0**，即 `0xSSSS0000`。

例：`s = 0x5555` → 帧 = `0x00005555` → 输出 `0x55550000`。

### 4.2 DMA 配置（`RX_DMA_PCM_STEREO` / 测试 `RX_DMA_PCM_TEST`）

```c
#define RX_DMA_PCM_TEST  (DMA_DEST_PCM |        // 目标 = PCM
                          DMA_TRANSFER_M_TO_P | // 内存→外设
                          DMA_LITTLE_ENDIAN |
                          DMA_COMPLETE_INT_ENABLE |  // 完成中断（重武装用）
                          DMA_COUNTER_INT_DISABLE |
                          DMA_DEST_WORD_SIZE_32 |    // ★ 32-bit 写
                          DMA_SRC_WORD_SIZE_32 |     // ★ 32-bit 读
                          DMA_SRC_ADDR_INC |
                          DMA_DEST_ADDR_STATIC |     // PCM->TX_DATA 固定
                          DMA_ADDR_LIN |             // 线性，完成后重武装
                          DMA_DISABLE)
```

**关键**：`SRC/DEST_WORD_SIZE_32`。缓冲必须是 `int32_t[]`（每项一个帧），不能是 `int16_t[]`。

## 5. 时钟配置

### 5.1 从机模式（真实设备提供时钟）

```c
Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP,
                  PCM_CLK_DO, PCM_FRAME_SYNC, PCM_SER_DI, PCM_SER_DO,
                  DIO_MODE_INPUT);
```
- BCLK 384kHz / FS 12kHz 由外部设备提供（DIO2/3 输入）
- 帧 = 32 BCLK（384k/12k）

### 5.2 主机模式（RSL10 自己产生，用于自测）

```c
// BCLK = USRCLK = SYSCLK / prescaler
CLK->DIV_CFG0 = (CLK->DIV_CFG0 & ~CLK_DIV_CFG0_USRCLK_PRESCALE_Mask) |
                ((uint32_t)(41U << CLK_DIV_CFG0_USRCLK_PRESCALE_Pos));  // /42
Sys_PCM_ConfigClk(PCM_SELECT_MASTER, DIO_WEAK_PULL_UP,
                  PCM_CLK_DO, PCM_FRAME_SYNC, PCM_SER_DI, PCM_SER_DO,
                  DIO_MODE_USRCLK);
```

| 分频 | BCLK | FS（=BCLK/32） | 说明 |
|------|------|----------------|------|
| ÷64（原） | ~250k | ~7.8k | 太慢 |
| **÷42** | **~381k** | **~11.9k** | 最接近 384k/12k（SYSCLK=16MHz 下非整数分频） |

> 约束：SYSCLK=16MHz（48MHz XTAL ÷3），无法整除出精确 384k。若需精确 384k 需改 SYSCLK。

## 6. 调试结论（哪些配置无效，为什么）

| 尝试 | 结果 | 原因 |
|------|------|------|
| **16-bit DMA 写** | ❌ 50% 帧欠载 | PCM TX_DATA 需 32-bit 整帧写，16-bit 写导致 DMA/PCM 错位 |
| `WORD_SIZE_16` | ❌ 数据乱/欠载 | 同上，字宽必须是 32 |
| `SUBFRAME_DISABLE` | ❌ 数据错位 | 帧结构异常，需 ENABLE |
| `FRAME_ALIGN_LAST` | ❌ 数据从第 2 个 BCLK 开始 | FS 对齐到帧尾导致 1 BCLK 滞后 |
| 信号发生器模拟主机 | ❌ 数据乱/6+6 | 外部方波不满足 PCM 从机时序，用主机模式自测更可靠 |
| circular DMA | ❌ 循环边界欠载 | 换 LIN + 完成中断重武装 |

## 7. 测试音配置（`PCM_TEST_TONE=1`）

```c
// 缓冲：int32_t[240]，每项 = 0x5555（低16=数据，高16=0）
// 所有帧相同 → 输出 0x55550000 恒定
int32_t pcm_test_buf[PCM_TEST_BUF_LEN] = { 0x5555, ... };

// 启动顺序（SAI 式）：
// 1. 禁 PCM（Sys_PCM_Config(PCM_CFG_TX)，含 PCM_DISABLE）
// 2. 预载 TX_DATA = pcm_test_buf[0]
// 3. 配置 + 使能 ch5 DMA（32-bit, LIN, 完成中断）
// 4. 开 DMA5 NVIC（重武装）
// 5. 最后 Sys_PCM_Enable()
```

## 8. 待办：应用到真实 Path B

| # | 改动 | 位置 |
|---|------|------|
| 1 | DMA4 打包 `(s<<16)\|s` → **`s`**（word0=s, word1=0） | app_func.c |
| 2 | `PCM_SELECT_MASTER` → `SLAVE` | app.h `PCM_CFG_TX` |
| 3 | `PCM_TEST_TONE` 1 → 0 | app.h |
| 4 | ch5 已 32-bit ✓ | app.h `RX_DMA_PCM_STEREO` |
| 5 | 时钟恢复从机（DIO2/3 输入） | app_init.c `Initialize_Raw_PCM_Output_Type` |

## 9. 相关文件

- [remote_mic_rx_raw/include/app.h](../remote_mic_rx_raw/include/app.h) — `PCM_CFG_TX`、`RX_DMA_PCM_STEREO`、`PCM_TEST_*`
- [remote_mic_rx_raw/code/app_init.c](../remote_mic_rx_raw/code/app_init.c) — `Initialize_Raw_PCM_Output_Type`、测试音路径
- [remote_mic_rx_raw/code/app_func.c](../remote_mic_rx_raw/code/app_func.c) — DMA4 打包、DMA5 重武装
