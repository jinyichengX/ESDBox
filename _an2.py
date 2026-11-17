import array, math
p = r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw"
a = array.array('f'); a.frombytes(open(p,'rb').read())
L = a[0::2]
n = len(L)
W = 1024
BUF = 44100*3
quiet = []
for i in range(0, n-W, W):
    s = 0.0
    for x in L[i:i+W]: s += x*x
    if math.sqrt(s/W) < 0.0008:
        quiet.append(i/44100.0)
print("quiet count", len(quiet), "last", quiet[-3:] if quiet else None)
# 统计跳变
jumps = []
for i in range(1, n):
    d = abs(L[i]-L[i-1])
    if d > 0.25:
        jumps.append((i, d))
print("jumps>0.25:", len(jumps))
for i, d in jumps[:20]:
    print("  %.3fs d=%.3f frame%%buf=%d" % (i/44100.0, d, i % BUF))
# 检查音频是否只是静音段之外正常
print("sec", n/44100.0)
