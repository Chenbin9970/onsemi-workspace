/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
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
 * rm_app.c
 * - Remote mic application
 * ----------------------------------------------------------------------------
 * $Revision: 1.12 $
 * $Date: 2019/12/27 18:50:38 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include <printf.h>
#ifdef BS300_ENABLE
#include "bs300_ram_sync.h"
#include "ble_rempro_cmd.h"
#endif    /* ifdef BS300_ENABLE */

uint32_t data_rd = 0;

/* For Test */
uint8_t tmp;
/* 与 app_func.c 解码块重复定义 audio_sink_phase_cnt；未使用，注释掉（同 sleep） */
//uint32_t ascc_cnt, audio_sink_phase_cnt, erraaa = 0;

uint8_t inTempBuffLeft[100]  = {
    0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf,
    0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf
};
uint8_t inTempBuffRight[100] = {
    0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc,
    0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc
};
uint8_t outTempBuff[100];
/* PLC 用：最后一帧好 payload。坏包/丢包时重复它（见 RM_Callback_TRX） */
uint8_t rm_last_good[sizeof(outTempBuff)];
uint16_t app_sendCntrRight = 0, app_sendCntrLeft = 0, app_receiveCntr = 0;
uint32_t app_err1 = 0, app_err2 = 0, app_err3 = 0, app_err4 = 0, app_err5 = 0,
         app_err6 = 0, app_err7 = 0;

struct app_env_tag app_env;

const uint8_t coded_sample[4 * 60] = {
    0xb1, 0x5b, 0x5d, 0xdf, 0xef, 0x7b, 0xb7, 0xff, 0x3c, 0xff, 0xbf, 0x3b,
    0xff, 0xcb, 0x5c, 0xb7,
    0xbb, 0x5d, 0xfa, 0xc7, 0x7d, 0xf3, 0xef, 0x7e, 0xbb, 0xd7, 0xbd, 0xff,
    0xe9, 0xff, 0xfb, 0x7f,
    0xe6, 0xdf, 0x59, 0xd5, 0xd6, 0xfd, 0x35, 0xd3, 0xf9, 0x14, 0x9f, 0x64,
    0xf7, 0x9b, 0x55, 0x3f,
    0x99, 0x49, 0xbf, 0xd5, 0x6d, 0xbf, 0xd7, 0x6b, 0x75, 0xbd, 0x7f, 0xb7,

    0xfa, 0xe7, 0xee, 0x32, 0x7a, 0x1a, 0x4e, 0xd7, 0x5e, 0xf7, 0xef, 0x7f,
    0xdf, 0xef, 0xbe, 0x5b,
    0xed, 0xf6, 0xdb, 0xfd, 0xb5, 0xdf, 0x5d, 0xa7, 0xd7, 0x79, 0xb6, 0xfe,
    0x79, 0xfe, 0xbf, 0xef,
    0x7e, 0xfa, 0xbf, 0x7b, 0xfb, 0xcb, 0x5b, 0xfa, 0xd7, 0x3f, 0x76, 0xf7,
    0x77, 0xde, 0xf5, 0xb5,
    0xfd, 0x5d, 0x36, 0x93, 0x6d, 0x76, 0xdb, 0x4f, 0xe5, 0xff, 0x79, 0xfe,

    0x9f, 0xd3, 0x6d, 0x36, 0xda, 0x18, 0x57, 0x47, 0xdd, 0xde, 0xff, 0xd6,
    0xd1, 0xdf, 0xfe, 0xbb,
    0xef, 0xb6, 0xf1, 0xef, 0x2f, 0xdb, 0xe5, 0x7e, 0x5b, 0xfd, 0x65, 0xfd,
    0xff, 0xff, 0xb3, 0xff,
    0xad, 0xed, 0xcd, 0xed, 0xd9, 0xfd, 0xb7, 0x5b, 0x59, 0xb4, 0xfd, 0x6f,
    0xbd, 0xba, 0xdf, 0x3e,
    0xef, 0xcf, 0x7f, 0xfe, 0x7d, 0x77, 0x57, 0x65, 0x24, 0xff, 0x6b, 0xbe,

    0xf5, 0xdb, 0x0c, 0xb4, 0xac, 0x63, 0xa5, 0xdd, 0xf7, 0x5d, 0xef, 0xad,
    0xfb, 0xff, 0xde, 0xfd,
    0xdb, 0x7f, 0xff, 0xd7, 0xfd, 0xff, 0xdf, 0xee, 0xf9, 0xef, 0xfe, 0xfb,
    0xff, 0xef, 0xff, 0xe5,
    0xae, 0xd6, 0xfd, 0xf5, 0xff, 0x6d, 0x76, 0xdb, 0x6d, 0x76, 0xd6, 0x6d,
    0xb4, 0xff, 0x5d, 0x75,
    0xff, 0x59, 0x77, 0xdf, 0x4f, 0xb6, 0xd7, 0xe9, 0x6e, 0x9e, 0x7d, 0xff
};
uint32_t coded_cntr = 0;

