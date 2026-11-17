import array, math
dump = array.array('f'); dump.frombytes(open(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw",'rb').read())
d = dump[0::2]
ref = array.array('f'); ref.frombytes(open(r"M:\ESDBox\ref.raw",'rb').read())
def rms_per_sec(a, n=40):
    out=[]
    for s in range(n):
        i=s*44100; j=min(i+44100, len(a))
        if j<=i: break
        acc=0.0
        for x in a[i:j]: acc+=x*x
        out.append(math.sqrt(acc/(j-i)))
    return out
rd = rms_per_sec(d); rr = rms_per_sec(ref)
print("sec | dump  | ref")
for i in range(len(rr)):
    print("%3d | %.4f | %.4f" % (i, rd[i], rr[i]))
