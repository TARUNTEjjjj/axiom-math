#include "axiom/math/rational.hpp"

#include <limits>
#include <stdexcept>

namespace axiom::math {
namespace {

// GCC and Clang provide these integer types on the supported 64-bit targets.
// Keeping the extension here leaves the public API in standard C++20 types.
#if !defined(__SIZEOF_INT128__)
#error "AxiomMath requires compiler support for 128-bit integer intermediates."
#endif
__extension__ using Wide = __int128;
__extension__ using UnsignedWide = unsigned __int128;

struct Canonical {
    std::int64_t numerator;
    std::int64_t denominator;
};

UnsignedWide magnitude(Wide value) noexcept {
    // Unlike -value, this is defined even for the most negative signed value.
    return value < 0 ? static_cast<UnsignedWide>(-(value + 1)) + 1
                     : static_cast<UnsignedWide>(value);
}

UnsignedWide gcd(UnsignedWide left, UnsignedWide right) noexcept {
    while (right != 0) {
        const UnsignedWide remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

Canonical canonical(Wide numerator, Wide denominator) {
    if (denominator == 0) {
        throw std::domain_error("division by zero");
    }
    if (numerator == 0) {
        return {0, 1};
    }

    const bool negative = (numerator < 0) != (denominator < 0);
    UnsignedWide numerator_magnitude = magnitude(numerator);
    UnsignedWide denominator_magnitude = magnitude(denominator);
    const UnsignedWide divisor = gcd(numerator_magnitude, denominator_magnitude);
    numerator_magnitude /= divisor;
    denominator_magnitude /= divisor;

    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    const UnsignedWide positive_limit = static_cast<UnsignedWide>(maximum);
    const UnsignedWide negative_limit = positive_limit + 1;
    if (denominator_magnitude > positive_limit) {
        throw std::overflow_error("reduced denominator exceeds the signed 64-bit range");
    }
    if (numerator_magnitude > (negative ? negative_limit : positive_limit)) {
        throw std::overflow_error("reduced numerator exceeds the signed 64-bit range");
    }

    std::int64_t reduced_numerator;
    if (negative && numerator_magnitude == negative_limit) {
        reduced_numerator = std::numeric_limits<std::int64_t>::min();
    } else {
        const auto positive_numerator = static_cast<std::int64_t>(numerator_magnitude);
        reduced_numerator = negative ? -positive_numerator : positive_numerator;
    }
    return {reduced_numerator, static_cast<std::int64_t>(denominator_magnitude)};
}

Rational from_wide(Wide numerator, Wide denominator) {
    const Canonical reduced = canonical(numerator, denominator);
    return Rational(reduced.numerator, reduced.denominator);
}

bool is_digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

} // namespace

Rational::Rational(std::int64_t numerator, std::int64_t denominator) {
    const Canonical reduced = canonical(static_cast<Wide>(numerator),
                                        static_cast<Wide>(denominator));
    numerator_ = reduced.numerator;
    denominator_ = reduced.denominator;
}

std::string Rational::str() const {
    if (denominator_ == 1) {
        return std::to_string(numerator_);
    }
    return std::to_string(numerator_) + "/" + std::to_string(denominator_);
}

Rational Rational::from_decimal(std::string_view literal) {
    if (literal.empty()) {
        throw std::invalid_argument("decimal literal is empty");
    }
    if (literal.size() > 64) {
        throw std::overflow_error("decimal literal exceeds the 64-character limit");
    }

    std::size_t position = 0;
    bool negative = false;
    if (literal[position] == '+' || literal[position] == '-') {
        negative = literal[position] == '-';
        ++position;
    }

    Wide numerator = 0;
    Wide denominator = 1;
    std::size_t integer_digits = 0;
    std::size_t fractional_digits = 0;
    while (position < literal.size() && is_digit(literal[position])) {
        if (++integer_digits > 19) {
            throw std::overflow_error("decimal literal exceeds 19 integer digits");
        }
        numerator = numerator * 10 + (literal[position] - '0');
        ++position;
    }

    if (position < literal.size() && literal[position] == '.') {
        ++position;
        while (position < literal.size() && is_digit(literal[position])) {
            if (++fractional_digits > 18) {
                throw std::overflow_error("decimal literal exceeds 18 fractional digits");
            }
            numerator = numerator * 10 + (literal[position] - '0');
            denominator *= 10;
            ++position;
        }
    }
    if (integer_digits + fractional_digits == 0 || position != literal.size()) {
        throw std::invalid_argument("expected an integer or finite decimal literal");
    }

    // At most 37 digits are accumulated, strictly below 10^37 and therefore
    // below the signed 128-bit maximum. Reduce before testing the int64 bounds.
    return from_wide(negative ? -numerator : numerator, denominator);
}

Rational Rational::operator-() const {
    return from_wide(-static_cast<Wide>(numerator_), static_cast<Wide>(denominator_));
}

std::strong_ordering operator<=>(const Rational& left, const Rational& right) noexcept {
    const Wide left_scaled = static_cast<Wide>(left.numerator_) * right.denominator_;
    const Wide right_scaled = static_cast<Wide>(right.numerator_) * left.denominator_;
    if (left_scaled < right_scaled) {
        return std::strong_ordering::less;
    }
    if (left_scaled > right_scaled) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

Rational operator+(const Rational& left, const Rational& right) {
    // Denominators are positive int64 values. Even the sum of both full-width
    // cross products fits in signed 128 bits. Narrow only after reduction,
    // allowing cancellation to produce a representable result.
    const Wide numerator = static_cast<Wide>(left.numerator_) * right.denominator_ +
                           static_cast<Wide>(right.numerator_) * left.denominator_;
    const Wide denominator = static_cast<Wide>(left.denominator_) * right.denominator_;
    return from_wide(numerator, denominator);
}

Rational operator-(const Rational& left, const Rational& right) {
    // Do not implement subtraction as addition of -right: -INT64_MIN may be
    // unrepresentable even when the complete subtraction is representable.
    const Wide numerator = static_cast<Wide>(left.numerator_) * right.denominator_ -
                           static_cast<Wide>(right.numerator_) * left.denominator_;
    const Wide denominator = static_cast<Wide>(left.denominator_) * right.denominator_;
    return from_wide(numerator, denominator);
}

Rational operator*(const Rational& left, const Rational& right) {
    return from_wide(static_cast<Wide>(left.numerator_) * right.numerator_,
                     static_cast<Wide>(left.denominator_) * right.denominator_);
}

Rational operator/(const Rational& left, const Rational& right) {
    return from_wide(static_cast<Wide>(left.numerator_) * right.denominator_,
                     static_cast<Wide>(left.denominator_) * right.numerator_);
}

} // namespace axiom::math
