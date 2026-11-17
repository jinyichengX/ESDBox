import array, math
dump = array.array('f'); dump.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
dL = dump[0::2]
ref = array.array('f'); ref.frombytes(open(r"M:\ESDBox\ref.raw",'rb').read())
n = min(len(dL), len(ref))
print("dump", len(dL), "ref", len(ref))

# 找对齐：用前 5 秒里能量最大的窗口做互相关
def rms(a, i, w=2048):
    s = 0.0
    for x in a[i:i+w]: s += x*x
    return math.sqrt(s/w)

# 粗对齐：找 dump 里第一个 rms>0.01 的位置，和 ref 里对应位置
def first_loud(a):
    for i in range(0, len(a)-2048, 512):
        if rms(a, i) > 0.02: return i
    return -1
print("dump first loud", first_loud(dL)/44100.0, "ref first loud", first_loud(ref)/44100.0)
