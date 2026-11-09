"""音高提升：只改变基频 F0，保持谱包络（音色/共振峰）不变。

与上一步的区别：
  - 音高 = 谐波在频率轴上的位置（间距 = F0）
  - 音色 = 谐波幅度的包络形状（共振峰位置）
  这次搬动谐波位置，包络原地不动；上一步是包络搬动，谐波原地不动。

算法（单次 STFT，相位声码器思路）：
  1. 逐帧求幅度谱，用倒谱平滑分离出包络与谐波细结构
  2. 计算每个频点的瞬时频率 ω（相位差分 + 解卷绕）
  3. 把第 k 个频点的内容搬到第 round(k * ratio) 个频点：
       目的幅度 = 源细结构 × 目的频率处的原包络   ← 包络不跟着搬，这就是音色不变的关键
       目的相位 = 上一帧该频点的合成相位 + ω * ratio * hop
  4. 逆变换 + 重叠相加，时长保持不变（分析/合成都用同一个 hop）

F0 从 104.3 Hz 提升 ratio 倍，目标约 118 Hz，仍在成年男声 85-180 Hz 内。
"""

from pathlib import Path

import numpy as np
import matplotlib
from scipy.io import wavfile

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import rcParams

from fft_spectrum import read_mono, amplitude_spectrum, rms_dbfs, MALE_F0_HZ
from timbre_shift import f0_median, spectral_envelope

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

        env = spectral_envelope(mag)
        detail = mag / (env + 1e-30)

        out_detail = np.zeros(nbins)
        out_phase = np.zeros(nbins)
        src = np.arange(nbins)
        dst = np.rint(src * ratio).astype(int)
        keep = dst < nbins

        for k in src[keep]:
            d = dst[k]
            out_detail[d] += detail[k]
            out_phase[d] = synth_ph[d]
            synth_ph[d] += omega[k] * ratio * HOP

        spec_new = out_detail * env * np.exp(1j * out_phase)
        y[start:start + N] += np.fft.irfft(spec_new, N) * win
        wsum[start:start + N] += win * win

    y = y / np.maximum(wsum, 1e-9)
    return y[pad:pad + len(x)]


def average_envelope(sig: np.ndarray, fs: int) -> tuple[np.ndarray, np.ndarray]:
    """逐帧求谱包络再取平均，得到平滑的声道传递函数曲线。

    用包络曲线比"谐波包络峰"更适合验证共振峰：
    F0 升高后谐波间距变宽（118 Hz），共振峰落在两根谐波之间时，
    谐波峰值会在相邻谐波间跳动，看起来像 F1 变了，其实是采样点变稀。
    """
    win = np.hanning(N)
    acc = np.zeros(N // 2 + 1)
    cnt = 0
    for start in range(0, len(sig) - N, HOP):
        fr = sig[start:start + N] * win
        if np.sqrt(np.mean(fr * fr)) < 1e-3:
            continue
        mag = np.abs(np.fft.rfft(fr)) / (np.sum(win) / 2.0)   # 归一化到 dBFS
        acc += spectral_envelope(mag)
        cnt += 1
    f = np.fft.rfftfreq(N, d=1.0 / fs)
    return f, 20.0 * np.log10(acc / max(cnt, 1) + 1e-12)


def envelope_peak(f: np.ndarray, db: np.ndarray, lo: float = 250.0,
                  hi: float = 800.0) -> float:
    sel = (f > lo) & (f < hi)
    return float(f[sel][np.argmax(db[sel])]) if sel.any() else 0.0


def main() -> None:
    x, fs = read_mono(SRC)
    y = pitch_shift(x, fs, RATIO)

    # 与原始等响度：避免"更响=更好听"的错觉干扰对比
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
    print(f"音高提升 ratio={RATIO:.3f}，实测 {f0_out / f0_in:.3f} 倍；男声范围 "
          f"{MALE_F0_HZ[0]:.0f}-{MALE_F0_HZ[1]:.0f} Hz")

    ef0, ea0 = average_envelope(x, fs)
    ef1, ea1 = average_envelope(y, fs)
    p0, p1 = envelope_peak(ef0, ea0), envelope_peak(ef1, ea1)
    print(f"F1(包络峰) 原始 {p0:.0f} Hz -> 变换后 {p1:.0f} Hz，相差 {abs(p1 - p0):.0f} Hz")
    print(f"已保存: {DST}")

    fig, axes = plt.subplots(3, 1, figsize=(12, 11))
    for ax, sig, name, f0 in ((axes[0], x, "原始 " + SRC.name, f0_in),
                              (axes[1], y, "音高提升 " + DST.name, f0_out)):
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
    ax.semilogx(ef0, ea0, color="#4a5568", linewidth=2.0,
                label=f"原始包络  F0={f0_in:.0f} Hz  F1={p0:.0f} Hz")
    ax.semilogx(ef1, ea1, color="#c53030", linewidth=1.4, linestyle="--",
                label=f"提升后包络 F0={f0_out:.0f} Hz  F1={p1:.0f} Hz")
    ax.axvline(p0, color="#4a5568", linestyle=":", linewidth=1)
    ax.set_xlim(80, 4000)
    ax.set_ylim(-100, -10)
    ax.set_title("谱包络对比：两条包络基本重合 → 共振峰没动，音色未变")
    ax.set_xlabel("频率 (Hz)")
    ax.set_ylabel("包络电平 (dB)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="upper right", fontsize=9, framealpha=0.9)

    fig.suptitle(f"音高提升对比（F0 {f0_in:.0f} -> {f0_out:.0f} Hz，共振峰保持不动）", fontsize=13)
    fig.tight_layout()
    fig.savefig(PNG, dpi=150)
    print(f"已保存: {PNG}")


if __name__ == "__main__":
    main()
