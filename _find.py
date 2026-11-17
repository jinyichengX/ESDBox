import array
d = array.array('f'); d.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
L = d[0::2]
base = int(14.15*44100)
n = 4000
best = []
for j in range(0, len(L)-n, 1):
    if abs(j-base) < 800: continue
    e=0.0;c=0
    for k in range(0, n, 9):
        e += abs(L[base+k]-L[j+k]); c+=1
    e/=c
    if e < 0.01: best.append((j, e))
print("matches for window@14.15s:", len(best))
for j,e in best[:10]: print("  at %.3fs err=%.5f" % (j/44100.0, e))
# 也看看窗口内到底是什么（前 20 个样本）
print("samples", [round(L[base+i],3) for i in range(0, 20)])
