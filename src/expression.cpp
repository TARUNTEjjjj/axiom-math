#include "axiom/math/expression.hpp"

#include <utility>

namespace axiom::math {

ExpressionError::ExpressionError(std::string message, const std::size_t position)
    : std::runtime_error(std::move(message)), position_(position) {}

std::size_t ExpressionError::position() const noexcept { return position_; }

namespace {

bool digit(const char character) noexcept {
    return character >= '0' && character <= '9';
}

bool whitespace(const char character) noexcept {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\f' || character == '\v';
}

struct Value {
    Rational rational;
    std::size_t begin;
    std::size_t end;
};

class Parser {
public:
    Parser(const std::string_view input, const bool collect_steps)
        : input_(input), collect_steps_(collect_steps) {}

    Evaluation parse() {
        if (input_.size() > max_expression_length) {
            fail("Expression exceeds the 4096-byte limit.", max_expression_length);
        }
        auto result = expression(0);
        skip_whitespace();
        if (position_ != input_.size()) {
            fail("Unexpected input after the expression.", position_);
        }
        return {result.rational, std::move(steps_)};
    }

private:
    [[noreturn]] static void fail(const std::string& message,
                                  const std::size_t position) {
        throw ExpressionError(message, position);
    }

    void skip_whitespace() {
        while (position_ < input_.size() && whitespace(input_[position_])) {
            ++position_;
        }
    }

    void token(const std::size_t position) {
        if (tokens_ == max_expression_tokens) {
            fail("Expression exceeds the 1024-token limit.", position);
        }
        ++tokens_;
    }

    static void require_depth(const std::size_t depth,
                              const std::size_t position) {
        if (depth == max_expression_depth) {
            fail("Expression exceeds the 64-level nesting limit.", position);
        }
    }

    template <typename Operation>
    Rational checked(const std::size_t position, Operation operation) {
        try {
            return operation();
        } catch (const std::overflow_error& error) {
            fail(error.what(), position);
        } catch (const std::domain_error& error) {
            fail(error.what(), position);
        } catch (const std::invalid_argument& error) {
            fail(error.what(), position);
        }
    }

    void record(const Value& value) {
        if (collect_steps_) {
            steps_.push_back({std::string(input_.substr(value.begin,
                                                       value.end - value.begin)),
                              value.rational});
        }
    }

    Value expression(const std::size_t depth) {
        auto left = term(depth);
        while (true) {
            skip_whitespace();
            if (position_ == input_.size() ||
                (input_[position_] != '+' && input_[position_] != '-')) {
                return left;
            }
            const auto operator_position = position_;
            const char operation = input_[position_++];
            token(operator_position);
            const auto right = term(depth);
            left = {checked(operator_position, [&] {
                        return operation == '+' ? left.rational + right.rational
                                                : left.rational - right.rational;
                    }),
                    left.begin, right.end};
            record(left);
        }
    }

    Value term(const std::size_t depth) {
        auto left = unary(depth);
        while (true) {
            skip_whitespace();
            if (position_ == input_.size() ||
                (input_[position_] != '*' && input_[position_] != '/')) {
                return left;
            }
            const auto operator_position = position_;
            const char operation = input_[position_++];
            token(operator_position);
            const auto right = unary(depth);
            left = {checked(operator_position, [&] {
                        return operation == '*' ? left.rational * right.rational
                                                : left.rational / right.rational;
                    }),
                    left.begin, right.end};
            record(left);
        }
    }

    Value unary(const std::size_t depth) {
        skip_whitespace();
        if (position_ < input_.size() &&
            (input_[position_] == '+' || input_[position_] == '-')) {
            const auto begin = position_;
            require_depth(depth, begin);
            const char sign = input_[position_++];
            token(begin);
            skip_whitespace();

            // Parse a signed numeric operand directly. Its positive magnitude
            // need not be representable first, so INT64_MIN remains usable.
            if (position_ < input_.size() &&
                (digit(input_[position_]) || input_[position_] == '.')) {
                const auto value = number(sign, begin);
                record(value);
                return value;
            }

            const auto operand = unary(depth + 1);
            const Value value{checked(begin, [&] {
                                  return sign == '-' ? -operand.rational
                                                     : operand.rational;
                              }),
                              begin, operand.end};
            record(value);
            return value;
        }
        return primary(depth);
    }

    Value primary(const std::size_t depth) {
        skip_whitespace();
        if (position_ == input_.size()) {
            fail("Expected a number or '('.", position_);
        }
        if (input_[position_] == '(') {
            const auto begin = position_;
            require_depth(depth, begin);
            token(position_++);
            auto value = expression(depth + 1);
            skip_whitespace();
            if (position_ == input_.size() || input_[position_] != ')') {
                fail("Expected ')'.", position_);
            }
            token(position_++);
            value.begin = begin;
            value.end = position_;
            return value;
        }
        if (digit(input_[position_]) || input_[position_] == '.') {
            return number('\0', position_);
        }
        fail("Expected a number or '('.", position_);
    }

    Value number(const char sign, const std::size_t begin) {
        const auto number_begin = position_;
        token(number_begin);
        bool has_digits = false;
        while (position_ < input_.size() && digit(input_[position_])) {
            has_digits = true;
            ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            while (position_ < input_.size() && digit(input_[position_])) {
                has_digits = true;
                ++position_;
            }
        }
        if (!has_digits) {
            fail("A decimal literal must contain a digit.", number_begin);
        }
        const auto literal = input_.substr(number_begin, position_ - number_begin);
        const auto value = checked(begin, [&] {
            if (sign == '\0') {
                return Rational::from_decimal(literal);
            }
            std::string signed_literal(1, sign);
            signed_literal.append(literal);
            return Rational::from_decimal(signed_literal);
        });
        return {value, begin, position_};
    }

    std::string_view input_;
    bool collect_steps_;
    std::size_t position_ = 0;
    std::size_t tokens_ = 0;
    std::vector<Step> steps_;
};

}  // namespace

Evaluation evaluate(const std::string_view expression, const bool collect_steps) {
    return Parser(expression, collect_steps).parse();
}

bool equivalent(const std::string_view left, const std::string_view right) {
    return evaluate(left).value == evaluate(right).value;
}

}  // namespace axiom::math
