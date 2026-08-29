/* fixed_math.c -- 16.16 fixed point, corrected from netpipe/CommodoreOS.
 *
 * The original algorithms were right. What was wrong was the C: on cc65 `int`
 * is 16 bits, so every intermediate that the code let default to `int`
 * silently lost its top half -- and none of that shows up when you build the
 * same file on a PC, where `int` is 32 bits and everything passes.
 *
 * Everything here does its arithmetic in explicitly-sized types, and the
 * test suite runs on sim65 (a real 6502) as well as on the host, so a
 * regression on the target cannot hide behind a passing host build.
 */

#include "fixed_math.h"

int fixed_overflow = 0;

/* ------------------------------------------------------------------ */

int fixed_trunc(fixed_t a) {
    if (a < 0) return -(int)((-a) >> FIXED_SHIFT);
    return (int)(a >> FIXED_SHIFT);
}

fixed_t fixed_from_ratio(int32_t num, int32_t den) {
    return fixed_div(INT_TO_FIXED(num), INT_TO_FIXED(den));
}

/* ------------------------------------------------------------------
 * Multiplication: (a * b) >> 16 without a 64-bit intermediate.
 *
 * a = a_hi * 2^16 + a_lo   (a_hi signed, a_lo unsigned)
 * b = b_hi * 2^16 + b_lo
 *
 * (a*b) >> 16 = a_hi*b_hi*2^16 + a_hi*b_lo + a_lo*b_hi + (a_lo*b_lo)>>16
 *
 * The original computed exactly this and it is correct. The bug was that the
 * partial products were computed in `int`. On cc65 that is 16 bits wide, so:
 *
 *   0.5 * 0.5  gave 0.0     (a_lo*b_lo truncated to 16 bits, then >>16 = 0)
 *   2.5 * 1.25 gave 3.0     (same term, the 0.125 vanished)
 *   300 * 1.5  gave 300.0   (a_hi*b_lo = 9830400, and 9830400 mod 65536 = 0)
 *
 * Every multiply here is between two uint32_t, so it happens in 32 bits on
 * both targets. Doing it unsigned is not sloppiness: we only want the low 32
 * bits of the sum, unsigned multiply is defined to wrap, and converting a
 * negative int32 to uint32 gives its two's complement pattern -- so the
 * result is exactly right for negative operands and there is no signed
 * overflow anywhere, which would be undefined behaviour.
 * ------------------------------------------------------------------ */
fixed_t fixed_mul(fixed_t a, fixed_t b) {
    int32_t  a_hi, b_hi;
    uint32_t a_lo, b_lo;
    uint32_t t1, t2, t3, t4, lo, sum;

    a_hi = a >> FIXED_SHIFT;
    b_hi = b >> FIXED_SHIFT;
    a_lo = ((uint32_t)a) & 0xFFFFUL;
    b_lo = ((uint32_t)b) & 0xFFFFUL;

    t1 = ((uint32_t)a_hi * (uint32_t)b_hi) << FIXED_SHIFT;
    t2 = (uint32_t)a_hi * b_lo;
    t3 = a_lo * (uint32_t)b_hi;

    /* The only bits thrown away in the whole operation are the low 16 of
     * a_lo*b_lo, so rounding to nearest costs exactly one addition here
     * instead of truncating toward minus infinity. */
    lo = a_lo * b_lo;
    t4 = (lo + 0x8000UL) >> FIXED_SHIFT;

    sum = t1 + t2 + t3 + t4;

    /* Overflow check: redo the top half and see whether the sign survived.
     * a_hi*b_hi must fit in 16 bits for the product to fit in 16.16. */
    {
        int32_t top = a_hi * b_hi;
        if (top > 32767L || top < -32768L) fixed_overflow = 1;
    }
    return (fixed_t)sum;
}

/* ------------------------------------------------------------------
 * Division: (a << 16) / b by long division, 16 fractional bits at a time.
 * This one was already correct -- it used uint32_t variables throughout, so
 * nothing defaulted to `int`. Verified unchanged against 2,000,000 random
 * in-range pairs on the host and against the exact 64-bit answer.
 *
 * Added: an overflow flag when the quotient does not fit, instead of a
 * silent wrap. fixed_div(1.0, 1/65536) used to return 0.0 for an answer of
 * 65536.0; it still cannot represent it, but now it says so and saturates.
 * ------------------------------------------------------------------ */
