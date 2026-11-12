/*
 * comp_soft.h — 软拐点压缩器（feedforward soft-knee compressor）
 *
 * comp_soft_cfg_t 是链路配置（主流程填充，统计字段为输出）；
 * 侧链内部状态不对外暴露。公式出处与链路结构见 comp_soft.c。
 */
#ifndef COMP_SOFT_H
#define COMP_SOFT_H

#include <stdint.h>

typedef struct {
    float threshold_db;   /* 阈值 T，dBFS */
    float ratio;          /* 压缩比 R（>1） */
    float knee_db;        /* 软拐点宽度 W，dB */
    float atk_ms;         /* 起控时间 */
    float rel_ms;         /* 释放时间 */
    float pregain;        /* 累加后、压缩前的线性增益 */
    int   fs;             /* 采样率，决定 atk/rel 对应的系数 */
    long  active_samples; /* 输出：增益 < 1 的样本数 */
    long  hard_clips;     /* 输出：安全钳位次数 */
    double gain_sum;      /* 输出：线性增益累加（已除以样本数），即平均增益 */
    float  gain_min;      /* 输出：最小增益，用于求最大衰减 */
} comp_soft_cfg_t;

long mix_soft_knee(const int16_t *a, const int16_t *b,
                   float *out, int n, void *ctx);

#endif
