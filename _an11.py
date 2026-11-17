import array
d = array.array('f'); d.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
dL = d[0::2]
r = array.array('f'); r.frombytes(open(r"M:\ESDBox\ref2.raw",'rb').read())
rL = r[0::2]

def err_at(t, off, span=0.002):
    i0=int(t*44100); n=int(span*44100)
    e=0.0; c=0
    for i in range(i0, i0+n, 3):
        j=i+off
        if j<0 or j>=len(dL) or i>=len(rL): return 9.9
        e += abs(dL[j]-rL[i]); c+=1
    return e/max(c,1)

# 1) 细扫 14.0-15.0 找断点（off=0）
prev=0.0
for k in range(0, 21):
    t=14.0+k*0.05
    e=err_at(t,0)
    print("t=%.2f err=%.5f" % (t,e))