void APP_RM_Init(uint8_t side)
{
    struct rm_callback callback;

    uint8_t temp[16] = RM_HOPLIST;

    app_env.rm_link_status              = LINK_DISCONNECTED;
    app_env.rm_lostLink_counter         = 0;
    app_env.rm_unsuccessLink_cunter     = 0;
    app_env.audio_streaming             = 0;

    app_env.rm_param.audioChnl          = side;
    app_env.rm_param.role = RM_SLAVE_ROLE;
    app_env.rm_param.interval_time      = 10000;
    app_env.rm_param.retrans_time       = 5000;
    app_env.rm_param.audio_rate         = 48;
    app_env.rm_param.radio_rate         = 2000;
    app_env.rm_param.scan_time          = 6500;
    app_env.rm_param.preamble           = 0x55;
    app_env.rm_param.accessword         = (0x00cde629 | (0xf2 << 24));//f2

    app_env.rm_param.payloadFlowRequest = APP_RM_DATA_REQUEST_TYPE;
    app_env.rm_param.renderDelay        = 200;

    if (app_env.rm_param.payloadFlowRequest == RM_APP_REQUEST)
    {
        app_env.rm_param.preFetchDelay = 1300;
    }
    else
    {
        app_env.rm_param.preFetchDelay = 400;
    }

    app_env.rm_param.pktLostLowThrshld     = 10;
    app_env.rm_param.pktLostHighThrshld    = 200;
    app_env.rm_param.pktLostLowThrshldSlow = 1;

    app_env.rm_param.searchTryCntThrshld   = 20;
    app_env.rm_param.waitCntGranularity    = 200;

    app_env.rm_param.stepSize = 1;
    app_env.rm_param.numChnlInHopList = 7;

    app_env.rm_param.mod_idx  = BLE_MOD_IDX;
    app_env.rm_param.dma_memcpy_num   = MEMCPY_DMA_NUM;

    app_env.rm_param.debug_dio_num[0] = 0xff;
    app_env.rm_param.debug_dio_num[1] = 0xff;
    app_env.rm_param.debug_dio_num[2] = 0xff;
    app_env.rm_param.debug_dio_num[3] = 0xff;

    memcpy(app_env.rm_param.hopList, temp, 16);

    callback.trx_event     = RM_Callback_TRX;
    callback.status_update = RM_Callback_StatusUpdate;

    RM_Configure(&app_env.rm_param, callback);

    rm_env.intf.status_update(LINK_DISCONNECTED);
}

uint8_t ptr_right[ENCODED_FRAME_LENGTH];
uint8_t cntr_enc_rm0 = 0, cntr_enc_rm1 = 0;

#if (OUTPUT_INTRF == OD_OUTPUT)
/* 流中断判定：连续 N 个非好包即静音。TX 掉电后没有新解码数据，但 ASRC 输入枯竭
 * 仍输出极限环/残留，经 ch4 → BufferOut → ch5 送到 OD（听感「滋」，持续 1~2 秒）；
 * 不能等 LINK_DISCONNECTED —— 那要丢满 pktLostHighThrshld=200 包（≈2s）。 */
