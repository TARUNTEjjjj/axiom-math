#include <axiom/math/expression.hpp>
#include <axiom/math/rational.hpp>

#include <cstdint>

namespace {
// The address of an inline member, held in a volatile variable that the compiler
// must keep, so the plugin contains its own copy of that function.
std::int64_t (axiom::math::Rational::* volatile denominator_of)() const noexcept =
    &axiom::math::Rational::denominator;
}  // namespace

// The plugin's only export. It uses the arithmetic, the parser and the error
// types, so the plugin contains the whole static AxiomMath library. It returns
// 0 when every result is the expected one.
extern "C" __attribute__((visibility("default"))) int axiom_embedded_plugin_check() {
    using axiom::math::Rational;
    if (axiom::math::evaluate("1/3 + 1/6").value != Rational(1, 2) || Rational(2, 3).pow(-3).str() != "27/8" ||
        (Rational(1, 3).*denominator_of)() != 3) {
        return 1;
    }
    try {
        (void)axiom::math::evaluate("1/0");
    } catch (const axiom::math::ExpressionError& error) {
        return error.position() == 1 ? 0 : 1;
    }
    return 1;
}
