# Approximate answer-checking policy

Exact comparison remains the default. `Rational` equality, `operator<=>`,
`equivalent()` and `axiom-math check` compare exact values and are unchanged by
this policy: `1/3` and `0.333` are still *not equivalent*. Accepting an answer
that is close but not equal is a separate decision with its own API, its own CLI
command and its own tests, so a caller has to ask for it explicitly.

## Policy

A check compares an **expected** value `e` (the answer key) with a submitted
**answer** `a` under a tolerance made of two non-negative rationals:

- `absolute`: the largest accepted distance, in the units of the answer.
- `relative`: the largest accepted distance as a fraction of `|e|`.

The answer is accepted when

```text
|a - e| <= absolute    or    |a - e| <= relative * |e|
```

Every quantity is an exact rational. The decision is made without rounding and
without floating point, so no answer is accepted or rejected because of a binary
representation error.

The rules that follow from this definition, and the reasons for them:

1. **Boundaries are inclusive.** A distance exactly equal to a bound is
   accepted. A published tolerance such as "within 0.001" reads naturally as
   inclusive, and with exact arithmetic the boundary is a precise value rather
   than a rounding accident.
2. **Either bound accepts.** The policy accepts if the answer is within the
   absolute bound *or* the relative bound, the same combination as Python's
   `math.isclose`. A caller who wants only one kind of bound sets the other to 0.
3. **The relative bound scales with the expected value only.** `math.isclose`
   scales by the larger of both magnitudes, which is symmetric but lets a wildly
   wrong answer widen its own tolerance. Here the answer key alone sets the
   scale, so the check is deliberately not symmetric: under a 1% relative
   tolerance, 99 is accepted for an expected 100, but 100 is not accepted for an
   expected 99 (the bound there is 0.99).
4. **Zero tolerances mean exact equality.** With both bounds 0, the check accepts
   exactly when `a == e`, so it agrees with exact comparison.
5. **At an expected value of 0, only the absolute bound applies.** `relative * |0|`
   is 0, so a relative tolerance alone accepts only an exact 0. Set an absolute
   bound when 0 is a possible answer.
6. **Negative values behave like positive ones.** Negating both `e` and `a` never
   changes the decision; only distances and magnitudes are compared. The
   relative bound is below `|e|` (rule 7), so an answer it accepts is never 0
   and has the sign of a nonzero `e`. The absolute bound is a plain distance and
   can accept an answer of the other sign near 0, for example `-0.0001` for an
   expected `0.0001` with an absolute bound of `0.001`.
7. **Invalid tolerances are rejected, not clamped.** A negative `absolute`, a
   negative `relative`, or a `relative` of 1 or more throws
   `std::invalid_argument` (CLI exit status 2). A relative bound of 1 would
   accept 0 as an answer to every question, and 2 would accept `-e`; neither is a
   meaningful answer check, so it is treated as a configuration error.
   The check never silently changes a tolerance.