#define RM_STREAM_BREAK_LOSS_N   2
static uint8_t rm_loss_cnt = 0;

static void rm_stream_loss(void)
{
    if (rm_loss_cnt < RM_STREAM_BREAK_LOSS_N)
    {
        rm_loss_cnt++;
    }
    if (rm_loss_cnt >= RM_STREAM_BREAK_LOSS_N)
    {
        Od_Stream_Break();
    }
}

static void rm_stream_good(void)
{
    rm_loss_cnt = 0;
    Od_Stream_Resume();      /* 未静音时内部自判为空操作 */
}
#endif    /* if (OUTPUT_INTRF == OD_OUTPUT) */

uint8_t RM_Callback_TRX(uint8_t type, uint8_t *length, uint8_t *ptr)
{
    switch (type)
    {
        case RM_TX_PAYLOAD_READY_LEFT:
        {
        }
        break;

        case RM_TX_PAYLOAD_READY_RIGHT:
        {
        }
        break;

        case RM_RX_TRANSFER_GOODPKT:
        case RM_RX_TRANSFER_BADCRCPKT:
        case RM_RX_TRANSFER_NOPKT:
        {
            if ((*length) == 0)
            {
                /* PLC should be applied as tx hasn't sent data
                 * for example: repeat previous packet,
                 * 防御分支：库实际不会传 0（见下面 else 的说明），真到了也没好帧
                 * 可重复，按静音填充处理。 */
                memset(outTempBuff, 0xaa, ((app_env.rm_param.audio_rate *
                                            app_env.rm_param.interval_time) /
                                           8000));

                app_err1++;
            }
            else
            {
                uint8_t *frame_src = ptr;
#if (OUTPUT_DECODE_PATH)
                uint8_t  feed      = 1;
#endif    /* if (OUTPUT_DECODE_PATH) */

                if (type == RM_RX_TRANSFER_GOODPKT)
                {
                    memcpy(rm_last_good, ptr, *length);   /* 好帧存下来供 PLC 用 */
#if (OUTPUT_INTRF == OD_OUTPUT)
                    rm_stream_good();     /* 流恢复：解除静音 */
#endif    /* if (OUTPUT_INTRF == OD_OUTPUT) */
                }
                else
                {
                    /* 坏包 / 无包统一走 PLC：重复最后一帧好数据 —— 单包丢失时听感
                     * 连续（即库注释 "repeat previous packet" 的意图）；连丢 N 包
                     * 由 rm_stream_loss() 静音兜住，避免同一帧循环播放变成卡带音。
                     * 库对三种类型都传非 0 的 packet_length（rm_pkt_hdl.c:812-826，
                     * 该字段在 rm_event.c:75 按音频配置算一次、从不归零），所以
                     * ((*length) == 0) 那条「无包」分支永远走不到，损坏 payload
                     * 会被解码成满量级爆音 —— 一字节都不能进解码器。 */
                    app_err1++;
#if (OUTPUT_INTRF == OD_OUTPUT)
                    rm_stream_loss();
                    if (od_break)
                    {
                        feed = 0;     /* 已静音：不喂解码器，免得陈旧数据积在 ASRC 里 */
                    }
#endif    /* if (OUTPUT_INTRF == OD_OUTPUT) */
                    frame_src = rm_last_good;
                }

                memcpy(outTempBuff, frame_src, *length);

#if (OUTPUT_DECODE_PATH)
                if (feed)
                {
                    Rendering_func(outTempBuff);
                }
#endif    /* if (OUTPUT_DECODE_PATH) */

#if (OUTPUT_INTRF == SPI_TX_CODED_OUTPUT)
                SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_1_BITBAND;
                SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_0_BITBAND;
#if 0
                Sys_DMA_Set_ChannelDestAddress(TX_DMA_NUM, (uint32_t)ptr);
                Sys_DMA_ClearChannelStatus(TX_DMA_NUM);
                Sys_DMA_ChannelEnable(TX_DMA_NUM);
#else    /* if 0 */
                Sys_DMA_ChannelConfig(
                    TX_DMA_NUM,
                    TX_DMA_SPI,
                    AUDIO_FRAME_SIZE,
                    0,
                    (uint32_t)outTempBuff,
                    (uint32_t)&SPI0->TX_DATA
                    );
                Sys_DMA_ClearChannelStatus(TX_DMA_NUM);
                Sys_DMA_ChannelEnable(TX_DMA_NUM);
#endif    /* if 0 */
#endif    /* if (OUTPUT_INTRF == SPI_TX_CODED_OUTPUT) */
                if (type == RM_RX_TRANSFER_GOODPKT)
                {
                    if ((app_receiveCntr + 1) != (((outTempBuff[1] << 8) |
                                                   outTempBuff[0])))
                    {
                        app_err2++;
                    }

                    app_receiveCntr = ((outTempBuff[1] << 8) | outTempBuff[0]);

                    if (app_env.rm_param.audioChnl == RM_FIRST_AUDIO_CHANNEL)
                    {
                        if (outTempBuff[20] != 0xaf)
                        {
                            app_err3++;
                        }
                    }
                    else
                    {
                        if (outTempBuff[20] != 0xbc)
                        {
                            app_err3++;
                        }
                    }
                }
            }
        }
        break;

        case RM_SWPLL_SYNC:
        {
        }
        break;

        default:
        {
        }
        break;
    }

    return (0);
}

