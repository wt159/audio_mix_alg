/*
 * truepeak.c — 真峰值测量与真峰值前瞻限幅（ITU-R BS.1770-4 Annex 2）
 * =============================================================================
 *
 * 检测链（BS.1770-4 Annex 2 §3）：
 *   ① 12.04 dB 衰减 —— 仅整数算法需要余量；本实现 float 域，Annex 2 §3
 *      原文 "This step is not necessary if the calculations are performed in
 *      floating point"，省略（其后 ⑤ 的 +12.04 dB 补偿同步省略）。
 *   ② 4× 过采样：插入 3 个零样本，48 kHz→192 kHz；
 *   ③ 48 阶 4 相位 FIR 插值低通（系数表逐字录入自 Annex 2 §3，见下）；
 *   ④ 取绝对值；
 *   ⑤ dBTP = 20·log10(过采样后峰值)（+12.04 dB 补偿已随 ① 省略）。
 *
 * 相位约定：每输入样本 x[i] 输出 4 个过采样点 y[4i..4i+3]，
 *   y[4i+p] = Σ_t coef[p][t] · x[i−t]，t = 0..11（t=0 为最新样本）。
 * 各相位直流增益 ≈ 1（1.0016 / 0.9730），滤波器输出与输入同量纲——
 * 这正是 Annex 2 "normalises the overall gain of the processing to unity" 的含义。
 *
 * 限幅链路 = src/limiter.c 的前瞻结构 + 本文件真峰值检测驱动的窗口峰值：
 *   延迟线保存 look 个未来样本 -> 窗口内"真峰值"（每样本 4 个过采样点中的
 *   最大 |值|）取峰值 -> 目标增益 -> 一阶起控/释放平滑 -> 乘到延迟样本上。
 * ceiling 语义 = EBU R128-2023 考虑条款 m)：制作阶段节目真峰值 ≤ −1 dBTP。
 *
 * 系数表来源（逐字录入，含负号；空白格即 0 系数已按列对齐核对）：
 *   ITU-R BS.1770-4 (10/2015) Annex 2 §3, Table "order 48, 4-phase, FIR
 *   interpolating" filter coefficients
 *   https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-4-201510-S!!PDF-E.pdf
 *   （第 16 页，Phase 0–3 列，12 行）
 *   交叉核对：broadcast-loudness 0.7.0 true_peak.rs FIR_COEFFS（BS.1770-5 同表）
 *   https://docs.rs/broadcast-loudness/latest/src/broadcast_loudness/true_peak.rs.html
 * =============================================================================
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "truepeak.h"

/* BS.1770-4 Annex 2 §3：48 阶 4 相位 FIR 插值系数，Phase 0..3 各 12 系数。
 * coef[p][t]：相位 p（输出时刻 i+p/4）、抽头 t（t=0 最新输入样本）。 */
static const double tp_coef[4][12] = {
    { /* Phase 0 */
        0.0017089843750,  0.0109863281250, -0.0196533203125,
        0.0332031250000, -0.0594482421875,  0.1373291015625,
        0.9721679687500, -0.1022949218750,  0.0476074218750,
       -0.0266113281250,  0.0148925781250, -0.0083007812500 },
    { /* Phase 1 */
       -0.0291748046875,  0.0292968750000, -0.0517578125000,
        0.0891113281250, -0.1665039062500,  0.4650878906250,
        0.7797851562500, -0.2003173828125,  0.1015625000000,
       -0.0582275390625,  0.0330810546875, -0.0189208984375 },
    { /* Phase 2 */
       -0.0189208984375,  0.0330810546875, -0.0582275390625,
        0.1015625000000, -0.2003173828125,  0.7797851562500,
        0.4650878906250, -0.1665039062500,  0.0891113281250,
       -0.0517578125000,  0.0292968750000, -0.0291748046875 },
    { /* Phase 3 */
       -0.0083007812500,  0.0148925781250, -0.0266113281250,
        0.0476074218750, -0.1022949218750,  0.9721679687500,
        0.1373291015625, -0.0594482421875,  0.0332031250000,
       -0.0196533203125,  0.0109863281250,  0.0017089843750 },
};

/* 4× 过采样 FIR：输入最新样本 v，与 11 个历史样本（hist[t-1] = x[i-t]）一起
 * 产生 4 个过采样点，返回其中的最大绝对值（该样本的真峰值贡献）。 */
static double tp_oversample_peak(const double *hist, double v)
{
    double peak = 0.0;
    int p, t;
    for (p = 0; p < 4; p++) {
        double acc = tp_coef[p][0] * v;
        for (t = 1; t < 12; t++)
            acc += tp_coef[p][t] * hist[t - 1];
        if (acc < 0.0) acc = -acc;
        if (acc > peak) peak = acc;
    }
    return peak;
}

/* ============================================================================
 * 真峰值测量（无状态，指标列复用）
 * ==========================================================================*/
double truepeak_dbtp(const float *x, int n)
{
    double hist[11] = {0};
    double peak = 0.0;
    int i;
    if (!x || n <= 0) return -999.0;
    for (i = 0; i < n; i++) {
        double tp = tp_oversample_peak(hist, (double)x[i]);
        if (tp > peak) peak = tp;
        memmove(hist + 1, hist, 10 * sizeof(double));
        hist[0] = (double)x[i];
    }
    return (peak > 1e-12) ? 20.0 * log10(peak) : -999.0;
}

/* ============================================================================
 * 真峰值驱动的 lookahead 限幅器（内部状态，不对外暴露）
 * ==========================================================================*/
