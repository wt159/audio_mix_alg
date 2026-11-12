/*
 * mix_algos.c — 逐样本算法（四个经典 + tanh 软饱和 + 三次软削波 + μ/A 律对照）
 *
 * 统一约定：输入两路 int16，输出 float（未量化），返回被钳位的样本数。
 * 之所以不直接输出 int16，是为了让四者与 float 参考链路共用同一套度量。
 */

#include <stdint.h>
#include <math.h>

#include "mix_algos.h"

/*
 * [1] 简单相加 + 硬饱和。
 * 未过载时输出与输入严格线性（残差为 -inf），只有超过满量程的样本被削平。
 */
long mix_audio_simple(const int16_t *a, const int16_t *b,
                      float *out, int n, void *ctx)
{
    long clamped = 0;
    int i;
    (void)ctx;

    for (i = 0; i < n; i++) {
        int32_t v = (int32_t)a[i] + (int32_t)b[i];
        if (v > INT16_MAX)      { v = INT16_MAX; clamped++; }
        else if (v < INT16_MIN) { v = INT16_MIN; clamped++; }
        out[i] = (float)v / 32768.0f;
    }
    return clamped;
}

/*
 * [2] 平均。完全线性、零失真，但恒定 -6 dB；路数变化时整体电平会跳变。
 * 注意：C 的整数除法向零截断，正负半周舍入方向不一致，
 *      工程实现通常写成 (a + b + 1) >> 1。
 */
long mix_audio_average(const int16_t *a, const int16_t *b,
                       float *out, int n, void *ctx)
{
    int i;
    (void)ctx;

    for (i = 0; i < n; i++) {
        int32_t v = ((int32_t)a[i] + (int32_t)b[i]) / 2;
        out[i] = (float)v / 32768.0f;
    }
    return 0;
}

/*
 * [3] "防削波" 公式 a + b - a*b/M（仅同号时启用）。
 *
 * 同号时它等价于 M*(1 - (1-a/M)(1-b/M))，即把两路幅度当作"软并集"，
 * 输出恒不超过满量程；异号时退化为线性相加。
 * 实测：-6 dBFS 双音互调约 -20 dB，不是"失真不明显"。
 * 另外，它的分支条件是"同号"，并非 Viktor Toth 原文的"是否低于中点值"
 * ——原文要求先把有符号样本偏移到无符号域再分两支。
 */
long mix_audio_no_clipping(const int16_t *a, const int16_t *b,
                           float *out, int n, void *ctx)
{
    long clamped = 0;
    int i;
    (void)ctx;

    for (i = 0; i < n; i++) {
        int32_t s1 = a[i];
        int32_t s2 = b[i];
        int32_t v;

        if (s1 < 0 && s2 < 0) {
            v = s1 + s2 - (s1 * s2) / INT16_MIN;
        } else if (s1 > 0 && s2 > 0) {
            v = s1 + s2 - (s1 * s2) / INT16_MAX;
        } else {
            v = s1 + s2;
        }

        if (v > INT16_MAX)      { v = INT16_MAX; clamped++; }
        else if (v < INT16_MIN) { v = INT16_MIN; clamped++; }
        out[i] = (float)v / 32768.0f;
    }
    return clamped;
}

/*
 * [4] (a^2 + b^2) / (a + b)。
 *
 * 这是 a、b 的 contraharmonic mean（反调和平均），三个结构性缺陷：
 *   (a) d/db 在 b->0 处恒等于 -1：微弱信号被反相、原幅度地混入；
 *   (b) 非单调：a=10000 时 b 从 0 增到 4142，输出反而从 10000 降到 8284；
 *   (c) 在 a + b = 0 附近发散：a=10000, b=-9999 输出直接顶到满量程，
 *       输入变化 1 个 LSB，输出在 +满量程 / 0 / -满量程 之间跳变。
 * 分子用 int64 计算，避免 (a^2+b^2)/s 在 s 很小时溢出 int32。
 */
