/*
 * audio_mix.c — 音频混音算法对比 / A/B 试听工具
 * =============================================================================
 *
 * 本程序把一个常被混淆的问题拆开：
 *
 *   混音（mixing）  = 线性相加，数学上唯一正确，且可逆；
 *   防削波          = 独立的动态处理（限幅器），带时间常数，不是逐样本公式。
 *
 * 文件里保留五个「逐样本公式」作为被测对象，另加一条参考链路：
 *
 *   [1] 线性相加 + 硬饱和      —— 基准，未过载时比特级无损
 *   [2] 平均 (a+b)/2           —— 完全线性，但每路固定 -6 dB
 *   [3] 防削波 a+b-ab/M        —— 同号压缩、异号线性，零点斜率跳变
 *   [4] (a^2+b^2)/(a+b)        —— 对 b 的增量增益恒为 -1，且在 a+b=0 处发散
 *   [5] float 域 + 前瞻限幅     —— 推荐做法：1/sqrt(N) 预增益 + lookahead limiter
 *   [6] tanh 软饱和             —— 奇对称光滑压缩：小信号线性、单调有界，永不硬削波
 *
 * 每条链路都会输出一个 16-bit WAV（响度已对齐，可直接 A/B），并打印：
 *
 *   峰值 dBFS / RMS dBFS / 波峰因数 / 钳位样本数 / 匹配增益
 *   波形残差  = 节目素材上、剥离 10 ms 尺度慢变增益后的残差（能量比，dB）
 *   互调残差  = 1 kHz + 1.5 kHz（单音 0 dBFS）稳态双音下的同一指标
 *
 * 残差指标说明：
 *   先求 g = argmin ||y - g*s||，再取 10*log10(||y-g*s||^2 / ||g*s||^2)，
 *   其中 s 是未做任何处理的线性叠加。这样把「整体音量差」和「波形形状差」
 *   分开：g 吸收了增益，残差只反映非线性。线性算法残差趋近 -inf。
 *
 *   为什么节目素材要多一步「剥离慢变增益」：限幅器与逐样本公式的偏差性质
 *   不同——限幅器改变的是电平（波形局部形状不变），削波改变的是波形。
 *   不剥离包络，限幅器会因为「骑乘了整个节目」而被判成高残差，比较不公平。
 *   单音双音测试是稳态信号，无需剥离，直接反映非线性。
 *
 * 用法：
 *   audio_mix                          合成素材，跑全部对比
 *   audio_mix a.wav b.wav              用两路 16-bit PCM WAV 作素材
 *   audio_mix --sec 12 --rate 44100    调整时长与采样率
 *   audio_mix --match -20              响度对齐目标（默认 -18 dBFS）
 *   audio_mix --no-match               关闭响度对齐（听真实电平差）
 *   audio_mix --outdir out             指定输出目录
 *
 * 编译：
 *   gcc -O2 -std=c99 -o audio_mix audio_mix.c -lm
 *
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MAKE_DIR(p) mkdir((p), 0755)
#endif

#define PI 3.14159265358979323846

/* ============================================================================
 * 0. 基础工具
 * ==========================================================================*/

/* 线性幅度 -> dBFS；下限保护，避免 log(0) */
static double to_db(double x)
{
    return (x > 1e-12) ? 20.0 * log10(x) : -999.0;
}

/* float -> 16-bit PCM，四舍五入并饱和 */
static int16_t to_pcm16(float v)
{
    float s;
    if (v >= 1.0f)  return 32767;
    if (v <= -1.0f) return -32768;
    s = v * 32767.0f;
    return (int16_t)(s >= 0.0f ? s + 0.5f : s - 0.5f);
}

static double buf_peak(const float *x, int n)
{
    double p = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        double v = fabs((double)x[i]);
        if (v > p) p = v;
    }
    return p;
}

/* 两路叠加后的实际峰值（不是两路峰值之和，后者只是上界） */
static double buf_peak_sum(const float *a, const float *b, int n)
{
    double p = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        double v = fabs((double)a[i] + (double)b[i]);
        if (v > p) p = v;
    }
    return p;
}

static double buf_rms(const float *x, int n)
{
    double s = 0.0;
    int i;
    for (i = 0; i < n; i++) s += (double)x[i] * x[i];
    return (n > 0) ? sqrt(s / n) : 0.0;
}

static void buf_scale(float *x, int n, float g)
{
    int i;
    for (i = 0; i < n; i++) x[i] *= g;
}

static double smoothstep(double e0, double e1, double x)
{
    double t = (x - e0) / (e1 - e0);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return t * t * (3.0 - 2.0 * t);
}

