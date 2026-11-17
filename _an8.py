import array
dump = array.array('f'); dump.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
mono = [ (dump[i*2]+dump[i*2+1])*0.5 for i in range(10*44100, 40*44100) ]
ref = array.array('f'); ref.frombytes(open(r"M:\ESDBox\ref.raw",'rb').read())
refseg = ref[10*44100:40*44100]
def stats(a, name):
    j25 = 0; mx = 0
    for i in range(1, len(a)):
        d = abs(a[i]-a[i-1])
        if d > 0.25: j25 += 1
        if d > mx: mx = d
    print(name, "jumps>0.25:", j25, "max", round(mx,3))
stats(mono, "dump mono")
stats(refseg, "ref  mono")