long mix_audio_your_formula(const int16_t *a, const int16_t *b,
                            float *out, int n, void *ctx)
{
    long clamped = 0;
    int i;
    (void)ctx;

    for (i = 0; i < n; i++) {
        int64_t s1 = a[i];
        int64_t s2 = b[i];
        int64_t s  = s1 + s2;
        int64_t v;

        if (s == 0) {
            v = 0;
        } else {
            v = (s1 * s1 + s2 * s2) / s;
        }

        if (v > INT16_MAX)      { v = INT16_MAX; clamped++; }
        else if (v < INT16_MIN) { v = INT16_MIN; clamped++; }
        out[i] = (float)v / 32768.0f;
    }
    return clamped;
}

/*
 * [6] tanh 软饱和：y = x（小信号），越过 ±0.5 接点后平滑压向 ±0.875。
 *
 * 设计目标：修复 contra / 硬饱和的根本缺陷，同时保住逐样本、无记忆结构。
 *   y(x) = x                                  |x| <= 0.5
 *   y(x) = sign(x) * ( 0.25 + tanh(k*(|x|-0.5)) / (2k) )   否则
 *
 * 性质：
 *   - 奇对称、严格单调、处处连续可微：无 contra 的反相 / 非单调 / 发散问题；
 *   - 接点两侧斜率均为 1（tanh'(0)=k，除以 k 归一），C1 连续，无增益跳变；
 *   - |y| < 0.25 + 1/(2k) ≈ 0.875（约 -1.2 dBFS），数学上永不削波；
 *   - k = 0.8 时，1.0 的输入对应输出约 0.76，叠加密度为 -2.4 dB。
 * 代价：接点以上的波形整体被压缩（逐样本公式无法区分峰值与持续响度），
 *       谐波失真集中在过载段，与 [1] 的硬削波同源但起音平缓。
 */
long mix_audio_tanh_sat(const int16_t *a, const int16_t *b,
                        float *out, int n, void *ctx)
{
    const double k = 0.8;
    long clamped = 0;
    int i;
    (void)ctx;

    for (i = 0; i < n; i++) {
        double x = ((double)a[i] + (double)b[i]) / 32768.0;
        double v;

        if (x > 0.5) {
            v = 0.25 + tanh(k * (x - 0.5)) / (2.0 * k);
        } else if (x < -0.5) {
            v = -0.25 + tanh(k * (x + 0.5)) / (2.0 * k);
        } else {
            v = x;                       /* -6 dBFS 以下完全线性，零失真 */
        }

        if (v > 1.0)       { v = 1.0;  clamped++; }   /* 理论上不可达，兜底 */
        else if (v < -1.0) { v = -1.0; clamped++; }
        out[i] = (float)v;
    }
    return clamped;
}

/*
 * [8] μ 律对照（ITU-T STL-2000 手册 g711.tex 的连续压缩律公式，μ = 255）。
 * 语义裁定见 docs/adr/0004（票 #8/#10）：放大族对照，非软削波。
 *
 *   F(x) = ln(1 + μ·|x|) / ln(1 + μ)，x = (a+b)/32768 ∈ [-1, 1]
 *
 * 性质：
 *   - 对数域全动态压缩映射：端点 (0,0)、(1,1)，严格凹 ⇒ 除端点外全程增益 >1；
 *     原点斜率 μ/ln(1+μ) = 255/ln 256 ≈ 46.0（+33.25 dB，票 #9 更正，旧值 48.9 为笔误）；
 *   - 单调有界、奇对称；全表唯一破「不放大」不变量的链路，
 *     登记价值在测量对照——检验响度对齐框架（resid_db 吸收整体增益）对放大族是否公平；
 *   - 与 [9] A 律构成「放大强度」维度的天然 A/B（原点斜率 46.0 vs 16.0，差 9.2 dB）；
 *   - 钳位统计语义（登记表 §2.6）：线性相加 |x|>1 时输入钳到 ±1 再过曲线；
 *     F 有界且 F(±1)=±1，钳位后无需再截断，仅计数。
 */
long mix_audio_mulaw(const int16_t *a, const int16_t *b,
                     float *out, int n, void *ctx)
{
    const double mu = 255.0;
    const double denom = log(1.0 + mu);          /* ln 256 ≈ 5.545 */
    long clamped = 0;
    int i;
    (void)ctx;

    for (i = 0; i < n; i++) {
        double x = ((double)a[i] + (double)b[i]) / 32768.0;
        double ax = fabs(x);

        if (ax > 1.0) {                          /* 越界输入钳位（§2.6） */
            ax = 1.0;
            clamped++;
        }
        out[i] = (float)(copysign(log(1.0 + mu * ax) / denom, x));
    }
    return clamped;
}

