/* 由 scripts/gen_dsp_7100_parm.py 从 parm1604.txt 生成。勿手改。 */
#include "dsp_7100_init.h"

static const uint8_t dsp_parm_p0[] = { 0xA7, 0x01, 0x00, 0x00, 0x00, 0x25, };  /* pid 1 */
static const uint8_t dsp_parm_p1[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 4 */
static const uint8_t dsp_parm_p2[] = { 0xA7, 0x01, 0x00, 0x00, 0x00, 0x35, };  /* pid 7 */
static const uint8_t dsp_parm_p3[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 10 */
static const uint8_t dsp_parm_p4[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x24, };  /* pid 13 */
static const uint8_t dsp_parm_p5[] = { 0xA7, 0x01, 0x00, 0x30, 0x00, 0x09, };  /* pid 16 */
static const uint8_t dsp_parm_p6[] = { 0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x01, };  /* pid 19 */
static const uint8_t dsp_parm_p7[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 22 */
static const uint8_t dsp_parm_p8[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x05, 0x01, };  /* pid 25 */
static const uint8_t dsp_parm_p9[] = { 0xA7, 0x01, 0x00, 0x0F, 0x00, 0x38, };  /* pid 28 */
static const uint8_t dsp_parm_p10[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x07, 0x01, };  /* pid 31 */
static const uint8_t dsp_parm_p11[] = { 0xA7, 0x01, 0x00, 0x77, 0x01, 0x38, };  /* pid 34 */
static const uint8_t dsp_parm_p12[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x0A, 0x01, };  /* pid 37 */
static const uint8_t dsp_parm_p13[] = { 0xA7, 0x01, 0x00, 0x32, 0x01, 0x38, };  /* pid 40 */
static const uint8_t dsp_parm_p14[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x08, 0x01, };  /* pid 43 */
static const uint8_t dsp_parm_p15[] = { 0xA7, 0x01, 0x00, 0x06, 0x00, 0x38, };  /* pid 46 */
static const uint8_t dsp_parm_p16[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x09, 0x01, };  /* pid 49 */
static const uint8_t dsp_parm_p17[] = { 0xA7, 0x01, 0x00, 0xAE, 0x00, 0x38, };  /* pid 52 */
static const uint8_t dsp_parm_p18[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x0B, 0x01, };  /* pid 55 */
static const uint8_t dsp_parm_p19[] = { 0xA7, 0x01, 0x00, 0x0F, 0x00, 0x38, };  /* pid 58 */
static const uint8_t dsp_parm_p20[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x10, 0x01, };  /* pid 61 */
static const uint8_t dsp_parm_p21[] = { 0xA7, 0x01, 0x00, 0x0E, 0x01, 0x38, };  /* pid 64 */
static const uint8_t dsp_parm_p22[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x06, 0x01, };  /* pid 67 */
static const uint8_t dsp_parm_p23[] = { 0xA7, 0x01, 0x00, 0x12, 0x00, 0x38, };  /* pid 70 */
static const uint8_t dsp_parm_p24[] = { 0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x01, };  /* pid 73 */
static const uint8_t dsp_parm_p25[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 76 */
static const uint8_t dsp_parm_p26[] = { 0xA7, 0x01, 0x00, 0x00, 0x00, 0x0C, };  /* pid 79 */
static const uint8_t dsp_parm_p27[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 82 */
static const uint8_t dsp_parm_p28[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 85 */
static const uint8_t dsp_parm_p29[] = { 0xA7, 0x01, 0x00, 0x00, 0x00, 0x35, };  /* pid 88 */
static const uint8_t dsp_parm_p30[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 91 */
static const uint8_t dsp_parm_p31[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x30, };  /* pid 94 */
static const uint8_t dsp_parm_p32[] = { 0xA7, 0x01, 0x00, 0x30, 0x00, 0x09, };  /* pid 97 */
static const uint8_t dsp_parm_p33[] = { 0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x02, };  /* pid 100 */
static const uint8_t dsp_parm_p34[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 103 */
static const uint8_t dsp_parm_p35[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x05, 0x02, };  /* pid 106 */
static const uint8_t dsp_parm_p36[] = { 0xA7, 0x01, 0x00, 0x0F, 0x00, 0x38, };  /* pid 109 */
static const uint8_t dsp_parm_p37[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x07, 0x02, };  /* pid 112 */
static const uint8_t dsp_parm_p38[] = { 0xA7, 0x01, 0x00, 0x77, 0x01, 0x38, };  /* pid 115 */
static const uint8_t dsp_parm_p39[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x0A, 0x02, };  /* pid 118 */
static const uint8_t dsp_parm_p40[] = { 0xA7, 0x01, 0x00, 0x32, 0x01, 0x38, };  /* pid 121 */
static const uint8_t dsp_parm_p41[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x08, 0x02, };  /* pid 124 */
static const uint8_t dsp_parm_p42[] = { 0xA7, 0x01, 0x00, 0x06, 0x00, 0x38, };  /* pid 127 */
static const uint8_t dsp_parm_p43[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x09, 0x02, };  /* pid 130 */
static const uint8_t dsp_parm_p44[] = { 0xA7, 0x01, 0x00, 0xAE, 0x00, 0x38, };  /* pid 133 */
static const uint8_t dsp_parm_p45[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x0B, 0x02, };  /* pid 136 */
static const uint8_t dsp_parm_p46[] = { 0xA7, 0x01, 0x00, 0x0F, 0x00, 0x38, };  /* pid 139 */
static const uint8_t dsp_parm_p47[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x10, 0x02, };  /* pid 142 */
static const uint8_t dsp_parm_p48[] = { 0xA7, 0x01, 0x00, 0x0E, 0x01, 0x38, };  /* pid 145 */
static const uint8_t dsp_parm_p49[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x06, 0x02, };  /* pid 149 */
static const uint8_t dsp_parm_p50[] = { 0xA7, 0x01, 0x00, 0x12, 0x00, 0x38, };  /* pid 152 */
static const uint8_t dsp_parm_p51[] = { 0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x01, };  /* pid 155 */
static const uint8_t dsp_parm_p52[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 158 */
static const uint8_t dsp_parm_p53[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 161 */
static const uint8_t dsp_parm_p54[] = { 0xA7, 0x01, 0x00, 0x00, 0x00, 0x35, };  /* pid 164 */
static const uint8_t dsp_parm_p55[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 167 */
static const uint8_t dsp_parm_p56[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x3C, };  /* pid 170 */
static const uint8_t dsp_parm_p57[] = { 0xA7, 0x01, 0x00, 0x30, 0x00, 0x09, };  /* pid 173 */
static const uint8_t dsp_parm_p58[] = { 0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x03, };  /* pid 176 */
static const uint8_t dsp_parm_p59[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 179 */
static const uint8_t dsp_parm_p60[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x05, 0x03, };  /* pid 182 */
static const uint8_t dsp_parm_p61[] = { 0xA7, 0x01, 0x00, 0x0F, 0x00, 0x38, };  /* pid 185 */
static const uint8_t dsp_parm_p62[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x07, 0x03, };  /* pid 188 */
static const uint8_t dsp_parm_p63[] = { 0xA7, 0x01, 0x00, 0x77, 0x01, 0x38, };  /* pid 191 */
static const uint8_t dsp_parm_p64[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x0A, 0x03, };  /* pid 194 */
static const uint8_t dsp_parm_p65[] = { 0xA7, 0x01, 0x00, 0x32, 0x01, 0x38, };  /* pid 197 */
static const uint8_t dsp_parm_p66[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x08, 0x03, };  /* pid 200 */
static const uint8_t dsp_parm_p67[] = { 0xA7, 0x01, 0x00, 0x06, 0x00, 0x38, };  /* pid 203 */
static const uint8_t dsp_parm_p68[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x09, 0x03, };  /* pid 206 */
static const uint8_t dsp_parm_p69[] = { 0xA7, 0x01, 0x00, 0xAE, 0x00, 0x38, };  /* pid 209 */
static const uint8_t dsp_parm_p70[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x0B, 0x03, };  /* pid 212 */
static const uint8_t dsp_parm_p71[] = { 0xA7, 0x01, 0x00, 0x0F, 0x00, 0x38, };  /* pid 215 */
static const uint8_t dsp_parm_p72[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x10, 0x03, };  /* pid 218 */
static const uint8_t dsp_parm_p73[] = { 0xA7, 0x01, 0x00, 0x0E, 0x01, 0x38, };  /* pid 221 */
static const uint8_t dsp_parm_p74[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x06, 0x03, };  /* pid 224 */
static const uint8_t dsp_parm_p75[] = { 0xA7, 0x01, 0x00, 0x12, 0x00, 0x38, };  /* pid 227 */
static const uint8_t dsp_parm_p76[] = { 0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x01, };  /* pid 230 */
static const uint8_t dsp_parm_p77[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 233 */
static const uint8_t dsp_parm_p78[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 236 */
static const uint8_t dsp_parm_p79[] = { 0xA7, 0x01, 0x00, 0x00, 0x00, 0x35, };  /* pid 239 */
static const uint8_t dsp_parm_p80[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 242 */
static const uint8_t dsp_parm_p81[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x48, };  /* pid 245 */
static const uint8_t dsp_parm_p82[] = { 0xA7, 0x01, 0x00, 0x30, 0x00, 0x09, };  /* pid 248 */
static const uint8_t dsp_parm_p83[] = { 0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x04, };  /* pid 251 */
static const uint8_t dsp_parm_p84[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 254 */
static const uint8_t dsp_parm_p85[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x05, 0x04, };  /* pid 257 */
static const uint8_t dsp_parm_p86[] = { 0xA7, 0x01, 0x00, 0x0F, 0x00, 0x38, };  /* pid 260 */
static const uint8_t dsp_parm_p87[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x07, 0x04, };  /* pid 263 */
static const uint8_t dsp_parm_p88[] = { 0xA7, 0x01, 0x00, 0x77, 0x01, 0x38, };  /* pid 266 */
static const uint8_t dsp_parm_p89[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x0A, 0x04, };  /* pid 269 */
static const uint8_t dsp_parm_p90[] = { 0xA7, 0x01, 0x00, 0x32, 0x01, 0x38, };  /* pid 272 */
static const uint8_t dsp_parm_p91[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x08, 0x04, };  /* pid 275 */
static const uint8_t dsp_parm_p92[] = { 0xA7, 0x01, 0x00, 0x06, 0x00, 0x38, };  /* pid 278 */
static const uint8_t dsp_parm_p93[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x09, 0x04, };  /* pid 281 */
static const uint8_t dsp_parm_p94[] = { 0xA7, 0x01, 0x00, 0xAE, 0x00, 0x38, };  /* pid 284 */
static const uint8_t dsp_parm_p95[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x0B, 0x04, };  /* pid 287 */
static const uint8_t dsp_parm_p96[] = { 0xA7, 0x01, 0x00, 0x0F, 0x00, 0x38, };  /* pid 290 */
static const uint8_t dsp_parm_p97[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x10, 0x04, };  /* pid 293 */
static const uint8_t dsp_parm_p98[] = { 0xA7, 0x01, 0x00, 0x0E, 0x01, 0x38, };  /* pid 296 */
static const uint8_t dsp_parm_p99[] = { 0xA7, 0x03, 0x00, 0x00, 0x00, 0x37, 0x06, 0x04, };  /* pid 299 */
static const uint8_t dsp_parm_p100[] = { 0xA7, 0x01, 0x00, 0x12, 0x00, 0x38, };  /* pid 302 */
static const uint8_t dsp_parm_p101[] = { 0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x01, };  /* pid 305 */
static const uint8_t dsp_parm_p102[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 308 */
static const uint8_t dsp_parm_p103[] = { 0xA7, 0x01, 0x00, 0x00, 0x00, 0x26, };  /* pid 311 */
static const uint8_t dsp_parm_p104[] = { 0xA7, 0x01, 0x00, 0x03, 0x00, 0x02, };  /* pid 314 */

const dsp_a7_cmd_t dsp_parm_cmds[] = {
    { dsp_parm_p0, (uint16_t)sizeof(dsp_parm_p0) },
    { dsp_parm_p1, (uint16_t)sizeof(dsp_parm_p1) },
    { dsp_parm_p2, (uint16_t)sizeof(dsp_parm_p2) },
    { dsp_parm_p3, (uint16_t)sizeof(dsp_parm_p3) },
    { dsp_parm_p4, (uint16_t)sizeof(dsp_parm_p4) },
    { dsp_parm_p5, (uint16_t)sizeof(dsp_parm_p5) },
    { dsp_parm_p6, (uint16_t)sizeof(dsp_parm_p6) },
    { dsp_parm_p7, (uint16_t)sizeof(dsp_parm_p7) },
    { dsp_parm_p8, (uint16_t)sizeof(dsp_parm_p8) },
    { dsp_parm_p9, (uint16_t)sizeof(dsp_parm_p9) },
    { dsp_parm_p10, (uint16_t)sizeof(dsp_parm_p10) },
    { dsp_parm_p11, (uint16_t)sizeof(dsp_parm_p11) },
    { dsp_parm_p12, (uint16_t)sizeof(dsp_parm_p12) },
    { dsp_parm_p13, (uint16_t)sizeof(dsp_parm_p13) },
    { dsp_parm_p14, (uint16_t)sizeof(dsp_parm_p14) },
    { dsp_parm_p15, (uint16_t)sizeof(dsp_parm_p15) },
    { dsp_parm_p16, (uint16_t)sizeof(dsp_parm_p16) },
    { dsp_parm_p17, (uint16_t)sizeof(dsp_parm_p17) },
    { dsp_parm_p18, (uint16_t)sizeof(dsp_parm_p18) },
    { dsp_parm_p19, (uint16_t)sizeof(dsp_parm_p19) },
    { dsp_parm_p20, (uint16_t)sizeof(dsp_parm_p20) },
    { dsp_parm_p21, (uint16_t)sizeof(dsp_parm_p21) },
    { dsp_parm_p22, (uint16_t)sizeof(dsp_parm_p22) },
    { dsp_parm_p23, (uint16_t)sizeof(dsp_parm_p23) },
    { dsp_parm_p24, (uint16_t)sizeof(dsp_parm_p24) },
    { dsp_parm_p25, (uint16_t)sizeof(dsp_parm_p25) },
    { dsp_parm_p26, (uint16_t)sizeof(dsp_parm_p26) },
    { dsp_parm_p27, (uint16_t)sizeof(dsp_parm_p27) },
    { dsp_parm_p28, (uint16_t)sizeof(dsp_parm_p28) },
    { dsp_parm_p29, (uint16_t)sizeof(dsp_parm_p29) },
    { dsp_parm_p30, (uint16_t)sizeof(dsp_parm_p30) },
    { dsp_parm_p31, (uint16_t)sizeof(dsp_parm_p31) },
    { dsp_parm_p32, (uint16_t)sizeof(dsp_parm_p32) },
    { dsp_parm_p33, (uint16_t)sizeof(dsp_parm_p33) },
    { dsp_parm_p34, (uint16_t)sizeof(dsp_parm_p34) },
    { dsp_parm_p35, (uint16_t)sizeof(dsp_parm_p35) },
    { dsp_parm_p36, (uint16_t)sizeof(dsp_parm_p36) },
    { dsp_parm_p37, (uint16_t)sizeof(dsp_parm_p37) },
    { dsp_parm_p38, (uint16_t)sizeof(dsp_parm_p38) },
    { dsp_parm_p39, (uint16_t)sizeof(dsp_parm_p39) },
    { dsp_parm_p40, (uint16_t)sizeof(dsp_parm_p40) },
    { dsp_parm_p41, (uint16_t)sizeof(dsp_parm_p41) },
    { dsp_parm_p42, (uint16_t)sizeof(dsp_parm_p42) },
    { dsp_parm_p43, (uint16_t)sizeof(dsp_parm_p43) },
    { dsp_parm_p44, (uint16_t)sizeof(dsp_parm_p44) },
    { dsp_parm_p45, (uint16_t)sizeof(dsp_parm_p45) },
    { dsp_parm_p46, (uint16_t)sizeof(dsp_parm_p46) },
    { dsp_parm_p47, (uint16_t)sizeof(dsp_parm_p47) },
    { dsp_parm_p48, (uint16_t)sizeof(dsp_parm_p48) },
    { dsp_parm_p49, (uint16_t)sizeof(dsp_parm_p49) },
    { dsp_parm_p50, (uint16_t)sizeof(dsp_parm_p50) },
    { dsp_parm_p51, (uint16_t)sizeof(dsp_parm_p51) },
    { dsp_parm_p52, (uint16_t)sizeof(dsp_parm_p52) },
    { dsp_parm_p53, (uint16_t)sizeof(dsp_parm_p53) },
    { dsp_parm_p54, (uint16_t)sizeof(dsp_parm_p54) },
    { dsp_parm_p55, (uint16_t)sizeof(dsp_parm_p55) },
    { dsp_parm_p56, (uint16_t)sizeof(dsp_parm_p56) },
    { dsp_parm_p57, (uint16_t)sizeof(dsp_parm_p57) },
    { dsp_parm_p58, (uint16_t)sizeof(dsp_parm_p58) },
    { dsp_parm_p59, (uint16_t)sizeof(dsp_parm_p59) },
    { dsp_parm_p60, (uint16_t)sizeof(dsp_parm_p60) },
    { dsp_parm_p61, (uint16_t)sizeof(dsp_parm_p61) },
    { dsp_parm_p62, (uint16_t)sizeof(dsp_parm_p62) },
    { dsp_parm_p63, (uint16_t)sizeof(dsp_parm_p63) },
    { dsp_parm_p64, (uint16_t)sizeof(dsp_parm_p64) },
    { dsp_parm_p65, (uint16_t)sizeof(dsp_parm_p65) },
    { dsp_parm_p66, (uint16_t)sizeof(dsp_parm_p66) },
    { dsp_parm_p67, (uint16_t)sizeof(dsp_parm_p67) },
    { dsp_parm_p68, (uint16_t)sizeof(dsp_parm_p68) },
    { dsp_parm_p69, (uint16_t)sizeof(dsp_parm_p69) },
    { dsp_parm_p70, (uint16_t)sizeof(dsp_parm_p70) },
    { dsp_parm_p71, (uint16_t)sizeof(dsp_parm_p71) },
    { dsp_parm_p72, (uint16_t)sizeof(dsp_parm_p72) },
    { dsp_parm_p73, (uint16_t)sizeof(dsp_parm_p73) },
    { dsp_parm_p74, (uint16_t)sizeof(dsp_parm_p74) },
    { dsp_parm_p75, (uint16_t)sizeof(dsp_parm_p75) },
    { dsp_parm_p76, (uint16_t)sizeof(dsp_parm_p76) },
    { dsp_parm_p77, (uint16_t)sizeof(dsp_parm_p77) },
    { dsp_parm_p78, (uint16_t)sizeof(dsp_parm_p78) },
    { dsp_parm_p79, (uint16_t)sizeof(dsp_parm_p79) },
    { dsp_parm_p80, (uint16_t)sizeof(dsp_parm_p80) },
    { dsp_parm_p81, (uint16_t)sizeof(dsp_parm_p81) },
    { dsp_parm_p82, (uint16_t)sizeof(dsp_parm_p82) },
    { dsp_parm_p83, (uint16_t)sizeof(dsp_parm_p83) },
    { dsp_parm_p84, (uint16_t)sizeof(dsp_parm_p84) },
    { dsp_parm_p85, (uint16_t)sizeof(dsp_parm_p85) },
    { dsp_parm_p86, (uint16_t)sizeof(dsp_parm_p86) },
    { dsp_parm_p87, (uint16_t)sizeof(dsp_parm_p87) },
    { dsp_parm_p88, (uint16_t)sizeof(dsp_parm_p88) },
    { dsp_parm_p89, (uint16_t)sizeof(dsp_parm_p89) },
    { dsp_parm_p90, (uint16_t)sizeof(dsp_parm_p90) },
    { dsp_parm_p91, (uint16_t)sizeof(dsp_parm_p91) },
    { dsp_parm_p92, (uint16_t)sizeof(dsp_parm_p92) },
    { dsp_parm_p93, (uint16_t)sizeof(dsp_parm_p93) },
    { dsp_parm_p94, (uint16_t)sizeof(dsp_parm_p94) },
    { dsp_parm_p95, (uint16_t)sizeof(dsp_parm_p95) },
    { dsp_parm_p96, (uint16_t)sizeof(dsp_parm_p96) },
    { dsp_parm_p97, (uint16_t)sizeof(dsp_parm_p97) },
    { dsp_parm_p98, (uint16_t)sizeof(dsp_parm_p98) },
    { dsp_parm_p99, (uint16_t)sizeof(dsp_parm_p99) },
    { dsp_parm_p100, (uint16_t)sizeof(dsp_parm_p100) },
    { dsp_parm_p101, (uint16_t)sizeof(dsp_parm_p101) },
    { dsp_parm_p102, (uint16_t)sizeof(dsp_parm_p102) },
    { dsp_parm_p103, (uint16_t)sizeof(dsp_parm_p103) },
    { dsp_parm_p104, (uint16_t)sizeof(dsp_parm_p104) },
};

const uint16_t dsp_parm_cmd_cnt = 105;