typedef struct {
    float ceiling;
    int   look;
    int   size;
    float atk;
    float rel;
    float *buf;      /* 延迟线：look+1 个样本，与 limiter.c 相同 */
    double *tpbuf;   /* 每个延迟样本的 4 相位过采样峰值，供窗口峰值扫描 */
    double hist[11]; /* FIR 历史（11 个已输入样本） */
    int   pos;
    float gain;
    long  active;
    long  hard_clip;
    double gain_sum;
    float gain_min;
} tp_limiter_t;

static int tp_limiter_init(tp_limiter_t *L, float ceiling, float look_ms,
                           float atk_ms, float rel_ms, int fs)
{
    memset(L, 0, sizeof(*L));
    L->ceiling = ceiling;
    L->look = (int)(look_ms * 0.001f * (float)fs + 0.5f);
    if (L->look < 1) L->look = 1;
    L->size = L->look + 1;

    L->buf = (float *)calloc((size_t)L->size, sizeof(float));
    L->tpbuf = (double *)calloc((size_t)L->size, sizeof(double));
    if (!L->buf || !L->tpbuf) {
        free(L->buf); free(L->tpbuf);
        L->buf = NULL; L->tpbuf = NULL;
        return -1;
    }
    L->atk = 1.0f - expf(-1.0f / (atk_ms * 0.001f * (float)fs));
    L->rel = 1.0f - expf(-1.0f / (rel_ms * 0.001f * (float)fs));
    L->gain = 1.0f;
    L->gain_min = 1.0f;
    return 0;
}

static void tp_limiter_free(tp_limiter_t *L)
{
    free(L->buf);
    free(L->tpbuf);
    L->buf = NULL;
    L->tpbuf = NULL;
}

/* 前进一步：输入样本 x，返回延迟 look 后、施加增益的样本。
 * 窗口峰值扫描的是各延迟位置上的"真峰值"（过采样后 4 点最大 |·|），
 * 因为乘上增益后各过采样点同乘 g，样本间峰同样被压到 ceiling 之下。 */
static float tp_limiter_step(tp_limiter_t *L, float x)
{
    double peak = 0.0;
    float need, c, y;
    int i, rd;

    L->buf[L->pos] = x;
    L->tpbuf[L->pos] = tp_oversample_peak(L->hist, (double)x);

    /* 更新 FIR 历史（在读取窗口之前，使当前样本参与其自身的过采样） */
    memmove(L->hist + 1, L->hist, 10 * sizeof(double));
    L->hist[0] = (double)x;

    for (i = 0; i < L->size; i++) {
        double v = L->tpbuf[i];
        if (v > peak) peak = v;
    }

    need = (peak > L->ceiling) ? (float)(L->ceiling / peak) : 1.0f;
    c = (need < L->gain) ? L->atk : L->rel;      /* 降增益快，回升慢 */
    L->gain += c * (need - L->gain);

    rd = (L->pos + 1) % L->size;                 /* 最老的样本 = 延迟 look 的那个 */
    y = L->buf[rd] * L->gain;
    L->pos = rd;

    if (L->gain < 0.999f) L->active++;
    L->gain_sum += (double)L->gain;
    if (L->gain < L->gain_min) L->gain_min = L->gain;

    /* 安全钳位兜底：限制最终样本幅度（平滑残差与 FIR 欠读兜底） */
    if (fabsf(y) > L->ceiling) {
        y = (y > 0.0f) ? L->ceiling : -L->ceiling;
        L->hard_clip++;
    }
    return y;
}

/*
 * [9] 真峰值前瞻限幅：float 域累加 + 1/sqrt(N) 预增益 + dBTP 驱动的限幅。
 *
 * 与 [5] mix_float_limiter 的差别只在窗口峰值检测：那里看采样点绝对值，
 * 这里看 4× 过采样后的样本间峰（重建峰），因此输出不但在采样点不越
 * ceiling，DAC sinc 重建后也不会越（欠读 ≤0.688 dB 已由 ceiling 的
 * −1 dBTP + 安全钳位双重余量覆盖）。返回安全钳位样本数。
 */
long mix_truepeak_limiter(const int16_t *a, const int16_t *b,
                          float *out, int n, void *ctx)
{
    truepeak_cfg_t *cfg = (truepeak_cfg_t *)ctx;
    tp_limiter_t L;
    float *tmp;
    long hard_clips;
    int i, total;

    cfg->active_samples = 0;
    cfg->hard_clips = 0;
    cfg->gain_sum = 0.0;
    cfg->gain_min = 1.0f;

    if (tp_limiter_init(&L, cfg->ceiling, cfg->look_ms,
                        cfg->atk_ms, cfg->rel_ms, cfg->fs) != 0) {
        memset(out, 0, (size_t)n * sizeof(float));
        return 0;
    }

    total = n + L.look;
    tmp = (float *)malloc((size_t)total * sizeof(float));
    if (!tmp) {
        tp_limiter_free(&L);
        memset(out, 0, (size_t)n * sizeof(float));
        return 0;
    }

    /* 末尾补 look 个零，把延迟线里剩余样本冲出（同 limiter.c） */
    for (i = 0; i < total; i++) {
        float s = (i < n)
                ? (float)((int32_t)a[i] + (int32_t)b[i]) / 32768.0f
                : 0.0f;
        tmp[i] = tp_limiter_step(&L, s * cfg->pregain);
    }

    /* 补偿 lookahead 延迟，使输出与其它算法样本对齐 */
    memcpy(out, tmp + L.look, (size_t)n * sizeof(float));

    hard_clips = L.hard_clip;
    cfg->active_samples = L.active;
    cfg->hard_clips = L.hard_clip;
    cfg->gain_sum = L.gain_sum / (double)total;
    cfg->gain_min = L.gain_min;
    free(tmp);
    tp_limiter_free(&L);
    return hard_clips;
}
