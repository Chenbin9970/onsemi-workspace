#!/usr/bin/env python3
"""
Find all valid (start, cnt) band ranges per channel that produce the correct NT*10.
A range is valid if: round(5.307*(185 - mic1Cal) - 371.2) == chip_nt10
where mic1Cal = floor(sum(bands[start:start+cnt]) * 10 / cnt) / 10
"""
import math

chip_nt10 = [
    -185, -185, -185, -196, -175, -180, -201, -196,
    -180, -175, -175, -185, -148, -154, -148, -148,
]

mic1_cal = [150, 150, 151, 152, 147, 152, 147, 150, 147, 148, 147, 157,
            149, 156, 147, 149, 148, 150, 149, 150, 151, 145, 147, 141,
            143, 143, 143, 143, 143, 143, 143, 143]

freqs = ['0', '0', '375', '875', '1375', '1875', '2375', '2875',
         '3375', '3875', '4375', '4875', '5375', '5875', '6375', '6875']

# Precompute all possible mic1Cal values from contiguous ranges
all_ranges = {}  # mic1Cal -> list of (start, cnt, avg_exact)
for start in range(32):
    for cnt in range(1, 9):  # max 8 bands per channel
        end = start + cnt - 1
        if end >= 32:
            break
        s = sum(mic1_cal[start:end+1])
        avg = math.floor(s * 10 / cnt) / 10
        if avg not in all_ranges:
            all_ranges[avg] = []
        all_ranges[avg].append((start, cnt, round(s/cnt, 2)))

def check_m1cal(m1, target_nt):
    """Check if this mic1Cal produces the target NT*10"""
    result = round(5.307 * (185.0 - m1) - 371.2)
    return result == target_nt

# For each channel, find all valid mic1Cal values and their band ranges
print("=" * 100)
print("Per-Channel Valid Band Ranges (monotonically usable)")
print("=" * 100)

for i in range(16):
    nt = chip_nt10[i]
    freq = freqs[i]
    print(f"\n--- ch{i} ({freq} Hz) NT*10={nt:+d} ---")

    valid = []
    for m1, ranges in sorted(all_ranges.items()):
        if check_m1cal(m1, nt):
            for start, cnt, exact in ranges:
                # Only show reasonable ranges (not too many bands for high freq)
                valid.append((m1, start, cnt, exact))

    # Deduplicate by mic1Cal
    seen = set()
    for m1, start, cnt, exact in valid:
        key = (m1, start, cnt)
        if key not in seen:
            seen.add(key)
            print(f"  m1Cal={m1:6.1f}  bands[{start:>2}:{start+cnt-1:<2}] cnt={cnt}  avg_exact={exact}  vals={mic1_cal[start:start+cnt]}")

# Now try to find a monotonic mapping (frequency increases, band index increases)
print("\n" + "=" * 100)
print("Monotonic Mapping Candidates")
print("=" * 100)
print("Assumption: higher frequency → higher band start index")
print()

# For each channel, get the set of valid (start, cnt) params
chan_valid = []
for i in range(16):
    nt = chip_nt10[i]
    valid = set()
    for m1, ranges in sorted(all_ranges.items()):
        if check_m1cal(m1, nt):
            for start, cnt, exact in ranges:
                # Only reasonable cnt for bandwidth
                valid.add((m1, start, cnt))
    chan_valid.append(valid)

# Try to find a sequence where start indices are non-decreasing
def find_monotonic(chan_valid, min_start=0):
    """DFS to find monotonic mapping. Returns list of (m1, start, cnt) or None."""
    result = [None] * 16

    def dfs(ch, prev_start):
        if ch == 16:
            return True
        # Sort valid ranges by start index
        options = sorted(chan_valid[ch], key=lambda x: (x[1], x[2]))
        for m1, start, cnt in options:
            if start < prev_start - 2:  # allow slight overlap
                continue
            result[ch] = (m1, start, cnt)
            if dfs(ch + 1, start):
                return True
            result[ch] = None
        return False

    if dfs(0, 0):
        return result
    return None

# Also try from the back (higher freq must use higher bands)
# For now, just print all valid starts per channel
print(f"{'Ch':<5} {'Freq':<8} {'NT*10':<8} {'Valid start indices (cnt)'}")
print("-" * 70)
for i in range(16):
    nt = chip_nt10[i]
    freq = freqs[i]
    starts = sorted(set(s for _, s, _ in chan_valid[i] if s < 32))
    if len(starts) > 12:
        starts_str = f"[{min(starts)}..{max(starts)}] ({len(starts)} options)"
    else:
        starts_str = ', '.join(str(s) for s in starts)
    print(f"ch{i:<3} {freq:<8} {nt:+<8} {starts_str}")
