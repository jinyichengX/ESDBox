"""音高提升（整体等比例上移）：所有频率分量乘同一个系数，频谱形状原封不动。

与上一版的区别：
  上一版：谐波搬走，谱包络(共振峰)留在原地 → 音高变、音色不变
  这一版：谐波和包络一起乘 ratio → 整个频谱像照片一样等比放大，相对幅度关系不动
         代价是共振峰也跟着上移，听起来会连带"变亮/变小"，这是"整体上移"的物理含义

算法（相位声码器，标准 smbPitchShift 思路）：
  1. 逐帧求幅度谱和瞬时频率 ω（相位差分 + 解卷绕）
  2. 把第 k 个频点的**原始幅度**搬到 round(k * ratio)，不做任何包络补偿
  3. 目的频点的相位 = 上一帧该频点的合成相位 + ω * ratio * hop
  4. 逆变换 + 重叠相加，分析/合成 hop 相同，时长不变

F0 从 104.3 Hz 提到约 118 Hz（ratio = 17/15），仍在成年男声 85-180 Hz 内。
"""

from pathlib import Path
import os

# 本机没有页面文件，BLAS 多线程会预留大量提交内存，导致 numpy 分配 1MB 都失败
os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")

import numpy as np
import matplotlib
from scipy.io import wavfile

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import rcParams

from fft_spectrum import read_mono, amplitude_spectrum, rms_dbfs, MALE_F0_HZ
from timbre_shift import f0_median

rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
rcParams["axes.unicode_minus"] = False

HERE = Path(__file__).resolve().parent
SRC = HERE / "录音.wav"
DST = HERE / "录音_音高提升.wav"
PNG = HERE / "spectrum_pitch.png"

N = 2048
HOP = 512
RATIO = 17 / 15     # 1.133，F0 104.3 -> 118.2 Hz


def pitch_shift(x: np.ndarray, fs: int, ratio: float) -> np.ndarray:
    win = np.hanning(N)
    nbins = N // 2 + 1
    f = np.fft.rfftfreq(N, d=1.0 / fs)
    expected = 2.0 * np.pi * f * HOP / fs        # 每个 hop 的理论相位增量

    pad = N
    xp = np.concatenate([np.zeros(pad), x, np.zeros(pad)])
    y = np.zeros_like(xp)
    wsum = np.zeros_like(xp)

    prev_ph = None
    synth_ph = None
    src = np.arange(nbins)
    dst = np.rint(src * ratio).astype(int)
    keep = dst < nbins

    for start in range(0, len(xp) - N, HOP):
        frame = xp[start:start + N] * win
        spec = np.fft.rfft(frame)
        mag = np.abs(spec)
        ph = np.angle(spec)

        if prev_ph is None:
            omega = expected / HOP
            synth_ph = ph.copy()
        else:
            dphi = ph - prev_ph - expected
            dphi = (dphi + np.pi) % (2.0 * np.pi) - np.pi   # 解卷绕到 [-π, π]
            omega = (expected + dphi) / HOP                  # 瞬时角频率 rad/sample
        prev_ph = ph

        # 原始幅度整体搬到新的频点，不做包络补偿 —— 这就是"原封不动"的关键
        out_mag = np.zeros(nbins)
        out_phase = np.zeros(nbins)
        for k in src[keep]:
            d = dst[k]
            out_mag[d] += mag[k]
            out_phase[d] = synth_ph[d]
            synth_ph[d] += omega[k] * ratio * HOP

        spec_new = out_mag * np.exp(1j * out_phase)
        y[start:start + N] += np.fft.irfft(spec_new, N) * win
        wsum[start:start + N] += win * win

    y = y / np.maximum(wsum, 1e-9)
    return y[pad:pad + len(x)]


def harmonic_amps(sig: np.ndarray, fs: int, f0: float, kmax: int = 40) -> np.ndarray:
    """逐次谐波取幅度，返回长度 kmax 的数组（dB）。"""
    f, db = amplitude_spectrum(sig, fs)
    ks = np.arange(1, kmax + 1)
    return np.array([db[(f > k * f0 - 18) & (f < k * f0 + 18)].max() for k in ks])


