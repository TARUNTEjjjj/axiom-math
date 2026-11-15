#pragma once

#include "axiom/math/export.hpp"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace axiom::math {

namespace detail {
// Types that convert implicitly to std::int64_t only by a narrowing conversion:
// floating-point values and integer types with values outside int64_t's range.
template <class T>
concept narrows_to_int64 = std::is_convertible_v<T, std::int64_t> &&
                           !requires(T value) { std::int64_t{value}; };
}  // namespace detail

// An exact, reduced fraction with a signed 64-bit numerator and a positive
// signed 64-bit denominator. Zero always has the representation 0/1.
// Operations throw std::overflow_error when the reduced result cannot fit;
// division by zero throws std::domain_error. No operation rounds its result.
class AXIOM_MATH_API Rational {
public:
    Rational(std::int64_t numerator = 0, std::int64_t denominator = 1);

    // Converting a floating-point value or an integer that may not fit in
    // std::int64_t (such as std::uint64_t) would truncate or wrap it, so such
    // arguments do not compile, including as operands of the operators below.
    // Use from_decimal for decimal text; range-check and cast wider integers.
    template <class Numerator, class Denominator = std::int64_t>
        requires(detail::narrows_to_int64<Numerator> || detail::narrows_to_int64<Denominator>)
    Rational(Numerator, Denominator = 1) = delete;

    [[nodiscard]] constexpr std::int64_t numerator() const noexcept { return numerator_; }
    [[nodiscard]] constexpr std::int64_t denominator() const noexcept { return denominator_; }
    [[nodiscard]] std::string str() const;

    // Accepts an optional sign and an integer or finite decimal, including .5
    // and 5. At least one digit is required. No whitespace or exponent syntax.
    // Limits: 19 integer digits, 18 fractional digits, 64 input characters.
    // Malformed input throws std::invalid_argument, however long it is; a
    // well-formed literal beyond a numeric limit throws std::overflow_error.
    // Decimal conversion is exact, never floating point.
    [[nodiscard]] static Rational from_decimal(std::string_view literal);

    [[nodiscard]] Rational operator-() const;

    // Exact integer powers in O(log |exponent|) multiplications. Zero to the
    // zeroth power is defined as 1; zero to a negative power is a domain error.
    // An unrepresentable reduced result throws std::overflow_error.
    [[nodiscard]] Rational pow(std::int64_t exponent) const;
    // As with the constructor, an exponent that the conversion to std::int64_t
    // would truncate or wrap (a floating-point value, or an integer type such as
    // std::uint64_t) does not compile.
    template <class Exponent>
        requires detail::narrows_to_int64<Exponent>
    Rational pow(Exponent) const = delete;

    friend bool operator==(const Rational&, const Rational&) noexcept = default;
    // Friends are not members, so they need their own export marking.
    friend AXIOM_MATH_API std::strong_ordering operator<=>(const Rational& left, const Rational& right) noexcept;
    friend AXIOM_MATH_API Rational operator+(const Rational& left, const Rational& right);
    friend AXIOM_MATH_API Rational operator-(const Rational& left, const Rational& right);
    friend AXIOM_MATH_API Rational operator*(const Rational& left, const Rational& right);
    friend AXIOM_MATH_API Rational operator/(const Rational& left, const Rational& right);

private:
    // Stores a pair that src/rational.cpp has already reduced and range-checked,
    // so that an arithmetic result is not reduced a second time.
    struct Reduced {};
    constexpr Rational(Reduced, std::int64_t reduced_numerator, std::int64_t reduced_denominator) noexcept
        : numerator_(reduced_numerator), denominator_(reduced_denominator) {}

    std::int64_t numerator_;
    std::int64_t denominator_;
};

} // namespace axiom::math
