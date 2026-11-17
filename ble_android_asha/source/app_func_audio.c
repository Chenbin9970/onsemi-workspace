/* ----------------------------------------------------------------------------
 * Copyright (c) 2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * app_func.c
 * - Audio application functions
 * ------------------------------------------------------------------------- */

#include "app.h"
#include "app_audio.h"
#include "queue.h"

#include <ble_asha.h>

/* Global variables */
uint32_t primEvtCnt = 0, secEvtCnt = 0, invalidRxCnt = 0;
uint32_t cntr_coded = 0;
int16_t temp_sin_signal[FRAME_LENGTH];
uint8_t volumeShift = 0;
uint8_t seq_num_prev;
uint8_t frames_per_packet;
uint8_t expected_seq_num, fragmentNo_count;
uint8_t seq_num, fragment_num, packet_state, maxQueNum, found_seq_num;
uint32_t frame_counter = 0;

sync_param_t env_sync;
audio_frame_param_t env_audio;
struct queue_t audio_queue;

/* 数据抓数（非破坏，不动 ch4）：cap_in(16k DSP 输出) + cap_out(ASRC->OUT
 * 轮询采样，在 ASRC 输入 DMA 中断里读寄存器)。各满 CAP_N 后 cap_done=1 冻结。
 * J-Link 读满快照；复位 cap_done=0 且两索引=0 重抓。 */
#define CAP_N   512
int16_t cap_in[CAP_N];
int16_t cap_out[CAP_N];
volatile uint32_t cap_in_idx;
volatile uint32_t cap_out_idx;
volatile uint8_t  cap_done;

#if (OUTPUT_INTRF == PCM_TX_OUTPUT)
/* PCM 环形缓冲状态（见 include/app_audio.h）：ch4 填 pcm_wr 块、ch5 依次流 pcm_rd 块。
   两个 DMA 完成中断同优先级、互不抢占，故这些共享变量无需临界区。 */
volatile uint8_t pcm_wr;
volatile uint8_t pcm_rd;
volatile uint8_t pcm_cnt;
volatile uint8_t pcm_playing;
volatile uint8_t pcm_started;
#endif    /* if (OUTPUT_INTRF == PCM_TX_OUTPUT) */

/* Enable / disable PLC feature */
bool plc_enable = true;

bool sequence_started = false;

/* ----------------------------------------------------------------------------
 * Function      : void DIO0_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Toggle selection to enable/ disable PLC feature
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
#if 0
void DIO0_IRQHandler(void)
{
    static uint8_t ignore_next_dio_int = 0;
    if (ignore_next_dio_int)
    {
        ignore_next_dio_int = 0;
    }
    else if (DIO_DATA->ALIAS[BUTTON_DIO] == 0)
    {
        NVIC_DisableIRQ(DIO0_IRQn);

        /* Button is pressed: Ignore next interrupt.
         * This is required to deal with the debounce circuit limitations. */
        ignore_next_dio_int = 1;

        //plc_enable = !(plc_enable);

        PRINTF("%s\r\n", (plc_enable) ? "PLC Enabled" : "PLC Disabled");
        NVIC_EnableIRQ(DIO0_IRQn);
    }
}
#endif

