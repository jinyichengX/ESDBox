import numpy as np
L = np.fromfile(r"M:\ESDBox\ESDBox_IPGUI\apps\spectrum\output_dump.raw", dtype=np.float32)[0::2]
R = np.fromfile(r"M:\ESDBox\dec_out.raw", dtype=np.float32)[0::2]
n = min(len(L), len(R), 44*44100)
diff = np.abs(L[:n] - R[:n])
bad = np.where(diff > 1e-4)[0]
print("compared %.1fs, first mismatch:" % (n/44100.0), (bad[0]/44100.0 if len(bad) else None))
print("samples over threshold:", len(bad))
print("mean abs diff:", float(np.mean(diff)), "max", float(np.max(diff)))
