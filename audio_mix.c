/*
 * audio_mix.c — 音频混音算法对比 / A/B 试听工具
 * =============================================================================
 *
 * 本程序把一个常被混淆的问题拆开：
 *
 *   混音（mixing）  = 线性相加，数学上唯一正确，且可逆；
 *   防削波          = 独立的动态处理（限幅器），带时间常数，不是逐样本公式。
 *
 * 文件里保留六个「逐样本公式」作为被测对象，另加三条动态链路（[5] float
 * 前瞻限幅、[8] softknee 压缩器、[9] truepeak 真峰值限幅）：
 *
 *   [1] 线性相加 + 硬饱和         —— 基准，未过载时比特级无损
 *   [2] 平均 (a+b)/2              —— 完全线性，但每路固定 -6 dB
 *   [3] 防削波 a+b-ab/M           —— 同号压缩、异号线性，零点斜率跳变
 *   [4] (a^2+b^2)/(a+b)           —— 对 b 的增量增益恒为 -1，且在 a+b=0 处发散
 *   [5] float 前瞻限幅            —— 推荐做法（动态链路）：1/sqrt(N) 预增益
 *                                    + lookahead limiter，保护采样点峰值
 *   [6] tanh 软饱和               —— 奇对称光滑压缩：小信号线性、单调有界，永不硬削波
 *   [7] 三次软削波 x-x³/3         —— 代数多项式族：无超越函数，带宽扩展=3（JOS PASP）
 *   [8] 软拐点压缩器 softknee     —— 动态链路：标准压缩器语义——阈值/比率/拐点宽度
 *                                    三元组（JAES 2012），软拐点、慢起释、无前瞻
 *   [9] 真峰值前瞻限幅 truepeak   —— 动态链路：dBTP 驱动——窗口峰值看 4× 过采样后的
 *                                    样本间峰（BS.1770-4），sinc 重建后也不越 ceiling
 *
 * 每条链路都会输出一个 16-bit WAV（响度已对齐，可直接 A/B），并打印：
 *
 *   峰值 dBFS / RMS dBFS / 波峰因数 / 钳位样本数 / 匹配增益 / dBTP
 *   波形残差  = 节目素材上、剥离 10 ms 尺度慢变增益后的残差（能量比，dB）
 *   互调残差  = 1 kHz + 1.5 kHz（单音 0 dBFS）稳态双音下的同一指标
 *
 * dBTP 指标列：对全部算法的节目输出（响度对齐后）用 BS.1770-4 Annex 2 的
 * 4× FIR 检测链测量真峰值（src/truepeak.c 复用同一套系数与过采样器）。
 * 满幅正弦波样本峰恰在采样点上时读数 ≈ 0 dBTP；样本间峰（重建峰）
 * 高于采样峰的部分才体现为正读数。
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
 *   gcc -O2 -std=c99 -o audio_mix \
 *       audio_mix.c src/mix_algos.c src/limiter.c src/comp_soft.c \
 *       src/truepeak.c -lm
 *
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "src/mix_algos.h"
#include "src/limiter.h"
#include "src/comp_soft.h"
#include "src/truepeak.h"

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
 * 3. 客观指标
 * ==========================================================================*/