/* ----------------------------------------------------------------------------
 * Function      : void APP_Audio_Transfer(const uint8_t *audio_buff,
 *                                    uint8_t audio_length, uint8_t seqNum)
 * ----------------------------------------------------------------------------
 * Description   : Handles a received Android Audio frame
 * Inputs        : audio_buff    - pointer to the received packet data
 *                 audio_length  - number of bytes of encoded data in this packet
 *                 seqNum        - sequence number associated with this packet
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void APP_Audio_Transfer(uint8_t *audio_buff, uint16_t audio_length, uint8_t seqNum)
{
    uint8_t i, j;
    uint8_t missedPackets;

    Sys_GPIO_Toggle(ASHA_EVENT_DIO);

    /* Add a credit */
    ASHA_AddCredits(1);

    /* If a packet comes in while the link is not yet established (outside of the
     * start/stop events), ignore it.
     */
    if(env_audio.state == LINK_DISCONNECTED)
    {
        return;
    }

    /* Set the number of samples per packet based on the size of the encoded data.
     * NOTE: This assumes the CODEC is G.722 in mode 3. This should be updated
     * if a different CODEC configuration is used.
     */
    env_sync.samples_per_packet = audio_length * 2;

    /* Set the initial seq_num_prev if this is the first frame */
    if(sequence_started == false)
    {
        seq_num_prev = seqNum - 1;
        sequence_started = true;
    }

    /* Print a message when missed packets are detected - useful for debug */
    if( ((uint8_t) (seqNum - seq_num_prev - 1)) != 0 ) {
        PRINTF("\r\n APP_Audio_Transfer: seq_num_prev = %d, seqNum = %d\r\n", seq_num_prev, seqNum);
    }

    /* Check for missed packets and insert bad packets into the queue for any missed sequence numbers
     * to ensure continuity in the queue
     */
    if(sequence_started == true)
    {
		missedPackets = (uint8_t) (seqNum - seq_num_prev - 1);
		if(missedPackets > 0)
		{
		    for (j = 0; j < missedPackets; j++)
		    {
				for (i = 0; i < audio_length; i+= ENCODED_FRAME_LENGTH)
				{
					QueueInsert(&audio_queue, &audio_buff[i], BAD_PACKET, (uint8_t)(seqNum - missedPackets + j), (i / ENCODED_FRAME_LENGTH));
				}
		    }
		}
    }

    /* Update the previous sequence number */
    seq_num_prev = seqNum;

    /* Set the frame per packet count. Frame processing is always done at a 10ms interval,
     * but the packet connection interval may be some multiple of this. When this is the case
     * packets are split up into frame-sized chunks and inserted in the queue.
     */
    frames_per_packet = (audio_length/ ENCODED_FRAME_LENGTH);

    /* Insert the good packet into the queue, splitting up into frames as necessary */
    for (i = 0; i < audio_length; i+= ENCODED_FRAME_LENGTH)
    {
		QueueInsert(&audio_queue, &audio_buff[i], GOOD_PACKET, seqNum, (i / ENCODED_FRAME_LENGTH));
    }

    env_sync.cntr_connection = 0;

    /* Start the audio path if we've buffered enough packets */
    if((env_audio.state == LINK_TRANSIENT) && (QueueCount(&audio_queue) >= AUDIO_START_BUFFERED_FRAMES))
    {
		APP_Audio_Start();
	}
}

/* ----------------------------------------------------------------------------
 * Function      : void APP_Audio_Start(void)
 * ----------------------------------------------------------------------------
 * Description   : Starts the audio path for data processing
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void APP_Audio_Start(void)
{
    PRINTF("Established - size: %d, first: %d\r\n", QueueCount(&audio_queue), audio_queue.front->seq_num);

    env_sync.cntr_transient = 0;
    env_sync.audio_sink_cnt = 0;
    env_sync.flag_ascc_phase = false;

    ASRC_CTRL->ASRC_ENABLE_ALIAS = ASRC_ENABLED_BITBAND;
    Sys_ASRC_Reset();

    /* ASCC interrupts */
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

    /* LPDSP32 interrupt */
    NVIC_EnableIRQ(DSP0_IRQn);

    /* Timer interrupts */
    NVIC_EnableIRQ(TIMER_IRQn(TIMER_RENDER));

    /* DMA interrupts */
    NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));

    /* Reset the capture for this streaming session */
    cap_in_idx = 0;
    cap_out_idx = 0;
    cap_done = 0;

#if (OUTPUT_INTRF == PCM_TX_OUTPUT)
    /* 武装 PCM 环形缓冲：ch4 填 pcm_wr 块，ch5 预充 PCM_PREFILL 块后由 ch4 中断启动。
       每次开流都重新初始化 PCM 块 + 重配 ch4（1664 建链时同款）：
       只 Sys_PCM_Enable 会让上次会话残留的 PCM 状态（如清不掉的 OVERRUN 标志/
       FIFO 状态）跨会话带过来，表现为丢采样咔哒声。 */
    Sys_DMA_ChannelDisable(PCM_DMA_NUM);
    Sys_PCM_Config(PCM_CFG_TX);

    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT, PCM_FRAME_WORDS, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)&pcm_tx_buf[0][0]);

    pcm_wr      = 0;
    pcm_rd      = 0;
    pcm_cnt     = 0;
    pcm_playing = 0;
    pcm_started = 0;      /* ch5 等预充 PCM_PREFILL 块后由 ch4 完成中断启动 */

    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_OUT_IDX));
    NVIC_EnableIRQ(DMA_IRQn(ASRC_OUT_IDX));
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

    NVIC_ClearPendingIRQ(DMA_IRQn(PCM_DMA_NUM));
    NVIC_EnableIRQ(DMA_IRQn(PCM_DMA_NUM));
    Sys_PCM_Enable();
