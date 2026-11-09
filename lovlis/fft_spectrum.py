"""读取两段音频，分别绘制频域图（幅度谱）。

坐标说明：
  X 轴 = 频率(Hz)，对数刻度。人耳对频率的感知接近对数，用对数轴低频细节不会被压扁。
  Y 轴 = 幅度(dBFS)。0 dBFS 代表满量程正弦波，语音信号通常落在 -60 ~ -10 dBFS。

算法步骤：
  1. 读取 WAV，多声道取平均混成单声道，各类采样格式统一归一化到 [-1, 1]
  2. 加 Hann 窗，抑制因截断产生的频谱泄漏（旁瓣 -31dB，比矩形窗低很多）
  3. rFFT 得到复数谱，取模得到幅度
  4. 幅度归一化到满量程，转 dB：20*log10(mag)
"""

import sys
from pathlib import Path

import numpy as np
import matplotlib
from scipy.io import wavfile

matplotlib.use("Agg")          # 只存图不弹窗
import matplotlib.pyplot as plt
from matplotlib import rcParams

# 中文显示
rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
rcParams["axes.unicode_minus"] = False

HERE = Path(__file__).resolve().parent
FILES = [HERE / "录音.wav", HERE / "录音_strong.wav"]

# 成年男声基频(F0)正常范围，单位 Hz（成年女声通常 165-255 Hz）
MALE_F0_HZ = (85.0, 180.0)


def read_mono(path: Path) -> tuple[np.ndarray, int]:
    """读 WAV，返回 (float64 单声道样本, 采样率)。"""
    fs, data = wavfile.read(str(path))

    if data.dtype == np.int16:
        x = data.astype(np.float64) / 32768.0
    elif data.dtype == np.int32:
        x = data.astype(np.float64) / 2147483648.0
    elif data.dtype == np.uint8:
        x = (data.astype(np.float64) - 128.0) / 128.0
    elif data.dtype == np.float32 or data.dtype == np.float64:
        x = data.astype(np.float64)
    else:
        raise TypeError(f"不支持的采样格式: {data.dtype}")

    if x.ndim > 1:                       # (样本数, 声道数) -> 平均
        x = x.mean(axis=1)
    return x, int(fs)


def amplitude_spectrum(x: np.ndarray, fs: int) -> tuple[np.ndarray, np.ndarray]:
    """返回 (频率轴 Hz, 幅度谱 dBFS)。"""
    n = x.size
    win = np.hanning(n)                  # Hann 窗
    xw = (x - x.mean()) * win            # 去直流 + 加窗

    spec = np.fft.rfft(xw)
    mag = np.abs(spec) / (np.sum(win) / 2.0)   # 归一化到正弦波幅度
    mag_db = 20.0 * np.log10(mag + 1e-12)

    freq = np.fft.rfftfreq(n, d=1.0 / fs)
    return freq, mag_db


def rms_dbfs(x: np.ndarray) -> float:
    return 20.0 * np.log10(np.sqrt(np.mean(x * x)) + 1e-12)


def main() -> None:
    missing = [str(p) for p in FILES if not p.exists()]
    if missing:
        sys.exit("找不到文件: " + ", ".join(missing))

    fig, axes = plt.subplots(len(FILES), 1, figsize=(12, 8), sharex=False)

    for ax, path in zip(axes, FILES):
        x, fs = read_mono(path)
        freq, mag_db = amplitude_spectrum(x, fs)

        ax.semilogx(freq[1:], mag_db[1:], linewidth=0.8, color="#2b6cb0",
                    label="幅度谱")

        # 男声基频范围：用竖向着色带标出
        ax.axvspan(MALE_F0_HZ[0], MALE_F0_HZ[1], color="#f6ad55", alpha=0.20,
                   lw=0, zorder=0,
                   label=f"成年男声基频 {MALE_F0_HZ[0]:.0f}-{MALE_F0_HZ[1]:.0f} Hz")

        ax.set_xlim(20, fs / 2)
        ax.set_ylim(-110, 5)
        ax.set_title(f"{path.name}    采样率 {fs} Hz / {x.size / fs:.2f} s / RMS {rms_dbfs(x):.1f} dBFS")
        ax.set_xlabel("频率 (Hz)")
        ax.set_ylabel("幅度 (dBFS)")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(loc="upper right", fontsize=9, framealpha=0.9)

        peak = int(np.argmax(mag_db[1:]) + 1)
        print(f"{path.name}: fs={fs} Hz, {x.size / fs:.2f} s, "
              f"RMS={rms_dbfs(x):.2f} dBFS, 峰值频率={freq[peak]:.1f} Hz")

    fig.suptitle("频域图对比 (Hann 窗, 幅度谱)", fontsize=13)
    fig.tight_layout()

    out = HERE / "spectrum.png"
    fig.savefig(out, dpi=150)
    print(f"已保存: {out}")


if __name__ == "__main__":
    main()
