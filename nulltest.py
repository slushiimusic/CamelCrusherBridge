#!/usr/bin/env python3
"""Compare two raw float32 renders: original (Intel, direct) vs bridge (native)."""
import sys, struct, math

def load(p):
    with open(p, 'rb') as f:
        d = f.read()
    n = len(d) // 4
    return struct.unpack('<%df' % n, d[:n*4])

a = load(sys.argv[1])
b = load(sys.argv[2])
label = sys.argv[3] if len(sys.argv) > 3 else ''

if len(a) != len(b):
    print(f"  {label}: LENGTH MISMATCH {len(a)} vs {len(b)}")
    sys.exit(1)

maxdiff = 0.0
sumsq_d = 0.0
sumsq_a = 0.0
nonfinite = 0
first_bad = None
exact = 0

for i, (x, y) in enumerate(zip(a, b)):
    if not (math.isfinite(x) and math.isfinite(y)):
        nonfinite += 1
        continue
    d = abs(x - y)
    if d == 0.0:
        exact += 1
    elif first_bad is None:
        first_bad = (i, x, y, d)
    if d > maxdiff:
        maxdiff = d
    sumsq_d += d * d
    sumsq_a += x * x

n = len(a)
rms_d = math.sqrt(sumsq_d / n)
rms_a = math.sqrt(sumsq_a / n)
null_db = (20 * math.log10(rms_d / rms_a)) if rms_d > 0 and rms_a > 0 else float('-inf')

print(f"  {label}")
print(f"    samples          : {n}")
print(f"    bit-identical    : {exact}/{n}  ({100.0*exact/n:.4f}%)")
print(f"    max |difference| : {maxdiff:.3e}")
print(f"    signal RMS       : {rms_a:.6f}")
print(f"    residual RMS     : {rms_d:.3e}")
print(f"    null depth       : {'-inf dB (perfect null)' if null_db == float('-inf') else f'{null_db:.1f} dB'}")
if nonfinite:
    print(f"    NON-FINITE       : {nonfinite}")
if first_bad:
    i, x, y, d = first_bad
    print(f"    first difference : idx {i}  orig={x:.9f}  bridge={y:.9f}  d={d:.3e}")
print(f"    VERDICT          : {'IDENTICAL' if maxdiff == 0.0 else 'DIFFERS'}")
