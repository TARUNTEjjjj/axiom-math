#include "axiom/math/expression.hpp"
#include "axiom/math/rational.hpp"
#include "axiom/math/tolerance.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using axiom::math::ExpressionError;
using axiom::math::Rational;
using axiom::math::Tolerance;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::abort();
    }
}

struct SplitMix64 {
    std::uint64_t state;
    explicit SplitMix64(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    std::int64_t range(std::int64_t lo, std::int64_t hi) {
        const auto span = static_cast<std::uint64_t>(hi - lo) + 1ULL;
        return lo + static_cast<std::int64_t>(next() % span);
    }
};

Rational rand_bounded_rational(SplitMix64& rng, std::int64_t max_mag) {
    const std::int64_t num = rng.range(-max_mag, max_mag);
    const std::int64_t den = rng.range(1, max_mag);
    return Rational(num, den);
}

void test_field_axioms_and_stern_brocot() {
    SplitMix64 rng(0xA10DA7A20261126ULL);
    for (int i = 0; i < 1500; ++i) {
        const Rational a = rand_bounded_rational(rng, 250);
        const Rational b = rand_bounded_rational(rng, 250);
        const Rational c = rand_bounded_rational(rng, 250);

        check(a + b == b + a, "addition commutativity");
        check(a * b == b * a, "multiplication commutativity");
        check((a + b) + c == a + (b + c), "addition associativity");
        check((a * b) * c == a * (b * c), "multiplication associativity");
        check(a * (b + c) == (a * b) + (a * c), "distributivity");
        check(a + (-a) == Rational(0), "additive inverse");
        check(a + Rational(0) == a, "additive identity");
        check(a * Rational(1) == a, "multiplicative identity");
        if (b != Rational(0)) {
            check((a / b) * b == a, "multiplicative inverse");
        }
        // Order compatibility with positive translation and scaling
        if (a < b) {
            check(a + c < b + c, "order translation invariance");
            if (c > Rational(0)) {
                check(a * c < b * c, "order positive scaling");
            } else if (c < Rational(0)) {
                check(b * c < a * c, "order negative scaling reversal");
            }
            const Rational mediant(a.numerator() + b.numerator(), a.denominator() + b.denominator());
            check(a < mediant && mediant < b, "Stern-Brocot mediant strictly inside (a, b)");
        }
    }

    // Farey sequence neighbor determinant p_next * q - p * q_next == 1
    std::vector<Rational> farey{Rational(0, 1), Rational(1, 7), Rational(1, 6), Rational(1, 5),
                                Rational(1, 4), Rational(2, 7), Rational(1, 3), Rational(2, 5),
                                Rational(3, 7), Rational(1, 2), Rational(4, 7), Rational(3, 5),
                                Rational(2, 3), Rational(5, 7), Rational(3, 4), Rational(4, 5),
                                Rational(5, 6), Rational(6, 7), Rational(1, 1)};
    for (std::size_t i = 0; i + 1 < farey.size(); ++i) {
        const auto& l = farey[i];
        const auto& r = farey[i + 1];
        const std::int64_t det = r.numerator() * l.denominator() - l.numerator() * r.denominator();
        check(det == 1, "adjacent F_7 Farey neighbors have unimodular determinant 1");
    }
}

void test_metamorphic_expressions_and_fuzz_parser() {
    SplitMix64 rng(0x5EEDBEEF98765432ULL);
    for (int i = 0; i < 400; ++i) {
        const std::int64_t a = rng.range(-50, 50);
        const std::int64_t b = rng.range(1, 30);
        const std::int64_t c = rng.range(-40, 40);
        const std::int64_t d = rng.range(1, 30);
        const std::int64_t e = rng.range(-20, 20);

        const std::string sa = std::to_string(a) + "/" + std::to_string(b);
        const std::string sc = std::to_string(c) + "/" + std::to_string(d);
        const std::string se = std::to_string(e);

        const std::string lhs = "(" + sa + " + " + sc + ") * " + se;
        const std::string rhs = sa + " * " + se + " + (" + sc + ") * (" + se + ")";
        check(axiom::math::equivalent(lhs, rhs), "metamorphic distributive equivalence");
    }

    // Adversarial byte fuzzing: never crash, always bounded error position <= input.size()
    static constexpr const char alphabet[] = "0123456789+-*/(). \t\n\v\x01\x7f\xc3\xa9!";
    for (int i = 0; i < 1200; ++i) {
        const std::size_t len = static_cast<std::size_t>(rng.range(0, 96));
        std::string fuzz;
        fuzz.reserve(len);
        for (std::size_t k = 0; k < len; ++k) {
            fuzz.push_back(alphabet[rng.next() % (sizeof(alphabet) - 1)]);
        }
        try {
            const auto ev = axiom::math::evaluate(fuzz, (i & 1) == 0);
            check(ev.value.denominator() > 0, "canonical denominator strictly positive");
            const auto untraced = axiom::math::evaluate(fuzz, false);
            check(ev.value == untraced.value, "traced evaluation value matches untraced");
        } catch (const ExpressionError& err) {
            check(err.position() <= fuzz.size(), "ExpressionError byte offset within input bounds");
        } catch (const std::overflow_error&) {
        } catch (const std::domain_error&) {
        }
    }

    // Extreme tolerance invariants with signed 64-bit bounds without overflow
    const Rational lo(std::numeric_limits<std::int64_t>::min(), 1);
    const Rational hi(std::numeric_limits<std::int64_t>::max(), 1);
    check(!axiom::math::within_tolerance(lo, hi, Tolerance{Rational(1), Rational(0)}),
          "wide separation outside unit tolerance");
    check(axiom::math::within_tolerance(hi, Rational(std::numeric_limits<std::int64_t>::max() - 1, 1),
                                        Tolerance{Rational(1), Rational(0)}),
          "boundary inclusive tolerance near INT64_MAX");
    const Rational half_max(std::numeric_limits<std::int64_t>::max() / 4, 1);
    check(axiom::math::within_tolerance(half_max, half_max - Rational(1, 2), Tolerance{Rational(1, 2), Rational(0)}),
          "fractional boundary inclusive tolerance at large magnitude");
}

} // namespace

int main() {
    test_field_axioms_and_stern_brocot();
    test_metamorphic_expressions_and_fuzz_parser();
    std::cout << "PASS axiom-math field axioms, Stern-Brocot Farey unimodularity, and parser fuzzing\n";
    return 0;
}
