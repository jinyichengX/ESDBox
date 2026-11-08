/* ============================================================================
 * FORTIS Voice - 桌面离线验证工具  v0.1
 * ----------------------------------------------------------------------------
 * 目的：在写任何固件之前，先用电脑验证"处理后的声音是否真的更好听"。
 *       离线都做不到的效果，实时一定做不到。
 *
 * 处理链：
 *   HPF(去嗡鸣) -> 中低频去浑浊 -> 低频体感 -> 临场感 -> 空气感
 *   -> AGC(自动增益) -> 压缩器(控制动态) -> 谐波激励(增加明亮与穿透)
 *   -> 限幅器
 *
 * 编译（MinGW / MSVC 均可）：
 *   gcc -O2 -o fortis main.c -lm
 *
 * 用法：
 *   fortis <输入.wav> <输出.wav> [soft|normal|strong]
 *
 * 输入要求：
 *   16/24/32bit PCM 或 32bit float 的 WAV，单/双声道均可（自动混为单声道）。
 *   手机录音（m4a/aac）请先转成 WAV，例如：
 *     ffmpeg -i in.m4a -ac 1 -ar 48000 -sample_fmt s16 in.wav
 *
 * 注意：
 *   1. 本工具只做"放大 + 照亮"，不做变声、不改变音高，保持"还是你"。
 *   2. 输入若有明显底噪，AGC 会把底噪一起抬起来，录音时尽量安静。
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define PI_F 3.14159265358979f

/* ==========================================================================
 * 1. WAV 读写
 * ========================================================================*/

static uint32_t rd_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static unsigned char *load_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long sz;

    if (!f) {
        fprintf(stderr, "[error] cannot open input: %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz <= 0) { fclose(f); fprintf(stderr, "[error] empty input\n"); return NULL; }
    rewind(f);

    buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); fprintf(stderr, "[error] out of memory\n"); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); free(buf);
        fprintf(stderr, "[error] read failed\n");
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

/* 读取 WAV，返回混为单声道的 float 样本（[-1,1]） */
static float *wav_read_mono(const char *path, size_t *out_n, uint32_t *out_fs)
{
    unsigned char *buf;
    size_t size = 0, pos = 12;
    int have_fmt = 0, have_data = 0;
    uint16_t fmt_tag = 0, channels = 0, bits = 0;
    uint32_t fs = 0;
    const unsigned char *data = NULL;
    uint32_t data_size = 0;
    size_t frames, i;
    float *mono;
    int bytes_per_sample, is_float;

    buf = load_file(path, &size);
    if (!buf) return NULL;

    if (size < 44 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "[error] not a RIFF/WAVE file\n");
        free(buf);
        return NULL;
    }

    while (pos + 8 <= size) {
        char id[5];
        uint32_t csz;
        const unsigned char *c;

        memcpy(id, buf + pos, 4);
        id[4] = 0;
        csz = rd_u32(buf + pos + 4);
        c = buf + pos + 8;

        if (strcmp(id, "fmt ") == 0 && csz >= 16 && pos + 8 + csz <= size) {
            fmt_tag = rd_u16(c);
            channels = rd_u16(c + 2);
            fs = rd_u32(c + 4);
            bits = rd_u16(c + 14);
            /* WAVE_FORMAT_EXTENSIBLE：真实格式在子格式 GUID 前两字节 */
            if (fmt_tag == 0xFFFE && csz >= 40) fmt_tag = rd_u16(c + 24);
            have_fmt = 1;
        } else if (strcmp(id, "data") == 0) {
            data = c;
            data_size = csz;
            if (pos + 8 + (size_t)csz > size) data_size = (uint32_t)(size - (pos + 8));
            have_data = 1;
        }

        pos += 8 + (size_t)csz + (csz & 1);
    }

    if (!have_fmt || !have_data || channels == 0 || fs == 0) {
        fprintf(stderr, "[error] incomplete WAV (fmt=%d data=%d ch=%u)\n", have_fmt, have_data, channels);
        free(buf);
        return NULL;
    }

    is_float = (fmt_tag == 3);
    bytes_per_sample = bits / 8;
    if (bytes_per_sample <= 0) {
        fprintf(stderr, "[error] unsupported bits: %u\n", bits);
        free(buf);
        return NULL;
    }

    frames = data_size / ((size_t)bytes_per_sample * channels);
    if (frames == 0) {
        fprintf(stderr, "[error] no audio frames\n");
        free(buf);
        return NULL;
    }

    mono = (float *)malloc(frames * sizeof(float));
    if (!mono) { free(buf); return NULL; }

    for (i = 0; i < frames; i++) {
        double acc = 0.0;
        uint16_t ch;
        for (ch = 0; ch < channels; ch++) {
            const unsigned char *p = data + (i * channels + ch) * bytes_per_sample;
            float v = 0.0f;

            if (is_float && bits == 32) {
                float f;
                memcpy(&f, p, 4);
                v = f;
            } else if (bits == 8) {
                v = ((float)p[0] - 128.0f) / 128.0f;
            } else if (bits == 16) {
                v = (float)(int16_t)rd_u16(p) / 32768.0f;
            } else if (bits == 24) {
                int32_t s = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
                if (s & 0x800000) s |= (int32_t)0xFF000000;
                v = (float)s / 8388608.0f;
            } else if (bits == 32) {
                v = (float)((double)(int32_t)rd_u32(p) / 2147483648.0);
            }
            acc += v;
        }
        mono[i] = (float)(acc / channels);
    }

    free(buf);
    *out_n = frames;
    *out_fs = fs;
    return mono;
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