#endif    /* if (OUTPUT_INTRF == PCM_TX_OUTPUT) */

    env_audio.state = LINK_ESTABLISHED;
    /* 4.5 ms (connection interval is 10 ms) + channel delay */
    /* TODO: android_support_env.channel_delay */
    Sys_Timer_Set_Control(TIMER_RENDER, TIMER_SHOT_MODE | \
                          (RENDER_TIME_US - 1)          | \
                          TIMER_SLOWCLK_DIV2);
    Sys_Timers_Start(1U << TIMER_RENDER);
    env_sync.timer_free_run = false;

    /* Set the expected sequence number to the value from the first packet in the queue */
    expected_seq_num = audio_queue.front->seq_num;

    /* Reset the frame counter */
    frame_counter = 0;
}

/* ----------------------------------------------------------------------------
 * Function      : void APP_Audio_Disconnect(void)
 * ----------------------------------------------------------------------------
 * Description   : Stops the audio path
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void APP_Audio_Disconnect(void)
{
    APP_ResetPrevSeqNumber();

    PRINTF("\r\nDisconnect\r\n");

    env_sync.cntr_connection = 0;

    ASRC_CTRL->ASRC_DISABLE_ALIAS = ASRC_DISABLED_BITBAND;

    /* ASCC interrupt */
    NVIC_DisableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

    /* LPDSP32 interrupt */
    NVIC_DisableIRQ(DSP0_IRQn);

    NVIC_DisableIRQ(DMA_IRQn(ASRC_IN_IDX));

#if (OUTPUT_INTRF == PCM_TX_OUTPUT)
    /* 停止 PCM 输出并冻结双缓冲 */
    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelDisable(PCM_DMA_NUM);
    NVIC_DisableIRQ(DMA_IRQn(ASRC_OUT_IDX));
    NVIC_DisableIRQ(DMA_IRQn(PCM_DMA_NUM));
    Sys_PCM_Disable();
#endif    /* if (OUTPUT_INTRF == PCM_TX_OUTPUT) */

    /* Timer interrupts */
    NVIC_DisableIRQ(TIMER_IRQn(TIMER_RENDER));
    Sys_Timers_Stop(1U << TIMER_RENDER);

    APP_Audio_FlushQueue();
    QueueInit(&audio_queue);

    frame_counter = 0;
    sequence_started = false;

    env_audio.state = LINK_DISCONNECTED;

    Reset_Audio_Sync_Param(&env_sync, &env_audio);
}

/* ----------------------------------------------------------------------------
 * Function      : void APP_Audio_FlushQueue(void)
 * ----------------------------------------------------------------------------
 * Description   : Flushes the audio queue
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void APP_Audio_FlushQueue(void)
{
    uint16_t queue_len;
    uint16_t i;

    queue_len = QueueCount(&audio_queue);
    for(i = 0; i < queue_len; ++i)
        QueueFree(&audio_queue);
}

/* ----------------------------------------------------------------------------
 * Function      : void Volume_Set(int8_t volume)
 * ----------------------------------------------------------------------------
 * Description   : Normalizes the volume range from [-128, 0] to [16, 0]. The
 * 				   new scale is used in Volume_Set() to adjust by shifting the
 * 				   samples to the right
 * Inputs        : - volume	- Volume [-128 to 0 range]
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Volume_Set(int8_t volume)
{
    volumeShift = 16 - ((volume + 128) / 8);
}

/* ----------------------------------------------------------------------------
 * Function      : void Volume_Shift_Subframe (int16_t *src)
 * ----------------------------------------------------------------------------
 * Description   : Adjust the audio level by a simple shift.
 * Inputs        : - src	- pointer to the audio frame
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Volume_Shift_Subframe(int16_t *src)
{
    /* Shift subframe samples to change the volume */
    for (uint16_t i = 0; i < SUBFRAME_LENGTH; i++)
    {
        src[i] = src[i] >> volumeShift;
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void APP_ResetPrevSeqNumber(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize ASHA sequence number control after a disconnect.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void APP_ResetPrevSeqNumber(void)
{
    sequence_started = false;
}

/* ----------------------------------------------------------------------------
 * Function      : void DMA_IRQ_FUNC(ASRC_IN_IDX)(void)
 * ----------------------------------------------------------------------------
 * Description   : ASRC input DMA interrupt handler
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DMA_IRQ_FUNC(ASRC_IN_IDX)(void)
{
    env_audio.frame_idx += SUBFRAME_LENGTH;
    if (env_audio.frame_idx < FRAME_LENGTH)
    {
        Volume_Shift_Subframe(&env_audio.frame_dec[env_audio.frame_idx]);
        Sys_DMA_Set_ChannelSourceAddress(ASRC_IN_IDX, (uint32_t)&env_audio.frame_dec[env_audio.frame_idx]);

        /* Re-enable DMA for ASRC input */
        Sys_DMA_ChannelEnable(ASRC_IN_IDX);

        /* Enable ASRC block */
        Sys_ASRC_StatusConfig(ASRC_ENABLE);
    }
    else
    {
        /* Start the timer that is used to reset the SPI CS line; The timeout
         * for this timer is set to expire after the last words of the frame
         * have been transmitted via SPI. At this point, SPI CS should be
         * re-asserted and the ASRC disabled.
         */
        NVIC_EnableIRQ(TIMER_IRQn(TIMER_FRAME_TX_END));
        Sys_Timer_Set_Control(TIMER_FRAME_TX_END, TIMER_SHOT_MODE  | \
                              (FRAME_TX_END_TIME_US - 1)        | \
                              TIMER_SLOWCLK_DIV2);
        Sys_Timers_Start(1U << TIMER_FRAME_TX_END);
    }
}