#ifdef BS300_ENABLE
/* RM 前程序记录：RM 断开后切回原程序并 active，避免停在程序3 静音（参照 sleep saved_prog_before_rm） */
static uint8_t s_saved_prog_before_rm = 0xFF;

/* RM 断开切回助听模式的过渡音量：切换**之前**先把目标程序音量压到 5（随切换会话下发），
 * 切换完成 active() 之后起 2s 倒计时，再恢复到过渡前的用户设定值。
 * 过渡只改 RAM 影子（s_volumes），到点原样写回，不改变用户设定。
 *
 * 倒计时挂在 APP_Timer（开机自启、自我重装的 200ms 周期定时器）上，不用
 * bs300_schedule_delayed_push：那是与测听共用的单槽，且任何 BS300 会话重装
 * BS300_SYNC_TIMER 都会把它的延时提前或让它搁浅。 */
#define RM_TRANS_VOL_LEVEL         5U    /* 过渡音量档位 */
#define RM_TRANS_VOL_RESTORE_TICKS 10U   /* 恢复倒计时，200ms/tick → 2s */

static uint8_t  s_trans_vol_prog  = 0xFF; /* 过渡中的程序号，0xFF = 无过渡 */
static uint8_t  s_trans_vol_saved;        /* 过渡前的用户设定值 */
static uint16_t s_trans_vol_ticks;        /* 恢复倒计时，0 = 未启动 */

/* 切回前调用：把 prog 的音量压成过渡值 5。
 * 已在过渡中（同一个程序）就不重记设定值，否则会把过渡值 5 当成用户设定值，
 * 恢复时就再也回不去了（RM 闪断会在 2s 窗口内重入）。 */
static void rm_trans_volume_arm(uint8_t prog)
{
    s_trans_vol_ticks = 0;      /* 取消上一轮未到点的恢复 */

    if (s_trans_vol_prog != prog)
    {
        s_trans_vol_saved = bs300_get_module_volume(prog);
        s_trans_vol_prog  = prog;
    }

    if (s_trans_vol_saved == RM_TRANS_VOL_LEVEL)
    {
        s_trans_vol_prog = 0xFF;    /* 设定值本来就是 5，无需过渡 */
        return;
    }
    bs300_set_prog_volume(prog, RM_TRANS_VOL_LEVEL);
}

/* 2s 后恢复过渡前的用户设定值。
 * 期间若已经切走（RM 重连到程序3）或用户自己改过音量，就不动当前发声，
 * 只把影子状态里的过渡值写回设定值，避免把用户设定冲成 5。 */