def main() -> None:
    x, fs = read_mono(SRC)
    y = pitch_shift(x, fs, RATIO)

    # 与原始等响度，避免"更响=更好听"的错觉干扰对比
    rms_in = np.sqrt(np.mean(x * x))
    rms_out = np.sqrt(np.mean(y * y))
    y = y * (rms_in / max(rms_out, 1e-12))
    peak = np.max(np.abs(y))
    if peak > 0.99:
        y = y * (0.99 / peak)
    wavfile.write(str(DST), fs, np.int16(np.clip(y, -1.0, 1.0) * 32767))

    f0_in, f0_out = f0_median(x, fs), f0_median(y, fs)
    print(f"输入 : {SRC.name}  F0中位 {f0_in:.1f} Hz  RMS {rms_dbfs(x):.1f} dBFS")
    print(f"输出 : {DST.name}  F0中位 {f0_out:.1f} Hz  RMS {rms_dbfs(y):.1f} dBFS")
    print(f"整体上移 ratio={RATIO:.3f}，实测 {f0_out / f0_in:.3f} 倍；男声范围 "
          f"{MALE_F0_HZ[0]:.0f}-{MALE_F0_HZ[1]:.0f} Hz")

    ha0 = harmonic_amps(x, fs, f0_in)
    ha1 = harmonic_amps(y, fs, f0_out)
    # 以第 1 次谐波为基准，比较"相对幅度形状"是否守恒
    shape_diff = np.abs((ha1 - ha1[0]) - (ha0 - ha0[0]))[:20]
    print(f"前 20 次谐波的相对幅度最大偏差 {shape_diff.max():.2f} dB（越小说明形状越原封不动）")
    print(f"已保存: {DST}")

    fig, axes = plt.subplots(3, 1, figsize=(12, 11))
    for ax, sig, name, f0 in ((axes[0], x, "原始 " + SRC.name, f0_in),
                              (axes[1], y, "整体上移 " + DST.name, f0_out)):
        freq, mag_db = amplitude_spectrum(sig, fs)
        ax.semilogx(freq[1:], mag_db[1:], linewidth=0.8, color="#2b6cb0", label="幅度谱")
        ax.axvspan(*MALE_F0_HZ, color="#f6ad55", alpha=0.18, lw=0, zorder=0,
                   label=f"男声基频 {MALE_F0_HZ[0]:.0f}-{MALE_F0_HZ[1]:.0f} Hz")
        ax.set_xlim(20, fs / 2)
        ax.set_ylim(-110, 5)
        ax.set_title(f"{name}    F0 {f0:.1f} Hz / RMS {rms_dbfs(sig):.1f} dBFS")
        ax.set_xlabel("频率 (Hz)")
        ax.set_ylabel("幅度 (dBFS)")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(loc="upper right", fontsize=9, framealpha=0.9)

    ax = axes[2]
    ks = np.arange(1, 41)
    ax.plot(ks, ha0, "o-", color="#4a5568", linewidth=1.2, ms=4,
            label=f"原始（谐波间距 {f0_in:.0f} Hz）")
    ax.plot(ks, ha1, "s--", color="#c53030", linewidth=1.2, ms=4,
            label=f"整体上移（谐波间距 {f0_out:.0f} Hz）")
    ax.set_xlabel("谐波序号 k")
    ax.set_ylabel("谐波幅度 (dBFS)")
    ax.set_title("按谐波序号对齐：两条曲线基本重合 → 相对幅度关系原封不动，只是间距被拉宽")
    ax.set_xlim(0, 41)
    ax.set_ylim(-110, -20)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="upper right", fontsize=9, framealpha=0.9)

    fig.suptitle(f"整体等比例上移（所有分量 ×{RATIO:.3f}，F0 {f0_in:.0f} -> {f0_out:.0f} Hz）",
                 fontsize=13)
    fig.tight_layout()
    fig.savefig(PNG, dpi=150)
    print(f"已保存: {PNG}")


if __name__ == "__main__":
    main()
