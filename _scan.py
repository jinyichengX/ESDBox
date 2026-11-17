import numpy as np
L = np.fromfile(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw", dtype=np.float32)[0::2]
R = np.fromfile(r"M:\ESDBox\dec_out.raw", dtype=np.float32)[0::2]
print("dump", len(L)/44100, "s, ref", len(R)/44100, "s")

def best_off(t, span=0.05, lo=-12000, hi=12000, step=4):
    i0 = int(t*44100); n = int(span*44100)
    seg = R[i0:i0+n]
    best = (0, 1e9)
    for off in range(lo, hi+1, step):
        j = i0+off
        if j < 0 or j+n > len(L): continue
        e = np.mean(np.abs(L[j:j+n] - seg))
        if e < best[1]: best = (off, e)
    return best

for t in [14.0, 14.2, 14.4, 14.6, 15.0, 15.4, 16.0, 16.5, 17.0, 18.0, 20.0, 25.0, 30.0]:
    off, e = best_off(t)
    print("t=%5.1f  off=%7d  err=%.6f" % (t, off, e))