static void rm_restore_volume_cb(void)
{
    uint8_t prog  = s_trans_vol_prog;
    uint8_t saved = s_trans_vol_saved;

    s_trans_vol_prog = 0xFF;
    if (prog == 0xFF) return;
    if (bs300_get_module_volume(prog) != RM_TRANS_VOL_LEVEL) return;

    if (bs300_get_active_prog() == prog)
    {
        PRINTF("[RM] trans volume restore: prog=%u vol=%u\r\n", prog, saved);
        bs300_set_volume_notone_async(saved, NULL);
    }
    else
    {
        bs300_set_prog_volume(prog, saved);
    }
}

/* 由 APP_Timer 每 200ms 调用：倒计时到点则恢复设定值 */
void rm_trans_volume_tick(void)
{
    if (s_trans_vol_prog == 0xFF || s_trans_vol_ticks == 0) return;
    if (--s_trans_vol_ticks != 0) return;
    rm_restore_volume_cb();
}

/* 程序切换完成回调：会话收敛后才 active()。
 *
 * 若期间又来了更新的切换请求，本次会话会被 abort 并留下排队的请求
 * （bs300_switch_pending() 为真）—— 此时**不能** active()，否则会在过渡
 * 中途解除静音，紧接着撞上下一次切换的参数写入（那次没有 mute 兜底）。
 * 让最后那一次会话收尾。 */
static void rm_bs300_switch_done(void)
{
    if (bs300_switch_pending()) return;
    bs300_active();

    /* 音频已恢复，起 2s 倒计时回到用户设定值 */
    if (s_trans_vol_prog != 0xFF)
    {
        s_trans_vol_ticks = RM_TRANS_VOL_RESTORE_TICKS;
    }
}
#endif    /* ifdef BS300_ENABLE */

