#pragma once

#include "axiom/math/rational.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace axiom::math {

inline constexpr std::size_t max_expression_length = 4096;
inline constexpr std::size_t max_expression_depth = 64;
inline constexpr std::size_t max_expression_tokens = 1024;

// Positions are zero-based byte offsets in the original input. A position at
// input.size() points just after the last character (for example a missing ')').
class ExpressionError : public std::runtime_error {
public:
    ExpressionError(std::string message, std::size_t position);
    [[nodiscard]] std::size_t position() const noexcept;

private:
    std::size_t position_;
};

struct Step {
    std::string expression;
    Rational result;
};

struct Evaluation {
    Rational value;
    std::vector<Step> steps;
};

// Evaluate decimal/integer literals, +, -, *, /, unary signs and parentheses.
// Operators use normal arithmetic precedence and left associativity. Decimals
// are exact; variables, functions, exponent notation and implicit multiplication
// are not part of this grammar. All errors carry an input position.
//
// If requested, steps contain original source slices for unary/binary operations
// in evaluation order. An unsigned literal alone has no calculation steps;
// an explicit + or - is a unary operation and contributes a step.
[[nodiscard]] Evaluation evaluate(std::string_view expression,
                                  bool collect_steps = false);

[[nodiscard]] bool equivalent(std::string_view left, std::string_view right);

}  // namespace axiom::math
