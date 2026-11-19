#include "axiom/math/expression.hpp"
#include "axiom/math/rational.hpp"
#include "axiom/math/tolerance.hpp"

#include <compare>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using axiom::math::ExpressionError;
using axiom::math::OperandError;
using axiom::math::Rational;
using axiom::math::Tolerance;
using axiom::math::approximately_equivalent;
using axiom::math::within_tolerance;
using axiom::math::equivalent;
using axiom::math::evaluate;

// Integers whose values all fit in int64_t convert implicitly. Floating-point
// values and wider integers would be truncated or wrapped, so they must not
// compile as constructor arguments or as operands.
template <class Left, class Right>
concept arithmetic_with = requires(Left left, Right right) {
    left + right;
    left - right;
    left * right;
    left / right;
    left == right;
    left < right;
};
static_assert(std::is_convertible_v<int, Rational> && std::is_convertible_v<std::int64_t, Rational>);
static_assert(std::is_convertible_v<std::uint32_t, Rational> && std::is_constructible_v<Rational, long long, int>);
static_assert(arithmetic_with<Rational, int> && arithmetic_with<std::int64_t, Rational>);
static_assert(!std::is_convertible_v<double, Rational> && !std::is_constructible_v<Rational, double>);
static_assert(!std::is_convertible_v<float, Rational> && !std::is_convertible_v<long double, Rational>);
static_assert(!std::is_convertible_v<std::uint64_t, Rational> && !std::is_constructible_v<Rational, std::uint64_t>);
static_assert(!std::is_constructible_v<Rational, int, double> && !std::is_constructible_v<Rational, double, int>);
static_assert(!std::is_constructible_v<Rational, std::int64_t, std::uint64_t>);
static_assert(!arithmetic_with<Rational, double> && !arithmetic_with<double, Rational>);
static_assert(!arithmetic_with<Rational, std::uint64_t> && !arithmetic_with<std::uint64_t, Rational>);
// The same holds for the exponent of pow.
template <class Exponent>
concept power_with = requires(const Rational base, Exponent exponent) { base.pow(exponent); };
static_assert(power_with<int> && power_with<std::int64_t> && power_with<std::uint32_t>);
static_assert(!power_with<double> && !power_with<float> && !power_with<long double>);
static_assert(!power_with<std::uint64_t> && !power_with<std::size_t>);

std::size_t checks = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) throw std::runtime_error(std::string(message));
}

// A non-empty fragment must occur in the error message. It identifies which
// of several checks that throw the same exception type rejected the input.
template <class Exception, class Function>
void requires_throw(Function&& function, std::string_view message, std::string_view fragment = {}) {
    ++checks;
    try {
        function();
    } catch (const Exception& error) {
        if (std::string_view(error.what()).find(fragment) != std::string_view::npos) return;
        throw std::runtime_error(std::string(message) + ": wrong error: " + error.what());
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(message) + ": wrong exception: " + error.what());
    }
    throw std::runtime_error(std::string(message) + ": no exception");
}

// std::uniform_int_distribution's algorithm is implementation-defined, so a
// fixed seed would draw different values with libc++ and libstdc++. Map the
// fully specified 32-bit std::mt19937 output instead, rejecting the biased tail.
std::int64_t draw(std::mt19937& generator, std::int64_t low, std::int64_t high) {
    const auto range = static_cast<std::uint64_t>(high - low) + 1;
    constexpr std::uint64_t outputs = std::uint64_t{1} << 32;
    const std::uint64_t limit = outputs - outputs % range;
    std::uint64_t value = generator();
    while (value >= limit) value = generator();
    return low + static_cast<std::int64_t>(value % range);
}

void expect(const Rational& actual, std::int64_t numerator, std::int64_t denominator = 1) {
    require(actual.numerator() == numerator, "unexpected numerator: " + actual.str());
    require(actual.denominator() == denominator, "unexpected denominator: " + actual.str());
}

// This oracle intentionally uses small operands and ordinary integer arithmetic.
// Its cross-products stay below 2,000,000; it shares no Rational implementation.
void expect_small(const Rational& actual, std::int64_t numerator, std::int64_t denominator) {
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    const auto divisor = std::gcd(numerator, denominator);
    expect(actual, numerator / divisor, denominator / divisor);
}

