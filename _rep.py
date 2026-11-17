import array
a = array.array('f'); a.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
L = a[0::2]
def mad(i, j, n):
    e=0.0
    for k in range(0, n, 7):
        e += abs(L[i+k]-L[j+k])
    return e/(n/7)
# 断点附近的重复检查
for (seg_len) in [5760, 4608, 5184, 1764, 882]:
    i = int(14.26*44100)
    print("len %d: mad(dump[14.26s], dump[14.26s-%d]) = %.6f" % (seg_len, seg_len, mad(i, i-seg_len, 4000)))
# 检查 dump 与自身 5760 位移在断点前后
print("around break:")
for t in [14.0, 14.1, 14.2, 14.3]:
    i=int(t*44100)
    print("  t=%.1f mad(+5760)=%.6f" % (t, mad(i, i+5760, 3000)))
