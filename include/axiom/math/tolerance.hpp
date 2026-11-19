#pragma once

#include "axiom/math/export.hpp"
#include "axiom/math/expression.hpp"
#include "axiom/math/rational.hpp"

#include <string_view>

namespace axiom::math {

// An approximate answer-checking tolerance. docs/answer-checking.md specifies
// the policy. Exact comparison (==, <=>, equivalent()) does not use it.
struct Tolerance {
    // The largest accepted distance |answer - expected|. Must be >= 0.
    Rational absolute{};
    // The largest accepted distance as a fraction of |expected|. Must be >= 0
    // and < 1: a bound of 1 would accept 0 as an answer to every question.
    Rational relative{};
};

// True when |answer - expected| <= absolute or |answer - expected| <=
// relative * |expected|, decided exactly. Both boundaries are inclusive, and
// zero tolerances make this exact equality. The relative bound scales with the
// expected value only, so the check is deliberately not symmetric.
//
// An invalid tolerance throws std::invalid_argument. For valid arguments this
// never throws: the distance and bounds are compared with exact intermediates,
// even where they are not representable as a Rational.
[[nodiscard]] AXIOM_MATH_API bool within_tolerance(const Rational& expected, const Rational& answer,
                                                   const Tolerance& tolerance);

// Evaluates both expressions and applies within_tolerance(). The tolerance is
// validated first (std::invalid_argument). The expected expression is then
// evaluated before the answer; an invalid one throws OperandError naming it as
// Operand::left (expected) or Operand::right (answer), as equivalent() does.
[[nodiscard]] AXIOM_MATH_API bool approximately_equivalent(std::string_view expected, std::string_view answer,
                                                           const Tolerance& tolerance);

}  // namespace axiom::math
