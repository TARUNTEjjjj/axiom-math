#include "axiom/math/expression.hpp"
#include "axiom/math/tolerance.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

#if !defined(__SIZEOF_INT128__)
#error "AxiomMath requires compiler support for 128-bit integer intermediates."
#endif
__extension__ using Wide = __int128;
__extension__ using UWide = unsigned __int128;

void usage(std::ostream& out) {
    out << "AxiomMath 0.1.0 — exact arithmetic in C++\n\n"
        << "Usage:\n"
        << "  axiom-math eval EXPR [--json]          Print a reduced exact result\n"
        << "  axiom-math check LEFT RIGHT [--json]   Check exact arithmetic equivalence\n"
        << "  axiom-math approx EXPECTED ANSWER ABSOLUTE RELATIVE [--json]\n"
        << "                                         Accept ANSWER if |ANSWER - EXPECTED| <= ABSOLUTE\n"
        << "                                         or <= RELATIVE * |EXPECTED| (exact, inclusive)\n"
        << "  axiom-math explain EXPR [--json]       Show calculations in evaluation order\n"
        << "  axiom-math pow EXPR EXPONENT [--json]  Raise an exact result to an integer power\n"
        << "  axiom-math cf EXPR [--json]            Canonical continued fraction and convergents\n"
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

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 4);
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (byte < 0x20 || byte >= 0x7f) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(byte));
                out += buf;
            } else {
                out.push_back(ch);
            }
            break;
        }
    }
    return out;
}

std::string wide_to_string(Wide value) {
    if (value == 0) return "0";
    const bool neg = value < 0;
    UWide mag = neg ? static_cast<UWide>(-(value + 1)) + 1U : static_cast<UWide>(value);
    std::string digits;
    while (mag > 0U) {
        digits.push_back(static_cast<char>('0' + static_cast<unsigned>(mag % 10U)));
        mag /= 10U;
    }
    if (neg) digits.push_back('-');
    std::reverse(digits.begin(), digits.end());
    return digits;
}

std::string u64_to_string(std::uint64_t value) {
    std::array<char, 32> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value);
    return std::string(buf.data(), static_cast<std::size_t>(res.ptr - buf.data()));
}

std::string i64_to_string(std::int64_t value) {
    std::array<char, 32> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value);
    return std::string(buf.data(), static_cast<std::size_t>(res.ptr - buf.data()));
}

struct ContinuedFraction {
    std::int64_t a0 = 0;
    std::vector<std::uint64_t> tail;
    std::vector<std::string> convergents;
};

ContinuedFraction compute_continued_fraction(const axiom::math::Rational& value) {
    ContinuedFraction cf;
    const std::int64_t p = value.numerator();
    const std::uint64_t den = static_cast<std::uint64_t>(value.denominator());
    std::uint64_t num = den;
    std::uint64_t rem = 0;
    if (p >= 0) {
        cf.a0 = p / value.denominator();
        rem = static_cast<std::uint64_t>(p % value.denominator());
    } else {
        const std::uint64_t mag = std::uint64_t{0} - static_cast<std::uint64_t>(p);
        const std::uint64_t q_abs = mag / den;
        const std::uint64_t r_abs = mag % den;
        if (r_abs == 0) {
            cf.a0 = p; // Reduced integer (denominator == 1), handles INT64_MIN safely.
            rem = 0;
        } else {
            cf.a0 = -static_cast<std::int64_t>(q_abs + 1U);
            rem = den - r_abs;
        }
    }
    while (rem != 0) {
        const std::uint64_t qk = num / rem;
        const std::uint64_t next_r = num % rem;
        cf.tail.push_back(qk);
        num = rem;
        rem = next_r;
    }

    Wide h_prev = 1;
    Wide h_curr = cf.a0;
    Wide g_prev = 0;
    Wide g_curr = 1;
    cf.convergents.push_back(wide_to_string(h_curr));
    for (const std::uint64_t ai : cf.tail) {
        const Wide a = static_cast<Wide>(ai);
        const Wide h_next = a * h_curr + h_prev;
        const Wide g_next = a * g_curr + g_prev;
        h_prev = h_curr;
        h_curr = h_next;
        g_prev = g_curr;
        g_curr = g_next;
        cf.convergents.push_back(wide_to_string(h_curr) + "/" + wide_to_string(g_curr));
    }
    return cf;
}

struct DecimalExpansion {
    std::string text;
    bool terminating = true;
    bool truncated = false;
    std::size_t period_length = 0;
};

