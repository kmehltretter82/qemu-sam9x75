/* SPDX-License-Identifier: BSD-2-Clause */
/* API capability observations, not a floating-point conformance gate. */
#include <fenv.h>
#include <float.h>
#include <stdio.h>

static void mode(const char *name, int available, int requested)
{
    int reset = fesetenv(FE_DFL_ENV);
    int result = available ? fesetround(requested) : -999;
    printf("MATH_MODE %s available=%d requested=%d set_rc=%d get=%d reset_rc=%d\n",
           name, available, requested, result, fegetround(), reset);
}

static void exception(const char *name, int available, int mask)
{
    int clear = feclearexcept(FE_ALL_EXCEPT);
    int result = available ? feraiseexcept(mask) : -999;
    printf("MATH_EXCEPTION %s available=%d mask=%d raise_rc=%d observed=%d clear_rc=%d\n",
           name, available, mask, result, fetestexcept(FE_ALL_EXCEPT), clear);
}

int main(void)
{
    printf("MATH_ABI ptr=%zu float=%zu double=%zu long_double=%zu mant=%d max_exp=%d all_except=%d\n",
           sizeof(void *), sizeof(float), sizeof(double), sizeof(long double),
           LDBL_MANT_DIG, LDBL_MAX_EXP, FE_ALL_EXCEPT);
    mode("RN", 1, FE_TONEAREST);
#ifdef FE_TOWARDZERO
    mode("RZ", 1, FE_TOWARDZERO);
#else
    mode("RZ", 0, -1);
#endif
#ifdef FE_DOWNWARD
    mode("RD", 1, FE_DOWNWARD);
#else
    mode("RD", 0, -1);
#endif
#ifdef FE_UPWARD
    mode("RU", 1, FE_UPWARD);
#else
    mode("RU", 0, -1);
#endif
#ifdef FE_INVALID
    exception("INVALID", 1, FE_INVALID);
#else
    exception("INVALID", 0, 0);
#endif
#ifdef FE_DIVBYZERO
    exception("DIVBYZERO", 1, FE_DIVBYZERO);
#else
    exception("DIVBYZERO", 0, 0);
#endif
#ifdef FE_OVERFLOW
    exception("OVERFLOW", 1, FE_OVERFLOW);
#else
    exception("OVERFLOW", 0, 0);
#endif
#ifdef FE_UNDERFLOW
    exception("UNDERFLOW", 1, FE_UNDERFLOW);
#else
    exception("UNDERFLOW", 0, 0);
#endif
#ifdef FE_INEXACT
    exception("INEXACT", 1, FE_INEXACT);
#else
    exception("INEXACT", 0, 0);
#endif
    printf("MATH_ENV_RESTORE rc=%d\n", fesetenv(FE_DFL_ENV));
    return 0;
}