/* ============================================================================
 * 1. 逐样本算法（四个经典 + tanh 软饱和）
 *
 * 统一约定：输入两路 int16，输出 float（未量化），返回被钳位的样本数。
 * 之所以不直接输出 int16，是为了让四者与 float 参考链路共用同一套度量。
 * ==========================================================================*/

/*
 * [1] 简单相加 + 硬饱和。
 * 未过载时输出与输入严格线性（残差为 -inf），只有超过满量程的样本被削平。
 */
static long mix_audio_simple(const int16_t *a, const int16_t *b,
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
static long mix_audio_average(const int16_t *a, const int16_t *b,
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
static long mix_audio_no_clipping(const int16_t *a, const int16_t *b,
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
static long mix_audio_your_formula(const int16_t *a, const int16_t *b,
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
static long mix_audio_tanh_sat(const int16_t *a, const int16_t *b,
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

/* ============================================================================
 * 2. 前瞻限幅器（lookahead limiter）
 *
 * 结构：环形延迟线保存 look 个未来样本 -> 窗口峰值 -> 目标增益 -> 一阶平滑
 *       -> 乘到延迟后的样本上。
 *
 * 为什么需要 lookahead：峰值检测总是滞后的，等检测到过载再降增益，
 * 峰值已经过去了。把信号延迟 look 毫秒，增益就有时间提前降下来。
 *
 * 为什么是一阶平滑而不是瞬间起控：瞬间起控会在波形上留下增益阶跃，
 * 听感是"咔哒"。用指数逼近，增益在 look 窗口内平滑下降，
 * 代价是到达目标有极小残差，由最后的安全钳位兜底（统计在 hard_clips）。
 * ==========================================================================*/

typedef struct {
    float ceiling;   /* 线性幅度上限，-1 dBFS = 0.8913 */
    float look_ms;
    float atk_ms;
    float rel_ms;
    float pregain;   /* 累加后、限幅前的增益，通常 1/sqrt(N) */
    int   fs;        /* 采样率，决定 look / atk / rel 对应的样本数 */
    long  active_samples;  /* 输出：增益 < 1 的样本数（窗口含峰即算） */
    long  hard_clips;      /* 输出：安全钳位次数 */
    double gain_sum;       /* 输出：线性增益累加，用于求平均衰减 */
    float  gain_min;       /* 输出：最小增益，用于求最大衰减 */
} limiter_cfg_t;

typedef struct {
    float ceiling;
    int   look;
    int   size;
    float atk;
    float rel;
    float *buf;
    int   pos;
    float gain;
    long  active;
    long  hard_clip;
    double gain_sum;
    float gain_min;
} limiter_t;

static int limiter_init(limiter_t *L, float ceiling,
                        float look_ms, float atk_ms, float rel_ms, int fs)
{
    memset(L, 0, sizeof(*L));

    L->ceiling = ceiling;
    L->look = (int)(look_ms * 0.001f * (float)fs + 0.5f);
    if (L->look < 1) L->look = 1;
    L->size = L->look + 1;

    L->buf = (float *)calloc((size_t)L->size, sizeof(float));
    if (!L->buf) return -1;

    /* 一阶低通系数：coef = 1 - exp(-1/(tau*fs)) */
    L->atk = 1.0f - expf(-1.0f / (atk_ms * 0.001f * (float)fs));
    L->rel = 1.0f - expf(-1.0f / (rel_ms * 0.001f * (float)fs));
    L->gain = 1.0f;
    L->gain_min = 1.0f;
    return 0;
}

static float limiter_step(limiter_t *L, float x)
{
    float peak = 0.0f;
    float need, c, y;
    int i, rd;

    L->buf[L->pos] = x;

    /* 窗口峰值 = 待输出样本 + look 个未来样本。
     * O(look) 线性扫描；look 通常 100~500，开销可忽略。
     * 实时实现可用单调队列把均摊代价降到 O(1)。 */
    for (i = 0; i < L->size; i++) {
        float v = fabsf(L->buf[i]);
        if (v > peak) peak = v;
    }

    need = (peak > L->ceiling) ? (L->ceiling / peak) : 1.0f;
    c = (need < L->gain) ? L->atk : L->rel;   /* 降增益快，回升慢 */
    L->gain += c * (need - L->gain);

    rd = (L->pos + 1) % L->size;              /* 最老的样本 = 延迟 look 的那个 */
    y = L->buf[rd] * L->gain;
    L->pos = rd;

    if (L->gain < 0.999f) L->active++;
    L->gain_sum += (double)L->gain;
    if (L->gain < L->gain_min) L->gain_min = L->gain;

    if (fabsf(y) > L->ceiling) {              /* 平滑残差兜底 */
        y = (y > 0.0f) ? L->ceiling : -L->ceiling;
        L->hard_clip++;
    }
    return y;
}

/*
 * [5] 参考链路：float 域累加 + 1/sqrt(N) 预增益 + 前瞻限幅。
 *
 * 两道关键设计与逐样本公式的区别：
 *   1) 累加在 float 域完成，中间结果不受 16-bit 限制，余量充足；
 *   2) 非线性只出现在限幅器，且带时间常数（有"记忆"），
 *      因此能区分"持续很响"和"瞬时峰值"，不会把所有样本一视同仁地压缩。
 */
static long mix_float_limiter(const int16_t *a, const int16_t *b,
                              float *out, int n, void *ctx)
{
    limiter_cfg_t *cfg = (limiter_cfg_t *)ctx;
    limiter_t L;
    float *tmp;
    long hard_clips;
    int i, total;

    cfg->active_samples = 0;
    cfg->hard_clips = 0;
    cfg->gain_sum = 0.0;
    cfg->gain_min = 1.0f;

    if (limiter_init(&L, cfg->ceiling, cfg->look_ms,
                     cfg->atk_ms, cfg->rel_ms, cfg->fs) != 0) {
        memset(out, 0, (size_t)n * sizeof(float));
        return 0;
    }

    total = n + L.look;
    tmp = (float *)malloc((size_t)total * sizeof(float));
    if (!tmp) {
        free(L.buf);
        memset(out, 0, (size_t)n * sizeof(float));
        return 0;
    }

    /* 末尾补 look 个零，把延迟线里剩余的样本冲出来。
     * 若直接零填充尾部，最后 look 个样本会丢，残差指标会被这段静音污染。 */
    for (i = 0; i < total; i++) {
        float s = (i < n)
                ? (float)((int32_t)a[i] + (int32_t)b[i]) / 32768.0f
                : 0.0f;
        tmp[i] = limiter_step(&L, s * cfg->pregain);
    }

    /* 补偿 lookahead 延迟，使输出与其它算法样本对齐 */
    memcpy(out, tmp + L.look, (size_t)n * sizeof(float));

    hard_clips = L.hard_clip;
    cfg->active_samples = L.active;
    cfg->hard_clips = L.hard_clip;
    cfg->gain_sum = L.gain_sum / (double)total;
    cfg->gain_min = L.gain_min;
    free(tmp);
    free(L.buf);
    return hard_clips;
}

/* ============================================================================
 * 3. 客观指标
 * ==========================================================================*/

typedef struct {
    double peak;
    double rms;
    double crest;
    double fit_gain;
    double resid_db;
    double shape_db;      /* 剥离慢变增益后的波形残差 */
    long   clamped;
} metrics_t;

/*
 * ref 为理想线性混音（未做任何处理）。先求最佳线性增益吸收整体音量差，
 * 再计算残差能量比，从而只反映"波形形状"的改变。
 */
static void measure(const float *y, const float *ref, int n, metrics_t *m)
{
    double pk = 0.0, sum2 = 0.0;
    double num = 0.0, den = 0.0;
    double e2 = 0.0, s2 = 0.0;
    int i;

    m->shape_db = -999.0;
    m->clamped = 0;

    for (i = 0; i < n; i++) {
        double v = fabs((double)y[i]);
        if (v > pk) pk = v;
        sum2 += (double)y[i] * y[i];
    }
    m->peak = pk;
    m->rms  = (n > 0) ? sqrt(sum2 / n) : 0.0;
    m->crest = (m->rms > 1e-12) ? 20.0 * log10(m->peak / m->rms) : 0.0;

    for (i = 0; i < n; i++) {
        num += (double)y[i] * (double)ref[i];
        den += (double)ref[i] * (double)ref[i];
    }
    m->fit_gain = (den > 0.0) ? num / den : 0.0;

    for (i = 0; i < n; i++) {
        double e = (double)y[i] - m->fit_gain * (double)ref[i];
        double s = m->fit_gain * (double)ref[i];
        e2 += e * e;
        s2 += s * s;
    }
    m->resid_db = (s2 > 0.0) ? 10.0 * log10((e2 + 1e-30) / (s2 + 1e-30)) : -999.0;
}

/*
 * 剥离慢变增益后的波形残差。
 *
 * 动机：限幅器的输出相对线性混音有两个偏差来源 —— 整体音量（常数增益）
 * 和缓慢的增益骑乘（包络），这两者都不是"失真"；真正的失真只有波形形状
 * 的改变。逐样本公式没有包络，用 resid_db 即可；但要和限幅器公平比较，
 * 必须先把慢变增益除掉。
 *
 * 做法：以 win 个样本为一块做最小二乘增益拟合，块中心之间线性插值成
 * 连续包络 g[i]，再算 ||y - g*ref||^2 / ||g*ref||^2。
 * win 取 10 ms 量级：远慢于削波（逐样本），快于音乐动态。
 */
static double resid_shape(const float *y, const float *ref, int n, int win)
{
    int nblk, b, i;
    double *g;
    double e2 = 0.0, s2 = 0.0;

    if (n <= 0 || win <= 0) return -999.0;
    nblk = (n + win - 1) / win;

    g = (double *)malloc((size_t)nblk * sizeof(double));
    if (!g) return -999.0;

    for (b = 0; b < nblk; b++) {
        int i0 = b * win;
        int i1 = i0 + win;
        double num = 0.0, den = 0.0;

        if (i1 > n) i1 = n;
        for (i = i0; i < i1; i++) {
            num += (double)y[i] * (double)ref[i];
            den += (double)ref[i] * (double)ref[i];
        }
        g[b] = (den > 1e-20) ? num / den : 1.0;
    }

    for (i = 0; i < n; i++) {
        double x = ((double)i - 0.5 * win) / (double)win;  /* 以块中心为整数点 */
        int b0 = (int)floor(x);
        double f = x - (double)b0;
        double gg, e, s;

        if (b0 < 0) { b0 = 0; f = 0.0; }
        if (b0 >= nblk - 1) { b0 = nblk - 1; f = 0.0; }

        gg = (b0 + 1 < nblk) ? g[b0] * (1.0 - f) + g[b0 + 1] * f : g[b0];
        e = (double)y[i] - gg * (double)ref[i];
        s = gg * (double)ref[i];
        e2 += e * e;
        s2 += s * s;
    }

    free(g);
    return (s2 > 0.0) ? 10.0 * log10((e2 + 1e-30) / (s2 + 1e-30)) : -999.0;
}

/* ============================================================================
 * 4. WAV 读写（仅 16-bit PCM）
 * ==========================================================================*/

static uint32_t rd_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void wr_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static int wav_write_mono(const char *path, const int16_t *pcm, int n, int fs)
{
    FILE *f = fopen(path, "wb");
    unsigned char h[44];
    uint32_t data_bytes = (uint32_t)n * 2;
    unsigned char *le;
    int i;

    if (!f) {
        fprintf(stderr, "无法写入 %s\n", path);
        return -1;
    }

    memcpy(h, "RIFF", 4);
    wr_u32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    wr_u32(h + 16, 16);              /* fmt chunk size */
    wr_u16(h + 20, 1);               /* PCM */
    wr_u16(h + 22, 1);               /* mono */
    wr_u32(h + 24, (uint32_t)fs);
    wr_u32(h + 28, (uint32_t)fs * 2);/* byte rate */
    wr_u16(h + 32, 2);               /* block align */
    wr_u16(h + 34, 16);              /* bits */
    memcpy(h + 36, "data", 4);
    wr_u32(h + 40, data_bytes);

    if (fwrite(h, 1, 44, f) != 44) { fclose(f); return -1; }

    /* 按小端序写出，不依赖主机字节序 */
    le = (unsigned char *)malloc((size_t)n * 2);
    if (!le) { fclose(f); return -1; }
    for (i = 0; i < n; i++) {
        uint16_t u = (uint16_t)pcm[i];
        le[2 * i]     = (unsigned char)(u & 0xFF);
        le[2 * i + 1] = (unsigned char)((u >> 8) & 0xFF);
    }
    fwrite(le, 1, (size_t)n * 2, f);
    free(le);

    fclose(f);
    return 0;
}

typedef struct {
    int16_t *pcm;
    int      n;
    int      channels;
    int      rate;
} wav_t;

static int wav_read_mono(const char *path, wav_t *w)
{
    FILE *f;
    unsigned char hdr[12];
    unsigned char ck[8];
    int tag = 0, ch = 0, rate = 0, bits = 0;
    long data_off = -1;
    uint32_t data_len = 0;
    int16_t *raw = NULL;
    int i, frames;

    memset(w, 0, sizeof(*w));
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "无法打开 %s\n", path); return -1; }

    if (fread(hdr, 1, 12, f) != 12 ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "%s 不是 RIFF/WAVE 文件\n", path);
        fclose(f);
        return -1;
    }

    for (;;) {
        uint32_t sz;
        if (fread(ck, 1, 8, f) != 8) break;
        sz = rd_u32(ck + 4);

        if (memcmp(ck, "fmt ", 4) == 0) {
            unsigned char b[16];
            if (fread(b, 1, 16, f) != 16) break;
            tag  = rd_u16(b);
            ch   = rd_u16(b + 2);
            rate = (int)rd_u32(b + 4);
            bits = rd_u16(b + 14);
            if (sz > 16) fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR);
        } else if (memcmp(ck, "data", 4) == 0) {
            data_off = ftell(f);
            data_len = sz;
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        } else {
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        }
    }

    if (tag != 1 || bits != 16 || (ch != 1 && ch != 2) || data_off < 0 || data_len == 0) {
        fprintf(stderr, "%s: 仅支持 16-bit PCM 单/双声道\n", path);
        fclose(f);
        return -1;
    }

    frames = (int)(data_len / (uint32_t)(2 * ch));
    raw = (int16_t *)malloc((size_t)frames * ch * sizeof(int16_t));
    w->pcm = (int16_t *)malloc((size_t)frames * sizeof(int16_t));
    if (!raw || !w->pcm) {
        fprintf(stderr, "内存不足\n");
        free(raw); free(w->pcm); w->pcm = NULL;
        fclose(f);
        return -1;
    }

    fseek(f, data_off, SEEK_SET);
    if (fread(raw, (size_t)2 * ch, (size_t)frames, f) != (size_t)frames) {
        fprintf(stderr, "%s: 数据段长度不足，可能已损坏\n", path);
        free(raw); free(w->pcm); w->pcm = NULL;
        fclose(f);
        return -1;
    }
    fclose(f);

    if (ch == 1) {
        memcpy(w->pcm, raw, (size_t)frames * sizeof(int16_t));
    } else {
        for (i = 0; i < frames; i++) {
            int32_t m = ((int32_t)raw[2 * i] + (int32_t)raw[2 * i + 1]) / 2;
            w->pcm[i] = (int16_t)m;
        }
    }

    free(raw);
    w->n = frames;
    w->channels = ch;
    w->rate = rate;
    return 0;
}

/* ============================================================================
 * 5. 合成素材
 *
 * 两路信号刻意做成不同音色、不同节奏，且电平足够高，使相加后间歇性过载。
 * 纯音 + 包络，不加噪声，以免噪声掩盖失真。
 * ==========================================================================*/

static void synth_voice(float *x, int n, int fs)
{
    const double f0 = 182.0;
    double hg[8];
    double ph = 0.0;
    int i, h;

    for (h = 1; h <= 7; h++) hg[h] = 1.0 / pow((double)h, 1.2);

    for (i = 0; i < n; i++) {
        double t = (double)i / fs;
        double f = f0 * (1.0 + 0.014 * sin(2.0 * PI * 5.1 * t));
        double v = 0.0;
        double syl, p, gate;

        ph += 2.0 * PI * f / fs;
        if (ph > 2.0 * PI) ph -= 2.0 * PI;

        for (h = 1; h <= 7; h++) v += hg[h] * sin((double)h * ph);

        syl = 0.5 - 0.5 * cos(2.0 * PI * 3.3 * t);
        syl = syl * syl;

        p = fmod(t, 2.4);
        gate = smoothstep(0.0, 0.15, p) * (1.0 - smoothstep(1.75, 1.95, p));

        x[i] = (float)(v * syl * gate);
    }
}

static void synth_pad(float *x, int n, int fs)
{
    static const double notes[3] = { 261.63, 329.63, 392.00 };
    double ph[3][3];
    int i, k, h;

    memset(ph, 0, sizeof(ph));

    for (i = 0; i < n; i++) {
        double t = (double)i / fs;
        double acc = 0.0;
        double trem, p, gate;

        for (k = 0; k < 3; k++) {
            for (h = 1; h <= 3; h++) {
                ph[k][h - 1] += 2.0 * PI * notes[k] * (double)h / fs;
                if (ph[k][h - 1] > 2.0 * PI) ph[k][h - 1] -= 2.0 * PI;
                acc += sin(ph[k][h - 1]) / (3.0 * (double)h);
            }
        }

        trem = 1.0 - 0.35 * (0.5 + 0.5 * sin(2.0 * PI * 0.55 * t));

        p = fmod(t, 3.2);
        gate = smoothstep(0.0, 0.35, p) * (1.0 - smoothstep(2.5, 2.9, p));

        x[i] = (float)(acc * trem * gate);
    }
}

/* 把单路缩放到指定峰值 */
static void normalize_peak(float *x, int n, double target)
{
    double pk = buf_peak(x, n);
    if (pk > 1e-9) buf_scale(x, n, (float)(target / pk));
}

/* 1 kHz + 1.5 kHz，各自幅度由 level_db 指定 */
static void make_two_tone(int16_t *a, int16_t *b, int n, int fs, double level_db)
{
    double amp = pow(10.0, level_db / 20.0) * 32767.0;
    int i;
    for (i = 0; i < n; i++) {
        double t = (double)i / fs;
        double va = amp * sin(2.0 * PI * 1000.0 * t);
        double vb = amp * sin(2.0 * PI * 1500.0 * t);
        /* 浮点结果可能略超 32767，直接强制转换是未定义行为 */
        if (va > 32767.0) va = 32767.0;
        if (va < -32768.0) va = -32768.0;
        if (vb > 32767.0) vb = 32767.0;
        if (vb < -32768.0) vb = -32768.0;
        a[i] = (int16_t)va;
        b[i] = (int16_t)vb;
    }
}

/* ============================================================================
 * 6. 表格输出（按显示宽度对齐，中文占 2 列）
 * ==========================================================================*/

static int disp_width(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    int w = 0;

    while (*p) {
        unsigned cp;
        int len, i;

        if (*p < 0x80)                { cp = *p;          len = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F;   len = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F;   len = 3; }
        else                          { cp = *p & 0x07;   len = 4; }

        for (i = 1; i < len && p[i]; i++) cp = (cp << 6) | (p[i] & 0x3F);
        p += len;

        w += (cp >= 0x2E80) ? 2 : 1;   /* CJK 及全角区按 2 列 */
    }
    return w;
}

static void print_padded(const char *s, int width)
{
    int w = disp_width(s);
    fputs(s, stdout);
    while (w++ < width) putchar(' ');
}

static void print_right(const char *s, int width)
{
    int w = disp_width(s);
    while (w++ < width) putchar(' ');
    fputs(s, stdout);
}

static void fmt_db(char *buf, size_t sz, double db)
{
    (void)sz;
    if (db < -200.0) sprintf(buf, "-inf");
    else             sprintf(buf, "%.1f", db);
}

static void join_path(char *dst, size_t sz, const char *dir, const char *name)
{
    snprintf(dst, sz, "%s/%s", dir, name);
}

/* ============================================================================
 * 7. 主流程
 * ==========================================================================*/

typedef long (*mix_fn)(const int16_t *, const int16_t *, float *, int, void *);

typedef struct {
    const char *name;
    const char *file;
    mix_fn      fn;
    void       *ctx;
    metrics_t   prog;        /* 节目素材指标（响度对齐后） */
    double      match_db;    /* 响度对齐施加的增益 */
    double      imd_db;      /* 过载双音残差 */
} algo_t;

static void print_usage(const char *prog)
{
    printf("用法: %s [选项] [源1.wav 源2.wav]\n\n", prog);
    printf("  --rate N       采样率，默认 48000\n");
    printf("  --sec  S       合成素材时长（秒），默认 8\n");
    printf("  --outdir DIR   输出目录，默认 audio_mix_out\n");
    printf("  --match DB     响度对齐目标，默认 -18 dBFS（只衰减不提升）\n");
    printf("  --no-match     关闭响度对齐\n");
    printf("  --help         显示本帮助\n\n");
    printf("不给 WAV 时使用内置合成素材。\n");
}

int main(int argc, char **argv)
{
    int fs = 48000;
    double seconds = 8.0;
    int match_on = 1;
    double match_db = -18.0;
    const char *outdir = "audio_mix_out";
    const char *in1 = NULL, *in2 = NULL;
    int i, n, nalgo;
    long lim_hard_prog = 0;
    double lim_avg_gain = 1.0, lim_min_gain = 1.0;
    int16_t *src_a = NULL, *src_b = NULL;
    float *ref = NULL, *mixbuf = NULL;
    int16_t *pcm = NULL;
    char path[512];
    limiter_cfg_t lim;
    algo_t algos[6];

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--rate") && i + 1 < argc) {
            fs = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--sec") && i + 1 < argc) {
            seconds = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--outdir") && i + 1 < argc) {
            outdir = argv[++i];
        } else if (!strcmp(argv[i], "--match") && i + 1 < argc) {
            match_db = atof(argv[++i]);
            match_on = 1;
        } else if (!strcmp(argv[i], "--no-match")) {
            match_on = 0;
        } else if (argv[i][0] != '-') {
            if (!in1) in1 = argv[i];
            else if (!in2) in2 = argv[i];
        }
    }

    if (fs < 8000 || fs > 192000) { fprintf(stderr, "采样率不合法\n"); return 1; }
    if (seconds < 0.5 || seconds > 600.0) { fprintf(stderr, "时长不合法\n"); return 1; }
    if (in1 && !in2) {
        fprintf(stderr, "需要两路 WAV；或都不给，使用合成素材\n");
        return 1;
    }

    MAKE_DIR(outdir);

    /* ---- 素材 ---- */
    if (in1 && in2) {
        wav_t wa, wb;
        if (wav_read_mono(in1, &wa) != 0) return 1;
        if (wav_read_mono(in2, &wb) != 0) { free(wa.pcm); return 1; }
        if (wa.rate != wb.rate) {
            fprintf(stderr, "两路采样率不同（%d / %d），请先重采样\n", wa.rate, wb.rate);
            free(wa.pcm); free(wb.pcm);
            return 1;
        }
        fs = wa.rate;
        n = wa.n < wb.n ? wa.n : wb.n;
        src_a = wa.pcm;
        src_b = wb.pcm;
        printf("素材: %s + %s（%d Hz, %.2f s, 取较短长度）\n",
               in1, in2, fs, (double)n / fs);
    } else {
        float *fa, *fb;
        n = (int)(seconds * fs);
        src_a = (int16_t *)malloc((size_t)n * sizeof(int16_t));
        src_b = (int16_t *)malloc((size_t)n * sizeof(int16_t));
        fa = (float *)malloc((size_t)n * sizeof(float));
        fb = (float *)malloc((size_t)n * sizeof(float));
        if (!src_a || !src_b || !fa || !fb) { fprintf(stderr, "内存不足\n"); return 1; }

        synth_voice(fa, n, fs);
        synth_pad(fb, n, fs);
        /* 每路各自满而不削，叠加后自然过载（约 +4~5 dBFS），
         * 这是真实混音里最常见的"总线推爆"场景。 */
        normalize_peak(fa, n, 0.95);
        normalize_peak(fb, n, 0.95);

        for (i = 0; i < n; i++) {
            src_a[i] = to_pcm16(fa[i]);
            src_b[i] = to_pcm16(fb[i]);
        }

        join_path(path, sizeof(path), outdir, "00_src_a.wav");
        wav_write_mono(path, src_a, n, fs);
        join_path(path, sizeof(path), outdir, "00_src_b.wav");
        wav_write_mono(path, src_b, n, fs);

        printf("素材: 合成（2 路, %d Hz, %.1f s）\n", fs, seconds);
        printf("      A 峰值 %.2f  B 峰值 %.2f  叠加峰值 %.2f（%+.1f dBFS）\n",
               buf_peak(fa, n), buf_peak(fb, n),
               buf_peak_sum(fa, fb, n), to_db(buf_peak_sum(fa, fb, n)));
        free(fa); free(fb);
    }

    printf("响度对齐: %s", match_on ? "开启" : "关闭");
    if (match_on) printf("（目标 %.0f dBFS，只衰减不提升）", match_db);
    printf("\n输出目录: %s\n\n", outdir);

    /* ---- 公共缓冲 ---- */
    ref    = (float *)malloc((size_t)n * sizeof(float));
    mixbuf = (float *)malloc((size_t)n * sizeof(float));
    pcm    = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    if (!ref || !mixbuf || !pcm) { fprintf(stderr, "内存不足\n"); return 1; }

    for (i = 0; i < n; i++)
        ref[i] = (float)((int32_t)src_a[i] + (int32_t)src_b[i]) / 32768.0f;

    /* ---- 算法表 ---- */
    memset(&lim, 0, sizeof(lim));
    lim.ceiling = 0.891251f;   /* -1 dBFS */
    lim.look_ms = 5.0f;
    lim.atk_ms  = 1.0f;
    lim.rel_ms  = 80.0f;
    lim.pregain = 0.70710678f; /* 1/sqrt(2) */
    lim.fs      = fs;

    algos[0].name = "线性相加 + 饱和";        algos[0].file = "01_sum_clip.wav";
    algos[0].fn = mix_audio_simple;          algos[0].ctx = NULL;
    algos[1].name = "平均 1/2";               algos[1].file = "02_average.wav";
    algos[1].fn = mix_audio_average;          algos[1].ctx = NULL;
    algos[2].name = "防削波 a+b-ab/M";        algos[2].file = "03_noclip.wav";
    algos[2].fn = mix_audio_no_clipping;      algos[2].ctx = NULL;
    algos[3].name = "contra (a^2+b^2)/(a+b)"; algos[3].file = "04_contra.wav";
    algos[3].fn = mix_audio_your_formula;     algos[3].ctx = NULL;
    algos[4].name = "tanh 软饱和";            algos[4].file = "05_tanh_sat.wav";
    algos[4].fn = mix_audio_tanh_sat;         algos[4].ctx = NULL;
    algos[5].name = "float + 前瞻限幅";       algos[5].file = "06_float_limiter.wav";
    algos[5].fn = mix_float_limiter;          algos[5].ctx = &lim;
    nalgo = 6;

    /* ---- 逐条链路：处理 -> 响度对齐 -> 度量 -> 写 WAV ---- */
    for (i = 0; i < nalgo; i++) {
        long clamped = algos[i].fn(src_a, src_b, mixbuf, n, algos[i].ctx);
        double rms = buf_rms(mixbuf, n);
        float g = 1.0f;
        int k;

        if (match_on && rms > 1e-9) {
            g = (float)(pow(10.0, match_db / 20.0) / rms);
            if (g > 1.0f) g = 1.0f;          /* 只衰减，避免重新削波 */
        }
        if (g != 1.0f) buf_scale(mixbuf, n, g);

        algos[i].match_db = 20.0 * log10((double)g);
        measure(mixbuf, ref, n, &algos[i].prog);
        algos[i].prog.clamped  = clamped;
        algos[i].prog.shape_db = resid_shape(mixbuf, ref, n, fs / 100);

        join_path(path, sizeof(path), outdir, algos[i].file);
        for (k = 0; k < n; k++) pcm[k] = to_pcm16(mixbuf[k]);
        wav_write_mono(path, pcm, n, fs);
    }

    /* 保存节目素材的限幅器统计，避免被后面的双音测试覆盖 */
    lim_hard_prog   = lim.hard_clips;
    lim_avg_gain    = lim.gain_sum;
    lim_min_gain    = lim.gain_min;

    /* ---- 过载双音测试：单音 0 dBFS，合成峰值 +6 dBFS ---- */
    {
        int tn = fs;                 /* 1 秒足够 */
        int16_t *ta = (int16_t *)malloc((size_t)tn * sizeof(int16_t));
        int16_t *tb = (int16_t *)malloc((size_t)tn * sizeof(int16_t));
        float *tref = (float *)malloc((size_t)tn * sizeof(float));
        float *tout = (float *)malloc((size_t)tn * sizeof(float));

        if (ta && tb && tref && tout) {
            int k;
            make_two_tone(ta, tb, tn, fs, 0.0);
            for (k = 0; k < tn; k++)
                tref[k] = (float)((int32_t)ta[k] + (int32_t)tb[k]) / 32768.0f;

            for (k = 0; k < nalgo; k++) {
                metrics_t m;
                algos[k].fn(ta, tb, tout, tn, algos[k].ctx);
                measure(tout, tref, tn, &m);
                algos[k].imd_db = m.resid_db;
            }
        }
        free(ta); free(tb); free(tref); free(tout);
    }

    /* ---- 输出表格 ---- */
    print_padded("方案", 22);
    print_right("峰值dBFS", 9);
    print_right("RMSdBFS", 8);
    print_right("波峰因数", 9);
    print_right("钳位", 9);
    print_right("匹配增益", 9);
    print_right("波形残差dB", 12);
    print_right("互调残差dB", 12);
    putchar('\n');
    printf("------------------------------------------------------------------------------------------\n");
    for (i = 0; i < nalgo; i++) {
        char s1[32], s2[32];
        fmt_db(s1, sizeof(s1), algos[i].prog.shape_db);
        fmt_db(s2, sizeof(s2), algos[i].imd_db);

        print_padded(algos[i].name, 22);
        printf("%9.1f%8.1f%9.1f%9ld%9.1f%12s%12s\n",
               to_db(algos[i].prog.peak),
               to_db(algos[i].prog.rms),
               algos[i].prog.crest,
               algos[i].prog.clamped,
               algos[i].match_db,
               s1, s2);
    }
    printf("------------------------------------------------------------------------------------------\n");
    printf("限幅器: 平均增益 %.1f dB, 最小增益 %.1f dB, 安全钳位 %ld, 前瞻 %.1f ms, 起控 %.1f ms, 释放 %.0f ms\n",
           to_db(lim_avg_gain), to_db(lim_min_gain), lim_hard_prog,
           lim.look_ms, lim.atk_ms, lim.rel_ms);

    printf("\n指标说明:\n");
    printf("  匹配增益   = 为对齐响度施加的增益；0 dB 表示该算法本身已低于目标电平。\n");
    printf("  波形残差   = 节目素材上、剥离 10 ms 尺度慢变增益后的残差。只反映波形形状改变，\n");
    printf("               因此限幅器与逐样本公式可以公平比较。-inf 表示完全线性。\n");
    printf("  互调残差   = 1 kHz + 1.5 kHz 各 0 dBFS（合成峰值 +6 dBFS）下的同一指标，不含增益剥离。\n");
    printf("  平均法的残差（约 -86 / -95 dB）来自整数除法向零截断，非算法缺陷；\n");
    printf("  改用 (a+b+1)>>1 可消除。\n");
    printf("  限幅器改变的是电平，不是波形：互调残差极低；削波改变的是波形，残差高且不可逆。\n");
    printf("  响度已对齐，WAV 之间的差别即为失真、动态与音色。\n");
    printf("\n生成的 WAV:\n");
    if (!in1) printf("  %s/00_src_a.wav, 00_src_b.wav   两路原始素材\n", outdir);
    for (i = 0; i < nalgo; i++) printf("  %s/%s\n", outdir, algos[i].file);

    free(src_a); free(src_b);
    free(ref); free(mixbuf); free(pcm);
    return 0;
}