DecimalExpansion format_decimal(const axiom::math::Rational& value) {
    DecimalExpansion out;
    const bool neg = value.numerator() < 0;
    const std::uint64_t mag = neg ? (std::uint64_t{0} - static_cast<std::uint64_t>(value.numerator()))
                                  : static_cast<std::uint64_t>(value.numerator());
    const std::uint64_t den = static_cast<std::uint64_t>(value.denominator());
    const std::uint64_t int_part = mag / den;
    std::uint64_t rem = mag % den;
    out.text = (neg ? "-" : "") + u64_to_string(int_part);
    if (rem == 0) {
        return out;
    }
    out.text.push_back('.');

    constexpr std::size_t max_digits = 64;
    std::vector<std::pair<std::uint64_t, std::size_t>> seen;
    seen.reserve(max_digits);
    std::string frac;
    frac.reserve(max_digits);

    for (std::size_t idx = 0; idx < max_digits && rem != 0; ++idx) {
        for (const auto& entry : seen) {
            if (entry.first == rem) {
                const std::size_t prefix_len = entry.second;
                out.terminating = false;
                out.period_length = frac.size() - prefix_len;
                out.text += frac.substr(0, prefix_len) + "(" + frac.substr(prefix_len) + ")";
                return out;
            }
        }
        seen.emplace_back(rem, idx);
        const UWide scaled = static_cast<UWide>(rem) * 10U;
        const auto digit = static_cast<unsigned>(scaled / den);
        rem = static_cast<std::uint64_t>(scaled % den);
        frac.push_back(static_cast<char>('0' + digit));
    }
    if (rem == 0) {
        out.text += frac;
        return out;
    }
    out.terminating = false;
    out.truncated = true;
    out.text += frac + "...";
    return out;
}

std::string format_mixed(const axiom::math::Rational& value) {
    if (value.denominator() == 1) return value.str();
    const bool neg = value.numerator() < 0;
    const std::uint64_t mag = neg ? (std::uint64_t{0} - static_cast<std::uint64_t>(value.numerator()))
                                  : static_cast<std::uint64_t>(value.numerator());
    const std::uint64_t den = static_cast<std::uint64_t>(value.denominator());
    const std::uint64_t whole = mag / den;
    const std::uint64_t rem = mag % den;
    if (whole == 0) return value.str();
    return (neg ? "-" : "") + u64_to_string(whole) + (neg ? " - " : " + ") + u64_to_string(rem) + "/" + u64_to_string(den);
}

