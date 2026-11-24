#ifndef B3_FREESTANDING_MATH_H
#define B3_FREESTANDING_MATH_H
#include "../b3_compat.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif
#define isnan(x) B3_ISNAN(x)
#define isfinite(x) B3_ISFINITE(x)
#define isinf(x) (!B3_ISFINITE(x) && !B3_ISNAN(x))
#endif