void normalization() {
    expect(Rational(), 0);
    expect(Rational(6, 8), 3, 4);
    expect(Rational(-6, 8), -3, 4);
    expect(Rational(6, -8), -3, 4);
    expect(Rational(-6, -8), 3, 4);
    expect(Rational(0, -12), 0);
    require(Rational(8, 4).str() == "2", "integers must have canonical formatting");
    require(Rational(-6, 8).str() == "-3/4", "fractions must have canonical formatting");
    require(Rational(2, 4) == Rational(1, 2), "equivalent fractions must compare equal");
    require(!(Rational(1, 2) == Rational(2, 3)), "unequal fractions compare equal");
    require(Rational(-1, 2) < Rational(-1, 3), "negative rational ordering");
    require(Rational(2, 3) > Rational(1, 2), "positive rational ordering");
    requires_throw<std::domain_error>([] { (void)Rational(1, 0); }, "zero denominator");
    requires_throw<std::domain_error>([] { (void)Rational(0, 0); }, "zero over zero");
    expect(Rational(1, 3) + 1, 4, 3);
    expect(2 * Rational(-1, 4), -1, 2);
    expect(Rational(std::uint32_t{4000000000U}, 3), 4000000000, 3);
    require(Rational(5) == 5 && Rational(-7, 2) < -3, "integer operands convert exactly");
}

void arithmetic() {
    expect(Rational(1, 3) + Rational(1, 6), 1, 2);
    expect(Rational(1, 3) - Rational(1, 6), 1, 6);
    expect(Rational(-2, 3) * Rational(9, 4), -3, 2);
    expect(Rational(3, 7) / Rational(-9, 14), -2, 3);
    expect(-Rational(-4, 5), 4, 5);
    expect(Rational(0) / Rational(-8, 3), 0);
    requires_throw<std::domain_error>([] { (void)(Rational(4) / Rational(0)); }, "division by zero");

    // The same seed draws the same pairs with every standard library, so a
    // failure reported by any CI job reproduces on any other machine.
    std::mt19937 generator(0x4158494fU);
    for (int index = 0; index < 3000; ++index) {
        const std::int64_t a = draw(generator, -1000, 1000);
        const std::int64_t b = draw(generator, 1, 1000);
        const std::int64_t c = draw(generator, -1000, 1000);
        const std::int64_t d = draw(generator, 1, 1000);
        if (index == 0) {
            require(a == 166 && b == 83 && c == -980 && d == 229, "seeded pairs differ from the fixed stream");
        }
        try {
            const Rational left(a, b);
            const Rational right(c, d);
            expect_small(left + right, a * d + c * b, b * d);
            expect_small(left - right, a * d - c * b, b * d);
            expect_small(left * right, a * c, b * d);
            if (c != 0) expect_small(left / right, a * d, b * c);
            require((left + right) - right == left, "addition inverse identity");
            if (c != 0) require((left * right) / right == left, "multiplication inverse identity");
            require(left + right == right + left, "addition commutativity");
        } catch (const std::exception& error) {
            throw std::runtime_error("pair " + std::to_string(index) + " (" + std::to_string(a) + "/" +
                                     std::to_string(b) + ", " + std::to_string(c) + "/" + std::to_string(d) +
                                     "): " + error.what());
        }
    }
}

void integer_boundaries() {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    expect(Rational(minimum), minimum);
    expect(Rational(minimum, minimum), 1);
    expect(Rational(0, minimum), 0);
    expect(Rational(2, minimum), -1, std::int64_t{1} << 62);
    expect(Rational(minimum, 2), minimum / 2);
    expect(Rational(maximum, 2) + Rational(maximum, 2), maximum);
    expect(Rational(maximum, 2) * Rational(2, maximum), 1);
    expect(Rational(minimum) - Rational(minimum), 0);
    expect(Rational(minimum) / Rational(minimum), 1);
    expect(Rational(maximum) + Rational(minimum), -1);
    require(Rational(minimum) < Rational(-maximum), "minimum integer ordering");
    require(Rational(maximum, maximum - 1) > Rational(1), "wide comparison products");
    // These cross products exceed int64_t; narrowing them before comparing
    // would overflow or wrap and report the wrong order.
    constexpr auto two_62 = std::int64_t{1} << 62;
    require(Rational(10000000000) > Rational(1, 1000000000), "wide comparison: 10^10 > 10^-9");
    require((Rational(two_62, 3) <=> Rational(two_62, 7)) == std::strong_ordering::greater, "wide comparison: 2^62/3 > 2^62/7");
    require((Rational(-two_62, 3) <=> Rational(-two_62, 7)) == std::strong_ordering::less, "wide comparison: -2^62/3 < -2^62/7");
    require(Rational(maximum, maximum - 1) < Rational(maximum - 1, maximum - 2), "wide comparison of adjacent full-width fractions");
    require(Rational(minimum + 1, maximum - 1) < Rational(minimum, maximum), "wide comparison of negative full-width fractions");
    require((Rational(maximum, maximum - 1) <=> Rational(maximum, maximum - 1)) == std::strong_ordering::equal, "wide comparison of equal fractions");
    expect(Rational(1, maximum) + Rational(1, maximum), 2, maximum);
    requires_throw<std::overflow_error>([=] { (void)Rational(1, minimum); }, "positive denominator range");
    requires_throw<std::overflow_error>([=] { (void)Rational(minimum, -1); }, "normalization overflow");
    requires_throw<std::overflow_error>([=] { (void)-Rational(minimum); }, "negation overflow");
    requires_throw<std::overflow_error>([=] { (void)(Rational(maximum) + Rational(1)); }, "addition overflow");
    requires_throw<std::overflow_error>([=] { (void)(Rational(minimum) - Rational(1)); }, "subtraction overflow");
    requires_throw<std::overflow_error>([=] { (void)(Rational(maximum) * Rational(2)); }, "multiplication overflow");
    requires_throw<std::overflow_error>([=] { (void)(Rational(minimum) / Rational(-1)); }, "division overflow");
    requires_throw<std::overflow_error>([=] { (void)(Rational(1, maximum) * Rational(1, maximum)); }, "denominator overflow");
}

