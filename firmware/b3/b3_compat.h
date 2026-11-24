/* B3 firmware - freestanding compatibility layer for the Beatrix/setBfree C engine.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * See LICENSE and NOTICE.txt in this folder for the Beatrix/setBfree notices.
 */
#ifndef B3_COMPAT_H
#define B3_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- static arena allocator (the VIBE firmware has no heap) ---------------- */
void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int code);
void abort(void);

/* --- minimal C library ------------------------------------------------------ */
int rand(void);
#define RAND_MAX 0x7fffffff
int abs(int v);
long labs(long v);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strpbrk(const char *s, const char *set);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
long strtol(const char *s, char **end, int base);
int atoi(const char *s);

/* --- no-op stdio: debug/config paths only ---------------------------------- */
typedef struct b3_file FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
int fprintf(FILE *f, const char *fmt, ...);
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int sscanf(const char *buf, const char *fmt, ...);
int fflush(FILE *f);
void perror(const char *s);

/* --- math, implemented with the SDK's DSP primitives ----------------------- */
double sin(double x);
double cos(double x);
double tan(double x);
double exp(double x);
double exp2(double x);
double log(double x);
double log2(double x);
double log10(double x);
double pow(double a, double b);
double sqrt(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double rint(double x);
double round(double x);
double trunc(double x);
double fmod(double x, double y);
double remainder(double x, double y);
float sinf(float x);
float cosf(float x);
float expf(float x);
float exp2f(float x);
float logf(float x);
float log2f(float x);
float log10f(float x);
float powf(float a, float b);
float sqrtf(float x);
float fabsf(float x);
float floorf(float x);
float ceilf(float x);
float rintf(float x);
float roundf(float x);
float truncf(float x);
float fmodf(float x, float y);
float remainderf(float x, float y);

#define B3_ISNAN(x) ((x) != (x))
#define B3_ISFINITE(x) ((x) == (x) && (x) < 1e300 && (x) > -1e300)

#ifdef __cplusplus
}
#endif

#endif /* B3_COMPAT_H */
