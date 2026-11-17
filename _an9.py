import array, math
d = array.array('f'); d.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
dL = d[0::2]
r = array.array('f'); r.frombytes(open(r"M:\ESDBox\ref2.raw",'rb').read())
rL = r[0::2]
# 用 5-8 秒做对齐
best=None
for off in range(0, 30000, 1):
    if off+8*44100 >= len(dL): break
    e = 0.0; c=0
    for i in range(5*44100, 5*44100+30000, 41):
        e += abs(dL[i+off]-rL[i]); c+=1
    e/=c
    if best is None or e<best[1]: best=(off,e)
off=best[0]
print("offset", off, "mean abs err", round(best[1],5))
# 对齐后 10-40 秒的误差
seg_start, seg_end = 10*44100, 40*44100
e=0.0;c=0;mx=0
for i in range(seg_start, seg_end, 13):
    v=abs(dL[i+off]-rL[i]); e+=v;c+=1
    if v>mx: mx=v
print("10-40s mean err %.5f max %.4f" % (e/c, mx))
# 逐秒误差
for s in range(10, 40, 5):
    i0=s*44100; i1=i0+44100
    e=0.0;c=0
    for i in range(i0,i1,17):
        e+=abs(dL[i+off]-rL[i]); c+=1
    print("sec %d: mean err %.5f" % (s, e/c))
