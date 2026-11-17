import array
d = array.array('f'); d.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
dL = d[0::2]
r = array.array('f'); r.frombytes(open(r"M:\ESDBox\ref2.raw",'rb').read())
rL = r[0::2]

def err(t, off, span=0.01):
    i0=int(t*44100); n=int(span*44100)
    e=0.0
    for i in range(i0, i0+n, 5):
        if i+off < 0 or i+off >= len(dL) or i >= len(rL): return None
        e += abs(dL[i+off]-rL[i])
    return e/(n/5)

# 逐 0.25 秒扫描偏移，看误差何时变大
for t in [10, 10.5, 11, 11.5, 12, 12.5, 13, 14, 15]:
    print("t=%.1f err0=%.5f" % (t, err(t, 0)))