#if (OUTPUT_INTRF == PCM_TX_OUTPUT)
/* ----------------------------------------------------------------------------
 * Function      : void Pcm_asrc_out_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASRC 输出 DMA 完成。ch4 刚填满的块入环、换下一块继续填；
 *                 ch5 空闲时按预充阈值（首播）或立即（续播）启动。
 * ------------------------------------------------------------------------- */
void Pcm_asrc_out_dma_isr(void)
{
    /* ch4 刚填满 pcm_wr 指向的那一块 */

    /* 抓 24k PCM 输出（出 ASRC 后，7100 实际收到的音频，32-bit 字低 16 位） */
    if (cap_out_idx < CAP_N)
    {
        uint32_t n = CAP_N - cap_out_idx;
        uint32_t i;
        if (n > PCM_FRAME_WORDS)
        {
            n = PCM_FRAME_WORDS;
        }
        for (i = 0; i < n; i++)
        {
            cap_out[cap_out_idx + i] =
                (int16_t)(pcm_tx_buf[pcm_wr][i] & 0xFFFFu);
        }
        cap_out_idx += n;
    }

    /* 该块就绪入环，ch4 换到下一块继续填 */
    pcm_cnt++;
    pcm_wr = (uint8_t)((pcm_wr + 1) % PCM_TX_BUFS);

    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT, PCM_FRAME_WORDS, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)&pcm_tx_buf[pcm_wr][0]);
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

    /* ch5 空闲时启动：首播按预充阈值启动（拉开与 ch4 的相位、留出余量），
       之后只要环里有块就立刻续播（避免恢复时多塞静音）。 */
    if (!pcm_playing &&
        (pcm_started ? (pcm_cnt > 0) : (pcm_cnt >= PCM_PREFILL)))
    {
        pcm_started = 1;
        pcm_playing = 1;
        pcm_cnt--;
        Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                              (uint32_t)&pcm_tx_buf[pcm_rd][0],
                              (uint32_t)&PCM->TX_DATA);
        Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
        Sys_DMA_ChannelEnable(PCM_DMA_NUM);
        pcm_rd = (uint8_t)((pcm_rd + 1) % PCM_TX_BUFS);
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void Pcm_tx_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : PCM TX DMA 完成。环里还有块就立刻续播，否则空着等 ch4 完成中断。
 * ------------------------------------------------------------------------- */
void Pcm_tx_dma_isr(void)
{
    pcm_playing = 0;

    if (pcm_cnt > 0)
    {
        pcm_playing = 1;
        pcm_cnt--;
        Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                              (uint32_t)&pcm_tx_buf[pcm_rd][0],
                              (uint32_t)&PCM->TX_DATA);
        Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
        Sys_DMA_ChannelEnable(PCM_DMA_NUM);
        pcm_rd = (uint8_t)((pcm_rd + 1) % PCM_TX_BUFS);
    }
    /* else: 环已空（余量被抖动吃光，属罕见）→ 不武装，等 ch4 完成中断重启 */
}

/* PCM DMA ISR 别名：向量表 DMA4/DMA5 -> 上述处理函数 */
void __attribute__ ((alias("Pcm_asrc_out_dma_isr")))
DMA_IRQ_FUNC(ASRC_OUT_IDX)(void);

void __attribute__ ((alias("Pcm_tx_dma_isr")))
DMA_IRQ_FUNC(PCM_DMA_NUM)(void);
#endif    /* if (OUTPUT_INTRF == PCM_TX_OUTPUT) */