8. **Overflow cannot change a decision.** For every pair of representable
   `Rational` values and every valid tolerance, the check returns a decision and
   never throws `std::overflow_error`, even when `a - e` or `relative * |e|` is
   itself not representable as a `Rational`. For example, the distance between
   `1/9223372036854775807` and `1/9223372036854775806` has a denominator near
   2^126. The comparison is evaluated with cross products of at most 192 bits,
   which hold every case exactly (see [the bounds](#exact-evaluation)). Overflow
   can still occur earlier, while an *expression* is evaluated; that is reported
   as it is for `eval`, before any comparison.

## API

```cpp
#include <axiom/math/tolerance.hpp>

namespace axiom::math {
struct Tolerance {
    Rational absolute{};  // >= 0
    Rational relative{};  // >= 0 and < 1
};

// Throws std::invalid_argument for an invalid tolerance; never overflows.
bool within_tolerance(const Rational& expected, const Rational& answer,
                      const Tolerance& tolerance);

// Validates the tolerance first, then evaluates expected (reported as the left
// operand of an OperandError) and answer (the right operand), like equivalent().
bool approximately_equivalent(std::string_view expected, std::string_view answer,
                              const Tolerance& tolerance);
}
```

The CLI command is

```console
$ axiom-math approx EXPECTED ANSWER ABSOLUTE RELATIVE
```

Each argument is an expression. It prints `within tolerance` (exit 0) or
`outside tolerance` (exit 1); invalid input, an invalid tolerance, overflow while
evaluating an argument, or an output failure exits with status 2.

```console
$ axiom-math approx "1/3" "0.333" 0.001 0
within tolerance
$ axiom-math approx "1/3" "0.333" 0 0.000999
outside tolerance
$ axiom-math check "1/3" "0.333"
not equivalent
```

## Worked examples

The verdict is `accept`, `reject`, or `invalid` (the tolerance is rejected before
any comparison). `tests/tolerance_oracle.py` reads this table, recomputes each
verdict with Python's `fractions.Fraction` and runs every row through the CLI,
so the table and the implementation cannot drift apart silently.

<!-- tolerance-examples:start -->
| Expected | Answer | Absolute | Relative | Verdict | Why |
| --- | --- | --- | --- | --- | --- |
| 1/3 | 0.333 | 0 | 0 | reject | Zero tolerances are exact equality; the distance is 1/3000. |
| 1/3 | 0.333 | 0.001 | 0 | accept | 1/3000 is below 1/1000. |
| 1/3 | 0.333 | 0.0003 | 0 | reject | 1/3000 is above 3/10000. |
| 1/3 | 0.333 | 0 | 0.001 | accept | Boundary: 0.001 × 1/3 is exactly 1/3000. |
| 1/3 | 0.333 | 0 | 0.000999 | reject | The relative bound is 333/1000000, just below 1/3000. |
| 1/3 | 0.3334 | 0 | 0.001 | accept | The distance 1/15000 is within 1/3000. |
| -1/3 | -0.333 | 0 | 0.001 | accept | Negating both values does not change the decision. |
| 1 | 1.1 | 0.1 | 0 | accept | Boundary from above. Binary floating point computes 0.10000000000000009 and would reject it. |
| 1 | 0.9 | 0.1 | 0 | accept | Boundary from below. |
| 1 | 1.100000000000000001 | 0.1 | 0 | reject | 10^-18 beyond the boundary. |
| 100 | 99 | 0 | 0.01 | accept | Boundary: 1% of 100 is 1. |
| 100 | 101.000000000000001 | 0 | 0.01 | reject | 10^-15 beyond the relative boundary. |
| 99 | 100 | 0 | 0.01 | reject | The bound scales with the expected value: 1% of 99 is 0.99. |
| -100 | -99 | 0 | 0.01 | accept | Negative values use magnitudes. |
| -100 | 99 | 0 | 0.01 | reject | A sign error is far outside a relative bound. |
| 2 | 2.004 | 0.001 | 0.002 | accept | Either bound accepts: the relative bound is exactly 0.004. |
| 2 | 2.004 | 0.001 | 0 | reject | The absolute bound alone is too small. |
| 0 | 0.000001 | 0 | 0.5 | reject | At an expected 0, a relative bound accepts only 0. |
| 0 | -0.000001 | 0.000001 | 0 | accept | Boundary at zero, from the negative side. |
| 0 | 0 | 0 | 0.5 | accept | An exact 0 is always accepted. |
| 0.0001 | -0.0001 | 0.001 | 0 | accept | The absolute bound can cross zero. |
| 1/9223372036854775807 | 1/9223372036854775806 | 1/9223372036854775807 | 0 | accept | The distance is not a representable Rational; the decision is still exact. |
| 1/9223372036854775807 | 1/9223372036854775806 | 0 | 0 | reject | Distinct full-width values are not equal. |
| 9223372036854775807 | -9223372036854775808 | 9223372036854775807 | 0.999999999999999999 | reject | The distance 2^64 - 1 exceeds both bounds and does not overflow. |
| -9223372036854775808 | -9223372036854775807 | 0 | 0.000000000000000001 | accept | 10^-18 of 2^63 is about 9.22, so a distance of 1 is within it. |
| -9223372036854775808 | -9223372036854775798 | 0 | 0.000000000000000001 | reject | A distance of 10 is beyond that bound. |
| 5 | 5 | -0.001 | 0 | invalid | A negative absolute bound is rejected. |
| 5 | 5 | 0 | -0.001 | invalid | A negative relative bound is rejected. |
| 5 | 0 | 0 | 1 | invalid | A relative bound of 1 would accept 0 for every question. |
<!-- tolerance-examples:end -->

## Exact evaluation

Write `e = en/ed`, `a = an/ad`, `absolute = tn/td` and `relative = rn/rd`, each
reduced with a positive denominator below 2^63 and a numerator magnitude of at
most 2^63. Multiplying both sides of each bound by positive denominators gives
integer comparisons:

```text
|an*ed - en*ad| * td  <=  tn * ad * ed
|an*ed - en*ad| * rd  <=  rn * |en| * ad
```

`|an*ed - en*ad|` is below 2^127, the same bound the subtraction operator uses,
so it is computed in 128 bits. Each side is then a product of that value, or of
two 63-bit factors, with one more factor below 2^64, and so lies below 2^191.
The implementation multiplies a 128-bit value by a 64-bit value into three
64-bit limbs and compares the limbs, so no intermediate is truncated.