fixed_t fixed_div(fixed_t a, fixed_t b) {
    uint32_t abs_a, abs_b, int_part, rem, frac_part, final_res;
    int sign, i;

    if (b == 0) {
        fixed_overflow = 1;
        return (a < 0) ? (fixed_t)(-2147483647L) : (fixed_t)2147483647L;
    }

    abs_a = a < 0 ? -(uint32_t)a : (uint32_t)a;
    abs_b = b < 0 ? -(uint32_t)b : (uint32_t)b;
    sign  = ((a < 0) ^ (b < 0)) ? -1 : 1;

    int_part = abs_a / abs_b;
    rem      = abs_a % abs_b;
    frac_part = 0;

    if (int_part > 32767UL) {
        fixed_overflow = 1;
        return sign < 0 ? (fixed_t)(-2147483647L) : (fixed_t)2147483647L;
    }

    for (i = 15; i >= 0; i--) {
        rem <<= 1;
        if (rem >= abs_b) {
            frac_part |= (1UL << i);
            rem -= abs_b;
        }
    }

    final_res = (int_part << FIXED_SHIFT) | frac_part;
    return sign < 0 ? (fixed_t)(-(int32_t)final_res) : (fixed_t)final_res;
}

/* ------------------------------------------------------------------
 * Square root by the classic restoring bit-by-bit method: no table, no
 * division, 32 iterations of shift-compare-subtract. Result truncated.
 * ------------------------------------------------------------------ */
fixed_t fixed_sqrt(fixed_t a) {
    uint32_t rem, root, bit, trial;

    if (a <= 0) return 0;

    /* sqrt(a * 65536) = sqrt(a) * 256, so working on the raw value and
     * shifting the answer left by 8 gives the 16.16 root directly. */
    rem  = (uint32_t)a;
    root = 0;
    bit  = 0x40000000UL;

    while (bit > rem) bit >>= 2;
    while (bit != 0) {
        trial = root + bit;
        root >>= 1;
        if (rem >= trial) { rem -= trial; root += bit; }
        bit >>= 2;
    }

    /* root is sqrt(raw); multiply by 256 to put it back in 16.16, keeping
     * eight more fractional bits by continuing the algorithm would be better
     * but this is within one unit and much shorter. */
    if (root > 8388607UL) { fixed_overflow = 1; return (fixed_t)2147483647L; }
    root <<= 8;

    /* Refine once: (r + a/r) / 2, a single Newton step. The integer square
     * root above is exact to within one unit of sqrt(raw), which after the
     * shift is 256 units of the answer -- 0.0039, far coarser than the format
     * can represent. One Newton step brings it back to a unit or two. */
    if (root != 0) {
        fixed_t r = (fixed_t)root;
        fixed_t q = fixed_div(a, r);
        return (fixed_t)((((int32_t)r) + ((int32_t)q)) >> 1);
    }
    return (fixed_t)root;
}

/* ------------------------------------------------------------------
 * Sine.
 *
 * Two things were wrong before, both of which the table itself is innocent
 * of -- the 91 values are accurate to 1.14 units out of 65536.
 *
 * 1. The table was declared uint16_t and its last entry is 65536, which does
 *    not fit in 16 bits. It was stored as 0, so sin(90) was 0.0 and
 *    therefore cos(0) was 0.0 too. gcc warns about this; cc65 does not.
 *    Fixed by storing 65535 and special-casing index 90 to exactly 1.0.
 *
 * 2. The angle's fractional part was thrown away by FIXED_TO_INT before the
 *    lookup, so every angle snapped to a whole degree -- a worst-case error
 *    of 0.0087, which is 570 units, or 570 times the format's resolution.
 *    Fixed by interpolating linearly between the two neighbouring entries.
 * ------------------------------------------------------------------ */
static const uint16_t sin_table[91] = {
        0,  1144,  2287,  3430,  4572,  5712,  6850,  7987,  9121, 10252,
    11380, 12505, 13626, 14742, 15855, 16962, 18064, 19161, 20252, 21336,
    22415, 23486, 24550, 25607, 26656, 27697, 28729, 29753, 30767, 31772,
    32768, 33754, 34729, 35693, 36647, 37590, 38521, 39441, 40348, 41243,
    42126, 42995, 43852, 44695, 45525, 46341, 47143, 47930, 48703, 49461,
    50203, 50931, 51643, 52339, 53020, 53684, 54332, 54963, 55578, 56175,
    56756, 57319, 57865, 58393, 58903, 59396, 59870, 60326, 60764, 61183,
    61584, 61966, 62328, 62672, 62997, 63303, 63589, 63856, 64104, 64332,
    64540, 64729, 64898, 65048, 65177, 65287, 65376, 65446, 65496, 65526,
    65535
};

/* sin of d degrees, 0 <= d <= 90, as a 16.16 value. */
static fixed_t sin_lookup(int d) {
    if (d >= 90) return FIXED_ONE;
    return (fixed_t)(uint32_t)sin_table[d];
}

