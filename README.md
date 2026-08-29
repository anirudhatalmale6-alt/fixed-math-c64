# 16.16 fixed point for cc65 — audit and correction

This is a review of, and a corrected replacement for,
[`fixed_math.c`](https://github.com/netpipe/CommodoreOS/blob/main/fixed_math.c)
from CommodoreOS.

```sh
make check      # builds and runs the suite twice: gcc here, and cc65 on a 6502
make compare    # the original next to the corrected version, against double
```

Both need `cc65` (`apt install cc65`), which brings `sim65`, a 6502 simulator.

## Summary

The algorithms were right. The C was wrong, and wrong in a way that a build on
a PC cannot show you: **on cc65 `int` is 16 bits**, and every intermediate the
original let default to `int` quietly lost its top half.

| | on a PC (`int` = 32 bits) | on a 6502 (`int` = 16 bits) |
|---|---|---|
| does it compile | yes | **no** |
| `12 * 5` | 60 ✓ | 60 ✓ |
| `2.5 * 1.25` | 3.125 ✓ | **3.0** |
| `0.5 * 0.5` | 0.25 ✓ | **0.0** |
| `300 * 1.5` | 450 ✓ | **300.0** |
| `sin(90)` | **0.0** | **0.0** |
| `cos(0)` | **0.0** | **0.0** |
| the demo's own decimal printer | `3.12` ✓ | **`3.0`** |

Those are measured, not predicted — the middle column is gcc, the right-hand
column is `sim65` running the real 6502 code that `cc65` emitted.

## The findings, in order of how much they cost

### 1. It does not compile with cc65 at all

```
orig.c(56): Error: Variable identifier expected
orig.c(56): Error: Undefined symbol: 'abs_a'
```

`fixed_div` opens with `if (b == 0) return ...;` and then declares
`uint32_t abs_a`. cc65 rejects a declaration after a statement, and — this is
the part worth knowing — it still rejects it under `--standard c99`. cc65's
C99 support does not include mixed declarations. `fixed_sin_deg` and
`fixed_tan_deg` have the same shape, and `for (int i = 15; ...)` in `fixed_div`
is the same problem again.

Everything here is C89 with declarations first, so it builds in cc65's default
mode with no flags.

### 2. `sin(90)` is zero, and therefore `cos(0)` is zero

```c
static const uint16_t sin_table[91] = { 0, 1144, ..., 65526, 65536 };
```

The last entry is 65536. A `uint16_t` holds 65535. It is stored as **0**, so
`fixed_sin_deg(90)` returns 0.0, and since `cos(x) = sin(x + 90)`,
`fixed_cos_deg(0)` returns 0.0 as well. Any rotation, any circle, any
projection built on this collapses at the cardinal angles.

The table's *numbers* are not the problem — they are accurate to 1.14 units
out of 65536, which is excellent. The container is one bit too small for the
last one.

gcc says so, if you build it on a PC:

```
warning: unsigned conversion from 'int' to 'short unsigned int'
         changes value from '65536' to '0' [-Woverflow]
```

cc65 does not warn. Fixed by storing 65535 and special-casing index 90 to
exactly `FIXED_ONE`, which keeps the table at 182 bytes rather than 364.

### 3. The multiply loses its low half on the target

The decomposition is correct:

```
(a*b) >> 16 = a_hi*b_hi*2^16 + a_hi*b_lo + a_lo*b_hi + (a_lo*b_lo)>>16
```

but the partial products were computed in `int`:

```c
uint32_t term4 = ((uint32_t)(a_lo * b_lo)) >> 16;
```

The cast is on the wrong side of the multiply. `a_lo * b_lo` happens first, in
`int`. On a 6502 that is 16 bits wide, so the product is truncated before the
cast ever sees it and `term4` is **always zero** — which is why `0.5 * 0.5`
gives 0.0 and `2.5 * 1.25` gives 3.0. `term2` fails the same way whenever
`a_hi * b_lo` exceeds 65535, which is what turns `300 * 1.5` into 300.

Every multiply in the corrected version is between two `uint32_t`, so it is a
32-bit multiply on both targets. Doing it unsigned is deliberate: only the low
32 bits of the sum are wanted, unsigned multiplication is defined to wrap, and
converting a negative `int32_t` to `uint32_t` gives its two's complement
pattern — so negative operands come out right *and* there is no signed
overflow, which would be undefined behaviour rather than merely wrong.

Rounding to nearest was added at the same time. The only bits discarded in the
whole operation are the low 16 of `a_lo*b_lo`, so it costs one addition.

### 4. Every angle snapped to a whole degree

```c
int deg = FIXED_TO_INT(angle) % 360;
```

The angle is a 16.16 value and its fractional part was thrown away before the
lookup, so `sin(30.5°)` returned `sin(30°)`. Measured away from the quadrant
edges, where the table bug is not also involved:

| | max error over 5°–85° in tenths of a degree |
|---|---|
| original | 0.015634 — **1025 units**, a thousand times the format's resolution |
| corrected | 0.000055 — 4 units |

Linear interpolation between the two neighbouring table entries, which is one
subtract, one multiply and one shift.

### 5. The demo's decimal printer has the same bug as the multiply

```c
cprintf("%d.%d\r\n", FIXED_TO_INT(prod), (int)((prod & 0xFFFF) * 100) / 65536);
```

`(int)` is applied to the product, not to the quotient. On cc65 that truncates
a value up to 6,553,500 to 16 bits and then divides, so the fraction printed is
noise — the demo prints `3.0` for 3.125 on the machine it was written for, and
`3.12` on the machine it was tested on. `fixed_str()` does the arithmetic in
32 bits and casts last.

### 6. `fixed_div` was already correct

Worth saying plainly, because it is the most intricate function in the file:
the long-division loop is right. It uses `uint32_t` variables throughout, so
nothing defaulted to `int`, and it survives the 16-bit target unchanged. Two
million random in-range pairs agree exactly with the 64-bit answer, including
every sign combination.

I nearly reported an error in it that was not there. The first version of my
harness compared `fixed_mul(x,y)` against `double(x)*double(y)` after
quantising `x` and `y` into 16.16, and reported a worst-case error of 0.0014 —
which was entirely the harness's own rounding of the inputs. Comparing against
the exact 16.16 answer for the exact same inputs, computed in 64-bit, both
functions come out perfect. A comparison only proves what it compares.

### 7. Overflow was silent

`fixed_mul(200, 200)` wants 40000, which does not fit in 16.16, and returned
−25536. `fixed_div(1.0, 1/65536)` wants 65536.0 and returned 0.0. There is now
a sticky `fixed_overflow` flag and the division saturates instead of wrapping.

## What is in here

| file | |
|---|---|
| `fixed_math.h` / `fixed_math.c` | the corrected library, C89, no dependencies beyond `stdint.h` |
| `test_fixed.c` | 43 checks, built and run on **both** targets |
| `compare.c` | original vs corrected vs `double`, host only |
| `Makefile` | `make check` runs both suites |

Added while in there: `fixed_sqrt` (restoring bit-by-bit integer root plus one
Newton step, max error 1 unit over 1..30000), `fixed_trunc`,
`fixed_from_ratio`, and `fixed_str`.

## Why the suite runs twice

Every bug above passes a host build. If the tests only ran under gcc they would
all be green and the C64 would still be drawing collapsed circles. `make check`
runs the identical suite under `cc65` on `sim65`, and the two runs must both
pass:

```
== gcc, this machine ==
int is 4 bytes, long is 8 bytes
43 checks, 0 failures
== cc65 + sim65, a real 6502 ==
int is 2 bytes, long is 4 bytes
43 checks, 0 failures
```

Three deliberate sabotages confirm the checks bite rather than merely pass:

| sabotage | caught by |
|---|---|
| partial products back to `int` width | 6 multiply checks and both `fixed_str` checks, on the 6502 only |
| 65536 back in the `uint16_t` table | 6 trig checks, on both targets |
| angle fraction discarded again | the `sin(30.5)` and `sin(45.5)` checks |

## Range and accuracy

16.16 covers −32768.0 to +32767.99998 with a resolution of 1/65536 =
0.0000153.

| operation | worst error |
|---|---|
| `fixed_mul` | exact (2,000,000 random in-range pairs agree with the 64-bit answer) |
| `fixed_div` | exact, truncating toward zero |
| `fixed_sin_deg` / `fixed_cos_deg` | 4 units, 0.000056 |
| `fixed_sqrt` | 1 unit, 0.000015 |
