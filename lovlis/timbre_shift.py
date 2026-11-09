"""音色变换：保持音高(F0)不变，只改变声道共振峰位置。

音色与音高是两个独立维度：
  - F0(基频) 决定"听到多高"，来自声带振动频率
  - 谱包络(共振峰) 决定"音色像谁"，来自声道形状（口腔、咽腔长度与开合）

同一个人把 F0 唱高唱低，音色不变；不同人 F0 相同，音色也可以完全不同——
差异就在谱包络上。所以这里不动 F0，只把包络沿频率轴缩放。

算法（STFT 域，逐帧处理）：
  1. 短时傅里叶变换，得到每帧的幅度谱和相位谱
  2. 倒谱平滑：把 log 幅度谱做 IFFT 得到倒谱，截掉高倒频（对应谐波细结构），
     反变换回来就是平滑的谱包络；原谱除以包络即得声门激励的细结构
  3. 包络沿频率轴缩放 alpha 倍（alpha>1 模拟声道变短，共振峰整体上移）
  4. 新幅度谱 = 缩放后的包络 × 原始细结构，相位保持原样
  5. 逆 STFT + 重叠相加重建

因为谐波位置(细结构)和相位都没动，F0 完全不变，只有共振峰搬家。
"""

from pathlib import Path

import numpy as np
import matplotlib
from scipy.io import wavfile

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import rcParams

from fft_spectrum import read_mono, amplitude_spectrum, rms_dbfs, MALE_F0_HZ

rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
rcParams["axes.unicode_minus"] = False

HERE = Path(__file__).resolve().parent
SRC = HERE / "录音.wav"
DST = HERE / "录音_音色变换.wav"
PNG = HERE / "spectrum_timbre.png"

N = 2048            # 帧长 42.7ms @48k，频率分辨率 23.4 Hz
HOP = 512           # 75% 重叠
ALPHA = 1.25        # 共振峰整体上移 25%，相当于声道缩短 20%（F1 375 -> 469 Hz）
LIFTER = 300        # 倒谱截断：包络平滑宽度 fs/LIFTER = 160 Hz
                    # 须大于 F0 间距(105 Hz)才能抹掉谐波、小于共振峰间距才能分辨共振峰
F_LO, F_HI = 100.0, 300.0   # 缩放权重从 0 渐变到 1 的过渡带
                            # 100 Hz 以下完全不缩放，保住基频和低频体感；300 Hz 以上全量缩放


def spectral_envelope(mag: np.ndarray) -> np.ndarray:
    """倒谱平滑求谱包络（保留声道传递函数，抹掉谐波梳状结构）。"""
    logm = np.log(mag + 1e-9)
    cep = np.fft.irfft(logm, N)
    cep[LIFTER:] = 0.0
    if LIFTER > 1:
        cep[1:LIFTER] *= 2.0        # 补回被截掉的负倒频对称分量
    return np.exp(np.fft.rfft(cep, N).real)


def formant_shift(x: np.ndarray, fs: int, alpha: float) -> np.ndarray:
    win = np.hanning(N)
    f = np.fft.rfftfreq(N, d=1.0 / fs)
    # 缩放权重：F_LO 以下为 0，F_HI 以上为 1，中间用升余弦平滑过渡，避免拼接处出现台阶
    w = np.clip(np.log2(np.maximum(f, 1.0) / F_LO) / np.log2(F_HI / F_LO), 0.0, 1.0)
    w = 0.5 - 0.5 * np.cos(np.pi * w)

    pad = N
    xp = np.concatenate([np.zeros(pad), x, np.zeros(pad)])
    y = np.zeros_like(xp)
    wsum = np.zeros_like(xp)

    for start in range(0, len(xp) - N, HOP):
        fr = xp[start:start + N] * win
        spec = np.fft.rfft(fr)

        mag = np.abs(spec)
        env = spectral_envelope(mag)
        detail = mag / (env + 1e-30)

        env_new = (1.0 - w) * env + w * np.interp(f / alpha, f, env)
        spec_new = env_new * detail * np.exp(1j * np.angle(spec))

        y[start:start + N] += np.fft.irfft(spec_new, N) * win
        wsum[start:start + N] += win * win

    y = y / np.maximum(wsum, 1e-9)
    return y[pad:pad + len(x)]


def f0_median(x: np.ndarray, fs: int) -> float:
    """自相关法估计基频中位数，用于验证音高没变。"""
    n, hop, out = 2048, 512, []
    for i in range(0, len(x) - n, hop):
        fr = x[i:i + n]
        if np.sqrt(np.mean(fr * fr)) < 0.01:
            continue
        fr = fr - fr.mean()
        ac = np.correlate(fr, fr, "full")[n - 1:]
        lo, hi = int(fs / 400), int(fs / 60)
        seg = ac[lo:hi]
        if seg.size:
            out.append(fs / (lo + int(np.argmax(seg))))
    return float(np.median(out)) if out else 0.0


