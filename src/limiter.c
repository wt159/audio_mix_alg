/*
 * limiter.c — 前瞻限幅器（lookahead limiter）
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
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "limiter.h"

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
long mix_float_limiter(const int16_t *a, const int16_t *b,
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
