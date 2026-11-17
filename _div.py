import array
d = array.array('f'); d.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
L = d[0::2]
r = array.array('f'); r.frombytes(open(r"M:\ESDBox\dec_out.raw",'rb').read())
R = r[0::2]
n = min(len(L), len(R), 30*44100)
# 找到第一处不同
first = None
for i in range(0, n):
    if abs(L[i]-R[i]) > 1e-4:
        first = i
        break
print("first divergence frame", first, "=%.4fs" % (first/44100.0) if first else "")
if first:
    for delta in [0, -5760, 5760, -4608, 4608, -1764, 1764, -3528, 3528]:
        e=0.0;c=0
        for k in range(first+2000, first+6000, 7):
            j=first+k+delta
            if 0<=j<len(L) and first+k<len(R):
                e+=abs(L[j]-R[first+k]); c+=1
        print("delta %6d -> mad %.6f" % (delta, e/max(c,1)))