static int wav_write_mono16(const char *path, const float *x, size_t n, uint32_t fs)
{
    unsigned char hdr[44];
    FILE *f;
    size_t i;
    uint32_t data_bytes = (uint32_t)(n * 2);
    int16_t *pcm;

    pcm = (int16_t *)malloc(n * sizeof(int16_t));
    if (!pcm) return -1;

    for (i = 0; i < n; i++) {
        float v = x[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[i] = (int16_t)lrintf(v * 32767.0f);
    }

    memcpy(hdr, "RIFF", 4);
    wr_u32(hdr + 4, 36 + data_bytes);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wr_u32(hdr + 16, 16);
    wr_u16(hdr + 20, 1);              /* PCM */
    wr_u16(hdr + 22, 1);              /* mono */
    wr_u32(hdr + 24, fs);
    wr_u32(hdr + 28, fs * 2);         /* byte rate */
    wr_u16(hdr + 32, 2);              /* block align */
    wr_u16(hdr + 34, 16);             /* bits */
    memcpy(hdr + 36, "data", 4);
    wr_u32(hdr + 40, data_bytes);

    f = fopen(path, "wb");
    if (!f) { free(pcm); fprintf(stderr, "[error] cannot write: %s\n", path); return -1; }
    fwrite(hdr, 1, 44, f);
    fwrite(pcm, sizeof(int16_t), n, f);
    fclose(f);
    free(pcm);
    return 0;
}

/* ==========================================================================
 * 2. 双二阶滤波器（RBJ Audio EQ Cookbook）
 * ========================================================================*/

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} Biquad;

static void bq_reset(Biquad *f)
{
    f->z1 = 0.0f;
    f->z2 = 0.0f;
}

