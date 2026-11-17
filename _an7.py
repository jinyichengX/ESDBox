import array
dump = array.array('f'); dump.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
L = dump[0::2]
start = 10*44100; end = 40*44100
seg = L[start:end]
zeros = sum(1 for x in seg if x == 0.0)
print("samples", len(seg), "zeros", zeros, "ratio %.3f" % (zeros/len(seg)))
# 最长连续零
mx = 0; cur = 0; pos = 0; mxpos = 0
for i, x in enumerate(seg):
    if x == 0.0:
        cur += 1
        if cur > mx: mx = cur; mxpos = i
    else:
        cur = 0
print("longest zero run", mx, "samples = %.1f ms at %.2fs" % (mx/44.1, (start+mxpos)/44100.0))
# 检查靠近零的小值（静音填充）
small = sum(1 for x in seg if abs(x) < 1e-6)
print("|x|<1e-6 ratio %.3f" % (small/len(seg)))
