import array, math
dump = array.array('f'); dump.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
d = dump[0::2]
ref = array.array('f'); ref.frombytes(open(r"M:\ESDBox\ref.raw",'rb').read())
STEP = 441  # 10ms
def env(a):
    out = []
    for i in range(0, len(a)-STEP, STEP):
        s = 0.0
        for x in a[i:i+STEP]: s += x*x
        out.append(math.sqrt(s/STEP))
    return out
ed, er = env(d), env(ref)
print("env len", len(ed), len(er))
# 初始偏移：在 0-5 秒找最优 lag
def best_lag(t_sec, span=1.0, maxlag=500):
    i0 = int(t_sec*100)
    best = (0, 1e9)
    for lag in range(-maxlag, maxlag+1):
        if i0+lag < 0 or i0+lag+int(span*100) >= len(ed): continue
        e = 0.0
        for k in range(int(span*100)):
            e += abs(ed[i0+lag+k] - er[i0+k])
        if e < best[1]: best = (lag, e)
    return best
for t in [2, 5, 10, 15, 20, 25, 30, 35, 40]:
    lag, e = best_lag(t)
    print("t=%2ds lag=%4d samples (%.1f ms) err=%.2f" % (t, lag, lag*10, e))
