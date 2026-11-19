#include <axiom/math/expression.hpp>
#include <axiom/math/rational.hpp>
#include <axiom/math/tolerance.hpp>

#include <exception>
#include <iostream>

int main() {
    using axiom::math::Rational;
    if (Rational(2, 4) != Rational(1, 2)) return 1;
    if (axiom::math::evaluate("0.1 + 0.2").value != Rational(3, 10)) return 1;
    if (!axiom::math::equivalent("2 / 4", ".5") || axiom::math::equivalent("1/3", "0.333")) return 1;
    if (!axiom::math::approximately_equivalent("1/3", "0.333", {Rational(1, 1000), 0}) ||
        axiom::math::within_tolerance(Rational(1, 3), Rational(333, 1000), {})) return 1;
    // This consumer is built with hidden visibility. Errors thrown inside a
    // shared AxiomMath library must still match a catch by their own types.
    try {
        (void)axiom::math::equivalent("1", "2/0");
        std::cerr << "equivalent(\"1\", \"2/0\") did not throw\n";
        return 1;
    } catch (const axiom::math::OperandError& error) {
        if (error.operand() != axiom::math::OperandError::Operand::right || error.position() != 1) return 1;
    } catch (const std::exception& error) {
        std::cerr << "OperandError was caught only as std::exception: " << error.what() << '\n';
        return 1;
    }
    try {
        (void)axiom::math::evaluate("1/0");
    } catch (const axiom::math::ExpressionError& error) {
        return error.position() == 1 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "ExpressionError was caught only as std::exception: " << error.what() << '\n';
        return 1;
    }
    std::cerr << "1/0 did not throw\n";
    return 1;
}
