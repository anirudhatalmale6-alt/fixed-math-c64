/* test_fixed.c -- the same self-checking suite, built twice:
 *
 *   make test        gcc, runs on this PC
 *   make test6502    cc65 + sim65, runs on a real 6502 with a 16-bit int
 *
 * Both matter, and only the second one would have caught the original bugs.
 * Every expected value below is a raw 16.16 integer worked out by hand or
 * from the exact rational, never from the library being tested.
 */

#include <stdio.h>
#include "fixed_math.h"

static int failures = 0;
static int checks   = 0;

static void eq(const char *what, fixed_t got, fixed_t want, int32_t tol) {
    int32_t d;
    char b1[16], b2[16];
    checks++;
    d = (int32_t)got - (int32_t)want;
    if (d < 0) d = -d;
    if (d > tol) {
        fixed_str(got, b1);
        fixed_str(want, b2);
        printf("FAIL %-22s got %s (%ld) want %s (%ld) off by %ld\n",
               what, b1, (long)got, b2, (long)want, (long)d);
        failures++;
    }
}

int main(void) {
    char buf[16];
    fixed_t a, b;

    printf("int is %d bytes, long is %d bytes\n",
           (int)sizeof(int), (int)sizeof(long));

    /* ---- the multiply cases the original got wrong on cc65 ---- */
    a = INT_TO_FIXED(2) + FIXED_HALF;                 /* 2.5  */
    b = INT_TO_FIXED(1) + (fixed_t)16384L;            /* 1.25 */
    eq("2.5 * 1.25 = 3.125",  fixed_mul(a, b), 204800L, 0);
    eq("0.5 * 0.5 = 0.25",    fixed_mul(FIXED_HALF, FIXED_HALF), 16384L, 0);
    eq("300 * 1.5 = 450",     fixed_mul(INT_TO_FIXED(300),
                                        INT_TO_FIXED(1) + FIXED_HALF), 29491200L, 0);
    eq("12 * 5 = 60",         fixed_mul(INT_TO_FIXED(12), INT_TO_FIXED(5)), 3932160L, 0);
    eq("-2.5 * 1.25",         fixed_mul(-a, b), -204800L, 0);
    eq("-2.5 * -1.25",        fixed_mul(-a, -b), 204800L, 0);
    eq("0.1 * 0.1 = 0.01",    fixed_mul(6554L, 6554L), 655L, 1);
    eq("1 * x = x",           fixed_mul(FIXED_ONE, 12345L), 12345L, 0);
    eq("0 * x = 0",           fixed_mul(0, 12345L), 0, 0);

    /* ---- division, which was already correct ---- */
    eq("2.5 / 1.25 = 2",      fixed_div(a, b), 131072L, 0);
    eq("1 / 3",               fixed_div(FIXED_ONE, INT_TO_FIXED(3)), 21845L, 0);
    eq("12 / 5 = 2.4",        fixed_div(INT_TO_FIXED(12), INT_TO_FIXED(5)), 157286L, 1);
    eq("-1 / 3",              fixed_div(-FIXED_ONE, INT_TO_FIXED(3)), -21845L, 0);
    eq("1 / -3",              fixed_div(FIXED_ONE, INT_TO_FIXED(-3)), -21845L, 0);
    eq("-1 / -3",             fixed_div(-FIXED_ONE, INT_TO_FIXED(-3)), 21845L, 0);

    /* ---- the trig values the original returned as zero ---- */
    eq("sin(0) = 0",          fixed_sin_deg(0), 0, 0);
    eq("sin(30) = 0.5",       fixed_sin_deg(INT_TO_FIXED(30)), 32768L, 2);
    eq("sin(90) = 1",         fixed_sin_deg(INT_TO_FIXED(90)), 65536L, 2);
    eq("sin(150) = 0.5",      fixed_sin_deg(INT_TO_FIXED(150)), 32768L, 2);
    eq("sin(180) = 0",        fixed_sin_deg(INT_TO_FIXED(180)), 0, 2);
    eq("sin(210) = -0.5",     fixed_sin_deg(INT_TO_FIXED(210)), -32768L, 2);
    eq("sin(270) = -1",       fixed_sin_deg(INT_TO_FIXED(270)), -65536L, 2);
    eq("sin(330) = -0.5",     fixed_sin_deg(INT_TO_FIXED(330)), -32768L, 2);
    eq("sin(360) = 0",        fixed_sin_deg(INT_TO_FIXED(360)), 0, 2);
    eq("sin(-90) = -1",       fixed_sin_deg(INT_TO_FIXED(-90)), -65536L, 2);
    eq("cos(0) = 1",          fixed_cos_deg(0), 65536L, 2);
    eq("cos(60) = 0.5",       fixed_cos_deg(INT_TO_FIXED(60)), 32768L, 2);
    eq("cos(90) = 0",         fixed_cos_deg(INT_TO_FIXED(90)), 0, 2);
    eq("cos(180) = -1",       fixed_cos_deg(INT_TO_FIXED(180)), -65536L, 2);

    /* ---- the fractional angle the original discarded ----
     * sin(30.5) = 0.507538, raw 33262. The original returned sin(30). */
    eq("sin(30.5)",           fixed_sin_deg(INT_TO_FIXED(30) + FIXED_HALF), 33262L, 40);
    eq("sin(45.5)",           fixed_sin_deg(INT_TO_FIXED(45) + FIXED_HALF), 46744L, 40);
    eq("sin(89.5)",           fixed_sin_deg(INT_TO_FIXED(89) + FIXED_HALF), 65533L, 40);

    /* ---- sqrt ---- */
    eq("sqrt(4) = 2",         fixed_sqrt(INT_TO_FIXED(4)), 131072L, 2);
    eq("sqrt(2)",             fixed_sqrt(INT_TO_FIXED(2)), 92682L, 2);
    eq("sqrt(1) = 1",         fixed_sqrt(FIXED_ONE), 65536L, 2);
    eq("sqrt(10000) = 100",   fixed_sqrt(INT_TO_FIXED(10000)), 6553600L, 4);
    eq("sqrt(0.25) = 0.5",    fixed_sqrt(16384L), 32768L, 2);

    /* ---- pythagoras, the thing this is actually for ---- */
    {
        fixed_t x = INT_TO_FIXED(3), y = INT_TO_FIXED(4);
        fixed_t d = fixed_sqrt(fixed_add(fixed_mul(x, x), fixed_mul(y, y)));
        eq("hypot(3,4) = 5", d, INT_TO_FIXED(5), 4);
    }

    /* ---- printing, which the original's one-liner got wrong on cc65 ---- */
    checks++;
    fixed_str(fixed_mul(a, b), buf);
    if (buf[0] != '3' || buf[1] != '.' || buf[2] != '1' || buf[3] != '2' || buf[4] != '5') {
        printf("FAIL fixed_str(3.125) gave \"%s\"\n", buf); failures++;
    }
    checks++;
    fixed_str(-fixed_mul(a, b), buf);
    if (buf[0] != '-' || buf[1] != '3' || buf[3] != '1') {
        printf("FAIL fixed_str(-3.125) gave \"%s\"\n", buf); failures++;
    }

    /* ---- overflow is reported rather than wrapping silently ---- */
    fixed_overflow = 0;
    fixed_mul(INT_TO_FIXED(200), INT_TO_FIXED(200));     /* 40000, will not fit */
    checks++;
    if (!fixed_overflow) { printf("FAIL 200*200 did not set the overflow flag\n"); failures++; }

    fixed_overflow = 0;
    fixed_div(FIXED_ONE, 1L);                            /* 65536.0, will not fit */
    checks++;
    if (!fixed_overflow) { printf("FAIL 1/(1/65536) did not set the overflow flag\n"); failures++; }

    fixed_overflow = 0;
    fixed_mul(INT_TO_FIXED(100), INT_TO_FIXED(100));     /* 10000, fits */
    checks++;
    if (fixed_overflow) { printf("FAIL 100*100 wrongly reported overflow\n"); failures++; }

    printf("%d checks, %d failures\n", checks, failures);
    if (failures == 0) printf("PASS: fixed point, same answers on this target as on any other\n");
    else               printf("FAILED\n");
    printf("TESTDONE\n");
    return failures ? 1 : 0;
}
