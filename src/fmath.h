/* Float helpers without libm (the app loader resolves no math library). SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef FMATH_H
#define FMATH_H
#define FM_PI 3.14159265f

static inline float fm_abs(float x) { return x < 0 ? -x : x; }
static inline int fm_floor(float x)
{
    int i = (int)x;
    return (float)i > x ? i - 1 : i;
}
static inline int fm_round(float x) { return fm_floor(x + 0.5f); }
static inline float fm_sqrt(float x)
{
    if (x <= 0) return 0;
    float r = x > 1 ? x / 2 : 1;
    for (int i = 0; i < 24; i++) r = 0.5f * (r + x / r);
    return r;
}
/* sin by range reduction to [-pi, pi] and a 9th-order Taylor polynomial on [-pi/2, pi/2] */
static inline float fm_sin(float x)
{
    float k = (float)fm_floor(x / (2 * FM_PI) + 0.5f);
    x -= k * 2 * FM_PI;
    if (x > FM_PI / 2) x = FM_PI - x;
    else if (x < -FM_PI / 2) x = -FM_PI - x;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static inline float fm_cos(float x) { return fm_sin(x + FM_PI / 2); }
#endif
