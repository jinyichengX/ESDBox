import array
d = array.array('f'); d.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
dL = d[0::2]
r = array.array('f'); r.frombytes(open(r"M:\ESDBox\ref2.raw",'rb').read())
rL = r[0::2]

def err_at(t, off, span=0.05):
    i0=int(t*44100); n=int(span*44100)
    e=0.0; c=0
    for i in range(i0, i0+n, 7):
        j=i+off
        if j<0 or j>=len(dL) or i>=len(rL): return 9.9
        e += abs(dL[j]-rL[i]); c+=1
    return e/max(c,1)

best=None
for off in range(-20000, 20001, 1):
    if off % 1: continue
    e = err_at(15.0, off)
    if e < 0.02:
        best=(off,e); break
print("lag at 15s:", best)

# 再验证 20s / 30s 是否同样的 lag
if best:
    off=best[0]
    for t in [16, 20, 25, 30, 35]:
        print("t=%d err(off=%d)=%.5f" % (t, off, err_at(t, off)))