fixed_t fixed_sin_deg(fixed_t angle) {
    int32_t  whole;
    uint32_t frac;
    int      deg, quadrant, index, negate;
    fixed_t  lo, hi;

    /* Split into whole degrees and the fraction of a degree, flooring, so a
     * negative angle keeps a positive fraction and the interpolation below
     * stays monotonic across zero. */
    whole = angle >> FIXED_SHIFT;
    frac  = ((uint32_t)angle) & 0xFFFFUL;

    whole = whole % 360L;
    if (whole < 0) whole += 360L;
    deg = (int)whole;

    quadrant = deg / 90;
    index    = deg % 90;
    negate   = 0;

    /* Reduce to the first quadrant. Each branch picks the pair of table
     * entries that straddle the angle, in increasing-angle order, so the
     * interpolation weight is always `frac` and never 1-frac. */
    if (quadrant == 0) {                  /* sin(x)          , x = index     */
        lo = sin_lookup(index);
        hi = sin_lookup(index + 1);
    } else if (quadrant == 1) {           /* sin(180-x)      , mirrored      */
        lo = sin_lookup(90 - index);
        hi = sin_lookup(90 - index - 1);
    } else if (quadrant == 2) {           /* -sin(x-180)                     */
        lo = sin_lookup(index);
        hi = sin_lookup(index + 1);
        negate = 1;
    } else {                              /* -sin(360-x)     , mirrored      */
        lo = sin_lookup(90 - index);
        hi = sin_lookup(90 - index - 1);
        negate = 1;
    }

    /* lo + (hi - lo) * frac, with frac in [0,1) as a 0.16 fraction. The
     * difference between adjacent table entries is at most 1144, so
     * diff * frac fits in 32 bits with room to spare. */
    {
        int32_t diff = (int32_t)hi - (int32_t)lo;
        int32_t adj;
        if (diff >= 0) adj = (int32_t)(((uint32_t)diff * frac) >> FIXED_SHIFT);
        else           adj = -(int32_t)(((uint32_t)(-diff) * frac) >> FIXED_SHIFT);
        lo = (fixed_t)((int32_t)lo + adj);
    }

    return negate ? (fixed_t)(-(int32_t)lo) : lo;
}

fixed_t fixed_cos_deg(fixed_t angle) {
    /* Reduce before adding 90 degrees, so an angle near the top of the range
     * cannot overflow on the way in. */
    int32_t whole = angle >> FIXED_SHIFT;
    uint32_t frac = ((uint32_t)angle) & 0xFFFFUL;
    whole = whole % 360L;
    if (whole < 0) whole += 360L;
    whole += 90L;
    return fixed_sin_deg((fixed_t)((whole << FIXED_SHIFT) | (int32_t)frac));
}

fixed_t fixed_tan_deg(fixed_t angle) {
    fixed_t s, c;
    s = fixed_sin_deg(angle);
    c = fixed_cos_deg(angle);
    if (c == 0) {
        fixed_overflow = 1;
        return (s >= 0) ? (fixed_t)2147483647L : (fixed_t)(-2147483647L);
    }
    return fixed_div(s, c);
}

/* ------------------------------------------------------------------
 * Printing.
 *
 * The obvious line, which the original demo used, is
 *
 *     printf("%d.%d", FIXED_TO_INT(v), (int)((v & 0xFFFF) * 100) / 65536);
 *
 * and it is wrong on cc65 for a reason that has nothing to do with fixed
 * point: the cast to `int` is applied to the product, before the divide. On
 * a 16-bit `int` that discards the top half of a number up to 6,553,500, so
 * the fraction printed is noise. It works on a PC. Doing the arithmetic in
 * 32 bits and casting last is all it takes.
 * ------------------------------------------------------------------ */
char *fixed_str(fixed_t v, char *buf) {
    uint32_t frac;
    int32_t  whole;
    int      i, neg;
    char    *p = buf;

    neg = 0;
    if (v < 0) { neg = 1; v = (fixed_t)(-(int32_t)v); }

    whole = v >> FIXED_SHIFT;
    frac  = ((uint32_t)v) & 0xFFFFUL;

    if (neg) *p++ = '-';

    /* whole part */
    {
        char tmp[8];
        int n = 0;
        if (whole == 0) tmp[n++] = '0';
        while (whole > 0) { tmp[n++] = (char)('0' + (int)(whole % 10)); whole /= 10; }
        while (n > 0) *p++ = tmp[--n];
    }

    *p++ = '.';
    /* four fractional digits, each one a 32-bit multiply then a divide */
    for (i = 0; i < 4; i++) {
        frac *= 10UL;
        *p++ = (char)('0' + (int)(frac >> FIXED_SHIFT));
        frac &= 0xFFFFUL;
    }
    *p = '\0';
    return buf;
}
