#include "axiom/math/expression.hpp"
#include "axiom/math/rational.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using axiom::math::ExpressionError;
using axiom::math::Rational;
using axiom::math::evaluate;

std::size_t checks = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) throw std::runtime_error(std::string(message));
}

template <class Exception, class Function>
void requires_throw(Function&& function, std::string_view message) {
    ++checks;
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(message) + ": wrong exception: " + error.what());
    }
    throw std::runtime_error(std::string(message) + ": no exception");
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
}

void arithmetic() {
    expect(Rational(1, 3) + Rational(1, 6), 1, 2);
    expect(Rational(1, 3) - Rational(1, 6), 1, 6);
    expect(Rational(-2, 3) * Rational(9, 4), -3, 2);
    expect(Rational(3, 7) / Rational(-9, 14), -2, 3);
    expect(-Rational(-4, 5), 4, 5);
    expect(Rational(0) / Rational(-8, 3), 0);
    requires_throw<std::domain_error>([] { (void)(Rational(4) / Rational(0)); }, "division by zero");

    std::mt19937 generator(0x4158494fU);
    std::uniform_int_distribution<int> numerators(-1000, 1000);
    std::uniform_int_distribution<int> denominators(1, 1000);
    for (int index = 0; index < 3000; ++index) {
        const std::int64_t a = numerators(generator);
        const std::int64_t b = denominators(generator);
        const std::int64_t c = numerators(generator);
        const std::int64_t d = denominators(generator);
        const Rational left(a, b);
        const Rational right(c, d);
        expect_small(left + right, a * d + c * b, b * d);
        expect_small(left - right, a * d - c * b, b * d);
        expect_small(left * right, a * c, b * d);
        if (c != 0) expect_small(left / right, a * d, b * c);
        require((left + right) - right == left, "addition inverse identity");
        if (c != 0) require((left * right) / right == left, "multiplication inverse identity");
        require(left + right == right + left, "addition commutativity");
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
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal("0.0000000000000000001"); }, "fractional digit limit");
    requires_throw<std::overflow_error>([] { (void)Rational::from_decimal(std::string(65, '0')); }, "literal length limit");
}

void expression_values() {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"1/3 + 1/6", "1/2"}, {"0.1 + 0.2", "3/10"}, {"2 + 3 * 4", "14"},
        {"(2 + 3) * 4", "20"}, {"8 / 4 / 2", "1"}, {"8 - 4 - 2", "2"},
        {"-(2+3)/10", "-1/2"}, {"- - 2", "2"}, {"2*-3", "-6"},
        {"1+-2", "-1"}, {"1--2", "3"}, {".5 + 5.", "11/2"},
        {"\t ( 1 + 2 ) \n / 6 \r", "1/2"}, {"+0", "0"},
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
}

void expression_failures() {
    const std::vector<std::string> invalid = {
        "", " ", "1 +", "*2", "1 2", "(1+2", "1+2)", "()", "2(3)",
        "1e3", "sqrt(4)", "x+1", "2^3", "1..2", ".", "1/0", "1/(2-2)",
        "9223372036854775807 + 1", "-(-9223372036854775808)",
        "0.0000000000000000001", "1 + \xC3\xA9", std::string("1\0+2", 4),
    };
    for (const auto& expression : invalid) {
        try {
            (void)evaluate(expression);
            throw std::runtime_error("accepted invalid expression: " + expression);
        } catch (const ExpressionError& error) {
            require(error.position() <= expression.size(), "error offset outside input");
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
    requires_throw<ExpressionError>([&] { (void)evaluate(deeply_nested); }, "nesting guard");
    requires_throw<ExpressionError>([] { (void)evaluate(std::string(70, '-') + "1"); }, "unary recursion guard");
    requires_throw<ExpressionError>([] { (void)evaluate(std::string(4097, ' ')); }, "input length guard");
    std::string many_operations = "1";
    for (int index = 0; index < 1200; ++index) many_operations += "+1";
    requires_throw<ExpressionError>([&] { (void)evaluate(many_operations); }, "operation or token limit");
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
}
}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"canonical normalization", normalization}, {"independent arithmetic oracle", arithmetic},
        {"integer boundaries and overflow", integer_boundaries}, {"exact decimal literals", decimal_literals},
        {"expression precedence and equivalence", expression_values}, {"expression errors and resource limits", expression_failures},
        {"worked calculation steps", explanations},
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