void integer_powers() {
    expect(Rational(2, 3).pow(3), 8, 27);
    expect(Rational(2, 3).pow(-3), 27, 8);
    expect(Rational(-2, 3).pow(3), -8, 27);
    expect(Rational(-2, 3).pow(4), 16, 81);
    expect(Rational(0).pow(0), 1);
    expect(Rational(0).pow(7), 0);
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    expect(Rational(minimum).pow(1), minimum);
    expect(Rational(maximum).pow(1), maximum);
    expect(Rational(-1).pow(minimum), 1);
    expect(Rational(-1).pow(maximum), -1);
    expect(Rational(1).pow(minimum), 1);
    expect(Rational(-2).pow(63), minimum);
    requires_throw<std::domain_error>([] { (void)Rational(0).pow(-1); }, "negative zero power");
    requires_throw<std::overflow_error>([] { (void)Rational(2).pow(63); }, "positive power overflow");
    requires_throw<std::overflow_error>([] { (void)Rational(2).pow(-63); }, "negative power denominator overflow");
}

void decimal_literals() {
    expect(Rational::from_decimal("0"), 0);
    expect(Rational::from_decimal("-0.0"), 0);
    expect(Rational::from_decimal("0.1"), 1, 10);
    expect(Rational::from_decimal("+.50"), 1, 2);
    expect(Rational::from_decimal("5."), 5);
    expect(Rational::from_decimal("-12.125"), -97, 8);
    expect(Rational::from_decimal("0.000000000000000001"), 1, 1000000000000000000LL);
    expect(Rational::from_decimal("9223372036854775807"), std::numeric_limits<std::int64_t>::max());
    expect(Rational::from_decimal("-9223372036854775808"), std::numeric_limits<std::int64_t>::min());
    expect(Rational::from_decimal("9223372036854775807.0"), std::numeric_limits<std::int64_t>::max());
    expect(Rational::from_decimal("-9223372036854775808.0"), std::numeric_limits<std::int64_t>::min());
    for (const auto text : {"", ".", "+", "-", "1.2.3", "1e2", "NaN", "inf", "1/2", "--1", "1x"}) {
        requires_throw<std::invalid_argument>([=] { (void)Rational::from_decimal(text); }, std::string("invalid literal ") + text);
    }
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal("9223372036854775808"); }, "literal overflow");
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal("0.0000000000000000001"); }, "fractional digit limit", "18 fractional digits");
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal(std::string(65, '0')); }, "literal length limit", "64-character limit");
    // The syntax is checked first, so malformed text is invalid_argument however
    // long it is, even when it also exceeds the length limit or a digit limit.
    for (const auto& text : {std::string(65, 'x'), std::string(65, ' '), std::string(64, '1') + "x",
                             std::string("12345678901234567890abc"), std::string("1.0000000000000000000 junk"),
                             std::string("3.14159265358979323846e0"), std::string("12345678901234567890 ")}) {
        requires_throw<std::invalid_argument>([&] { (void)Rational::from_decimal(text); }, "malformed literal " + text,
                                              "expected an integer or finite decimal literal");
    }

    // The digit limits keep the 128-bit accumulator in range. Leading and trailing
    // zeros count, so the limits alone must reject these representable values.
    expect(Rational::from_decimal("0000000000000000001"), 1);
    expect(Rational::from_decimal("0.500000000000000000"), 1, 2);
    expect(Rational::from_decimal("0000000000000000000.000000000000000001"), 1, 1000000000000000000LL);
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal("00000000000000000001"); }, "integer digit limit", "19 integer digits");
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal("0.5000000000000000000"); }, "fractional digit limit with a trailing zero", "18 fractional digits");
    // 2^128 + 5, and 10^39 mod 2^128 over 10^39: a wrapped accumulator would accept them.
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal("340282366920938463463374607431768211461"); }, "integer accumulator bound", "19 integer digits");
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal("0.319435266158123073073250785136463577088"); }, "fractional accumulator bound", "18 fractional digits");
}

