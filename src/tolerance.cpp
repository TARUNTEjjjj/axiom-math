#include "axiom/math/tolerance.hpp"

#include <cstdint>
#include <stdexcept>

namespace axiom::math {
namespace {

#if !defined(__SIZEOF_INT128__)
#error "AxiomMath requires compiler support for 128-bit integer intermediates."
#endif
__extension__ using Wide = __int128;
__extension__ using UnsignedWide = unsigned __int128;

UnsignedWide magnitude(Wide value) noexcept {
    // Unlike -value, this is defined even for the most negative signed value.
    return value < 0 ? static_cast<UnsignedWide>(-(value + 1)) + 1
                     : static_cast<UnsignedWide>(value);
}

// An unsigned value below 2^192 as three 64-bit limbs, most significant first.
struct Triple {
    std::uint64_t high;
    std::uint64_t middle;
    std::uint64_t low;

    friend bool operator<=(const Triple& left, const Triple& right) noexcept {
        if (left.high != right.high) return left.high < right.high;
        if (left.middle != right.middle) return left.middle < right.middle;
        return left.low <= right.low;
    }
};

// The exact product of a 128-bit and a 64-bit value. Each partial product is
// below 2^128, and the upper one plus the carry from the lower one is too.
Triple multiply(UnsignedWide wide, std::uint64_t factor) noexcept {
    const UnsignedWide lower = static_cast<UnsignedWide>(static_cast<std::uint64_t>(wide)) * factor;
    const UnsignedWide upper = static_cast<UnsignedWide>(static_cast<std::uint64_t>(wide >> 64)) * factor +
                               (lower >> 64);
    return {static_cast<std::uint64_t>(upper >> 64), static_cast<std::uint64_t>(upper),
            static_cast<std::uint64_t>(lower)};
}

std::uint64_t positive(std::int64_t value) noexcept {
    return static_cast<std::uint64_t>(value);
}

void validate(const Tolerance& tolerance) {
    if (tolerance.absolute < 0) {
        throw std::invalid_argument("absolute tolerance must not be negative");
    }
    if (tolerance.relative < 0) {
        throw std::invalid_argument("relative tolerance must not be negative");
    }
    if (tolerance.relative >= 1) {
        throw std::invalid_argument("relative tolerance must be less than 1");
    }
}

Rational operand_value(std::string_view text, OperandError::Operand operand) {
    try {
        return evaluate(text).value;
    } catch (const ExpressionError& error) {
        throw OperandError(error.what(), error.position(), operand);
    }
}

}  // namespace

bool within_tolerance(const Rational& expected, const Rational& answer, const Tolerance& tolerance) {
    validate(tolerance);
    // With e = en/ed and a = an/ad, |a - e| = |an*ed - en*ad| / (ad*ed). Each
    // cross product has magnitude at most 2^63 * (2^63 - 1), so the difference
    // is below 2^127 and fits. Multiplying each bound by the positive
    // denominators leaves integer products below 2^191 (docs/answer-checking.md).
    const UnsignedWide distance = magnitude(static_cast<Wide>(answer.numerator()) * expected.denominator() -
                                            static_cast<Wide>(expected.numerator()) * answer.denominator());
    const UnsignedWide denominators =
        static_cast<UnsignedWide>(positive(answer.denominator())) * positive(expected.denominator());

    // |a - e| <= tn/td  <=>  distance * td <= tn * ad * ed
    const Rational& absolute = tolerance.absolute;
    if (multiply(distance, positive(absolute.denominator())) <=
        multiply(denominators, positive(absolute.numerator()))) {
        return true;
    }
    // |a - e| <= (rn/rd) * |en|/ed  <=>  distance * rd <= rn * |en| * ad
    const Rational& relative = tolerance.relative;
    const auto expected_magnitude = static_cast<std::uint64_t>(magnitude(expected.numerator()));
    return multiply(distance, positive(relative.denominator())) <=
           multiply(static_cast<UnsignedWide>(positive(relative.numerator())) * expected_magnitude,
                    positive(answer.denominator()));
}

bool approximately_equivalent(std::string_view expected, std::string_view answer, const Tolerance& tolerance) {
    validate(tolerance);
    const Rational expected_value = operand_value(expected, OperandError::Operand::left);
    return within_tolerance(expected_value, operand_value(answer, OperandError::Operand::right), tolerance);
}

}  // namespace axiom::math
