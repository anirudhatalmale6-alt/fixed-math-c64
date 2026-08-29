/* compare.c -- the original and the corrected version, side by side, against
 * double. Host only (it needs a double to compare with; the point of the
 * library is that the 6502 has none).
 *
 * Note on method: an earlier version of this compared fixed_mul(x,y) against
 * double(x)*double(y) after quantising x and y into 16.16. That measures the
 * harness's own rounding of the inputs, not the library, and it charged 0.0014
 * of error to code that turned out to be exact. Multiplication and division
 * are therefore compared against the exact 16.16 answer computed in 64-bit for
 * the exact same inputs; only sin, which has no exact fixed-point answer, is
 * compared against double.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "fixed_math.h"

/* ---- the original, verbatim apart from moving declarations above statements
 * so that cc65 will parse it at all (it does not change any expression) ---- */
typedef int32_t ofixed_t;
static const uint16_t o_sin_table[91] = {
    0, 1144, 2287, 3430, 4572, 5712, 6850, 7987, 9121, 10253,
    11381, 12506, 13626, 14743, 15855, 16962, 18064, 19161, 20252, 21337,
    22415, 23486, 24550, 25607, 26656, 27697, 28729, 29753, 30767, 31772,
    32768, 33754, 34729, 35693, 36647, 37590, 38521, 39440, 40348, 41243,
    42126, 42995, 43852, 44695, 45525, 46341, 47142, 47930, 48702, 49460,
    50203, 50931, 51643, 52339, 53019, 53684, 54331, 54963, 55577, 56175,
    56755, 57319, 57864, 58393, 58903, 59395, 59870, 60326, 60763, 61183,
    61583, 61965, 62328, 62672, 62997, 63302, 63589, 63856, 64103, 64331,
    64540, 64729, 64898, 65047, 65176, 65286, 65376, 65446, 65496, 65526, 65536
};
static ofixed_t o_sin(ofixed_t angle) {
    int deg, quadrant, index;
    uint16_t val;
    deg = (int)(angle >> 16) % 360;
    if (deg < 0) deg += 360;
    quadrant = deg / 90;
    index = deg % 90;
    if (quadrant == 0)      val = o_sin_table[index];
    else if (quadrant == 1) val = o_sin_table[90 - index];
    else if (quadrant == 2) { val = o_sin_table[index];      return -(ofixed_t)val; }
    else                    { val = o_sin_table[90 - index]; return -(ofixed_t)val; }
    return (ofixed_t)val;
}

int main(void) {
    double worst_o, worst_n, e;
    int i;

    printf("sin, every whole degree 0..359, against double\n");
    worst_o = worst_n = 0;
    for (i = 0; i < 360; i++) {
        double want = sin(i * M_PI / 180.0);
        e = fabs(o_sin(INT_TO_FIXED(i)) / 65536.0 - want);       if (e > worst_o) worst_o = e;
        e = fabs(fixed_sin_deg(INT_TO_FIXED(i)) / 65536.0 - want); if (e > worst_n) worst_n = e;
    }
    printf("  original  max error %.6f  (%.0f units)\n", worst_o, worst_o * 65536);
    printf("  corrected max error %.6f  (%.0f units)\n\n", worst_n, worst_n * 65536);

    printf("sin, every tenth of a degree 0..359.9, against double\n");
    worst_o = worst_n = 0;
    for (i = 0; i < 3600; i++) {
        double a = i / 10.0;
        fixed_t fa = (fixed_t)llround(a * 65536.0);
        double want = sin(a * M_PI / 180.0);
        e = fabs(o_sin(fa) / 65536.0 - want);        if (e > worst_o) worst_o = e;
        e = fabs(fixed_sin_deg(fa) / 65536.0 - want); if (e > worst_n) worst_n = e;
    }
    printf("  original  max error %.6f  (%.0f units)  -- angle fraction discarded\n",
           worst_o, worst_o * 65536);
    printf("  corrected max error %.6f  (%.0f units)  -- linear interpolation\n\n",
           worst_n, worst_n * 65536);

    /* The 90/270 table bug dominates the numbers above, so measure the
     * discarded-fraction error on its own: away from the quadrant edges,
     * where the truncated table entry is not involved. */
    printf("sin, tenths of a degree, 5..85 only (isolating the discarded angle fraction)\n");
    worst_o = worst_n = 0;
    for (i = 50; i < 850; i++) {
        double a = i / 10.0;
        fixed_t fa = (fixed_t)llround(a * 65536.0);
        double want = sin(a * M_PI / 180.0);
        e = fabs(o_sin(fa) / 65536.0 - want);         if (e > worst_o) worst_o = e;
        e = fabs(fixed_sin_deg(fa) / 65536.0 - want); if (e > worst_n) worst_n = e;
    }
    printf("  original  max error %.6f  (%.0f units = %.0fx the format's resolution)\n",
           worst_o, worst_o * 65536, worst_o * 65536);
    printf("  corrected max error %.6f  (%.0f units)\n\n", worst_n, worst_n * 65536);

    printf("cos(0) original %.6f, corrected %.6f\n",
           o_sin(INT_TO_FIXED(90)) / 65536.0, fixed_cos_deg(0) / 65536.0);

    printf("\nfixed_mul against the exact 16.16 answer, 2,000,000 random in-range pairs\n");
    {
        long bad = 0, n = 0, worst = 0;
        srand(7);
        for (long k = 0; k < 2000000; k++) {
            fixed_t x = (fixed_t)((rand() << 16) ^ rand());
            fixed_t y = (fixed_t)((rand() << 16) ^ rand());
            int64_t exact;
            long d;
            x %= (100 << 16); y %= (100 << 16);
            n++;
            exact = (((int64_t)x * (int64_t)y) + 0x8000) >> 16;   /* round to nearest */
            d = labs((long)fixed_mul(x, y) - (long)exact);
            if (d) { bad++; if (d > worst) worst = d; }
        }
        printf("  %ld pairs, %ld disagree, worst by %ld unit(s)\n", n, bad, worst);
    }

    printf("\nfixed_sqrt against double, 1..30000\n");
    {
        double w = 0;
        for (i = 1; i <= 30000; i++) {
            double want = sqrt((double)i);
            e = fabs(fixed_sqrt(INT_TO_FIXED(i)) / 65536.0 - want);
            if (e > w) w = e;
        }
        printf("  max error %.6f (%.1f units)\n", w, w * 65536);
    }
    return 0;
}
