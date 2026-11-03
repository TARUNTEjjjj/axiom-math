# AxiomMath

[![C++ checks](https://github.com/TARUNTEjjjj/axiom-math/actions/workflows/ci.yml/badge.svg)](https://github.com/TARUNTEjjjj/axiom-math/actions/workflows/ci.yml)

Exact fractions, arithmetic expressions and equivalent-answer checks in C++20. AxiomMath is a small library and command-line tool with no third-party runtime dependencies.

```console
$ axiom-math eval "1/3 + 1/6"
1/2
$ axiom-math eval "0.1 + 0.2"
3/10
$ axiom-math check "(2 + 3) / 10" "0.5"
equivalent
$ axiom-math check "1/3" "0.333"
not equivalent
```

Decimals are converted to exact fractions. `0.333` is exactly `333/1000`, so it is not equal to `1/3`. Results are reduced fractions or integers; the library never silently rounds.

## Build and run

Requirements: CMake 3.20+, a C++20 compiler with 128-bit integer support (GCC or Clang on supported 64-bit targets), and a native build tool such as Make or Ninja. Python 3.9+ is optional locally and runs the independent differential tests. MSVC is not supported by this version.

```sh
git clone https://github.com/TARUNTEjjjj/axiom-math.git
cd axiom-math
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/axiom-math eval "1/3 + 1/6"
```

There is no package download during the build. The default build creates the CLI, the library, a library example and the C++ tests. If Python is unavailable, CMake explicitly reports that it is skipping the additional Python oracle.

## Commands

| Command | Result |
| --- | --- |
| `axiom-math eval "EXPR"` | Print the reduced exact result. |
| `axiom-math check "LEFT" "RIGHT"` | Print `equivalent` or `not equivalent`. |
| `axiom-math explain "EXPR"` | Print each unary/binary calculation in evaluation order and the final result. |
| `axiom-math --help` | Show usage and supported syntax. |

Always quote expressions so the shell does not interpret spaces, `*` or parentheses.

```console
$ axiom-math explain "(1/3 + 1/6) * 4"
1. 1/3 = 1/3
2. 1/6 = 1/6
3. 1/3 + 1/6 = 1/2
4. (1/3 + 1/6) * 4 = 2
Result: 2
```

The explanation traces arithmetic evaluation, including fraction construction. It is not a symbolic proof or an equation-solving tutor. Invalid input produces an error, a byte position and a source pointer on standard error.

Exit codes are `0` for successful evaluation or equality, `1` for a valid unequal comparison, and `2` for invalid syntax, division by zero, overflow, incorrect command usage or an output-write failure. Commands flush their output before reporting success, so a failed redirected write is reported as an error. As is conventional for Unix tools, a write to a closed pipe instead ends the command with `SIGPIPE`, which shells report as status 141. Successful output goes to standard output so commands can be used in scripts.

## Use the C++ library

```cpp
#include <axiom/math/expression.hpp>
#include <axiom/math/rational.hpp>

using axiom::math::Rational;

const auto sum = Rational(1, 3) + Rational(1, 6); // 1/2
const auto result = axiom::math::evaluate("0.1 + 0.2");
const bool same = result.value == Rational(3, 10); // true
const bool equivalent = axiom::math::equivalent("2 / 4", ".5");
```

Embed this repository using `add_subdirectory` and link `AxiomMath::axiom_math`, or install it:

```sh
cmake --install build --prefix "$PWD/install"
```

An installed CMake consumer can use `find_package(AxiomMath CONFIG REQUIRED)` and `target_link_libraries(your_target PRIVATE AxiomMath::axiom_math)`. Set `CMAKE_PREFIX_PATH` to the installation prefix. See [examples/basic.cpp](examples/basic.cpp) for a complete executable.

For a shared library, configure with `-DBUILD_SHARED_LIBS=ON`. On Linux and macOS,
the installed CLI locates its library relative to itself, so an installation with
relative directories can be moved as a unit. Standard CMake options such as
`CMAKE_INSTALL_BINDIR`, `CMAKE_INSTALL_LIBDIR` and `CMAKE_INSTALL_INCLUDEDIR` are
respected; keep their values relative for a relocatable installation.

## Supported syntax and limits

- Integers and finite decimal literals: `12`, `-3`, `.5`, `5.`, `0.125`.
- `+`, `-`, `*`, `/`, unary signs and parentheses, with standard precedence and left associativity. A fraction is a division expression.
- No variables, exponent notation, powers, functions, implicit multiplication or symbolic equations.
- Every evaluated subexpression must reduce to a signed 64-bit numerator and a positive signed 64-bit denominator. A larger intermediate product is allowed internally, but an out-of-range reduced subexpression throws an error even if a later operation would cancel it.
- A literal allows at most 19 integer digits and 18 fractional digits; leading/trailing zeros count toward those limits. The Rational literal API accepts at most 64 characters.
- Expressions are bounded to 4,096 bytes, 1,024 tokens and 64 nested parentheses/unary operators. Error positions are zero-based byte offsets in ASCII input.
- `-9223372036854775808` is supported as a directly signed literal. The positive magnitude `9223372036854775808` is outside the stored range, including when placed inside parentheses before an outer minus.

Arithmetic is implemented with checked, reduced fractions and private 128-bit integer intermediates. There is no arbitrary-precision fallback. See [engineering notes](docs/engineering.md) for the range argument, parsing rules and tradeoffs.

## Verification

The C++ suite exercises normalization, full-width integer boundaries, decimal parsing, operator precedence, error positions, input limits and worked steps. It also checks 3,000 deterministic rational pairs against independent small-integer calculations. Assertions remain active in Release builds.

The optional Python suites compare 300 generated expressions and 512 cases near
the full 64-bit boundaries against Python's `fractions.Fraction`. They check exact
results, cancellation, overflow and division-by-zero rejection, plus CLI output
and exit codes, including failed writes. Test input is synthetic; the project
needs no accounts, personal records, network service or model API.

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DAXIOM_MATH_SANITIZERS=ON -DAXIOM_MATH_WARNINGS_AS_ERRORS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

[GitHub Actions](.github/workflows/ci.yml) runs GCC Release and Clang sanitizer
builds on Linux, plus an Apple Clang Release build on macOS. Additional shared
library builds on Linux and macOS exercise custom installation directories,
relocation and a separate CMake consumer. Finite tests support the documented
behavior; they do not establish correctness for every possible input. See
[verification](docs/verification.md) for recorded results.

## Project direction

AxiomMath is a separate C++ project related to the arithmetic needs of [Axiom SAT Math](https://github.com/TARUNTEjjjj/axiom-sat). It is not yet integrated into that JavaScript application and does not implement SAT scoring.

Possible next work includes better teaching-oriented explanations, a separately specified answer-checking tolerance policy, and a WebAssembly experiment. These are future ideas. The current release focuses on exact arithmetic, reliable error handling and a reusable API.

Contributions should follow [CONTRIBUTING.md](CONTRIBUTING.md). [MIT license](LICENSE), copyright Tarun Tej.