void expression_values() {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"1/3 + 1/6", "1/2"}, {"0.1 + 0.2", "3/10"}, {"2 + 3 * 4", "14"},
        {"(2 + 3) * 4", "20"}, {"8 / 4 / 2", "1"}, {"8 - 4 - 2", "2"},
        {"-(2+3)/10", "-1/2"}, {"- - 2", "2"}, {"2*-3", "-6"},
        {"1+-2", "-1"}, {"1--2", "3"}, {".5 + 5.", "11/2"},
        {"\t ( 1 + 2 ) \n / 6 \r", "1/2"}, {"+0", "0"}, {"1\v+\f2", "3"},
        // A sign applies only to the operand after it, and mixed operators of
        // one level still associate to the left.
        {"-(1) + 2", "1"}, {"-(1/2) + 1/2", "0"}, {"- (2) - 3", "-5"}, {"--5 + 1", "6"},
        {"8 / 4 * 2", "4"}, {"1 - 2 + 3", "2"}, {"2 * 3 + 4 * 5", "26"}, {"1 + 2 * 3 - 4 / 2", "5"},
        // Unary + keeps the sign, both on a literal and on any other operand.
        {"+5", "5"}, {"1 * +2", "2"}, {"3 - +1", "2"}, {"+(1/2)", "1/2"}, {"++5", "5"},
        {"-+5", "-5"}, {"+-5", "-5"}, {"+ -(1/2)", "-1/2"}, {"+9223372036854775807", "9223372036854775807"},
        {"9223372036854775807 / 2 + 9223372036854775807 / 2", "9223372036854775807"},
        {"-9223372036854775808", "-9223372036854775808"},
    };
    for (const auto& [expression, expected] : cases) {
        const auto result = evaluate(expression);
        require(result.value.str() == expected, "incorrect expression: " + expression);
        require(result.steps.empty(), "steps should be opt-in");
    }
    require(evaluate("1/3 + 1/6").value == evaluate("0.5").value, "exact equivalent expressions");
    require(!(evaluate("1/3").value == evaluate("0.333333").value), "finite approximation must not equal exact third");

    // The library's own comparison, including the README's example.
    require(equivalent("2 / 4", ".5"), "README equivalent() example");
    require(equivalent("1/3 + 1/6", "0.5") && equivalent("(2 + 3) / 10", "0.5"), "equivalent() of equal values");
    require(equivalent("-0", "0") && equivalent("-9223372036854775808", "-4611686018427387904 * 2"), "equivalent() at zero and the minimum");
    require(!equivalent("1", "2") && !equivalent("2", "1"), "equivalent() of different integers");
    require(!equivalent("1/3", "0.333") && !equivalent("1/2", "-1/2"), "equivalent() of different fractions");
}

std::size_t error_position(const std::string& expression) {
    try {
        (void)evaluate(expression);
    } catch (const ExpressionError& error) {
        return error.position();
    }
    throw std::runtime_error("accepted invalid expression: " + expression);
}

// The operand that equivalent() names in its error, and the error's offset there.
std::pair<OperandError::Operand, std::size_t> operand_error(std::string_view left, std::string_view right) {
    try {
        (void)equivalent(left, right);
    } catch (const OperandError& error) {
        return {error.operand(), error.position()};
    }
    throw std::runtime_error("equivalent() accepted " + std::string(left) + " and " + std::string(right));
}

Rational parse(std::string_view text) { return evaluate(text).value; }

bool approx(std::string_view expected, std::string_view answer, std::string_view absolute, std::string_view relative) {
    return within_tolerance(parse(expected), parse(answer), Tolerance{parse(absolute), parse(relative)});
}

