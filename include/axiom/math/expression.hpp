#pragma once

#include "axiom/math/export.hpp"
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
// The steps of one traced evaluation record at most this many bytes of source slices.
inline constexpr std::size_t max_explanation_length = 65536;

// Positions are zero-based byte offsets in the original input. A position at
// input.size() points just after the last character (for example a missing ')').
class AXIOM_MATH_API ExpressionError : public std::runtime_error {
public:
    ExpressionError(std::string message, std::size_t position);
    ExpressionError(const ExpressionError&) = default;
    ExpressionError& operator=(const ExpressionError&) = default;
    // Defined in the library, so the vtable and type information have a single
    // exported definition. Consumers compiled with hidden visibility can then
    // still catch errors thrown by a shared AxiomMath library by this type.
    ~ExpressionError() override;
    [[nodiscard]] std::size_t position() const noexcept;

private:
    std::size_t position_;
};

// equivalent() throws this when one of its operands is invalid. The message
// and position are those that evaluate() reports for that operand alone, and
// operand() says which input position() refers to.
class AXIOM_MATH_API OperandError : public ExpressionError {
public:
    enum class Operand : std::size_t { left, right };

    OperandError(std::string message, std::size_t position, Operand operand);
    OperandError(const OperandError&) = default;
    OperandError& operator=(const OperandError&) = default;
    // Defined in the library, like ~ExpressionError().
    ~OperandError() override;
    [[nodiscard]] Operand operand() const noexcept;

private:
    Operand operand_;
};

struct Step {
    std::string expression;
    Rational result;
};

struct Evaluation {
    Rational value;
    std::vector<Step> steps;
    // How many operations after the last recorded step were left out of steps
    // to keep them within max_explanation_length.
    std::size_t omitted_steps = 0;
};

// Evaluate decimal/integer literals, +, -, *, /, unary signs and parentheses.
// Operators use normal arithmetic precedence and left associativity. Decimals
// are exact; variables, functions, exponent notation and implicit multiplication
// are not part of this grammar. All errors carry an input position.
//
// If requested, steps contain original source slices for unary/binary operations
// in evaluation order. An unsigned literal alone has no calculation steps;
// an explicit + or - is a unary operation and contributes a step. A slice
// includes the slices of the operations inside it, so steps hold only the first
// operations whose slices total at most max_explanation_length bytes, and
// omitted_steps counts the operations after them. The value is always complete:
// collecting steps never makes an expression fail.
[[nodiscard]] AXIOM_MATH_API Evaluation evaluate(std::string_view expression,
                                                 bool collect_steps = false);

// True when both expressions have exactly the same value. The left operand is
// evaluated first; an invalid operand throws OperandError, which names it.
[[nodiscard]] AXIOM_MATH_API bool equivalent(std::string_view left, std::string_view right);

}  // namespace axiom::math
