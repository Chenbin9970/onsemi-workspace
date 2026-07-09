#include "bs300_driver.h"
#include "system/includes.h"
#include "syscfg_id.h"

/**
 * @brief  Load one VM segment for a program.
 * @return 0 on success, -1 on error
 */
int bs300_vm_load_seg(u8 prog_idx, u8 seg_idx, u8 *buf, u16 len)
{
    u8 vm_id;

    if (buf == NULL || len != BS300_VM_SEG_SIZE
        || prog_idx >= BS300_PROG_COUNT || seg_idx >= BS300_VM_SEG_COUNT) {
        return -1;
    }
    vm_id = BS300_VM_ID_BASE + prog_idx * BS300_VM_SEG_COUNT + seg_idx;
    {
        int vm_retry;
        for (vm_retry = 0; vm_retry < 5; vm_retry++) {
            if (syscfg_read(vm_id, buf, len) >= 0) break;
            clr_wdt();
            delay(2000UL);  // 1ms — wait for TWS Flash to release
        }
        if (vm_retry >= 5) {
            bs300_debug("bs300: vm_load_seg p=%d s=%d id=%d fail\n",
                        prog_idx, seg_idx, vm_id);
            return -1;
        }
    }
    bs300_debug("bs300: vm_load_seg p=%d s=%d id=%d ok\n",
                prog_idx, seg_idx, vm_id);
    return 0;
}

/**
 * @brief  Save one VM segment for a program.
 * @return 0 on success, -1 on error
 */
int bs300_vm_save_seg(u8 prog_idx, u8 seg_idx, const u8 *buf, u16 len)
{
    u8 vm_id;

    if (buf == NULL || len != BS300_VM_SEG_SIZE
        || prog_idx >= BS300_PROG_COUNT || seg_idx >= BS300_VM_SEG_COUNT) {
        return -1;
    }
    vm_id = BS300_VM_ID_BASE + prog_idx * BS300_VM_SEG_COUNT + seg_idx;
    {
        int vm_retry;
        for (vm_retry = 0; vm_retry < 5; vm_retry++) {
            if (syscfg_write(vm_id, buf, len) >= 0) break;
            clr_wdt();
            delay(2000UL);
        }
        if (vm_retry >= 5) {
            bs300_debug("bs300: vm_save_seg p=%d s=%d id=%d fail\n",
                        prog_idx, seg_idx, vm_id);
            return -1;
        }
    }
    bs300_debug("bs300: vm_save_seg p=%d s=%d id=%d ok\n",
                prog_idx, seg_idx, vm_id);
    return 0;
}

/**
 * @brief  Load full program from VM segments into buffer.
 * @return 0 on success, -1 on error
 */