typedef struct {
    double peak;
    double rms;
    double crest;
    double fit_gain;
    double resid_db;
    double shape_db;      /* 剥离慢变增益后的波形残差 */
    double dbtp;          /* 真峰值（BS.1770-4 Annex 2 检测链） */
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
    long lim_hard_prog = 0, comp_hard_prog = 0, tp_hard_prog = 0;
    double lim_avg_gain = 1.0, lim_min_gain = 1.0;
    double comp_avg_gain = 1.0, comp_min_gain = 1.0;
    double tp_avg_gain = 1.0, tp_min_gain = 1.0;
    int16_t *src_a = NULL, *src_b = NULL;
    float *ref = NULL, *mixbuf = NULL;
    int16_t *pcm = NULL;
    char path[512];
    limiter_cfg_t lim;
    comp_soft_cfg_t comp;
    truepeak_cfg_t tplim;
    algo_t algos[9];

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

    memset(&comp, 0, sizeof(comp));
    comp.threshold_db = -18.0f;   /* 对齐仓库响度锚点 */
    comp.ratio        = 4.0f;
    comp.knee_db      = 6.0f;
    comp.atk_ms       = 10.0f;
    comp.rel_ms       = 100.0f;
    comp.pregain      = 1.0f;
    comp.fs           = fs;

    memset(&tplim, 0, sizeof(tplim));
    tplim.ceiling = 0.891251f;   /* −1 dBTP（EBU R128 §m 上限） */
    tplim.look_ms = 5.0f;
    tplim.atk_ms  = 1.0f;
    tplim.rel_ms  = 80.0f;
    tplim.pregain = 0.70710678f; /* 1/sqrt(2) */
    tplim.fs      = fs;

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
    algos[5].name = "三次软削波 x-x^3/3";     algos[5].file = "06_cubic_soft.wav";
    algos[5].fn = mix_audio_cubic_soft;     algos[5].ctx = NULL;
    algos[6].name = "float + 前瞻限幅";       algos[6].file = "07_float_limiter.wav";
    algos[6].fn = mix_float_limiter;          algos[6].ctx = &lim;
    algos[7].name = "softknee 压缩器";        algos[7].file = "08_softknee.wav";
    algos[7].fn = mix_soft_knee;              algos[7].ctx = &comp;
    algos[8].name = "truepeak 前瞻限幅";      algos[8].file = "09_truepeak.wav";
    algos[8].fn = mix_truepeak_limiter;       algos[8].ctx = &tplim;
    nalgo = 9;

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
        algos[i].prog.dbtp     = truepeak_dbtp(mixbuf, n);

        join_path(path, sizeof(path), outdir, algos[i].file);
        for (k = 0; k < n; k++) pcm[k] = to_pcm16(mixbuf[k]);
        wav_write_mono(path, pcm, n, fs);
    }

    /* 保存节目素材的限幅器/压缩器统计，避免被后面的双音测试覆盖 */
    lim_hard_prog   = lim.hard_clips;
    lim_avg_gain    = lim.gain_sum;
    lim_min_gain    = lim.gain_min;
    comp_hard_prog  = comp.hard_clips;
    comp_avg_gain   = comp.gain_sum;
    comp_min_gain   = comp.gain_min;
    tp_hard_prog    = tplim.hard_clips;
    tp_avg_gain     = tplim.gain_sum;
    tp_min_gain     = tplim.gain_min;

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
    print_right("dBTP", 9);
    print_right("波形残差dB", 12);
    print_right("互调残差dB", 12);
    putchar('\n');
    printf("---------------------------------------------------------------------------------------------------\n");
    for (i = 0; i < nalgo; i++) {
        char s1[32], s2[32];
        fmt_db(s1, sizeof(s1), algos[i].prog.shape_db);
        fmt_db(s2, sizeof(s2), algos[i].imd_db);

        print_padded(algos[i].name, 22);
        printf("%9.1f%8.1f%9.1f%9ld%9.1f%9.1f%12s%12s\n",
               to_db(algos[i].prog.peak),
               to_db(algos[i].prog.rms),
               algos[i].prog.crest,
               algos[i].prog.clamped,
               algos[i].match_db,
               algos[i].prog.dbtp,
               s1, s2);
    }
    printf("---------------------------------------------------------------------------------------------------\n");
    printf("限幅器: 平均增益 %.1f dB, 最小增益 %.1f dB, 安全钳位 %ld, 前瞻 %.1f ms, 起控 %.1f ms, 释放 %.0f ms\n",
           to_db(lim_avg_gain), to_db(lim_min_gain), lim_hard_prog,
           lim.look_ms, lim.atk_ms, lim.rel_ms);
    printf("压缩器: 平均增益 %.1f dB, 最小增益 %.1f dB（最大衰减 %.1f dB）, 安全钳位 %ld, T=%.0f dBFS, R=%.0f:1, W=%.0f dB, 起控 %.0f ms, 释放 %.0f ms\n",
           to_db(comp_avg_gain), to_db(comp_min_gain), -to_db(comp_min_gain),
           comp_hard_prog, comp.threshold_db, comp.ratio, comp.knee_db,
           comp.atk_ms, comp.rel_ms);
    printf("真峰值限幅器: 平均增益 %.1f dB, 最小增益 %.1f dB, 安全钳位 %ld, 前瞻 %.1f ms, 起控 %.1f ms, 释放 %.0f ms, ceiling %.4f (%.1f dBTP)\n",
           to_db(tp_avg_gain), to_db(tp_min_gain), tp_hard_prog,
           tplim.look_ms, tplim.atk_ms, tplim.rel_ms, tplim.ceiling,
           20.0 * log10((double)tplim.ceiling));

    printf("\n指标说明:\n");
    printf("  匹配增益   = 为对齐响度施加的增益；0 dB 表示该算法本身已低于目标电平。\n");
    printf("  波形残差   = 节目素材上、剥离 10 ms 尺度慢变增益后的残差。只反映波形形状改变，\n");
    printf("               因此限幅器与逐样本公式可以公平比较。-inf 表示完全线性。\n");
    printf("  互调残差   = 1 kHz + 1.5 kHz 各 0 dBFS（合成峰值 +6 dBFS）下的同一指标，不含增益剥离。\n");
    printf("  dBTP       = 真峰值：BS.1770-4 Annex 2 的 4× 过采样 FIR（48 阶 4 相位）检测链测得的\n");
    printf("               样本间峰（重建峰）。满幅正弦若峰恰在采样点上，读数 ≈ 0 dBTP；\n");
    printf("               样本间峰高于采样峰才体现为正读数（欠读界 0.688 dB @4×）。\n");
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
