"""
Real cross-validation: compare generated I2C frames against chip ground truth.
Reads param_commands_{prog}.json (chip readback), extracts 48B data sections,
compares against our encoded output byte-by-byte.
"""
import json, os, sys

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PARENT_DIR)

from flash_read import parse_program_data
from flash_write import _extract_readback_data
from show_i2c_frames import (
    encode_full_sync, simulate_switch, load_calib, program_flash_to_dict,
    build_advanced_write,
)

def load_ground_truth(prog_index):
    """Load param_commands_N.json, return dict {cmd_word: data_48B}."""
    path = os.path.join(_PARENT_DIR, 'data', f'param_commands_{prog_index}.json')
    with open(path, 'r', encoding='utf-8') as f:
        entries = json.load(f)
    gt = {}
    for e in entries:
        cmd = int(e['cmd_word'], 16)
        raw = bytes(int(h, 16) for h in e['bytes'])
        # param_commands format: Addr(1) Len(1) Cmd(3) Data(48) Chk(1) = 54B
        if len(raw) >= 54:
            gt[cmd] = raw[5:53]  # skip Addr+Len+Cmd, take 48B Data
    return gt

def cmp_bytes(name, cmd_word, our_data, gt_data):
    """Compare our data vs ground truth. Returns (passed, msg)."""
    if len(our_data) != 48 or len(gt_data) != 48:
        return False, f"  ERR  {name} (0x{cmd_word:06X}): size mismatch our={len(our_data)} gt={len(gt_data)}"

    diffs = [(i, our_data[i], gt_data[i], our_data[i] - gt_data[i])
             for i in range(48) if our_data[i] != gt_data[i]]

    if not diffs:
        return True, f"  MATCH  {name:30s} (0x{cmd_word:06X})"

    md = max(abs(d[3]) for d in diffs)
    if md <= 1:
        return True, f"  TOL ±1 {name:30s} (0x{cmd_word:06X}): {len(diffs):2d}/48 bytes differ"

    # Show details for failures
    lines = [f"  FAIL   {name:30s} (0x{cmd_word:06X}): {len(diffs)}/48 bytes differ, max_diff={md}"]
    for i, ov, gv, d in diffs[:6]:
        lines.append(f"         byte[{i:2d}]: our=0x{ov:02X}({ov:4d})  gt=0x{gv:02X}({gv:4d})  diff={d:+d}")
    if len(diffs) > 6:
        lines.append(f"         ... and {len(diffs)-6} more")
    return False, "\n".join(lines)