std::string format_json_double(const axiom::math::Rational& value) {
    if (value.numerator() == 0) return "0";
    const double approx = static_cast<double>(value.numerator()) / static_cast<double>(value.denominator());
    if (!std::isfinite(approx)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", approx);
    return std::string(buf);
}

std::string rational_json_object(const axiom::math::Rational& value) {
    const auto cf = compute_continued_fraction(value);
    const auto dec = format_decimal(value);
    const int sign = value.numerator() > 0 ? 1 : (value.numerator() < 0 ? -1 : 0);

    std::ostringstream os;
    os << "{\"canonical\":\"" << json_escape(value.str()) << "\""
       << ",\"numerator\":" << i64_to_string(value.numerator())
       << ",\"denominator\":" << i64_to_string(value.denominator())
       << ",\"is_integer\":" << (value.denominator() == 1 ? "true" : "false")
       << ",\"sign\":" << sign
       << ",\"mixed\":\"" << json_escape(format_mixed(value)) << "\""
       << ",\"decimal\":{\"text\":\"" << json_escape(dec.text) << "\""
       << ",\"terminating\":" << (dec.terminating ? "true" : "false")
       << ",\"truncated\":" << (dec.truncated ? "true" : "false")
       << ",\"period_length\":" << dec.period_length << "}"
       << ",\"double\":" << format_json_double(value)
       << ",\"continued_fraction\":[" << i64_to_string(cf.a0);
    for (const std::uint64_t a : cf.tail) {
        os << ',' << u64_to_string(a);
    }
    os << "],\"convergents\":[";
    for (std::size_t i = 0; i < cf.convergents.size(); ++i) {
        if (i > 0) os << ',';
        os << '"' << cf.convergents[i] << '"';
    }
    os << "]}";
    return os.str();
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

    bool json_output = false;
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc));
    int start_idx = 1;
    int end_idx = argc;
    if (argc >= 3 && std::string_view(argv[1]) == "--json") {
        json_output = true;
        start_idx = 2;
    } else if (argc >= 4 && std::string_view(argv[argc - 1]) == "--json") {
        json_output = true;
        end_idx = argc - 1;
    }
    for (int i = start_idx; i < end_idx; ++i) {
        args.emplace_back(argv[i]);
    }
    if (args.empty()) {
        usage(std::cerr);
        return 2;
    }

    const std::string_view command = args[0];
    const bool single = command == "eval" || command == "explain" || command == "cf";
    const bool approx = command == "approx";
    if ((!single && !approx && command != "check" && command != "pow") || (single && args.size() != 2) ||
        ((command == "check" || command == "pow") && args.size() != 3) || (approx && args.size() != 5)) {
        usage(std::cerr);
        return 2;
    }
    try {
        if (command == "pow") {
            std::string_view text = args[2];
            if (!text.empty() && text.front() == '+') {
                text.remove_prefix(1);
                if (!text.empty() && (text.front() == '-' || text.front() == '+'))
                    throw std::invalid_argument("exponent must be a signed 64-bit integer");
            }
            std::int64_t exponent = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), exponent);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                throw std::invalid_argument("exponent must be a signed 64-bit integer");
            const auto base = run(args[1], false, "base expression");
            const auto powered = base.value.pow(exponent);
            if (json_output) {
                std::cout << "{\"command\":\"pow\",\"ok\":true,\"exponent\":" << i64_to_string(exponent)
                          << ",\"base\":" << rational_json_object(base.value)
                          << ",\"result\":" << rational_json_object(powered) << "}\n";
            } else {
                std::cout << powered.str() << '\n';
            }
            return finish_output();
        }
        if (approx) {
            // Evaluate every argument, so each error names its own argument,
            // then validate the tolerance (std::invalid_argument, status 2).
            const auto expected = run(args[1], false, "expected value");
            const auto answer = run(args[2], false, "answer");
            const axiom::math::Tolerance tolerance{run(args[3], false, "absolute tolerance").value,
                                                   run(args[4], false, "relative tolerance").value};
            const bool accepted = axiom::math::within_tolerance(expected.value, answer.value, tolerance);
            if (json_output) {
                std::string diff_json = "\"exceeds_64bit_rational\"";
                try {
                    auto diff = answer.value - expected.value;
                    if (diff.numerator() < 0) diff = -diff;
                    diff_json = "\"" + json_escape(diff.str()) + "\"";
                } catch (const std::overflow_error&) {
                }
                std::cout << "{\"command\":\"approx\",\"ok\":true,\"accepted\":" << (accepted ? "true" : "false")
                          << ",\"expected\":" << rational_json_object(expected.value)
                          << ",\"answer\":" << rational_json_object(answer.value)
                          << ",\"abs_difference\":" << diff_json << "}\n";
            } else {
                std::cout << (accepted ? "within tolerance" : "outside tolerance") << '\n';
            }
            return finish_output(accepted ? 0 : 1);
        }
        if (command == "check") {
            const auto left = run(args[1], false, "left expression");
            const auto right = run(args[2], false, "right expression");
            const bool same = left.value == right.value;
            if (json_output) {
                std::cout << "{\"command\":\"check\",\"ok\":true,\"equivalent\":" << (same ? "true" : "false")
                          << ",\"left\":" << rational_json_object(left.value)
                          << ",\"right\":" << rational_json_object(right.value) << "}\n";
            } else {
                std::cout << (same ? "equivalent" : "not equivalent") << '\n';
            }
            return finish_output(same ? 0 : 1);
        }
        if (command == "cf") {
            const auto result = run(args[1], false, "expression");
            if (json_output) {
                std::cout << "{\"command\":\"cf\",\"ok\":true,\"result\":" << rational_json_object(result.value) << "}\n";
            } else {
                const auto cf = compute_continued_fraction(result.value);
                std::cout << '[' << i64_to_string(cf.a0);
                if (!cf.tail.empty()) {
                    std::cout << "; ";
                    for (std::size_t i = 0; i < cf.tail.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << u64_to_string(cf.tail[i]);
                    }
                }
                std::cout << "]\nConvergents: ";
                for (std::size_t i = 0; i < cf.convergents.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << cf.convergents[i];
                }
                std::cout << '\n';
            }
            return finish_output();
        }
        const auto result = run(args[1], command == "explain", "expression");
        if (json_output) {
            std::cout << "{\"command\":\"" << command << "\",\"ok\":true,\"result\":"
                      << rational_json_object(result.value);
            if (command == "explain") {
                std::cout << ",\"omitted_steps\":" << result.omitted_steps << ",\"steps\":[";
                for (std::size_t i = 0; i < result.steps.size(); ++i) {
                    if (i > 0) std::cout << ',';
                    std::cout << "{\"expression\":\"" << json_escape(printable(result.steps[i].expression))
                              << "\",\"value\":\"" << json_escape(result.steps[i].result.str()) << "\"}";
                }
                std::cout << ']';
            }
            std::cout << "}\n";
            return finish_output();
        }
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
