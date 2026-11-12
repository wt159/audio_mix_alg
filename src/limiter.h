/*
 * limiter.h — 前瞻限幅器与 float 参考链路
 *
 * limiter_cfg_t 是链路配置（主流程填充，统计字段为输出）；
 * 限幅器内部状态不对外暴露。结构与时间常数说明见 limiter.c。
 */
#ifndef LIMITER_H
#define LIMITER_H

#include <stdint.h>

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

long mix_float_limiter(const int16_t *a, const int16_t *b,
                       float *out, int n, void *ctx);

#endif