// docs/answer-checking.md specifies this policy; tests/tolerance_oracle.py
// checks its worked examples against Python Fraction through the CLI.
void tolerance_policy() {
    // Exact comparison is unchanged.
    require(!equivalent("1/3", "0.333") && !(parse("1/3") == parse("0.333")), "exact comparison still rejects 0.333");
    require(!approx("1/3", "0.333", "0", "0"), "zero tolerances are exact equality");
    require(approx("1/3", "0.333", "0.001", "0"), "1/3000 is within 0.001");
    require(!approx("1/3", "0.333", "0.0003", "0"), "1/3000 exceeds 0.0003");
    require(approx("1/3", "0.333", "0", "0.001"), "relative boundary is inclusive");
    require(!approx("1/3", "0.333", "0", "0.000999"), "just below the relative boundary");
    require(approx("1", "1.1", "0.1", "0") && approx("1", "0.9", "0.1", "0"), "absolute boundary is inclusive on both sides");
    require(!approx("1", "1.100000000000000001", "0.1", "0"), "10^-18 beyond the absolute boundary");
    require(approx("100", "99", "0", "0.01") && !approx("99", "100", "0", "0.01"), "relative bound scales with the expected value");
    require(approx("-100", "-99", "0", "0.01") && !approx("-100", "99", "0", "0.01"), "negative expected values");
    require(approx("2", "2.004", "0.001", "0.002") && !approx("2", "2.004", "0.001", "0"), "either bound accepts");
    require(!approx("0", "0.000001", "0", "0.5") && approx("0", "0", "0", "0.5"), "relative bound at zero accepts only zero");
    require(approx("0", "-0.000001", "0.000001", "0") && approx("0.0001", "-0.0001", "0.001", "0"), "absolute bound at and across zero");

    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    // Distances and bounds that no Rational can hold are still decided exactly.
    require(within_tolerance(Rational(1, maximum), Rational(1, maximum - 1), {Rational(1, maximum), 0}),
            "unrepresentable distance within the bound");
    // The distance 1/(maximum * (maximum - 1)) lies just above 1/maximum^2 and
    // below 2/maximum^2, both relative bounds that no Rational can hold either.
    require(!within_tolerance(Rational(1, maximum), Rational(1, maximum - 1), {0, Rational(1, maximum)}),
            "unrepresentable distance just beyond an unrepresentable bound");
    require(within_tolerance(Rational(1, maximum), Rational(1, maximum - 1), {0, Rational(2, maximum)}),
            "unrepresentable distance within an unrepresentable bound");
    require(!within_tolerance(maximum, minimum, {maximum, Rational(maximum - 1, maximum)}), "distance 2^64 - 1");
    require(within_tolerance(minimum, minimum + 1, {0, Rational(1, maximum)}), "relative bound at the minimum");
    require(!within_tolerance(minimum, maximum, {maximum, Rational(maximum - 1, maximum)}), "opposite extremes");
    require(within_tolerance(Rational(minimum, maximum), Rational(minimum + 1, maximum), {Rational(1, maximum), 0}),
            "full-width denominators on the absolute boundary");
    require(within_tolerance(maximum, maximum, {}) && !within_tolerance(maximum, maximum - 1, {}), "exact at the maximum");

    for (const auto& invalid : {Tolerance{-1, 0}, Tolerance{Rational(-1, maximum), 0}, Tolerance{0, Rational(-1, 2)},
                                Tolerance{0, 1}, Tolerance{0, Rational(maximum, maximum - 1)}}) {
        requires_throw<std::invalid_argument>([&] { (void)within_tolerance(1, 1, invalid); }, "invalid tolerance", "tolerance");
        // The tolerance is checked before the operands are evaluated.
        requires_throw<std::invalid_argument>([&] { (void)approximately_equivalent("1/0", "1", invalid); },
                                              "invalid tolerance before operands", "tolerance");
    }
    require(within_tolerance(5, 4, {1, Rational(maximum - 1, maximum)}), "largest valid relative tolerance");

    require(approximately_equivalent("1/3", "0.333", {Rational(1, 1000), 0}), "expression form");
    require(!approximately_equivalent("1/3", "0.333", {}), "expression form with zero tolerance");
    using Operand = OperandError::Operand;
    for (const auto& [expected, answer, operand, position] :
         {std::tuple{"1/0", "1", Operand::left, std::size_t{1}}, std::tuple{"1 +", "1/0", Operand::left, std::size_t{3}},
          std::tuple{"1", "2 * (3", Operand::right, std::size_t{6}}}) {
        try {
            (void)approximately_equivalent(expected, answer, {Rational(1, 10), 0});
            throw std::runtime_error("approximately_equivalent() accepted an invalid operand");
        } catch (const OperandError& error) {
            require(error.operand() == operand && error.position() == position, "approximately_equivalent() names the operand");
        }
    }

    // Independent small-integer oracle: every product stays below 10^6, so plain
    // int64 cross-multiplication decides each case without the 192-bit code.
    std::mt19937 generator(0x544f4cU);
    for (int index = 0; index < 4000; ++index) {
        const std::int64_t en = draw(generator, -60, 60), ed = draw(generator, 1, 12);
        std::int64_t an = draw(generator, -60, 60), ad = draw(generator, 1, 12);
        if (index % 3 == 0) {  // Often land on or near a bound.
            an = en * ad / ed + draw(generator, -2, 2);
        }
        const std::int64_t tn = draw(generator, 0, 30) * (index % 4 == 0 ? 0 : 1), td = draw(generator, 1, 12);
        const std::int64_t rn = draw(generator, 0, 11), rd = 12;
        const std::int64_t distance = std::abs(an * ed - en * ad);
        const bool expected = distance * td <= tn * ad * ed || distance * rd <= rn * std::abs(en) * ad;
        const Rational e(en, ed), a(an, ad);
        const Tolerance tolerance{Rational(tn, td), Rational(rn, rd)};
        const bool actual = within_tolerance(e, a, tolerance);
        require(actual == expected, "small-integer tolerance oracle at case " + std::to_string(index));
        // The same decision through Rational arithmetic, and the documented invariants.
        const Rational gap = a > e ? a - e : e - a;
        require(actual == (gap <= tolerance.absolute || gap <= tolerance.relative * (e < 0 ? -e : e)), "Rational arithmetic agrees");
        require(actual == within_tolerance(-e, -a, tolerance), "negating both values keeps the decision");
        require(within_tolerance(e, a, {}) == (e == a), "zero tolerances are exact equality");
        if (within_tolerance(e, a, {0, tolerance.relative})) {
            require(e == 0 ? a == 0 : (a != 0 && (a < 0) == (e < 0)), "relative bound keeps the expected sign");
        }
    }
}

