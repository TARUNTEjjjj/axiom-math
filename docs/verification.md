# Verification — 0.1.0

Checked on 1 November 2026 (IST), using Apple Clang 21 on macOS arm64, CMake 4.4.5 and Python 3.9.6.

## Local results

| Check | Result |
| --- | --- |
| Release build, strict warnings treated as errors | Passed |
| Debug build with AddressSanitizer and UndefinedBehaviorSanitizer | Passed |
| Shared-library Release build, strict warnings treated as errors | Passed |
| CTest: core, library example, CLI help, installed consumer, two Python oracles | All six passed in static Release, sanitizer and shared Release builds |
| C++ assertions | Seven groups, 33,198 assertions passed |
| Differential expression tests | 300 generated expressions matched Python `fractions.Fraction` |
| Full-width arithmetic oracle | 512 cases: 196 exact results, 311 expected overflows, five division-by-zero rejections |
| CLI contracts | Exact output/exit checks passed for equality, inequality, explanations, help/version, bad arguments, source pointers, control-byte handling and seven output-failure cases |
| Relocated installation | Installed CLI and external CMake consumer ran after moving the prefix, in static, shared and sanitizer builds |
| Custom installation directories | Shared CLI and consumer passed using `tools/bin`, `lib64` and `headers` |

The arithmetic tests include 3,000 deterministic rational pairs with an independent small-integer oracle, integer extrema, normalization, malformed decimals, unsupported syntax and resource boundaries. CLI differential inputs are generated with a fixed seed and use no personal records.

An independent code review checked arithmetic bounds, sign handling, parsing, CLI diagnostics and library consumption. It found a sanitizer-linkage issue for external static-library consumers; the fix propagates the required runtime link options and is covered by the installed-consumer check.

A follow-up review reproduced three additional problems: the CLI returned success
after failed stdout writes, the installed shared CLI could not locate its library,
and custom header installation directories were absent from the exported target.
The CLI now flushes and checks output, and installation paths honor the configured
layout. Regression tests cover each observed failure. The new full-width oracle
checks reduced results with Python's arbitrary-precision arithmetic and confirms
overflow/domain errors; it does not reuse the C++ arithmetic implementation.

## Reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAXIOM_MATH_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DAXIOM_MATH_WARNINGS_AS_ERRORS=ON -DAXIOM_MATH_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure

cmake -S . -B build-shared -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON -DAXIOM_MATH_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_INSTALL_BINDIR=tools/bin -DCMAKE_INSTALL_LIBDIR=lib64 \
  -DCMAKE_INSTALL_INCLUDEDIR=headers
cmake --build build-shared --parallel
ctest --test-dir build-shared --output-on-failure
```

The [CI workflow](../.github/workflows/ci.yml) defines six jobs: GCC/Linux with no
build type (keeps the configuration-less installed-consumer test covered), GCC/Linux
Release, Clang/Linux sanitizer, Apple Clang/macOS Release, and shared-library Release
builds on both Linux and macOS. The shared jobs also use custom install directories.
Consult the repository's Actions runs for the result associated with a particular
commit; the local record above does not claim a Linux run.

## Limits of this verification

The generated cases are finite samples. Sanitizers and independent oracles provide evidence for the implemented bounds and operations; they are not proofs covering every expression. This release does not include arbitrary precision, symbolic algebra, SAT scoring, a browser integration or performance benchmarks. Native MSVC is outside the documented compiler support scope.
