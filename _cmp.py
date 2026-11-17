import array
a = array.array('f'); a.frombytes(open(r"M:\ESDBox\dec_out.raw",'rb').read())
b = array.array('f'); b.frombytes(open(r"M:\ESDBox\ref2.raw",'rb').read())
n = min(len(a), len(b))
print("len min", n//2//44100.0, "s")
# 分段比较，找第一处不一致
step = 4410*2*2  # 0.2s
for s in range(0, n-step, step):
    e = 0.0
    for i in range(s, s+step, 37):
        e += abs(a[i]-b[i])
    e /= (step/37)
    if e > 0.0005:
        print("first mismatch at %.2fs err=%.5f" % (s/2/44100.0, e))
        break
else:
    print("identical up to %.2fs" % (n/2/44100.0))