static float bq_run(Biquad *f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

static void bq_peaking(Biquad *f, float fs, float f0, float gain_db, float q)
{
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * PI_F * f0 / fs;
    float cs = cosf(w0);
    float alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha / A;

    f->b0 = (1.0f + alpha * A) / a0;
    f->b1 = (-2.0f * cs) / a0;
    f->b2 = (1.0f - alpha * A) / a0;
    f->a1 = (-2.0f * cs) / a0;
    f->a2 = (1.0f - alpha / A) / a0;
    bq_reset(f);
}

static void bq_lowshelf(Biquad *f, float fs, float f0, float gain_db)
{
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * PI_F * f0 / fs;
    float cs = cosf(w0);
    float sn = sinf(w0);
    float alpha = sn * 0.5f * sqrtf((A + 1.0f / A) * (1.0f / 1.0f - 1.0f) + 2.0f);
    float beta = 2.0f * sqrtf(A) * alpha;
    float a0 = (A + 1.0f) + (A - 1.0f) * cs + beta;

    f->b0 = (A * ((A + 1.0f) - (A - 1.0f) * cs + beta)) / a0;
    f->b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
    f->b2 = (A * ((A + 1.0f) - (A - 1.0f) * cs - beta)) / a0;
    f->a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
    f->a2 = ((A + 1.0f) + (A - 1.0f) * cs - beta) / a0;
    bq_reset(f);
}

static void bq_highshelf(Biquad *f, float fs, float f0, float gain_db)
{
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * PI_F * f0 / fs;
    float cs = cosf(w0);
    float sn = sinf(w0);
    float alpha = sn * 0.5f * sqrtf((A + 1.0f / A) * (1.0f / 1.0f - 1.0f) + 2.0f);
    float beta = 2.0f * sqrtf(A) * alpha;
    float a0 = (A + 1.0f) - (A - 1.0f) * cs + beta;

    f->b0 = (A * ((A + 1.0f) + (A - 1.0f) * cs + beta)) / a0;
    f->b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
    f->b2 = (A * ((A + 1.0f) + (A - 1.0f) * cs - beta)) / a0;
    f->a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
    f->a2 = ((A + 1.0f) - (A - 1.0f) * cs - beta) / a0;
    bq_reset(f);
}

static void bq_hpf(Biquad *f, float fs, float f0, float q)
{
    float w0 = 2.0f * PI_F * f0 / fs;
    float cs = cosf(w0);
    float alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha;

    f->b0 = ((1.0f + cs) * 0.5f) / a0;
    f->b1 = (-(1.0f + cs)) / a0;
    f->b2 = ((1.0f + cs) * 0.5f) / a0;
    f->a1 = (-2.0f * cs) / a0;
    f->a2 = (1.0f - alpha) / a0;
    bq_reset(f);
}

static void bq_lpf(Biquad *f, float fs, float f0, float q)
{
    float w0 = 2.0f * PI_F * f0 / fs;
    float cs = cosf(w0);
    float alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha;

    f->b0 = ((1.0f - cs) * 0.5f) / a0;
    f->b1 = (1.0f - cs) / a0;
    f->b2 = ((1.0f - cs) * 0.5f) / a0;
    f->a1 = (-2.0f * cs) / a0;
    f->a2 = (1.0f - alpha) / a0;
    bq_reset(f);
}

/* ==========================================================================
 * 3. 动态处理：AGC / 压缩器 / 谐波激励 / 限幅
 * ========================================================================*/

typedef struct {
    float fs;
    float target;     /* 目标电平（线性） */
    float env;        /* 平滑后的幅度 */
    float gain;       /* 平滑后的增益 */
    float c_fast;     /* 快速时间常数（跟得上变大） */
    float c_slow;     /* 慢速时间常数（回落） */
    float c_dn;       /* 增益下降（快速，防过冲） */
    float c_up;       /* 增益上升（缓慢，防抽吸） */
    float gate;       /* 噪声门限（线性），低于此值冻结增益 */
    float lo, hi;     /* 增益上下限 */
} Agc;

static void agc_init(Agc *a, float fs, float target_db, float gate_db, float lo, float hi)
{
    a->fs = fs;
    a->target = powf(10.0f, target_db / 20.0f);
    a->env = 0.001f;
    a->gain = 1.0f;
    a->c_fast = expf(-1.0f / (fs * 0.05f));
    a->c_slow = expf(-1.0f / (fs * 0.30f));
    a->c_dn = expf(-1.0f / (fs * 0.03f));
    a->c_up = expf(-1.0f / (fs * 0.30f));
    a->gate = powf(10.0f, gate_db / 20.0f);
    a->lo = lo;
    a->hi = hi;
}

static float agc_run(Agc *a, float x)
{
    float m = fabsf(x);
    float want, c;

    /* 静音段冻结增益：否则环境会推到最大，说话时严重过冲 */
    if (m < a->gate) return x * a->gain;

    if (m > a->env) a->env = a->c_fast * a->env + (1.0f - a->c_fast) * m;
    else            a->env = a->c_slow * a->env + (1.0f - a->c_slow) * m;

    if (a->env < 1e-5f) a->env = 1e-5f;

    want = a->target / a->env;
    if (want > a->hi) want = a->hi;
    if (want < a->lo) want = a->lo;

    c = (want < a->gain) ? a->c_dn : a->c_up;
    a->gain = c * a->gain + (1.0f - c) * want;
    return x * a->gain;
}

typedef struct {
    float thresh_db;
    float ratio;
    float knee_db;
    float makeup_db;
    float c_atk;
    float c_rel;
    float env;
} Comp;

static void comp_init(Comp *c, float fs, float thresh_db, float ratio, float knee_db,
                      float makeup_db, float atk_ms, float rel_ms)
{
    c->thresh_db = thresh_db;
    c->ratio = ratio;
    c->knee_db = knee_db;
    c->makeup_db = makeup_db;
    c->c_atk = expf(-1.0f / (fs * atk_ms * 0.001f));
    c->c_rel = expf(-1.0f / (fs * rel_ms * 0.001f));
    c->env = 0.0f;
}

static float comp_run(Comp *c, float x)
{
    float m = fabsf(x);
    float env_db, over, gr_db, g;

    if (m > c->env) c->env = c->c_atk * c->env + (1.0f - c->c_atk) * m;
    else            c->env = c->c_rel * c->env + (1.0f - c->c_rel) * m;

    env_db = 20.0f * log10f(c->env + 1e-9f);
    over = env_db - c->thresh_db;
    gr_db = 0.0f;

    if (over <= -c->knee_db * 0.5f) {
        gr_db = 0.0f;
    } else if (over >= c->knee_db * 0.5f) {
        gr_db = over * (1.0f / c->ratio - 1.0f);
    } else {
        float t = over + c->knee_db * 0.5f;
        gr_db = (1.0f / c->ratio - 1.0f) * t * t / (2.0f * c->knee_db);
    }

    g = powf(10.0f, (gr_db + c->makeup_db) / 20.0f);
    return x * g;
}

typedef struct {
    float ceil;
    float gain;
    float c_rel;
} Limiter;

static void lim_init(Limiter *l, float fs, float ceil_db)
{
    l->ceil = powf(10.0f, ceil_db / 20.0f);
    l->gain = 1.0f;
    l->c_rel = expf(-1.0f / (fs * 0.05f));
}

static float lim_run(Limiter *l, float x)
{
    float peak = fabsf(x);
    float target = (peak > l->ceil) ? (l->ceil / peak) : 1.0f;

    if (target < l->gain) l->gain = target;
    else l->gain = l->c_rel * l->gain + (1.0f - l->c_rel) * target;

    x *= l->gain;
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return x;
}

/* ==========================================================================
 * 4. 预设参数
 * ========================================================================*/

typedef struct {
    const char *name;
    float hp_hz;
    float mud_hz, mud_db, mud_q;      /* 去浑浊 */
    float body_hz, body_db;           /* 低频体感 */
    float pres_hz, pres_db, pres_q;   /* 临场感 */
    float air_hz, air_db;             /* 空气感 */
    float agc_target_db;
    float comp_thresh_db, comp_ratio, comp_knee_db, comp_makeup_db;
    float comp_atk_ms, comp_rel_ms;
    float exc_lp_hz;                  /* 取低频做非线性 */
    float exc_hp_hz;                  /* 提取新生成的高频 */
    float exc_drive, exc_mix;
    float ceil_db;
} Preset;

static const Preset PRESET_SOFT = {
    "soft", 90.0f,
    300.0f, -1.5f, 0.90f,
    150.0f, 2.0f,
    3000.0f, 3.0f, 0.90f,
    8000.0f, 1.5f,
    -20.0f,
    -19.0f, 2.5f, 6.0f, 1.0f,
    15.0f, 120.0f,
    1500.0f, 2500.0f, 2.0f, 0.20f,
    -1.0f
};

static const Preset PRESET_NORMAL = {
    "normal", 95.0f,
    300.0f, -2.5f, 0.90f,
    150.0f, 3.0f,
    3000.0f, 5.0f, 0.90f,
    8000.0f, 3.0f,
    -19.0f,
    -21.0f, 4.0f, 6.0f, 2.0f,
    15.0f, 100.0f,
    1800.0f, 2800.0f, 3.0f, 0.35f,
    -1.0f
};

static const Preset PRESET_STRONG = {
    "strong", 100.0f,
    320.0f, -3.5f, 0.90f,
    150.0f, 4.0f,
    3200.0f, 7.0f, 0.85f,
    8500.0f, 4.5f,
    -18.0f,
    -23.0f, 5.0f, 8.0f, 3.0f,
    15.0f, 80.0f,
    2000.0f, 3000.0f, 4.0f, 0.50f,
    -1.0f
};

/* ==========================================================================
 * 5. 统计
 * ========================================================================*/

typedef struct {
    double sum_sq;
    double peak;
    size_t n;
} Stats;

static void stats_add(Stats *s, float v)
{
    double a = fabs((double)v);
    s->sum_sq += a * a;
    if (a > s->peak) s->peak = a;
    s->n++;
}

static double stats_rms_db(const Stats *s)
{
    double r = (s->n > 0) ? sqrt(s->sum_sq / (double)s->n) : 0.0;
    return 20.0 * log10(r + 1e-12);
}

static double stats_peak_db(const Stats *s)
{
    return 20.0 * log10(s->peak + 1e-12);
}

/* ==========================================================================
 * 6. 主处理
 * ========================================================================*/

static float *process_chain(const float *in, size_t n, uint32_t fs, const Preset *p,
                            double *presence_in_pct, double *presence_out_pct,
                            int debug)
{
    float *out = (float *)malloc(n * sizeof(float));
    Biquad hp, mud, body, pres, air, exc_lp, exc_hp, exc_hp2;
    /* 临场感能量测量：2k~6k 带通 */
    Biquad bin_hp, bin_lp, bout_hp, bout_lp;
    Agc agc;
    Comp comp;
    Limiter lim;
    size_t i;
    double e_tot_in = 0.0, e_band_in = 0.0;
    double e_tot_out = 0.0, e_band_out = 0.0;
    Stats st[5];
    const char *stage_name[5] = { "EQ", "AGC", "COMP", "EXC", "LIMIT" };

    memset(st, 0, sizeof(st));

    if (!out) return NULL;

    bq_hpf(&hp, (float)fs, p->hp_hz, 0.7071f);
    bq_peaking(&mud, (float)fs, p->mud_hz, p->mud_db, p->mud_q);
    bq_lowshelf(&body, (float)fs, p->body_hz, p->body_db);
    bq_peaking(&pres, (float)fs, p->pres_hz, p->pres_db, p->pres_q);
    bq_highshelf(&air, (float)fs, p->air_hz, p->air_db);
    bq_lpf(&exc_lp, (float)fs, p->exc_lp_hz, 0.7071f);
    bq_hpf(&exc_hp, (float)fs, p->exc_hp_hz, 0.7071f);
    bq_hpf(&exc_hp2, (float)fs, p->exc_hp_hz, 0.7071f);

    bq_hpf(&bin_hp, (float)fs, 2000.0f, 0.7071f);
    bq_lpf(&bin_lp, (float)fs, 6000.0f, 0.7071f);
    bq_hpf(&bout_hp, (float)fs, 2000.0f, 0.7071f);
    bq_lpf(&bout_lp, (float)fs, 6000.0f, 0.7071f);

    agc_init(&agc, (float)fs, p->agc_target_db, -50.0f, 0.25f, 4.0f);
    comp_init(&comp, (float)fs, p->comp_thresh_db, p->comp_ratio, p->comp_knee_db,
              p->comp_makeup_db, p->comp_atk_ms, p->comp_rel_ms);
    lim_init(&lim, (float)fs, p->ceil_db);

    for (i = 0; i < n; i++) {
        float x = in[i];
        float y, y_eq, y_agc, y_comp, y_exc, y_lim;
        float band;

        /* 测量输入 2k~6k 能量占比 */
        band = bq_run(&bin_lp, bq_run(&bin_hp, x));
        e_tot_in += (double)x * x;
        e_band_in += (double)band * band;

        /* 处理链 */
        y = bq_run(&hp, x);
        y = bq_run(&mud, y);
        y = bq_run(&body, y);
        y = bq_run(&pres, y);
        y = bq_run(&air, y);
        y_eq = y;

        y_agc = agc_run(&agc, y);
        y_comp = comp_run(&comp, y_agc);

        /* 谐波激励：用能量充足的低频驱动非线性，只混回"新造出来"的高频。
           之前先把输入高通再失真，对本来就缺高频的嗓音等于空转，已修正。 */
        {
            float low = bq_run(&exc_lp, y_comp);        /* 取 2kHz 以下基频区 */
            float sat = tanhf(low * p->exc_drive);      /* 非线性 -> 生成谐波 */
            float new_hf = bq_run(&exc_hp, sat) - bq_run(&exc_hp2, low);
            y_exc = y_comp + p->exc_mix * new_hf;
        }

        y_lim = lim_run(&lim, y_exc);
        out[i] = y_lim;

        if (debug) {
            stats_add(&st[0], y_eq);
            stats_add(&st[1], y_agc);
            stats_add(&st[2], y_comp);
            stats_add(&st[3], y_exc);
            stats_add(&st[4], y_lim);
        }

        band = bq_run(&bout_lp, bq_run(&bout_hp, y_lim));
        e_tot_out += (double)y_lim * y_lim;
        e_band_out += (double)band * band;
    }

    if (debug) {
        int k;
        printf("\n%-8s %10s %10s %10s\n", "stage", "RMS", "Peak", "Crest");
        for (k = 0; k < 5; k++) {
            double r = stats_rms_db(&st[k]);
            double pk = stats_peak_db(&st[k]);
            printf("%-8s %10.1f %10.1f %10.1f\n", stage_name[k], r, pk, pk - r);
        }
    }

    *presence_in_pct = (e_tot_in > 1e-12) ? 100.0 * e_band_in / e_tot_in : 0.0;
    *presence_out_pct = (e_tot_out > 1e-12) ? 100.0 * e_band_out / e_tot_out : 0.0;

    return out;
}

/* ==========================================================================
 * 7. 入口
 * ========================================================================*/

static void usage(const char *exe)
{
    printf("FORTIS Voice - offline validation tool v0.1\n");
    printf("usage: %s <input.wav> <output.wav> [soft|normal|strong] [--debug]\n", exe);
    printf("\n");
    printf("  input : 16/24/32-bit PCM or 32-bit float WAV (mono or stereo)\n");
    printf("  preset: soft = mild, normal = default, strong = aggressive\n");
    printf("  --debug: print RMS/peak/crest after each processing stage\n");
}

int main(int argc, char **argv)
{
    const Preset *preset = &PRESET_NORMAL;
    const char *in_path, *out_path;
    float *in = NULL, *out = NULL;
    size_t n = 0;
    uint32_t fs = 0;
    Stats s_in, s_out;
    double pin = 0.0, pout = 0.0;
    size_t i;
    int debug = 0;

    if (argc < 3) {
        usage(argc > 0 ? argv[0] : "fortis");
        return 1;
    }

    in_path = argv[1];
    out_path = argv[2];

    if (argc >= 4) {
        int a;
        for (a = 3; a < argc; a++) {
            if (strcmp(argv[a], "--debug") == 0) {
                debug = 1;
            } else if (strcmp(argv[a], "soft") == 0) {
                preset = &PRESET_SOFT;
            } else if (strcmp(argv[a], "normal") == 0) {
                preset = &PRESET_NORMAL;
            } else if (strcmp(argv[a], "strong") == 0) {
                preset = &PRESET_STRONG;
            } else {
                fprintf(stderr, "[error] unknown argument: %s\n", argv[a]);
                usage(argv[0]);
                return 1;
            }
        }
    }

    in = wav_read_mono(in_path, &n, &fs);
    if (!in) return 2;

    printf("input : %s\n", in_path);
    printf("format: %u Hz, mono, %.2f s\n", fs, (double)n / (double)fs);
    printf("preset: %s\n", preset->name);

    out = process_chain(in, n, fs, preset, &pin, &pout, debug);
    if (!out) {
        free(in);
        fprintf(stderr, "[error] processing failed\n");
        return 3;
    }

    /* 响度对齐（仅离线验证用）：让 A/B 只听音色，不听音量 */
    {
        double sum_in = 0.0, sum_out = 0.0, g, peak = 0.0;
        for (i = 0; i < n; i++) {
            sum_in += (double)in[i] * in[i];
            sum_out += (double)out[i] * out[i];
            if (fabs(out[i]) > peak) peak = fabs(out[i]);
        }
        sum_in = sqrt(sum_in / (double)n);
        sum_out = sqrt(sum_out / (double)n);
        if (sum_out > 1e-9) {
            g = sum_in / sum_out;
            if (peak * g > 0.89) g = 0.89 / peak;   /* 别顶到 0dBFS */
            for (i = 0; i < n; i++) out[i] *= (float)g;
            printf("loudness match: %+.1f dB (fair A/B)\n", 20.0 * log10(g + 1e-12));
        }
    }

    if (wav_write_mono16(out_path, out, n, fs) != 0) {
        free(in);
        free(out);
        return 4;
    }

    memset(&s_in, 0, sizeof(s_in));
    memset(&s_out, 0, sizeof(s_out));
    for (i = 0; i < n; i++) {
        stats_add(&s_in, in[i]);
        stats_add(&s_out, out[i]);
    }

    printf("\n%-10s %10s %10s %10s %10s\n", "", "RMS", "Peak", "Crest", "Presence");
    printf("%-10s %10.1f %10.1f %10.1f %9.1f%%\n", "before",
           stats_rms_db(&s_in), stats_peak_db(&s_in),
           stats_peak_db(&s_in) - stats_rms_db(&s_in), pin);
    printf("%-10s %10.1f %10.1f %10.1f %9.1f%%\n", "after",
           stats_rms_db(&s_out), stats_peak_db(&s_out),
           stats_peak_db(&s_out) - stats_rms_db(&s_out), pout);
    printf("\n(Levels in dBFS. Higher Presence %% = more 2k-6k clarity energy.)\n");
    printf("output: %s\n", out_path);

    free(in);
    free(out);
    return 0;
}
