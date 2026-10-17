"""Find kick onsets in a raw float32 stereo render and report the beat grid and gaps.

usage: python3 kicks.py file.f32 [low_hz]
"""
import sys
import numpy as np

sr = 44100
x = np.fromfile(sys.argv[1], dtype=np.float32).reshape(-1, 2).mean(axis=1)
low = float(sys.argv[2]) if len(sys.argv) > 2 else 150.0

# One-pole low-pass twice, then a 5 ms RMS envelope.
a = np.exp(-2 * np.pi * low / sr)
def lp(s):
    y = np.empty_like(s)
    acc = 0.0
    for i in range(0, len(s), 4096):
        seg = s[i:i + 4096]
        out = np.empty_like(seg)
        for j, v in enumerate(seg):
            acc = a * acc + (1 - a) * v
            out[j] = acc
        y[i:i + 4096] = out
    return y
# fast vectorised alternative via scipy if present
try:
    from scipy.signal import lfilter
    y = lfilter([1 - a], [1, -a], lfilter([1 - a], [1, -a], x))
except Exception:
    y = lp(lp(x))

hop = int(sr * 0.005)
n = len(y) // hop
env = np.sqrt((y[:n * hop].reshape(n, hop) ** 2).mean(axis=1))
peak = env.max()
print(f"frames {len(x)} ({len(x)/sr:.1f}s)  low-band peak {peak:.4g}  full-band rms {np.sqrt((x**2).mean()):.4g}")
if peak <= 0:
    sys.exit(0)

# Onsets: envelope rises above 30% of peak after being below 10% (hysteresis), min spacing 80 ms.
on = []
armed = True
for i, v in enumerate(env):
    if armed and v > 0.3 * peak:
        if not on or (i - on[-1]) * 0.005 > 0.08:
            on.append(i)
        armed = False
    elif v < 0.1 * peak:
        armed = True
t = np.array(on) * 0.005
print(f"onsets {len(t)}")
if len(t) < 3:
    sys.exit(0)
d = np.diff(t)
beat = np.median(d)
print(f"median interval {beat*1000:.1f} ms  (~{60/beat:.1f} BPM if quarter notes)")
gaps = [(t[i], d[i] / beat) for i in range(len(d)) if d[i] > 1.5 * beat]
missing = sum(int(round(g)) - 1 for _, g in gaps)
print(f"expected beats in span {int(round((t[-1]-t[0])/beat))+1}, found {len(t)}, missing ~{missing}")
for when, ratio in gaps[:40]:
    print(f"  gap at {when:7.3f}s  = {ratio:.2f} beats")
# Per-onset peak level, to spot weak (partially cut) hits.
lv = [env[i:i + 20].max() / peak for i in on]
weak = [(on[k] * 0.005, lv[k]) for k in range(len(on)) if lv[k] < 0.6]
print(f"weak hits (<60% of max): {len(weak)}")
for when, l in weak[:20]:
    print(f"  weak at {when:7.3f}s level {l:.2f}")
