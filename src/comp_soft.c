/*
 * comp_soft.c — 软拐点压缩器（feedforward soft-knee compressor）
 *
 * 出处：D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range
 * Compressor Design—A Tutorial and Analysis", J. Audio Eng. Soc. 60(6),
 * 2012 June, pp. 399–408。逐条对应：
 *
 *   增益计算机（§2.2 Eq (4)，软拐点三分支抛物线）：
 *     yG = xG                                     2(xG - T) <  -W
 *     yG = xG + (1/R - 1)·(xG - T + W/2)^2 / (2W) |xG - T| <= W/2
 *     yG = T + (xG - T)/R                         2(xG - T) >   W
 *     W=0 时退化为硬拐点 Eq (3)。各分支均满足 yG <= xG，故 cdB = yG - xG
 *     <= 0，增益只衰减不放大。
 *
 *   检测器（§2.3.3 Eq (15)，branching peak detector）：
 *     yL[n] = αA·yL[n-1] + (1-αA)·xL[n]    xL[n] >  yL[n-1]  （起控支路）
 *     yL[n] = αR·yL[n-1]                   xL[n] <= yL[n-1]  （释放支路）
 *     释放支路只对状态放电，保证实测释放时间 = τR（Eq (16) 才是带
 *     (1-αR)·xL[n] 项的变体，此处不用）。
 *
 *   域：xL = |x[n]|，检测器在线性域、dB 变换之前（论文 §2.4.1 Eq (20)
 *   推荐位置）；随后 xG = 20·log10(yL)，cdB = yG - xG。
 *
 *   α 系数（§2.3 Eq (5)(7)）：一阶滤波 s[n] = α·s[n-1] + (1-α)·r[n]，
 *   α = exp(-1/(tau·fs))。
 *
 * 结构（学 limiter.c 的状态藏 ctx 模式）：
 *   累加 float 域 -> pregain -> 分支峰值检测（线性域）-> dB 变换 ->
 *   软拐点增益计算机 -> 线性域增益 -> 乘回样本。T 直接定义在叠加信号的
 *   dBFS 上，无任何检测域缩放。
 *
 * 无前瞻：限幅器需要 lookahead 是因为等检测到过载再降增益，峰已经
 * 过去了；压缩器的起控平滑本身就是设计目标（Eq (15) 起控支路即平滑
 * 器，10 ms 起控使增益在峰附近渐入），瞬态残余由末尾的安全钳位兜底
 * （统计在 hard_clips）。因此本链路无延迟线，输出与其它算法样本对齐。
 */

#include <string.h>
#include <stdint.h>
#include <math.h>

#include "comp_soft.h"

typedef struct {
    float threshold_db;
    float ratio;
    float knee_db;
    float alpha_a;      /* 起控支路系数，αA = exp(-1/(τA·fs)) */
    float alpha_r;      /* 释放支路系数，αR = exp(-1/(τR·fs)) */
    float env;          /* 检测器状态 yL[n-1]（线性域） */
    long  active;
    long  hard_clip;
    double gain_sum;
    float gain_min;
} comp_soft_t;

static void comp_soft_init(comp_soft_t *C, const comp_soft_cfg_t *cfg)
{
    memset(C, 0, sizeof(*C));
    C->threshold_db = cfg->threshold_db;
    C->ratio        = cfg->ratio;
    C->knee_db      = cfg->knee_db;

    C->alpha_a = expf(-1.0f / (cfg->atk_ms * 0.001f * (float)cfg->fs));
    C->alpha_r = expf(-1.0f / (cfg->rel_ms * 0.001f * (float)cfg->fs));

    C->env      = 0.0f;
    C->gain_min = 1.0f;
}

/* 软拐点增益计算机 Eq (4)：输入检测电平 xG（dB），返回增益量（dB，<= 0） */
static float soft_knee_gain_db(const comp_soft_t *C, float xg)
{
    float over = 2.0f * (xg - C->threshold_db);   /* 2(xG - T) */
    float z;

    if (C->knee_db > 0.0f) {
        if (over < -C->knee_db) {
            return 0.0f;                          /* 低于拐点：单位增益 */
        }
        if (over <= C->knee_db) {
            z = xg - C->threshold_db + C->knee_db * 0.5f;
            return (1.0f / C->ratio - 1.0f) * z * z / (2.0f * C->knee_db);
        }
    } else if (xg <= C->threshold_db) {           /* W=0：硬拐点 */
        return 0.0f;
    }
    /* 过拐点区 / 硬拐点过阈值：cdB = (T - xG)(1 - 1/R) */
    return (C->threshold_db - xg) * (1.0f - 1.0f / C->ratio);
}

/*
 * [8] 软拐点压缩器：float 累加 + 分支峰值检测 + 软拐点增益计算机。
 *
 * 与 float+前瞻限幅的分工：限幅器保护峰值（硬拐点、快速、前瞻）；
 * 压缩器调动态（软拐点、慢起释、无前瞻）。两者同属动态链路，可在
 * 仓库的慢变增益剥离残差指标下公平对照。
 */
long mix_soft_knee(const int16_t *a, const int16_t *b,
                   float *out, int n, void *ctx)
{
    comp_soft_cfg_t *cfg = (comp_soft_cfg_t *)ctx;
    comp_soft_t C;
    int i;

    cfg->active_samples = 0;
    cfg->hard_clips = 0;
    cfg->gain_sum = 0.0;
    cfg->gain_min = 1.0f;

    comp_soft_init(&C, cfg);

    for (i = 0; i < n; i++) {
        float x, xl, xg, g_db, g, y;

        x = (float)((int32_t)a[i] + (int32_t)b[i]) / 32768.0f * cfg->pregain;

        /* 检测：线性域分支峰值检测器 Eq (15) */
        xl = fabsf(x);
        if (xl < 1e-9f) xl = 1e-9f;               /* 静音期 log10 的下限保护 */
        if (xl > C.env) {
            C.env = C.alpha_a * C.env + (1.0f - C.alpha_a) * xl;
        } else {
            C.env = C.alpha_r * C.env;
        }

        /* 增益计算机 Eq (4)：xG = 20·log10(yL)，cdB = yG - xG */
        xg = 20.0f * log10f(C.env);
        g_db = soft_knee_gain_db(&C, xg);
        if (g_db > 0.0f) g_db = 0.0f;             /* 只衰减不放大（门槛条款） */
        g = powf(10.0f, g_db * 0.05f);

        y = x * g;
        if (y > 1.0f)  { y = 1.0f;  C.hard_clip++; }
        if (y < -1.0f) { y = -1.0f; C.hard_clip++; }
        out[i] = y;

        if (g < 0.999f) C.active++;
        C.gain_sum += (double)g;
        if (g < C.gain_min) C.gain_min = g;
    }

    cfg->active_samples = C.active;
    cfg->hard_clips = C.hard_clip;
    cfg->gain_sum = C.gain_sum / (double)n;
    cfg->gain_min = C.gain_min;
    return C.hard_clip;
}
