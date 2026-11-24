#ifndef B3_FREESTANDING_LOCALE_H
#define B3_FREESTANDING_LOCALE_H
#define LC_ALL 0
#define LC_NUMERIC 1
struct lconv { int dummy; };
static inline char *setlocale(int cat, const char *loc) { (void)cat; (void)loc; return (char *)"C"; }
static inline struct lconv *localeconv(void) { static struct lconv lc; return &lc; }
#endif