/* ----------------------------------------------------------------------------
 * Function      : void TIMER_IRQ_FUNC(TIMER_FRAME_TX_END)(void)
 * ----------------------------------------------------------------------------
 * Description   : Timer handler used to reset the SPI CS line after a frame of
 *                 data has been transmitted via SPI.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void TIMER_IRQ_FUNC(TIMER_FRAME_TX_END)(void)
{
#if (OUTPUT_INTRF == SPI_TX_OUTPUT)
    /* De-assert SPI_CS */
    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_1_BITBAND;

    /* Check if the ASRC has finish on the frame */
    ASSERT(ASRC_CTRL->ASRC_PROC_STATUS_ALIAS == ASRC_IDLE_BITBAND);
    Sys_ASRC_StatusConfig(ASRC_DISABLE);
#endif    /* if (OUTPUT_INTRF == SPI_TX_OUTPUT) */

    /* PCM: ASRC 保持连续运行（ch4/ch5 持续排空），不逐帧关 ASRC */
    env_audio.proc = false;
}

/* ----------------------------------------------------------------------------
 * Function      : void TIMER_IRQ_FUNC(TIMER_RENDER)(void)
 * ----------------------------------------------------------------------------
 * Description   : Rendering timer interrupt handler; starts processing a new
 *                 frame worth of data from the queue.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void TIMER_IRQ_FUNC(TIMER_RENDER)(void)
{
    /* for accuracy start the rendering timer in free-run mode once */
    if (!env_sync.timer_free_run)
    {
        Sys_Timers_Stop(1U << TIMER_RENDER);
        Sys_Timer_Set_Control(TIMER_RENDER, TIMER_FREE_RUN  | \
                              (AUDIO_INTV_PERIOD - 1)    | \
                              TIMER_SLOWCLK_DIV2);
        Sys_Timers_Start(1U << TIMER_RENDER);
        env_sync.timer_free_run = true;
    }

    /* Retrieve frame from the front of the audio queue*/
    env_audio.frame_in = QueueFront(&audio_queue, &env_audio.packet_state,
                                    &seq_num, &fragment_num);

    if (env_audio.frame_in != NULL)
    {
        /* Copy frame data to the LDSP32 context if queue isn't empty */
        memcpy(lpdsp32.incoming, env_audio.frame_in,
               ENCODED_FRAME_LENGTH * sizeof(uint8_t));
        /* Free the retrieved frame from the audio queue */
        QueueFree(&audio_queue);
    }
    else
    {
        /* Mark packet as bad since queue is empty */
        env_audio.packet_state = BAD_PACKET;
    }

    /* Check if the decoder has finished on the frame */
    ASSERT(lpdsp32.state == DSP_IDLE);
    lpdsp32.state = DSP_BUSY;

    if (plc_enable)
    {
        if (env_audio.packet_state == GOOD_PACKET)
        {
            lpdsp32.channels.action &= ~DECODE_PLC;
        }
        else if (env_audio.packet_state == BAD_PACKET)
        {
            lpdsp32.channels.action |= DECODE_PLC;
        }
        else
        {
            lpdsp32.channels.action |= DECODE_PLC;
        }
    }
    else
    {
        /* No PLC performed */
        lpdsp32.channels.action &= ~DECODE_PLC;
    }

    codecSetParameters(lpdsp32.codec, &lpdsp32.channels);
    codecDecode(lpdsp32.codec);

    lpdsp32.channels.action &= ~DECODE_RESET;

    env_sync.cntr_connection++;

    /* Increment the frame counter for each frame rendered */
	frame_counter++;

	/* Increment the sequence number for every packet rendered from the queue */
	if((frame_counter % frames_per_packet) == 0)
	{
		expected_seq_num++;
	}
}

/* ----------------------------------------------------------------------------
 * Function      : void DSP0_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : LPDSP32 decoder interrupt handler (RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DSP0_IRQHandler(void)
{
    /* Check if there is any sub-frame from prev frame waiting to be processed */
    ASSERT(!env_audio.proc); //TODO: investigate why this is being triggered.
    env_audio.proc = true;

    env_audio.frame_dec = (int16_t *)lpdsp32.outgoing;
    if (env_sync.flag_ascc_phase)
    {
        asrc_reconfig(&env_sync);
        env_sync.flag_ascc_phase = false;
    }

#if SIMUL
    memcpy(env_audio.frame_dec, sin_signal, sizeof(uint16_t) * FRAME_LENGTH);
#else    /* if SIMUL */

    /* Uncomment these two lines if you need a predefined signal to generate
     * for debug */