/*
 * [9] A 律对照（ITU-T STL-2000 手册 g711.tex 的连续压缩律公式，A = 87.6；
 *     手册正文记 87.56，该值使原点斜率恰为 16.00，引用并存时注明——ADR-0004 裁定三）。
 *
 *   F(x) = sign(x)·A|x| / (1 + ln A)，           |x| < 1/A（阈下线性段）
 *   F(x) = sign(x)·(1 + ln(A|x|)) / (1 + ln A)， 1/A ≤ |x| ≤ 1
 *
 * 性质：
 *   - 放大族对照第二成员：原点斜率 A/(1+ln A) ≈ 16.0（+24.1 dB），
 *     与 μ 律（46.0）差 9.2 dB，构成放大强度维度的 A/B；
 *   - 阈下 1/A ≈ 0.0114 以下为线性段，但斜率 16.0 仍 >1——无任何透传段，
 *     结构上不属软削波（裁定一）；|x|=1/A 两侧斜率相同，C1 连续；
 *   - 端点 (0,0)、(1,1)，单调有界、奇对称；钳位统计语义与 [8] 相同（§2.6）。
 */
long mix_audio_alaw(const int16_t *a, const int16_t *b,
                    float *out, int n, void *ctx)
{
    const double aa = 87.6;
    const double denom = 1.0 + log(aa);          /* 1 + ln 87.6 ≈ 5.474 */
    const double linthr = 1.0 / aa;              /* 线性段阈值 1/A ≈ 0.0114 */
    long clamped = 0;
    int i;
    (void)ctx;

    for (i = 0; i < n; i++) {
        double x = ((double)a[i] + (double)b[i]) / 32768.0;
        double ax = fabs(x);
        double v;

        if (ax > 1.0) {                          /* 越界输入钳位（§2.6） */
            ax = 1.0;
            clamped++;
        }
        if (ax < linthr)
            v = aa * ax / denom;
        else
            v = (1.0 + log(aa * ax)) / denom;
        out[i] = (float)copysign(v, x);
    }
    return clamped;
}

/*
 * [7] 三次软削波（cubic soft clipper，JOS PASP "Cubic Soft Clipper"，§9.1.6）：
 *
 *   y(x) = -2/3              x <  -1
 *   y(x) = x - x^3/3        -1 <= x <= 1
 *   y(x) = +2/3              x >  +1
 *
 * 性质（与 [6] tanh 构成「代数 vs 超越」的直接 A/B 对照）：
 *   - 奇对称、严格单调（中段斜率 1-x^2 >= 0），无超越函数调用，
 *     每样本仅 2 次乘加，是本仓库最快的软饱和；
 *   - 曲线端点 f(±1) = ±2/3，平台段钳到端点值，±1 处值连续但斜率为 0，
 *     仅 C0 连续（tanh 实现是 C1，接点两侧斜率连续）；
 *   - JOS 谱页结论：带宽扩展限制为 3 倍，是奇函数非线性可得的最慢
 *     混叠率，失真以三次谐波为主；
 *   - 输入按线性叠加 x=(a+b)/32768 归一到 ±2（两路满量程同相叠加），
 *     输出恒有界 |y| <= 2/3（约 -3.5 dBFS），数学上不会削波。
 * 钳位统计语义与 [6] tanh 一致：统计 |x|>1 进入平台段的样本数。
 */
long mix_audio_cubic_soft(const int16_t *a, const int16_t *b,
                          float *out, int n, void *ctx)
{
    long clamped = 0;
    int i;
    (void)ctx;

    for (i = 0; i < n; i++) {
        double x = ((double)a[i] + (double)b[i]) / 32768.0;
        double v;

        if (x > 1.0) {
            v = 2.0 / 3.0;
            clamped++;                  /* 进入平台段，计入钳位 */
        } else if (x < -1.0) {
            v = -2.0 / 3.0;
            clamped++;
        } else {
            v = x - (x * x * x) / 3.0;
        }
        out[i] = (float)v;
    }
    return clamped;
}
