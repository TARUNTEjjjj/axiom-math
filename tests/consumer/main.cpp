#include <axiom/math/expression.hpp>
#include <axiom/math/rational.hpp>

int main() {
    using axiom::math::Rational;
    if (Rational(2, 4) != Rational(1, 2)) return 1;
    if (axiom::math::evaluate("0.1 + 0.2").value != Rational(3, 10)) return 1;
    return 0;
}
