import struct, array, math
p = r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw"
data = open(p,'rb').read()
n = len(data)//4
a = array.array('f'); a.frombytes(data[:n*4])
L = a[0::2]; R = a[1::2]
frames = len(L)
print("frames", frames, "sec", frames/44100.0)
# 每 1024 帧算 RMS，找静音洞
W = 1024
gaps = []
prevd = None
maxd = 0
maxdi = 0
prev = None
for i in range(0, frames-W, W):
    s = 0.0
    for x in L[i:i+W]:
        s += x*x
    rms = math.sqrt(s/W)
    if rms < 0.0008:
        gaps.append((i/44100.0, rms))
print("quiet windows(<0.0008):", len(gaps))
for g in gaps[:10]: print("  at %.3fs rms=%.6f" % g)
# 采样点跳变
md = 0; mi = 0
for i in range(1, frames):
    d = abs(L[i]-L[i-1])
    if d > md: md = d; mi = i
print("max sample jump %.4f at %.3fs" % (md, mi/44100.0))
# 检查是否存在整段重复（环形缓冲错位）
print("peak", max(abs(x) for x in L[:200000]))
