/* B3 firmware - freestanding compatibility layer.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * The Beatrix engine is GPL-2.0-or-later; see LICENSE and NOTICE.txt.
 */
#include "b3_compat.h"
#include "vibe_dsp.h"
#include "cfgParser.h"
#include "midi.h"

/* ------------------------------------------------------------------ allocator */
#define B3_ARENA_BYTES (12u * 1024u * 1024u)
static unsigned char b3_arena[B3_ARENA_BYTES] __attribute__((aligned(16)));
static size_t b3_arena_used = 0;

static void *b3_alloc(size_t n)
{
    size_t aligned = (n + 15u) & ~(size_t)15u;
    if (!aligned) aligned = 16;
    if (b3_arena_used + aligned > B3_ARENA_BYTES) return 0;
    void *p = &b3_arena[b3_arena_used];
    b3_arena_used += aligned;
    return p;
}

void *malloc(size_t n) { return b3_alloc(n); }

void *calloc(size_t n, size_t sz)
{
    size_t bytes = n * sz;
    void *p = b3_alloc(bytes);
    if (p) for (size_t i = 0; i < bytes; i++) ((unsigned char *)p)[i] = 0;
    return p;
}

void *realloc(void *p, size_t n)
{
    if (!p) return b3_alloc(n);
    /* Every engine realloc() in the init path starts from NULL. Grow-in-place is
       not possible in a bump arena; the existing block stays valid. */
    return p;
}

void free(void *p) { (void)p; }
void exit(int code) { (void)code; for (;;) {} }
void abort(void) { for (;;) {} }

/* --------------------------------------------------------------- string extras */
char *strpbrk(const char *s, const char *set)
{
    for (; *s; s++)
        for (const char *b = set; *b; b++)
            if (*s == *b) return (char *)s;
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------- no-op io */
struct b3_file { int dummy; };
static struct b3_file b3_stdin_obj, b3_stdout_obj, b3_stderr_obj;
FILE *stdin  = &b3_stdin_obj;
FILE *stdout = &b3_stdout_obj;
FILE *stderr = &b3_stderr_obj;

FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return 0; }
int fclose(FILE *f) { (void)f; return 0; }
int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int sprintf(char *buf, const char *fmt, ...) { (void)buf; (void)fmt; return 0; }
int snprintf(char *buf, size_t n, const char *fmt, ...) { (void)buf; (void)n; (void)fmt; return 0; }
int sscanf(const char *buf, const char *fmt, ...) { (void)buf; (void)fmt; return 0; }
int fflush(FILE *f) { (void)f; return 0; }
void perror(const char *s) { (void)s; }

/* ----------------------------------------------------------------------- rand */
static unsigned int b3_rng = 0x13579BDFu;
int rand(void)
{
    b3_rng ^= b3_rng << 13;
    b3_rng ^= b3_rng >> 17;
    b3_rng ^= b3_rng << 5;
    return (int)(b3_rng & 0x7fffffffu);
}

/* ----------------------------------------------------------------------- math */
typedef union { double d; uint64_t u; } b3_dbits;

static double b3_floor_d(double x)
{
    if (x != x || x < -1e300 || x > 1e300) return x;
    double t = (double)(long long)x;
    if (t > x) t -= 1.0;
    if (t < x - 1.0) t += 1.0;
    return t;
}

static double b3_exp2_d(double x)
{
    if (x < -1022.0) return 0.0;
    if (x > 1023.0) x = 1023.0;
    long long i = (long long)b3_floor_d(x);
    double f = x - (double)i;
    /* 2^f, f in [0,1) - minimax polynomial */
    double p = 1.0 + f * (0.6931471805599453 +
               f * (0.2402265069591007 +
               f * (0.05550410866482158 +
               f * (0.009618129107628477 +
               f * (0.0013333558146428443 +
               f * 0.00015403530393381624)))));
    b3_dbits b; b.u = (uint64_t)(i + 1023) << 52;
    return p * b.d;
}

