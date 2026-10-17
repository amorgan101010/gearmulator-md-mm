import re, sys
# Usage: steady_state_split.py DIR  (DIR holds mm5.txt mm15.txt md5.txt md15.txt: the hostsplit/hostsite
# lines printed by a bench built with host-split-instrumentation.patch). TSC is this machine's rate.
S = sys.argv[1].rstrip("/") + "/"
TSC = 3.919e9

def parse(fn):
    t = open(S + fn).read()
    tot = float(re.search(r"totalTsc=([\d.e+]+)", t).group(1))
    split = [float(x) for x in re.search(r"mixerDSP=([\d.]+)% producerDSP=([\d.]+)% uc=([\d.]+)%", t).groups()]
    sites = {}
    for m in re.finditer(r"hostsite: (\w+) dsp(\d) tsc=([\d.]+)% calls=(\d+)", t):
        sites[(m.group(1), int(m.group(2)))] = (float(m.group(3)) / 100 * tot, int(m.group(4)))
    uc = int(re.search(r"uc calls=(\d+)", t).group(1))
    h = re.search(r"hash=(\w+)", t).group(1)
    return tot, [s / 100 * tot for s in split], sites, uc, h

for mach in ("mm", "md"):
    a, b = parse(mach + "5.txt"), parse(mach + "15.txt")
    dt = b[0] - a[0]
    print(f"== {mach.upper()} steady state (15s run minus 5s run = 10 s emulated); hashes {a[4]} {b[4]}")
    print(f"  boot+5s host = {a[0]/TSC:.1f}s, extra 10s emulated costs {dt/TSC:.2f}s host -> {10/(dt/TSC):.2f}x realtime (instrumented)")
    names = ["mixerDSP", "producerDSP", "68K"]
    print("  split: " + "  ".join(f"{names[i]}={100*(b[1][i]-a[1][i])/dt:.1f}%" for i in range(3)))
    link = ucc = 0
    for k in sorted(b[2]):
        dts = b[2][k][0] - a[2][k][0]; dc = b[2][k][1] - a[2][k][1]
        print(f"  {k[0]:9s} dsp{k[1]}: {100*dts/dt:5.1f}%  calls/emulated-s={dc/10:,.0f}")
        if k[0] == "linkCatch": link += dc
        if k[0] == "ucCatch": ucc += dc
    hostns = dt / TSC * 1e9
    print(f"  link syncs/s={link/10:,.0f} uc->dsp syncs/s={ucc/10:,.0f}  -> one sync per {hostns/(link+ucc):.0f} ns host,"
          f" {101.6e6*10/(link+ucc):.0f} DSP cycles emulated")
    print(f"  boot share of the 5s run's host time: {100*(a[0]-dt/2)/a[0]:.0f}%")
