import array
d = array.array('f'); d.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
L = d[0::2]
def mad(i, j, n=8000):
    e=0.0
    for k in range(0, n, 11):
        e += abs(L[i+k]-L[j+k])
    return e/(n/11)
base = int(14.5*44100)
hits=[]
for j in range(max(0, base-22050*6), min(len(L)-8000, base+22050*6), 1):
    if abs(j-base) < 2000: continue
    v = mad(base, j)
    if v < 1e-5: hits.append((j, j/44100.0, v))
print("exact-ish matches for window@14.5s:", len(hits))
for h in hits[:12]: print("  at %.2fs (delta %d frames)" % (h[1], h[0]-base))
