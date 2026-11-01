#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace axiom::math {

// An exact, reduced fraction with a signed 64-bit numerator and a positive
// signed 64-bit denominator. Zero always has the representation 0/1.
// Operations throw std::overflow_error when the reduced result cannot fit;
// division by zero throws std::domain_error. No operation rounds its result.
class Rational {
public:
    Rational(std::int64_t numerator = 0, std::int64_t denominator = 1);

    [[nodiscard]] constexpr std::int64_t numerator() const noexcept { return numerator_; }
    [[nodiscard]] constexpr std::int64_t denominator() const noexcept { return denominator_; }
    [[nodiscard]] std::string str() const;

    // Accepts an optional sign and an integer or finite decimal, including .5
    // and 5. At least one digit is required. No whitespace or exponent syntax.
    // Limits: 19 integer digits, 18 fractional digits, 64 input characters.
    // Malformed input throws std::invalid_argument; numeric limits throw
    // std::overflow_error. Decimal conversion is exact, never floating point.
    [[nodiscard]] static Rational from_decimal(std::string_view literal);

    [[nodiscard]] Rational operator-() const;

    friend bool operator==(const Rational&, const Rational&) noexcept = default;
    friend std::strong_ordering operator<=>(const Rational& left, const Rational& right) noexcept;
    friend Rational operator+(const Rational& left, const Rational& right);
    friend Rational operator-(const Rational& left, const Rational& right);
    friend Rational operator*(const Rational& left, const Rational& right);
    friend Rational operator/(const Rational& left, const Rational& right);

private:
    std::int64_t numerator_;
    std::int64_t denominator_;
};

} // namespace axiom::math