def main():
    calib = load_calib()

    for prog_idx in [0, 1]:
        print(f"\n{'='*90}")
        print(f"PROGRAM {prog_idx} — Ground Truth Cross-Validation")
        print(f"{'='*90}")

        flash = _extract_readback_data(os.path.join(_PARENT_DIR, 'data', f'program_{prog_idx}.json'))
        prog = parse_program_data(flash)
        p = program_flash_to_dict(prog)

        inp = prog.inputs
        it = 'front_mic'
        if inp:
            it = inp.input_type

        print(f"  input_type: {it}, ch={p['wdrc']['total_channels']}, "
              f"kp={p['wdrc']['kp_mode']}KP, lim={'on' if p['wdrc']['limiter'] else 'off'}, "
              f"enr={'on' if p['enr']['enable_num_ch'] & 0x80 else 'off'}")

        # Generate our encoded commands
        our_cmds = encode_full_sync(p, calib, it)
        print(f"  Our encoded commands: {len(our_cmds)}")

        # Load ground truth
        gt = load_ground_truth(prog_idx)
        print(f"  Ground truth commands: {len(gt)}")

        results = []
        matched = 0
        tolerated = 0
        failed = 0
        not_in_gt = 0
        gt_not_sent = set(gt.keys())

        for name, cmd_word, our_data in our_cmds:
            # Build the full frame to show
            frame, chk = build_advanced_write(cmd_word, our_data)
            frame_hex = ' '.join(f'{b:02X}' for b in frame)

            if cmd_word in gt:
                gt_not_sent.discard(cmd_word)
                gt_data = gt[cmd_word]
                ok, msg = cmp_bytes(name, cmd_word, our_data, gt_data)
                results.append(ok)

                if 'MATCH' in msg:
                    matched += 1
                    print(msg)
                elif 'TOL' in msg:
                    tolerated += 1
                    print(msg)
                    # Show first few diffs for tolerated
                    diffs = [(i, our_data[i], gt_data[i])
                             for i in range(48) if our_data[i] != gt_data[i]]
                    for i, ov, gv in diffs[:3]:
                        print(f"         byte[{i:2d}]: our=0x{ov:02X} gt=0x{gv:02X}")
                else:
                    failed += 1
                    print(msg)
                    # Show full frame for failed commands
                    print(f"         OUR frame: {frame_hex}")
            else:
                not_in_gt += 1
                print(f"  NO_GT  {name:30s} (0x{cmd_word:06X}) — not in param_commands_{prog_idx}.json")
                print(f"         OUR frame: {frame_hex}")

        if gt_not_sent:
            print(f"\n  --- Ground truth entries we did NOT send ({len(gt_not_sent)}):")
            for cmd in sorted(gt_not_sent):
                print(f"       0x{cmd:06X}")

        total = matched + tolerated + failed
        print(f"\n  Program {prog_idx} Summary: MATCH={matched}, TOL±1={tolerated}, "
              f"FAIL={failed}, NO_GT={not_in_gt}")
        if failed == 0:
            print(f"  RESULT: PASS ✓")
        else:
            print(f"  RESULT: FAIL ✗ ({failed} commands have mismatches)")

    # ================================================================
    # Also validate the SWITCH scenario against param_commands_1
    # ================================================================
    print(f"\n{'='*90}")
    print(f"SWITCH: Program 0→1 — Compare switch commands against Program 1 ground truth")
    print(f"{'='*90}")

    flash0 = _extract_readback_data(os.path.join(_PARENT_DIR, 'data', 'program_0.json'))
    flash1 = _extract_readback_data(os.path.join(_PARENT_DIR, 'data', 'program_1.json'))
    prog0 = parse_program_data(flash0)
    prog1 = parse_program_data(flash1)
    p0 = program_flash_to_dict(prog0)
    p1 = program_flash_to_dict(prog1)

    def get_it(sel):
        if sel == 2: return 'telecoil'
        elif sel == 3: return 'dai'
        return 'front_mic'

    it0 = get_it(p0['modules']['input_selection'])
    it1 = get_it(p1['modules']['input_selection'])

    sent_cmds, disable_cmds = simulate_switch(p0, p1, it0, it1, calib)
    gt1 = load_ground_truth(1)

    sw_ok = 0; sw_tol = 0; sw_fail = 0
    for name, cmd_word, our_data in sent_cmds:
        frame, chk = build_advanced_write(cmd_word, our_data)
        frame_hex = ' '.join(f'{b:02X}' for b in frame)
        if cmd_word in gt1:
            gt_data = gt1[cmd_word]
            ok, msg = cmp_bytes(name, cmd_word, our_data, gt_data)
            if 'MATCH' in msg: sw_ok += 1
            elif 'TOL' in msg: sw_tol += 1
            else: sw_fail += 1
            print(msg)
            if 'FAIL' in msg:
                print(f"         OUR frame: {frame_hex}")
        else:
            print(f"  NO_GT  {name:30s} (0x{cmd_word:06X})")

    for name, cmd_word in disable_cmds:
        frame, _ = build_advanced_write(cmd_word, bytes(48))
        frame_hex = ' '.join(f'{b:02X}' for b in frame)
        print(f"  DISABLE {name:30s} (0x{cmd_word:06X})")
        print(f"         {frame_hex}")

    print(f"\n  Switch Summary: MATCH={sw_ok}, TOL±1={sw_tol}, FAIL={sw_fail}")
    if sw_fail == 0:
        print(f"  RESULT: PASS ✓")
    else:
        print(f"  RESULT: FAIL ✗")


if __name__ == '__main__':
    main()
