/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Floating point at the edges of the exponent range.  frcpa and frsqrta
 * raise a software-assist fault for register-format operands beyond what
 * double and extended values reach, and Linux completes the instruction
 * through FPSWA.  The double-precision divisions, square roots and
 * conversions around denormals must give the IEEE results.
 */

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include "ia64-test.h"

static uint64_t bits(double d)
{
    uint64_t u;

    memcpy(&u, &d, sizeof(u));
    return u;
}

/* volatile operands keep the compiler from folding the operations. */
static volatile double denorm_min = 4.9406564584124654e-324;
static volatile double tiny = 2.2250738585072014e-308;
static volatile double huge_value = 1.7976931348623157e308;
static volatile double three = 3.0;
static volatile double two = 2.0;

/*
 * frcpa on register-format operands outside the extended range: 2.0 over
 * 2^(1 - 65535) needs the assist, which gives the exact quotient and
 * clears the predicate that would run Newton-Raphson steps.  Status field
 * 1, which Linux sets up with the widest exponent range, holds 2^65535.
 */
static void test_frcpa_assist(void)
{
    uint64_t exp, sig, pred;

    asm volatile("setf.exp f6 = %3\n\t"
                 "fadd.s1 f7 = f1, f1 ;;\n\t"
                 "frcpa.s1 f8, p6 = f7, f6 ;;\n\t"
                 "(p6) mov %2 = 1\n\t"
                 "(p6) br.cond.sptk.few 1f ;;\n\t"
                 "mov %2 = 0 ;;\n"
                 "1:\n\t"
                 "getf.exp %0 = f8\n\t"
                 "getf.sig %1 = f8 ;;"
                 : "=r"(exp), "=r"(sig), "=&r"(pred)
                 : "r"((uint64_t)1)
                 : "f6", "f7", "f8", "p6", "memory");
    CHECK_EQ(pred, 0);
    CHECK_EQ(exp, 0xffff + 65535);
    CHECK_EQ(sig, 0x8000000000000000ULL);
}

/*
 * frsqrta of 2^(3 - 65535): with the predicate cleared the assist leaves
 * the square root, 2^-32766, for the sqrt sequence to return.
 */
static void test_frsqrta_assist(void)
{
    uint64_t exp, sig, pred;

    asm volatile("setf.exp f6 = %3 ;;\n\t"
                 "frsqrta.s1 f8, p6 = f6 ;;\n\t"
                 "(p6) mov %2 = 1\n\t"
                 "(p6) br.cond.sptk.few 1f ;;\n\t"
                 "mov %2 = 0 ;;\n"
                 "1:\n\t"
                 "getf.exp %0 = f8\n\t"
                 "getf.sig %1 = f8 ;;"
                 : "=r"(exp), "=r"(sig), "=&r"(pred)
                 : "r"((uint64_t)3)
                 : "f6", "f8", "p6", "memory");
    CHECK_EQ(pred, 0);
    CHECK_EQ(exp, 0xffff - 32766);
    CHECK_EQ(sig, 0x8000000000000000ULL);
}

int main(void)
{
    volatile long double ld;
    char buf[64];
    double q;

    /* Quotients with denormal operands and results. */
    q = denorm_min / three;
    CHECK_EQ(bits(q), 0);
    q = tiny / three;
    CHECK_EQ(bits(q), 0x0005555555555555ULL);
    q = denorm_min * 3 / three;
    CHECK_EQ(bits(q), 1);
    q = huge_value / (1 / three);
    CHECK(isinf(q));
    q = 1.0 / huge_value;
    CHECK_EQ(bits(q), 0x0004000000000000ULL);
    q = huge_value / two;
    CHECK_EQ(bits(q), 0x7fdfffffffffffffULL);

    /* Square roots of denormals. */
    CHECK_EQ(bits(sqrt(denorm_min)), 0x1e60000000000000ULL);
    CHECK_EQ(bits(sqrt(tiny)), 0x2000000000000000ULL);

    /* printf and strtod walk the whole exponent range. */
    snprintf(buf, sizeof(buf), "%.17g", (double)denorm_min);
    CHECK(strcmp(buf, "4.9406564584124654e-324") == 0);
    CHECK_EQ(bits(strtod("2.2250738585072011e-308", NULL)),
             0x000fffffffffffffULL);
    snprintf(buf, sizeof(buf), "%.6e", (double)huge_value);
    CHECK(strcmp(buf, "1.797693e+308") == 0);

    /* 80-bit long double and libm. */
    ld = 1.0L / 3.0L;
    snprintf(buf, sizeof(buf), "%.20Lf", ld);
    CHECK(strcmp(buf, "0.33333333333333333334") == 0);
    CHECK(fabs(sin(1.0) - 0.8414709848078965) < 1e-15);
    CHECK(fabs(exp(1.0) - 2.718281828459045) < 1e-15);
    CHECK_EQ(bits(pow(2.0, -1074)), 1);

    test_frcpa_assist();
    test_frsqrta_assist();

    /* Exception flags survive the assist. */
    feclearexcept(FE_ALL_EXCEPT);
    q = three / 0.0 * (denorm_min > 0);
    CHECK(isinf(q));
    CHECK(fetestexcept(FE_DIVBYZERO));
    return test_finish("fp-assist");
}