int bs300_program_load(u8 prog_idx, u8 *buf)
{
    int ret;

    if (buf == NULL || prog_idx >= BS300_PROG_COUNT) {
        return -1;
    }
    ret = bs300_vm_load_seg(prog_idx, 0, buf, BS300_VM_SEG_SIZE);
    if (ret < 0) {
        return -1;
    }
    ret = bs300_vm_load_seg(prog_idx, 1, buf + BS300_VM_SEG_SIZE,
                            BS300_VM_SEG_SIZE);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

/**
 * @brief  Save full program buffer to VM segments.
 * @return 0 on success, -1 on error
 */
int bs300_program_save(u8 prog_idx, const u8 *buf)
{
    int ret;

    if (buf == NULL || prog_idx >= BS300_PROG_COUNT) {
        return -1;
    }
    ret = bs300_vm_save_seg(prog_idx, 0, buf, BS300_VM_SEG_SIZE);
    if (ret < 0) {
        return -1;
    }
    ret = bs300_vm_save_seg(prog_idx, 1, buf + BS300_VM_SEG_SIZE,
                            BS300_VM_SEG_SIZE);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

/**
 * @brief  Compute 32-bit checksum (byte sum).
 * @return 32-bit checksum
 */
u32 bs300_checksum32(const u8 *buf, u16 len)
{
    u32 sum = 0;
    u16 i;

    for (i = 0; i < len; i++) {
        sum += buf[i];
    }
    return sum;
}

/**
 * @brief  Load calibration data from VM with checksum verification.
 *         Uses 270-byte max temp buffer. Clears VM on checksum mismatch.
 * @return 0 on success, -1 on error
 */
int bs300_calib_vm_load(u8 *buf)
{
    u8 tmp[BS300_VM_SEG_SIZE];
    u32 stored_chk, computed_chk;
    int i;

    if (buf == NULL) {
        return -1;
    }
    if (syscfg_read(BS300_VM_ID_CALIB, tmp, BS300_CALIB_SIZE) < 0) {
        bs300_debug("bs300: calib_vm_load read fail\n");
        return -1;
    }
    if (syscfg_read(BS300_VM_ID_CALIB_CHK, (u8 *)&stored_chk, 4) < 0) {
        bs300_debug("bs300: calib_vm_load no stored chk\n");
        return -1;
    }
    computed_chk = bs300_checksum32(tmp, BS300_CALIB_SIZE);
    bs300_debug("bs300: calib_vm_load chk comp=%lu store=%lu\n",
                computed_chk, stored_chk);
    if (computed_chk != stored_chk) {
        for (i = 0; i < BS300_CALIB_SIZE; i++) {
            tmp[i] = 0x00;
        }
        syscfg_write(BS300_VM_ID_CALIB, tmp, BS300_CALIB_SIZE);
        stored_chk = 0;
        syscfg_write(BS300_VM_ID_CALIB_CHK, (u8 *)&stored_chk, 4);
        return -1;
    }
    for (i = 0; i < BS300_CALIB_SIZE; i++) {
        buf[i] = tmp[i];
    }
    bs300_debug("bs300: calib_vm_load ok\n");
    return 0;
}

/**
 * @brief  Save calibration data to VM with checksum.
 * @return 0 on success, -1 on error
 */
int bs300_calib_vm_save(const u8 *buf)
{
    u32 chk;

    if (buf == NULL) {
        return -1;
    }
    if (syscfg_write(BS300_VM_ID_CALIB, buf, BS300_CALIB_SIZE) < 0) {
        bs300_debug("bs300: calib_vm_save fail\n");
        return -1;
    }
    chk = bs300_checksum32(buf, BS300_CALIB_SIZE);
    syscfg_write(BS300_VM_ID_CALIB_CHK, (u8 *)&chk, 4);
    bs300_debug("bs300: calib_vm_save ok chk=%lu\n", chk);
    return 0;
}

/* ================================================================
 *  VM ID 31 — Active program index + struct format version
 *  Layout: byte 0 = active program (0-3), byte 1 = struct version
 * ================================================================ */

int bs300_vm_read_active_prog(u8 *prog_idx)
{
    u8 buf[2];
    if (prog_idx == NULL) {
        return -1;
    }
    if (syscfg_read(BS300_VM_ID_ACTIVE_PROG, buf, 2) < 0) {
        bs300_debug("bs300: vm_read_active_prog empty, default 0\n");
        *prog_idx = 0;
        return 0;
    }
    if (buf[1] != BS300_STRUCT_VERSION) {
        bs300_debug("bs300: vm struct version mismatch vm=%d exp=%d\n",
                    buf[1], BS300_STRUCT_VERSION);
        *prog_idx = 0;
        return -2;  /* signal version mismatch */
    }
    if (buf[0] > 3) {
        bs300_debug("bs300: vm_read_active_prog invalid=%d, default 0\n",
                    buf[0]);
        *prog_idx = 0;
        return 0;
    }
    *prog_idx = buf[0];
    bs300_debug("bs300: vm_read_active_prog=%d ver=%d\n", buf[0], buf[1]);
    return 0;
}

int bs300_vm_write_active_prog(u8 prog_idx)
{
    u8 buf[2];
    if (prog_idx > 3) {
        return -1;
    }
    buf[0] = prog_idx;
    buf[1] = BS300_STRUCT_VERSION;
    bs300_debug("bs300: vm_write_active_prog=%d ver=%d\n", prog_idx, buf[1]);
    if (syscfg_write(BS300_VM_ID_ACTIVE_PROG, buf, 2) < 0) {
        bs300_debug("bs300: vm_write_active_prog fail\n");
        return -1;
    }
    return 0;
}

/* ================================================================
 *  Structured program VM load/save
 *  Uses same VM IDs (20-27) but stores structured data instead
 *  of raw Flash bit-packed format.
 *  Struct is serialized as flat bytes, split across 2 segments.
 * ================================================================ */

int bs300_vm_load_struct(u8 prog_idx, bs300_prog_struct_t *out)
{
    u8 seg_buf[BS300_VM_SEG_SIZE];
    u16 offset;
    u16 remaining;
    u16 i;
    u8 seg_idx;

    if (out == NULL || prog_idx >= BS300_PROG_COUNT) {
        return -1;
    }
    offset = 0;
    remaining = (u16)sizeof(bs300_prog_struct_t);
    if (remaining > (u16)(BS300_VM_SEG_SIZE * BS300_VM_SEG_COUNT)) {
        bs300_debug("bs300: vm_load_struct too large=%d\n", remaining);
        return -1;
    }
    for (seg_idx = 0; seg_idx < BS300_VM_SEG_COUNT; seg_idx++) {
        u16 copy_len;
        int ret;

        ret = bs300_vm_load_seg(prog_idx, seg_idx, seg_buf,
                                BS300_VM_SEG_SIZE);
        if (ret < 0) {
            bs300_debug("bs300: vm_load_struct seg=%d fail\n", seg_idx);
            return -1;
        }
        copy_len = remaining;
        if (copy_len > BS300_VM_SEG_SIZE) {
            copy_len = BS300_VM_SEG_SIZE;
        }
        for (i = 0; i < copy_len; i++) {
            ((u8 *)out)[offset + i] = seg_buf[i];
        }
        offset += copy_len;
        remaining -= copy_len;
    }
    bs300_debug("bs300: vm_load_struct p=%d ok size=%d\n",
                prog_idx, (int)sizeof(bs300_prog_struct_t));
    return 0;
}

int bs300_vm_save_struct(u8 prog_idx, const bs300_prog_struct_t *data)
{
    u8 seg_buf[BS300_VM_SEG_SIZE];
    u16 offset;
    u16 remaining;
    u16 i;
    u8 seg_idx;

    if (data == NULL || prog_idx >= BS300_PROG_COUNT) {
        return -1;
    }
    offset = 0;
    remaining = (u16)sizeof(bs300_prog_struct_t);
    if (remaining > (u16)(BS300_VM_SEG_SIZE * BS300_VM_SEG_COUNT)) {
        bs300_debug("bs300: vm_save_struct too large=%d\n", remaining);
        return -1;
    }
    for (seg_idx = 0; seg_idx < BS300_VM_SEG_COUNT; seg_idx++) {
        u16 copy_len;
        int ret;

        copy_len = remaining;
        if (copy_len > BS300_VM_SEG_SIZE) {
            copy_len = BS300_VM_SEG_SIZE;
        }
        for (i = 0; i < copy_len; i++) {
            seg_buf[i] = ((const u8 *)data)[offset + i];
        }
        for (i = copy_len; i < BS300_VM_SEG_SIZE; i++) {
            seg_buf[i] = 0x00;
        }
        ret = bs300_vm_save_seg(prog_idx, seg_idx, seg_buf,
                                BS300_VM_SEG_SIZE);
        if (ret < 0) {
            bs300_debug("bs300: vm_save_struct seg=%d fail\n", seg_idx);
            return -1;
        }
        offset += copy_len;
        remaining -= copy_len;
    }
    bs300_debug("bs300: vm_save_struct p=%d ok size=%d\n",
                prog_idx, (int)sizeof(bs300_prog_struct_t));
    return 0;
}