def harmonic_envelope(sig: np.ndarray, fs: int, f0: float, kmax: int = 40):
    """逐个谐波取幅度，得到"谐波包络"。

    为什么不用 1/3 倍频程平滑：F0=105 Hz 时谐波间隔 105 Hz，
    1/3 倍频程在 375 Hz 处带宽已有 ~97 Hz，平滑会把 h3/h4 抹成一块，
    共振峰搬了也不显示。逐谐波取点不受平滑带宽影响，能精确看出包络移动。
    """
    f, db = amplitude_spectrum(sig, fs)
    ks = np.arange(1, kmax + 1)
    amps = np.array([db[(f > k * f0 - 18) & (f < k * f0 + 18)].max() for k in ks])
    return ks * f0, amps


def low_band_peak(freq: np.ndarray, amps: np.ndarray, lo: float = 250.0,
                  hi: float = 800.0) -> float:
    """谐波包络在 lo~hi 区间内的峰值频率，用来量化 F1 的位置。"""
    sel = (freq > lo) & (freq < hi)
    return float(freq[sel][np.argmax(amps[sel])]) if sel.any() else 0.0


def main() -> None:
    x, fs = read_mono(SRC)
    y = formant_shift(x, fs, ALPHA)

    peak = np.max(np.abs(y))
    gain = min(1.0, 0.99 / peak) if peak > 0 else 1.0
    y = y * gain                      # 峰值归一化，防止写 int16 时削顶
    y16 = np.int16(np.clip(y, -1.0, 1.0) * 32767)
    wavfile.write(str(DST), fs, y16)

    print(f"输入 : {SRC.name}  F0中位 {f0_median(x, fs):.1f} Hz  RMS {rms_dbfs(x):.1f} dBFS")
    print(f"输出 : {DST.name}  F0中位 {f0_median(y, fs):.1f} Hz  RMS {rms_dbfs(y):.1f} dBFS")
    print(f"共振峰缩放 alpha = {ALPHA}（300 Hz 以上整体上移 {(ALPHA - 1) * 100:.0f}%）")

    f0 = f0_median(x, fs)
    hf0, ha0 = harmonic_envelope(x, fs, f0)
    hf1, ha1 = harmonic_envelope(y, fs, f0)
    p0 = low_band_peak(hf0, ha0)
    p1 = low_band_peak(hf1, ha1)
    print(f"F1(谐波包络峰) 原始 {p0:.0f} Hz -> 变换后 {p1:.0f} Hz，比值 {p1 / p0:.2f}")
    print(f"已保存: {DST}")

    fig, axes = plt.subplots(3, 1, figsize=(12, 11))

    for ax, sig, name in ((axes[0], x, "原始 " + SRC.name),
                          (axes[1], y, "变换后 " + DST.name)):
        freq, mag_db = amplitude_spectrum(sig, fs)
        ax.semilogx(freq[1:], mag_db[1:], linewidth=0.8, color="#2b6cb0", label="幅度谱")
        ax.axvspan(*MALE_F0_HZ, color="#f6ad55", alpha=0.18, lw=0, zorder=0,
                   label=f"男声基频 {MALE_F0_HZ[0]:.0f}-{MALE_F0_HZ[1]:.0f} Hz")
        ax.set_xlim(20, fs / 2)
        ax.set_ylim(-110, 5)
        ax.set_title(f"{name}    F0 {f0_median(sig, fs):.1f} Hz / RMS {rms_dbfs(sig):.1f} dBFS")
        ax.set_xlabel("频率 (Hz)")
        ax.set_ylabel("幅度 (dBFS)")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(loc="upper right", fontsize=9, framealpha=0.9)

    ax = axes[2]
    ax.semilogx(hf0, ha0, "o-", color="#4a5568", linewidth=1.2, ms=4,
                label=f"原始  F1={p0:.0f} Hz")
    ax.semilogx(hf1, ha1, "s-", color="#c53030", linewidth=1.2, ms=4,
                label=f"变换后 F1={p1:.0f} Hz")
    ax.axvline(p0, color="#4a5568", linestyle=":", linewidth=1)
    ax.axvline(p1, color="#c53030", linestyle=":", linewidth=1)
    ax.annotate("", xy=(p1, -34), xytext=(p0, -34),
                arrowprops=dict(arrowstyle="->", color="#dd6b20", lw=1.5))
    ax.text((p0 + p1) / 2, -32, f"F1 上移 {(p1 / p0 - 1) * 100:.0f}%",
            color="#dd6b20", ha="center", fontsize=9)
    ax.set_xlim(80, 4200)
    ax.set_ylim(-110, -10)
    ax.set_title("谐波幅度包络对比：包络峰（共振峰）整体右移，且谐波间距(F0)不变")
    ax.set_xlabel("频率 (Hz)")
    ax.set_ylabel("谐波幅度 (dBFS)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="lower left", fontsize=9, framealpha=0.9)

    fig.suptitle(f"音色变换前后频谱对比（共振峰上移 {(ALPHA - 1) * 100:.0f}%，音高不变）", fontsize=13)
    fig.tight_layout()
    fig.savefig(PNG, dpi=150)
    print(f"已保存: {PNG}")


if __name__ == "__main__":
    main()
