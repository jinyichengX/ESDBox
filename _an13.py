import array
d = array.array('f'); d.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
dL = d[0::2]
r = array.array('f'); r.frombytes(open(r"M:\ESDBox\ref2.raw",'rb').read())
rL = r[0::2]

def err_at(t, off, span=0.03):
    i0=int(t*44100); n=int(span*44100)
    e=0.0; c=0
    for i in range(i0, i0+n, 5):
        j=i+off
        if j<0 or j>=len(dL) or i>=len(rL): return 9.9
        e += abs(dL[j]-rL[i]); c+=1
    return e/max(c,1)

for t in [12, 13, 14, 14.5, 15, 16, 17, 18, 20]:
    best=(0,9.9)
    for off in range(-12000, 12001, 4):
        e=err_at(t, off)
        if e<best[1]: best=(off,e)
    print("t=%.1f best_off=%d err=%.5f" % (t, best[0], best[1]))