//    memcpy(temp_sin_signal, sin_signal, FRAME_LENGTH* sizeof(int16_t));
//    env_audio.frame_dec = temp_sin_signal;
#endif    /* if SIMUL */

    /* Apply the volume shift to the current subframe */
    Volume_Shift_Subframe(env_audio.frame_dec);

    /* 抓 16k DSP 解码输出（喂给 ASRC 的信号，被动拷贝不动 DMA） */
    if (cap_in_idx < CAP_N)
    {
        uint32_t n = CAP_N - cap_in_idx;
        if (n > FRAME_LENGTH)
        {
            n = FRAME_LENGTH;
        }
        memcpy(&cap_in[cap_in_idx], env_audio.frame_dec, n * sizeof(int16_t));
        cap_in_idx += n;
    }
    if (cap_in_idx >= CAP_N && cap_out_idx >= CAP_N)
    {
        cap_done = 1;
    }

    /* Assert SPI_CS */
    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_0_BITBAND;
    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);
    Sys_DMA_Set_ChannelSourceAddress(ASRC_IN_IDX, (uint32_t)env_audio.frame_dec);

    /* Re-enable DMA for ASRC input */
    Sys_DMA_ChannelEnable(ASRC_IN_IDX);

    /* Enable ASRC block */
    Sys_ASRC_StatusConfig(ASRC_ENABLE);

    env_audio.frame_idx = 0;
    lpdsp32.state = DSP_IDLE;
}

/* ----------------------------------------------------------------------------
 * Function      : void TIMER_IRQ_FUNC(TIMER_SIMUL)(void)
 * ----------------------------------------------------------------------------
 * Description   : Simulation timer interrupt routine. It's for debugging the
 *                 audio sync mechanism
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void TIMER_IRQ_FUNC(TIMER_SIMUL)(void)
{
#if SIMUL
    uint8_t frame_static[ENCODED_FRAME_LENGTH];
    static uint16_t crpt_cntr = 0;

    //Sys_GPIO_Toggle(ASCC_PHASE_ISR_DIO);


    crpt_cntr = (crpt_cntr > CONN_TIMES) ? 0 : (crpt_cntr + 1);

    /* Insert consecutive bad packets */
    if (crpt_cntr == 0)
    {
        env_audio.packet_state = BAD_PACKET;
    }
    else if (crpt_cntr <= 1)
    {
        env_audio.packet_state = BAD_CONSECUTIVE_PACKET;
    }
    else
    {
        memcpy(frame_static, &coded_sin[cntr_coded], sizeof(uint8_t)* ENCODED_FRAME_LENGTH) ;
        env_audio.packet_state = GOOD_PACKET;
    }

    cntr_coded = (cntr_coded + ENCODED_FRAME_LENGTH) % (3*ENCODED_FRAME_LENGTH);

    BBIF->SYNC_CFG = (ACTIVE | SYNC_ENABLE | SYNC_SOURCE_RF_RX);
    BBIF->SYNC_CFG = (ACTIVE | RX_ACTIVE | SYNC_ENABLE | SYNC_SOURCE_RF_RX);

    DIO->BB_RX_SRC = (BB_RF_SYNC_P_SRC_CONST_HIGH | (DIO->BB_RX_SRC &
                                                     ~DIO_BB_RX_SRC_RF_SYNC_P_Mask));
    DIO->BB_RX_SRC = (BB_RF_SYNC_P_SRC_CONST_LOW | (DIO->BB_RX_SRC &
                                                    ~DIO_BB_RX_SRC_RF_SYNC_P_Mask));

    BBIF->SYNC_CFG = (ACTIVE | RX_IDLE | SYNC_ENABLE | SYNC_SOURCE_RF_RX);
    BBIF->SYNC_CFG = (IDLE | RX_IDLE | SYNC_DISABLE | SYNC_SOURCE_RF_RX);

    Sys_GPIO_Toggle(ASCC_PHASE_ISR_DIO);

    QueueInsert(&audio_queue, frame_static, env_audio.packet_state);

    env_sync.cntr_connection = 0;
    if (env_audio.state == LINK_DISCONNECTED)
    {
        if (QueueCount(&audio_queue) >= PACK_NUM_QUEUE)
        {
            PRINTF("Transient\r\n");
            env_sync.cntr_transient = 0;
            env_sync.audio_sink_cnt = 0;
            env_sync.flag_ascc_phase = false;

            ASRC_CTRL->ASRC_ENABLE_ALIAS = ASRC_ENABLED_BITBAND;
            Sys_ASRC_Reset();

            /* ASCC interrupts */
            NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
            NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

            /* LPDSP32 interrupt */
            NVIC_EnableIRQ(DSP0_IRQn);

            /* Timer interrupts */
            NVIC_EnableIRQ(TIMER_IRQn(TIMER_RENDER));
            NVIC_EnableIRQ(TIMER_IRQn(TIMER_FRAME_TX_END));

            env_audio.state = LINK_TRANSIENT;
        }
    }