void expression_failures() {
    // Each error points at the offending byte: unexpected input, the end of the
    // input, the failing operator, or the start (or sign) of a rejected literal.
    const std::vector<std::pair<std::string, std::size_t>> invalid = {
        {"", 0}, {" ", 1}, {"1 +", 3}, {"*2", 0}, {"1 2", 2}, {"(1+2", 4}, {"1+2)", 3}, {"()", 1}, {"2(3)", 1},
        {"1e3", 1}, {"sqrt(4)", 0}, {"x+1", 0}, {"2^3", 1}, {"1..2", 2}, {".", 0}, {"1/0", 1}, {"1/(2-2)", 1},
        {"9223372036854775807 + 1", 20}, {"-(-9223372036854775808)", 0},
        {"0.0000000000000000001", 0}, {"1 + \xC3\xA9", 4}, {std::string("1\0+2", 4), 1},
        {"1 + 2 3", 6}, {"(1 + 2", 6}, {"1 + .", 4}, {"1 / 0", 2}, {"4611686018427387904 * 2", 20},
        {"(-9223372036854775807 - 1) - 1", 27}, {"1 + -(-9223372036854775808)", 4},
        {"1 + 99999999999999999999", 4}, {"2 * -9223372036854775809", 4}, {"+9223372036854775808", 0},
    };
    for (const auto& [expression, position] : invalid) {
        try {
            (void)evaluate(expression);
            throw std::runtime_error("accepted invalid expression: " + expression);
        } catch (const ExpressionError& error) {
            require(error.position() <= expression.size(), "error offset outside input");
            require(error.position() == position, "wrong error offset " + std::to_string(error.position()) +
                                                      " for: " + expression);
            require(std::string(error.what()).size() > 0, "error message must explain failure");
        }
    }
    try {
        (void)evaluate("1 + @");
        throw std::runtime_error("accepted unexpected token");
    } catch (const ExpressionError& error) {
        require(error.position() == 4, "unexpected-token offset should identify @");
    }
    const std::string deeply_nested = std::string(70, '(') + "1" + std::string(70, ')');
    require(evaluate(std::string(64, '(') + "1" + std::string(64, ')')).value == Rational(1), "maximum valid nesting");
    require(evaluate(std::string(64, '-') + "1").value == Rational(1), "maximum valid unary nesting");
    require(evaluate(std::string(4095, ' ') + "1").value == Rational(1), "maximum valid input size");
    requires_throw<ExpressionError>([&] { (void)evaluate(deeply_nested); }, "nesting guard", "64-level nesting limit");
    requires_throw<ExpressionError>([] { (void)evaluate(std::string(70, '-') + "1"); }, "unary recursion guard", "64-level nesting limit");
    // Spaces alone are also an incomplete expression, so the message identifies the guard.
    requires_throw<ExpressionError>([] { (void)evaluate(std::string(4097, ' ')); }, "input length guard", "4096-byte limit");
    std::string many_operations = "1";
    for (int index = 0; index < 1200; ++index) many_operations += "+1";
    requires_throw<ExpressionError>([&] { (void)evaluate(many_operations); }, "operation or token limit", "1024-token limit");
    // A limit error points at the first byte beyond the limit.
    require(error_position(deeply_nested) == 64, "nesting limit offset");
    require(error_position(std::string(70, '-') + "1") == 64, "unary nesting limit offset");
    require(error_position(std::string(4097, ' ')) == 4096, "input length limit offset");
    require(error_position(many_operations) == 1024, "token limit offset");

    // Each limit is exact: valid input at the limit is accepted, and the same kind
    // of valid input one level, byte or token beyond it is rejected by that limit.
    const std::string nested_65 = std::string(65, '(') + "1" + std::string(65, ')');
    const std::string unary_65 = std::string(65, '-') + "1";
    const std::string bytes_4097 = std::string(4096, ' ') + "1";
    requires_throw<ExpressionError>([&] { (void)evaluate(nested_65); }, "65 nested parentheses", "64-level nesting limit");
    requires_throw<ExpressionError>([&] { (void)evaluate(unary_65); }, "65 nested signs", "64-level nesting limit");
    requires_throw<ExpressionError>([&] { (void)evaluate(bytes_4097); }, "4,097-byte expression", "4096-byte limit");
    require(error_position(nested_65) == 64 && error_position(unary_65) == 64, "nesting limit offset at 65 levels");
    require(error_position(bytes_4097) == 4096, "input length limit offset at 4,097 bytes");
    // A sign and its literal are separate tokens: 2 + 511 * 2 = 1,024 tokens.
    std::string tokens_1024 = "-1";
    for (int index = 0; index < 511; ++index) tokens_1024 += "+1";
    require(evaluate(tokens_1024).value == Rational(510), "maximum valid token count");
    const std::string tokens_1025 = "1" + tokens_1024.substr(2) + "+1";
    requires_throw<ExpressionError>([&] { (void)evaluate(tokens_1025); }, "1,025 tokens", "1024-token limit");
    require(error_position(tokens_1025) == 1024, "token limit offset at 1,025 tokens");

    // equivalent() names the invalid operand, so that position() can be applied
    // to the right text. The left operand is evaluated first.
    using Operand = OperandError::Operand;
    require(operand_error("1", "2/0") == std::pair{Operand::right, std::size_t{1}}, "equivalent() error in the right operand");
    require(operand_error("2/0", "1") == std::pair{Operand::left, std::size_t{1}}, "equivalent() error in the left operand");
    require(operand_error("1", "1 + (2") == std::pair{Operand::right, std::size_t{6}}, "equivalent() offset beyond the left operand");
    require(operand_error("1 +", "1/0") == std::pair{Operand::left, std::size_t{3}}, "equivalent() evaluates the left operand first");
    // The error is still the ExpressionError that evaluate() reports for that operand.
    try {
        (void)equivalent("1", "1 + (2");
        throw std::runtime_error("equivalent() accepted 1 + (2");
    } catch (const ExpressionError& error) {
        require(std::string_view(error.what()) == "Expected ')'." && error.position() == 6, "equivalent() keeps the operand's error");
    }
}

