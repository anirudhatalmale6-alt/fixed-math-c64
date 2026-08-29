/* fixed_math.h -- 16.16 fixed point for cc65 and for hosted C.
 *
 * Format: a signed 32-bit integer holding value * 65536.
 * Range:  -32768.0 .. +32767.99998, resolution 1/65536 = 0.0000153.
 *
 * Written in C89 with declarations before statements, because cc65 rejects
 * declarations after statements in BOTH its default mode and --standard c99.
 */
#ifndef FIXED_MATH_H
#define FIXED_MATH_H

#include <stdint.h>

typedef int32_t fixed_t;

#define FIXED_SHIFT 16
#define FIXED_ONE   ((fixed_t)65536L)
#define FIXED_HALF  ((fixed_t)32768L)

#define INT_TO_FIXED(x) ((fixed_t)((int32_t)(x) << FIXED_SHIFT))

/* Rounds toward minus infinity, because >> on a negative value does.
 * FIXED_TO_INT(-0.5) is -1, not 0. That is deliberate and consistent; if you
 * want truncation toward zero use fixed_trunc(). */
#define FIXED_TO_INT(x) ((int)((x) >> FIXED_SHIFT))

#define fixed_add(a, b) ((fixed_t)((a) + (b)))
#define fixed_sub(a, b) ((fixed_t)((a) - (b)))
#define fixed_neg(a)    ((fixed_t)(-(a)))
#define fixed_abs(a)    ((fixed_t)((a) < 0 ? -(a) : (a)))

int      fixed_trunc(fixed_t a);          /* toward zero */
fixed_t  fixed_from_ratio(int32_t num, int32_t den);

fixed_t  fixed_mul(fixed_t a, fixed_t b);
fixed_t  fixed_div(fixed_t a, fixed_t b);
fixed_t  fixed_sqrt(fixed_t a);

/* Angle in degrees, itself a 16.16 value, so 30.5 degrees is representable
 * and IS used -- the fractional part is interpolated, not discarded. */
fixed_t  fixed_sin_deg(fixed_t angle);
fixed_t  fixed_cos_deg(fixed_t angle);
fixed_t  fixed_tan_deg(fixed_t angle);

/* Print a fixed_t as d.dddd into buf (needs 16 bytes). Returns buf.
 * Exists because the obvious one-liner is a trap on cc65 -- see README. */
char    *fixed_str(fixed_t v, char *buf);

/* Set whenever a result did not fit in 16.16. Sticky; clear it yourself. */
extern int fixed_overflow;

#endif
