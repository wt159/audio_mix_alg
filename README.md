# audio_mix_alg

音频混音算法对比 / A/B 试听工具（C99，无第三方依赖）。

布局：`audio_mix.c` 是入口（指标、WAV I/O、合成素材、表格输出、主流程）；被测算法和参考链路在 `src/`：

- `src/mix_algos.{c,h}` — 六个逐样本公式（被测对象，链路 1/2/3/4/6/7）；
- `src/limiter.{c,h}` — 前瞻限幅器与 float 参考链路（链路 5）；
- `src/comp_soft.{c,h}` — 软拐点压缩器（链路 8）；
- `src/truepeak.{c,h}` — 真峰值检测链与 dBTP 前瞻限幅（链路 9）与 dBTP 指标列。

把一个常被混淆的问题拆开：

- **混音（mixing）** = 线性相加，数学上唯一正确，且可逆；
- **防削波** = 独立的动态处理（限幅器），带时间常数，不是逐样本公式。

文件里保留六个「逐样本公式」作为被测对象，另加三条动态链路（[5] float 前瞻限幅、[8] softknee 压缩器、[9] truepeak 真峰值限幅）：

| # | 链路 | 特点 |
|---|------|------|
| 1 | 线性相加 + 硬饱和 | 基准，未过载时比特级无损 |
| 2 | 平均 (a+b)/2 | 完全线性，但每路固定 -6 dB |
| 3 | 防削波 `a+b-ab/M` | 同号压缩、异号线性，零点斜率跳变 |
| 4 | `(a²+b²)/(a+b)` | 对 b 的增量增益恒为 -1，且在 a+b=0 处发散 |
| 5 | float 域 + 前瞻限幅 | 推荐做法：1/√N 预增益 + lookahead limiter |
| 6 | tanh 软饱和 | 奇对称光滑压缩：小信号线性、单调有界，永不硬削波 |
| 7 | 三次软削波 `x-x³/3` | 代数多项式族软饱和：无超越函数、带宽扩展=3（JOS PASP），与 tanh 构成代数 vs 超越对照 |
| 8 | softknee 压缩器 | 软拐点前馈压缩器（JAES 2012 Eq(4)/(15)）：T=-18 dBFS、R=4:1、W=6 dB、10/100 ms，增益只衰减不放大 |
| 9 | truepeak 前瞻限幅 | dBTP 驱动：BS.1770-4 Annex 2 的 4× FIR 检测链看样本间峰，ceiling −1.0 dBTP（EBU R128 §m）；同链路兼作全部算法的 dBTP 指标列 |

每条链路输出一个 16-bit WAV（响度已对齐，可直接 A/B），并打印峰值/RMS/波峰因数、钳位样本数、dBTP、波形残差、互调残差等指标。

## 用法

```
audio_mix                          合成素材，跑全部对比
audio_mix a.wav b.wav              用两路 16-bit PCM WAV 作素材
audio_mix --sec 12 --rate 44100    调整时长与采样率
audio_mix --match -20              响度对齐目标（默认 -18 dBFS）
audio_mix --no-match               关闭响度对齐（听真实电平差）
audio_mix --outdir out             指定输出目录
```

## 编译

```
make          # release: -O2 -Wall -Wextra -pedantic
make debug    # -O0 -g + ASan/UBSan，调试算法问题时用
make clean
```

或手动：`gcc -O2 -std=c99 -o audio_mix audio_mix.c src/mix_algos.c src/limiter.c src/comp_soft.c src/truepeak.c -lm`

## 指标说明

残差指标：先求 g = argmin ‖y − g·s‖，再取 10·log₁₀(‖y−g·s‖²/‖g·s‖²)，其中 s 是未做任何处理的线性叠加。g 吸收了增益，残差只反映非线性；线性算法残差趋近 −∞。节目素材上还需剥离 10 ms 尺度慢变增益——限幅器改变的是电平（波形局部形状不变），削波改变的是波形，不剥离包络对限幅器不公平。单音双音测试是稳态信号，无需剥离。