// Requires exactly these steps, in order: each one's source slice and result.
void expect_steps(const std::string& input, const std::vector<std::pair<std::string, std::string>>& expected) {
    const auto explanation = evaluate(input, true);
    require(explanation.steps.size() == expected.size(),
            std::to_string(explanation.steps.size()) + " steps for: " + input);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto& step = explanation.steps[index];
        require(step.expression == expected[index].first && step.result.str() == expected[index].second,
                "step " + std::to_string(index + 1) + " for " + input + " is " + step.expression + " = " +
                    step.result.str());
    }
}

// A traced evaluation in brief: the value, the number of recorded steps, the
// bytes of source text they quote in total, and the number of steps left out.
struct Trace {
    std::string value;
    std::size_t steps;
    std::size_t quoted;
    std::size_t omitted;
    bool operator==(const Trace&) const = default;
};

Trace trace(const std::string& input) {
    const auto explanation = evaluate(input, true);
    std::size_t quoted = 0;
    for (const auto& step : explanation.steps) quoted += step.expression.size();
    return {explanation.value.str(), explanation.steps.size(), quoted, explanation.omitted_steps};
}

void explanations() {
    const std::string input = "1/3 + 1/6";
    const auto explanation = evaluate(input, true);
    require(explanation.value == Rational(1, 2), "explanation changed result");
    require(explanation.steps.size() == 3, "three arithmetic operations should produce three steps");
    require(explanation.steps[0].result == Rational(1, 3), "first division step");
    require(explanation.steps[1].result == Rational(1, 6), "second division step");
    require(explanation.steps[2].result == Rational(1, 2), "sum step");
    for (const auto& step : explanation.steps) {
        require(!step.expression.empty(), "empty explanation expression");
        require(input.find(step.expression) != std::string::npos, "step must refer to source text");
        require(evaluate(step.expression).value == step.result, "step explanation does not match its value");
    }
    require(evaluate("42", true).steps.empty(), "literal needs no arithmetic steps");
    const auto unary = evaluate("-(2+3)", true);
    require(unary.value == Rational(-5), "unary explanation result");
    require(!unary.steps.empty(), "unary explanation missing");

    // An explicit sign is a unary step whose slice starts at the sign, including
    // when the parser reads the sign together with its numeric literal.
    expect_steps("-5", {{"-5", "-5"}});
    expect_steps("+5", {{"+5", "5"}});
    expect_steps("- 5", {{"- 5", "-5"}});
    expect_steps("-5 * 2", {{"-5", "-5"}, {"-5 * 2", "-10"}});
    expect_steps("2 * -3", {{"-3", "-3"}, {"2 * -3", "-6"}});
    expect_steps("-9223372036854775808", {{"-9223372036854775808", "-9223372036854775808"}});
    expect_steps("--5", {{"-5", "-5"}, {"--5", "5"}});
    // A sign applied to any other operand records its own step after the operand's.
    expect_steps("-(2+3)", {{"2+3", "5"}, {"-(2+3)", "-5"}});
    expect_steps("+(1/2)", {{"1/2", "1/2"}, {"+(1/2)", "1/2"}});
    expect_steps("(1/3 + 1/6) * 4", {{"1/3", "1/3"}, {"1/6", "1/6"}, {"1/3 + 1/6", "1/2"}, {"(1/3 + 1/6) * 4", "2"}});

    // A slice includes the slices inside it, so the steps of one evaluation quote
    // at most max_explanation_length bytes in total. "1", 2,014 spaces and 32 "+1"
    // make 32 sums of 2,015 + 2k bytes (k = 1 to 32): exactly 65,536.
    const auto chain = [](std::size_t spaces, int terms, std::string_view term = "+1") {
        std::string text = "1" + std::string(spaces, ' ');
        for (int index = 0; index < terms; ++index) text += term;
        return text;
    };
    constexpr auto limit = axiom::math::max_explanation_length;
    require(trace(chain(2014, 32)) == Trace{"33", 32, limit, 0}, "explanation at the byte limit");
    // With one more byte the last step no longer fits. It is left out and counted,
    // whether it is a sum, a product, a sign or a signed literal, and the value
    // stays complete: collecting steps never makes an expression fail.
    require(trace(chain(2015, 32)) == Trace{"33", 31, 63488, 1}, "explanation without its last sum");
    require(trace(chain(2015, 32, "*1")) == Trace{"1", 31, 63488, 1}, "explanation without its last product");
    require(trace("-(" + chain(2014, 31) + ")") == Trace{"-32", 31, 63457, 1}, "explanation without its sign");
    require(trace(chain(2014, 32) + "+-1") == Trace{"32", 32, limit, 2}, "explanation without a signed literal");
    // The recorded steps are the first ones in evaluation order: once a step is
    // left out, so is every later one, even a short one that would still fit.
    const auto first = evaluate(chain(2015, 32), true);
    require(first.steps.back().expression == chain(2015, 31) && first.steps.back().result == Rational(32),
            "explanation keeps the first steps");
    require(trace(chain(2015, 32) + "+(2*3)") == Trace{"39", 31, 63488, 3}, "explanation without a later short step");
    // A sum of 257 ones (513 bytes) would quote 66,048 bytes, and a 4,096-byte
    // chain padded with spaces 1,832,446 bytes.
    require(trace(chain(0, 256)) == Trace{"257", 255, 65535, 1}, "explanation of 257 terms");
    const std::string padded = chain(3073, 511);
    require(trace(padded) == Trace{"512", 21, 65016, 490}, "explanation of a padded chain");
    const auto untraced = evaluate(padded);
    require(untraced.value == Rational(512) && untraced.steps.empty() && untraced.omitted_steps == 0,
            "evaluation without steps leaves none out");
    // Whatever the size of a chain, its traced value is the untraced one, and
    // each of its operations is either a recorded step or counted as left out.
    for (const std::size_t spaces : {std::size_t{0}, std::size_t{700}, std::size_t{3073}}) {
        for (const int terms : {1, 21, 22, 255, 256, 511}) {
            const std::string text = chain(spaces, terms);
            const Trace traced = trace(text);
            require(traced.value == evaluate(text).value.str() && traced.quoted <= limit &&
                        traced.steps + traced.omitted == static_cast<std::size_t>(terms),
                    "explanation of " + std::to_string(terms) + " sums after " + std::to_string(spaces) + " spaces");
        }
    }
}
}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"canonical normalization", normalization}, {"independent arithmetic oracle", arithmetic},
        {"integer boundaries and overflow", integer_boundaries}, {"exact decimal literals", decimal_literals},
        {"exact integer powers", integer_powers},
        {"expression precedence and equivalence", expression_values}, {"expression errors and resource limits", expression_failures},
        {"worked calculation steps", explanations}, {"approximate answer-checking policy", tolerance_policy},
    };
    std::size_t passed = 0;
    for (const auto& [name, function] : tests) {
        try {
            function();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << passed << '/' << tests.size() << " groups passed; " << checks << " assertions checked\n";
    return passed == tests.size() ? 0 : 1;
}