static double b3_log2_d(double x)
{
    if (x <= 0) return -1022.0;
    b3_dbits b; b.d = x;
    int e = (int)((b.u >> 52) & 0x7ff) - 1023;
    b.u = (b.u & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL;
    double m = b.d; /* 1..2 */
    /* log(m) via atanh series on t = (m-1)/(m+1), |t| <= 1/3 */
    double t = (m - 1.0) / (m + 1.0), t2 = t * t;
    double p = t * (2.0 + t2 * (0.6666666666666666 +
               t2 * (0.4 + t2 * (0.2857142857142857 +
               t2 * (0.2222222222222222 + t2 * 0.1818181818181818)))));
    return p * 1.4426950408889634 + (double)e;
}

double sin(double x)
{
    if (x != x) return x;
    double ph = x * 0.15915494309189535; /* x / 2pi */
    ph -= b3_floor_d(ph);
    return (double)vd_sin((float)ph);
}

double cos(double x) { return sin(x + 1.5707963267948966); }

double tan(double x) { double c = cos(x); return c == 0 ? 0.0 : sin(x) / c; }

double exp(double x) { return b3_exp2_d(x * 1.4426950408889634); }
double exp2(double x) { return b3_exp2_d(x); }
double log(double x) { return b3_log2_d(x) * 0.6931471805599453; }
double log2(double x) { return b3_log2_d(x); }
double log10(double x) { return b3_log2_d(x) * 0.3010299956639812; }

double pow(double a, double b)
{
    if (a == 0.0) return b > 0 ? 0.0 : 1.0;
    if (a < 0.0) {
        double r = b3_exp2_d(b * b3_log2_d(-a));
        double w = b3_floor_d(b);
        return (b - w == 0.0 && w - 2.0 * b3_floor_d(w / 2.0) != 0.0) ? -r : r;
    }
    return b3_exp2_d(b * b3_log2_d(a));
}

double sqrt(double x) { return __builtin_sqrt(x); }
double fabs(double x) { return x < 0 ? -x : x; }
double floor(double x) { return b3_floor_d(x); }
double ceil(double x) { return -b3_floor_d(-x); }
double trunc(double x) { return x < 0 ? ceil(x) : floor(x); }
double rint(double x) { double f = floor(x); double r = x - f; return (r > 0.5 || (r == 0.5 && f - 2.0 * floor(f / 2.0) != 0.0)) ? f + 1.0 : f; }
double round(double x) { double f = floor(x); return x - f >= 0.5 ? f + 1.0 : f; }

double fmod(double x, double y)
{
    if (y == 0.0 || x != x || y != y) return 0.0;
    double q = (double)(long long)(x / y);
    return x - q * y;
}

double remainder(double x, double y)
{
    if (y == 0.0 || x != x || y != y) return 0.0;
    double q = x / y, n = rint(q);
    return x - n * y;
}

#define B3_FLOAT_WRAP(name, type) type name(type x) { return (type)(x); }
float sinf(float x) { return vd_sin(x * 0.15915494309189535f); }
float cosf(float x) { return vd_sin(x * 0.15915494309189535f + 0.25f); }
float expf(float x) { return vd_exp2(x * 1.4426950408889634f); }
float exp2f(float x) { return vd_exp2(x); }
float logf(float x) { return vd_log2(x) * 0.6931471805599453f; }
float log2f(float x) { return vd_log2(x); }
float log10f(float x) { return vd_log2(x) * 0.3010299956639812f; }
float powf(float a, float b) { return vd_pow(a, b); }
float sqrtf(float x) { return vd_sqrt(x); }
float fabsf(float x) { return x < 0 ? -x : x; }
float floorf(float x) { return (float)floor((double)x); }
float ceilf(float x) { return (float)ceil((double)x); }
float rintf(float x) { return (float)rint((double)x); }
float roundf(float x) { return (float)round((double)x); }
float truncf(float x) { return (float)trunc((double)x); }
float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }
float remainderf(float x, float y) { return (float)remainder((double)x, (double)y); }

/* ------------------------------------------- engine glue (no config/MIDI files) */
int getConfigParameter_d(const char *par, ConfigContext *cfg, double *dp) { (void)par; (void)cfg; (void)dp; return 0; }
int getConfigParameter_i(const char *par, ConfigContext *cfg, int *ip) { (void)par; (void)cfg; (void)ip; return 0; }
int getConfigParameter_f(const char *par, ConfigContext *cfg, float *fp) { (void)par; (void)cfg; (void)fp; return 0; }
int getConfigParameter_ir(const char *par, ConfigContext *cfg, int *ip, int lo, int hi) { (void)par; (void)cfg; (void)ip; (void)lo; (void)hi; return 0; }
int getConfigParameter_dr(const char *par, ConfigContext *cfg, double *dp, double lo, double hi) { (void)par; (void)cfg; (void)dp; (void)lo; (void)hi; return 0; }
int getConfigParameter_fr(const char *par, ConfigContext *cfg, float *fp, float lo, float hi) { (void)par; (void)cfg; (void)fp; (void)lo; (void)hi; return 0; }
const char *getConfigValue(ConfigContext *cfg) { (void)cfg; return ""; }
void showConfigfileContext(ConfigContext *cfg, const char *msg) { (void)cfg; (void)msg; }
void configDoubleUnparsable(ConfigContext *cfg) { (void)cfg; }
void configIntOutOfRange(ConfigContext *cfg, int min, int max) { (void)cfg; (void)min; (void)max; }

void useMIDIControlFunction(void *m, const char *name, void (*fn)(void *, unsigned char), void *d)
{ (void)m; (void)name; (void)fn; (void)d; }
void notifyControlChangeByName(void *m, const char *name, unsigned char v) { (void)m; (void)name; (void)v; }
void notifyControlChangeById(void *m, int id, unsigned char v) { (void)m; (void)id; (void)v; }
void midi_panic(void *inst) { (void)inst; }
