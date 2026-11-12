/*
 * truepeak.h — 真峰值（inter-sample peak）测量与真峰值前瞻限幅
 *
 * 依据 ITU-R BS.1770-4 (10/2015) Annex 2：
 *   ① 12.04 dB 衰减（整数算法余量；本实现 float 域，Annex 2 §3 明示可省）
 *   ② 4× 过采样（48 kHz→192 kHz）
 *   ③ 48 阶 4 相位 FIR 低通
 *   ④ 取绝对值
 *   ⑤ dBTP = 20·log10(过采样后峰值)
 * ceiling 语义遵循 EBU R128-2023 §m：制作阶段节目真峰值 ≤ −1 dBTP。
 *
 * truepeak_cfg_t 是链路配置（主流程填充，统计字段为输出），
 * 模式学 limiter_cfg_t；FIR 历史与限幅器状态不对外暴露。
 */
#ifndef TRUEPEAK_H
#define TRUEPEAK_H

#include <stdint.h>

typedef struct {
    float ceiling;        /* 真峰值上限，线性幅度，-1 dBTP = 0.891251 */
    float look_ms;
    float atk_ms;
    float rel_ms;
    float pregain;        /* 累加后、限幅前的增益，通常 1/sqrt(N) */
    int   fs;             /* 采样率，决定 look/atk/rel 与 FIR 历史长度 */
    long  active_samples; /* 输出：真峰值链路增益 < 1 的样本数 */
    long  hard_clips;     /* 输出：安全钳位次数 */
    double gain_sum;      /* 输出：线性增益累加（已除样本数），求平均衰减 */
    float  gain_min;      /* 输出：最小增益，求最大衰减 */
} truepeak_cfg_t;

/* 真峰值测量（无状态，供指标列复用）：
 * x 为 float 域样本（|x| ≤ 1 约定），返回 20·log10(4× 过采样后峰值)。
 * 峰值为 0（静音）时返回 -999.0，与仓库 to_db() 约定一致。 */
double truepeak_dbtp(const float *x, int n);

/* 真峰值检测驱动的混音链路（统一签名，学 mix_float_limiter）：
 * float 域累加 -> pregain -> 4× FIR 真峰值检测驱动的 lookahead 限幅。
 * 返回安全钳位样本数。 */
long mix_truepeak_limiter(const int16_t *a, const int16_t *b,
                          float *out, int n, void *ctx);

#endif