uint8_t RM_Callback_StatusUpdate(uint8_t status)
{
    switch (status)
    {
        case LINK_DISCONNECTED:
        {
            PRINTF("__RM_LINK_DISCONNECTED\n");
            /* Stop audio transmission to avoid having annoying noise
             * decide if the number of lost links is large, do an action */
#if (OUTPUT_DECODE_PATH && SIMUL != 1)
            NVIC_DisableIRQ(AUDIOSINK_PHASE_IRQn);
            NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

            /* DMA interrupts */
            NVIC_DisableIRQ(DMA_IRQn(ASRC_IN_IDX));

            /* LPDSP32 interrupt */
            NVIC_DisableIRQ(DSP1_IRQn);

            /* Timer interrupts */
            NVIC_DisableIRQ(TIMER_IRQn(TIMER_REGUL));
            Sys_Timers_Stop(1 << TIMER_REGUL);
#if (OUTPUT_INTRF == OD_OUTPUT)
            /* 兜底静音（正常路径早在连续丢包时就静音了）：先停采 ASRC 并清零
               BufferOut，再停 ch5。 */
            Od_Stream_Break();
            /* 停 OD DMA → OD 下溢保护静音 */
            Sys_DMA_ChannelDisable(OD_DMA_NUM);
#endif    /* if (OUTPUT_INTRF == OD_OUTPUT) */
#endif    /* if (OUTPUT_DECODE_PATH && SIMUL != 1) */
#ifdef BS300_ENABLE
            if (app_env.audio_streaming)
            {
                /* 远端流中断 → BS300 静音（参照 peripheral_server_sleep rm_app） */
                bs300_mute();
                app_env.audio_streaming = 0;

                /* 程序恢复：切回 RM 前程序（异步，active 在完成回调里） */
                if (s_saved_prog_before_rm != 0xFF)
                {
                    if (s_saved_prog_before_rm != 3)
                    {
                        /* 切换前把目标程序音量压到过渡值 5（随会话下发），
                         * active 后 2s 再在 rm_restore_volume_cb 里恢复设定值 */
                        rm_trans_volume_arm(s_saved_prog_before_rm);
                        bs300_switch_program_async(s_saved_prog_before_rm,
                                                   rm_bs300_switch_done);
                    }
                    else
                    {
                        bs300_active();
                    }
                    /* RM 断开并恢复程序后，若有 BLE 连接则主动上报程序号 */
                    if (ble_env.state == APPM_CONNECTED)
                    {
                        rempro_push_scene_change(s_saved_prog_before_rm);
                    }
                    s_saved_prog_before_rm = 0xFF;
                }
            }
#endif    /* ifdef BS300_ENABLE */
            app_env.rm_lostLink_counter++;
        }
        break;

        case LINK_ESTABLISHMENT_UNSUCCESS:
        {
            app_env.rm_unsuccessLink_cunter++;
        }
        break;

        case LINK_ESTABLISHED:
        {
            PRINTF("__RM_LINK_ESTABLISHED\n");
            /* start audio transmission */
#if (OUTPUT_DECODE_PATH && SIMUL != 1)
            asrc_stable     = false;
            cntr_stability  = 0;
            audio_sink_cnt  = 0;
            flag_ascc_phase = false;

            Sys_ASRC_Reset();

#if (OUTPUT_INTRF == OD_OUTPUT)
            /* 恢复 ch4 采样（清 od_break + 丢包计数）；未静音时空操作 */
            rm_loss_cnt = 0;
            Od_Stream_Resume();

            /* 重启 OD DMA（BufferOut → OD_DATA） */
            Sys_DMA_ChannelDisable(OD_DMA_NUM);
            Sys_DMA_ChannelConfig(OD_DMA_NUM, RX_DMA_OD, 16, 0,
                                  (uint32_t)BufferOut,
                                  (uint32_t)&(AUDIO->OD_DATA));
            DMA_CTRL1[OD_DMA_NUM].TRANSFER_LENGTH_SHORT = 2 * FRAME_LENGTH;
            Sys_DMA_ChannelEnable(OD_DMA_NUM);
#endif    /* if (OUTPUT_INTRF == OD_OUTPUT) */

#ifdef BS300_ENABLE
            /* 记录 RM 前程序（断开后据此恢复），随后切到程序3 接管（参照 sleep） */
            /* 只在本次 RM 会话**首次**建链时记录原程序。
             *
             * 必须加守卫：RM 库的 rm_env 只有一个 statusChange 槽且置位前比较
             * oldLinkStatus，所以 ESTABLISHED→DISCONNECTED→ESTABLISHED 的闪断会被
             * 整个吞掉。若每次都重记，第二次建链时 s_cur_prog 已被上面的异步切换
             * 改成 3（bs300_switch_program_async 在发 I2C 前就改 s_cur_prog），
             * 「原程序」就被记成 3 —— 之后 if (saved != 3) 永远为假，
             * 断链再也切不回去，BS300 卡在程序3 静音。 */
            if (s_saved_prog_before_rm == 0xFF)
            {
                s_saved_prog_before_rm = bs300_get_active_prog();
            }
            bs300_set_prog_volume(3, 9);
            bs300_mute();
            /* 异步切换：不阻塞主循环。忙时 bs300_switch_program_async 会 abort 当前
             * 会话并排队（含按键/Rempro 触发的会话），主循环的 process_deferred 再启动。
             * diff 基准是 s_dsp_state（每命令被芯片确认后才更新），故自动从当前进度续传。 */
            bs300_switch_program_async(3, rm_bs300_switch_done);
            app_env.audio_streaming = 1;
            /* RM 连接切到程序3 播放时，若有 BLE 连接则主动上报程序号 */
            if (ble_env.state == APPM_CONNECTED)
            {
                rempro_push_scene_change(3);
            }
#endif    /* ifdef BS300_ENABLE */

            /* ASCC interrupts */
            NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
            NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

            /* DMA interrupts */
            NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));

            /* LPDSP32 interrupt */
            NVIC_EnableIRQ(DSP1_IRQn);

            /* Timer interrupts */
            NVIC_EnableIRQ(TIMER_IRQn(TIMER_REGUL));
#endif    /* if (OUTPUT_DECODE_PATH && SIMUL != 1) */
        }
        break;

        default:
        {
        }
        break;
    }

    app_env.rm_link_status = status;
    return (0);
}
