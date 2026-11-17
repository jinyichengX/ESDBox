import array, math
dump = array.array('f'); dump.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
d = dump[0::2]
ref = array.array('f'); ref.frombytes(open(r"M:\ESDBox\ref.raw",'rb').read())

# 在 9730 附近做精确对齐
best = None
base = 9730
for off in range(base-3000, base+3001, 1):
    s = 0.0
    for i in range(44100*3, 44100*3+20000, 7):
        s += abs(d[i+off] - ref[i])
    if best is None or s < best[1]:
        best = (off, s)
off = best[0]
print("best offset", off, "score", best[1])

# 逐段比较
W = 4410  # 100ms
n = min(len(d)-off, len(ref))
bad = []
for i in range(0, n-W, W):
    e = 0.0
    for k in range(i, i+W, 3):
        e += abs(d[k+off] - ref[k])
    e /= (W/3)
    if e > 0.01:
        bad.append((i/44100.0, e))
print("bad 100ms blocks:", len(bad))
for t, e in bad[:15]: print("  %.2fs err=%.4f" % (t, e))
print("total compared", n/44100.0, "s")