#endif    /* if SIMUL */
}

/* ----------------------------------------------------------------------------
 * Function      : void AUDIOSINK_PHASE_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : ASCC phase interrupt handler (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void AUDIOSINK_PHASE_IRQHandler(void)
{
    Sys_GPIO_Toggle(ASCC_PHASE_ISR_DIO);
#if SIMUL
    static uint32_t sim_missed = 0;
    sim_missed = (sim_missed + 1) % 100;

    if (sim_missed < 1)
#else    /* if SIMUL */
    if (AUDIOSINK_CTRL->PHASE_CNT_MISSED_STATUS_ALIAS)
#endif    /* if SIMUL */
    {
        env_sync.phase_cnt_missed = true;
    }
    else
    {
        /* Start the rendered timer */
        Sys_Timers_Stop(1U << TIMER_RENDER);

        /* 4.5 ms (connection interval is 10 ms) + channel delay */
        Sys_Timer_Set_Control(TIMER_RENDER, TIMER_SHOT_MODE             | \
                              (RENDER_TIME_US + /*TODO: android_support_env.channel_delay*/ - 1)  | \
                              TIMER_SLOWCLK_DIV2);
        Sys_Timers_Start(1U << TIMER_RENDER);
        env_sync.timer_free_run = false;

        /* Get audio sink phase count */
        env_sync.audio_sink_phase_cnt = Sys_Audiosink_PhaseCounter();
        if (!env_sync.phase_cnt_missed)
        {
            /* Get audio sink count */
            env_sync.audio_sink_cnt = Sys_Audiosink_Counter() << SHIFT_BIT;
            env_sync.audio_sink_cnt +=
                ((((env_sync.audio_sink_phase_cnt_prev - env_sync.audio_sink_phase_cnt))
                  << SHIFT_BIT) / env_sync.audio_sink_period_cnt);
        }

        /* store audio sink count phase for the next time */
        env_sync.audio_sink_phase_cnt_prev = env_sync.audio_sink_phase_cnt;
        env_sync.phase_cnt_missed = false;
    }
    env_sync.flag_ascc_phase = true;

    AUDIOSINK_CTRL->CNT_RESET_ALIAS = CNT_RESET_BITBAND;
    AUDIOSINK->PHASE_CNT = 0;
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS = PHASE_CNT_START_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void AUDIOSINK_PERIOD_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : ASCC period interrupt handler (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void AUDIOSINK_PERIOD_IRQHandler(void)
{
    env_sync.audio_sink_period_cnt = Sys_Audiosink_PeriodCounter() /
                                     (AUDIOSINK->CFG + 1);
    AUDIOSINK->PERIOD_CNT = 0;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void asrc_reconfig(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure ASRC (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void asrc_reconfig(sync_param_t *sync_param)
{
    int64_t asrc_inc_carrier = 0;

    sync_param->Cr = env_sync.samples_per_packet << SHIFT_BIT;
    sync_param->Ck = sync_param->audio_sink_cnt;

    /* 范围检查只重置稳定计数，不钳位 Ck（7160test 同款）。钳位会把 12k 的
     * Ck≈0.75×spp 压成错误值，导致 ASRC 输出 8k。 */
    if ((sync_param->Ck <= (env_sync.samples_per_packet - ASRC_CFG_THR) << SHIFT_BIT) ||
        (sync_param->Ck >= (env_sync.samples_per_packet + ASRC_CFG_THR) << SHIFT_BIT))
    {
        sync_param->avg_ck_outputcnt = 0;
    }

    /* Store Ck to apply on the next packet if the audio sink value is out of
     * range
     */
    sync_param->Ck_prev = sync_param->Ck;

    /* ASRC 卡死看门狗（1664 同款）：相位计数连续 20 次不动说明 ASRC 停了/卡住，
       复位一次让它自恢复。正常运行时 PHASE_CNT 每包都在变，不会触发。 */
    if (sync_param->asrc_cnt_prev == ASRC->PHASE_CNT)
    {
        sync_param->asrc_cnt_cnst++;
        if (sync_param->asrc_cnt_cnst >= 20)
        {
            Sys_ASRC_Reset();
            sync_param->asrc_cnt_cnst = 0;
        }
    }
    else
    {
        sync_param->asrc_cnt_cnst = 0;
    }
    sync_param->asrc_cnt_prev = ASRC->PHASE_CNT;

#if (OUTPUT_INTRF == PCM_TX_OUTPUT)
    /* 16k→24k（INT_MODE 上采样，PCM WORD_SIZE_16 + MULTIWORD_2 每 FS 周期 2 采样）
       用**名义固定比例**，不跟踪 audiosink 测量。

       原因（2026-09-23 实测，勿再改成闭环）：闭环公式 inc=(Cr-2Ck)/2Ck 隐含假设
       「每个重配置间隔送来的输入样本数 = Cr = samples_per_packet(=320=20ms)」。
       ASHA 的包间隔实测 ≈19.7～19.83ms（不等于包时长 20ms），该前提不成立 →
       闭环会把「20ms 音频在 19.7ms 内播完」配成 +1% 速率：实测 1kHz 纯音经 ASRC 后
       变 1009Hz（变调），且每秒多喂 7100 约 1% 采样 → 周期性溢出丢采样咔哒。
       （1664 能用闭环是因为 RM 包间隔精确 10ms，前提成立；ASHA 不成立。）
       名义比例：f_src/f_sink = 16000/24000 → inc = (f_src-f_sink)/f_sink << 29
       = -(1/3)<<29 = 0xF5555556。包间隔抖动交给 ASHA 的队列吸收。 */
    asrc_inc_carrier = 0xF5555556;
    Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_INT_MODE);
#else    /* if (OUTPUT_INTRF == PCM_TX_OUTPUT) */
    if (sync_param->Ck != 0)
    {
        asrc_inc_carrier = (((sync_param->Cr - sync_param->Ck) << 29) / sync_param->Ck);
        asrc_inc_carrier &= 0xFFFFFFFF;
        Sys_ASRC_Config(asrc_inc_carrier, LOW_DELAY | ASRC_DEC_MODE1);
    }
#endif    /* if (OUTPUT_INTRF == PCM_TX_OUTPUT) */

    static uint16_t dbg_cnt = 0;
    if ((++dbg_cnt & 0x1F) == 0)   /* 每 32 次打一条，避免刷屏 */
    {
        PRINTF("[ASRC] spp=%d Cr=%d Ck=%d inc=0x%08x\r\n",
               env_sync.samples_per_packet,
               (int)(sync_param->Cr >> SHIFT_BIT),
               (int)(sync_param->Ck >> SHIFT_BIT),
               (uint32_t)asrc_inc_carrier);
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void ASRC_ERROR_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : ASRC error interrupt handler (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void ASRC_ERROR_IRQHandler(void)
{
    ASSERT(ASRC_CTRL->ASRC_IN_ERR_ALIAS == 0);
    ASSERT(ASRC_CTRL->ASRC_UPDATE_ERR_ALIAS == 0);

    ASRC_CTRL->ASRC_UPDATE_ERR_CLR_ALIAS = CLR_ASRC_UPDATE_ERR_BITBAND;
    ASRC_CTRL->ASRC_IN_ERR_CLR_ALIAS = CLR_ASRC_IN_ERR_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void Reset_Audio_Sync_Param(sync_param_t *sync_param,
 *                                          audio_frame_param_t *audio_param)
 * ----------------------------------------------------------------------------
 * Description   : Reset the audio and sync instances
 * Inputs        : - sync_param  - pointer to the sync structure
 *                 - audio_param - pointer to the audio structure
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Reset_Audio_Sync_Param(sync_param_t *sync_param, audio_frame_param_t *audio_param)
{
    sync_param->Ck_prev = FRAME_LENGTH << SHIFT_BIT;
    sync_param->Cr = 0;
    sync_param->Ck = 0;
    sync_param->audio_sink_cnt = 0;
    sync_param->avg_ck_outputcnt = 0;
    sync_param->audio_sink_period_cnt = 0;
    sync_param->audio_sink_phase_cnt = 0;
    sync_param->audio_sink_phase_cnt_prev = 0;
    sync_param->asrc_cnt_cnst = 0;
    sync_param->asrc_cnt_prev = 0;
    sync_param->phase_cnt_missed = false;
    sync_param->flag_ascc_phase = false;
    sync_param->cntr_connection = 0;
    sync_param->cntr_transient = 0;
    sync_param->timer_free_run = false;

    audio_param->proc = false;
    audio_param->frame_idx = 0;
    audio_param->state = LINK_DISCONNECTED;
    audio_param->packet_state = BAD_PACKET;
}
