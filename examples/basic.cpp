#include "axiom/math/expression.hpp"
#include "axiom/math/rational.hpp"

#include <iostream>

int main() {
    using axiom::math::Rational;
    const Rational total = Rational(1, 3) + Rational(1, 6);
    std::cout << "1/3 + 1/6 = " << total.str() << '\n';
    std::cout << "0.1 + 0.2 = " << axiom::math::evaluate("0.1 + 0.2").value.str() << '\n';
    return total == Rational(1, 2) ? 0 : 1;
}
