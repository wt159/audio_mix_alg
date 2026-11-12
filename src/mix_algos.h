/*
 * mix_algos.h — 逐样本混音公式（被测对象）
 *
 * 统一签名：两路 int16 输入、float 输出（未量化），返回被钳位的样本数；
 * ctx 为各算法可选配置（逐样本公式均不使用）。实现见 mix_algos.c。
 */
#ifndef MIX_ALGOS_H
#define MIX_ALGOS_H

#include <stdint.h>

long mix_audio_simple(const int16_t *a, const int16_t *b,
                      float *out, int n, void *ctx);
long mix_audio_average(const int16_t *a, const int16_t *b,
                       float *out, int n, void *ctx);
long mix_audio_no_clipping(const int16_t *a, const int16_t *b,
                           float *out, int n, void *ctx);
long mix_audio_your_formula(const int16_t *a, const int16_t *b,
                            float *out, int n, void *ctx);
long mix_audio_tanh_sat(const int16_t *a, const int16_t *b,
                        float *out, int n, void *ctx);

#endif
