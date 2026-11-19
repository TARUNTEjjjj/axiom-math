#include "axiom/math/expression.hpp"
#include "axiom/math/tolerance.hpp"

#include <algorithm>
#include <charconv>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void usage(std::ostream& out) {
    out << "AxiomMath 0.1.0 — exact arithmetic in C++\n\n"
        << "Usage:\n"
        << "  axiom-math eval EXPR          Print a reduced exact result\n"
        << "  axiom-math check LEFT RIGHT   Check exact arithmetic equivalence\n"
        << "  axiom-math approx EXPECTED ANSWER ABSOLUTE RELATIVE\n"
        << "                                Accept ANSWER if |ANSWER - EXPECTED| <= ABSOLUTE\n"
        << "                                or <= RELATIVE * |EXPECTED| (exact, inclusive)\n"
        << "  axiom-math explain EXPR       Show calculations in evaluation order\n"
        << "  axiom-math pow EXPR EXPONENT  Raise an exact result to an integer power\n"
        << "  axiom-math --help\n"
        << "  axiom-math --version\n\n"
        << "Quote expressions containing spaces or shell operators.\n"
        << "Supported: integers, finite decimals, + - * /, unary signs, parentheses.\n"
        << "Exit codes: 0 success/equivalent/within tolerance; 1 not equivalent/outside tolerance;\n"
        << "2 invalid input, invalid tolerance or output error.\n";
}

int finish_output(int status = 0) {
    // A buffered write can fail only when flushed (for example a full disk or
    // a read-only stdout descriptor). Do not report a result we could not emit.
    std::cout.flush();
    if (!std::cout) {
        std::cerr << "error: could not write command output\n";
        return 2;
    }
    return status;
}

// Keep control bytes from an input expression out of terminal output. Byte
// offsets remain stable, including for unsupported non-ASCII input.
std::string printable(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        out.push_back(byte >= 32 && byte <= 126 ? static_cast<char>(byte) : ' ');
    }
    return out;
}

axiom::math::Evaluation run(std::string_view text, bool explain, std::string_view label) {
    try {
        return axiom::math::evaluate(text, explain);
    } catch (const axiom::math::ExpressionError& error) {
        const auto position = std::min(error.position(), text.size());
        const auto begin = position > 45 ? position - 45 : 0;
        const auto length = std::min<std::size_t>(text.size() - begin, 90);
        const std::string prefix = begin == 0 ? "" : "...";
        std::cerr << "error in " << label << " at byte " << error.position()
                  << ": " << error.what() << '\n'
                  << prefix << printable(text.substr(begin, length))
                  << (begin + length < text.size() ? "..." : "") << '\n'
                  << std::string(prefix.size() + position - begin, ' ') << "^\n";
        throw;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "AxiomMath 0.1.0\n";
        return finish_output();
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
        usage(std::cout);
        return finish_output();
    }
    if (argc < 2) {
        usage(std::cerr);
        return 2;
    }
    const std::string_view command(argv[1]);
    const bool single = command == "eval" || command == "explain";
    const bool approx = command == "approx";
    if ((!single && !approx && command != "check" && command != "pow") || (single && argc != 3) ||
        ((command == "check" || command == "pow") && argc != 4) || (approx && argc != 6)) {
        usage(std::cerr);
        return 2;
    }
    try {
        if (command == "pow") {
            std::string_view text(argv[3]);
            if (!text.empty() && text.front() == '+') {
                text.remove_prefix(1);
                if (!text.empty() && (text.front() == '-' || text.front() == '+'))
                    throw std::invalid_argument("exponent must be a signed 64-bit integer");
            }
            std::int64_t exponent = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), exponent);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                throw std::invalid_argument("exponent must be a signed 64-bit integer");
            const auto base = run(argv[2], false, "base expression");
            std::cout << base.value.pow(exponent).str() << '\n';
            return finish_output();
        }
        if (approx) {
            // Evaluate every argument, so each error names its own argument,
            // then validate the tolerance (std::invalid_argument, status 2).
            const auto expected = run(argv[2], false, "expected value");
            const auto answer = run(argv[3], false, "answer");
            const axiom::math::Tolerance tolerance{run(argv[4], false, "absolute tolerance").value,
                                                   run(argv[5], false, "relative tolerance").value};
            const bool accepted = axiom::math::within_tolerance(expected.value, answer.value, tolerance);
            std::cout << (accepted ? "within tolerance" : "outside tolerance") << '\n';
            return finish_output(accepted ? 0 : 1);
        }
        if (command == "check") {
            const auto left = run(argv[2], false, "left expression");
            const auto right = run(argv[3], false, "right expression");
            const bool same = left.value == right.value;
            std::cout << (same ? "equivalent" : "not equivalent") << '\n';
            return finish_output(same ? 0 : 1);
        }
        const auto result = run(argv[2], command == "explain", "expression");
        if (command == "explain") {
            std::size_t index = 1;
            for (const auto& step : result.steps) {
                std::cout << index++ << ". " << printable(step.expression) << " = " << step.result.str() << '\n';
            }
            // A long chain repeats its earlier text in every step, so the library
            // records only the first steps. The result is complete either way.
            if (result.omitted_steps != 0) {
                std::cout << "Omitted steps: " << result.omitted_steps << " (the steps of one explanation quote at most "
                          << axiom::math::max_explanation_length << " bytes)\n";
            }
            std::cout << "Result: ";
        }
        std::cout << result.value.str() << '\n';
        return finish_output();
    } catch (const axiom::math::ExpressionError&) {
        return 2; // run() already printed source context.
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
