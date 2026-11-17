import numpy as np
L = np.fromfile(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw", dtype=np.float32)[0::2]
R = np.fromfile(r"M:\ESDBox\dec_out.raw", dtype=np.float32)[0::2]
t = 14.2
i0 = int(t*44100); n = 4410
seg = L[i0:i0+n]
# 在整个参考里滑动找
best = (0, 1e9)
for j in range(0, len(R)-n, 64):
    e = np.mean(np.abs(R[j:j+n] - seg))
    if e < best[1]: best = (j, e)
print("dump@%.1fs best match in ref at %.3fs err=%.6f" % (t, best[0]/44100.0, best[1]))
# 再看断点前后 dump 与 ref 的关系
for t2 in [14.0, 14.05, 14.10, 14.12, 14.15, 14.2, 14.3]:
    i = int(t2*44100); nn=2205
    seg = L[i:i+nn]
    b=(0,1e9)
    for j in range(max(0,i-20000), min(len(R)-nn, i+20000), 8):
        e = np.mean(np.abs(R[j:j+nn]-seg))
        if e<b[1]: b=(j,e)
    print("  t=%.2f -> ref %.2f err %.5f" % (t2, b[0]/44100.0, b[1]))
